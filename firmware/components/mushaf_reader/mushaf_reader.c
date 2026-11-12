#include "mushaf_reader.h"

_Static_assert(sizeof(bqm_header_t) == 64, "bqm_header_t must be 64 bytes");
_Static_assert(sizeof(bqm_surah_entry_t) == 24, "bqm_surah_entry_t must be 24 bytes");
_Static_assert(sizeof(bqm_ayah_entry_t) == 24, "bqm_ayah_entry_t must be 24 bytes");
_Static_assert(sizeof(bqm_word_sprite_t) == 8, "bqm_word_sprite_t must be 8 bytes");
_Static_assert(sizeof(bqm_placed_word_t) == 6, "bqm_placed_word_t must be 6 bytes");
_Static_assert(sizeof(mushaf_bookmark_t) == 4, "mushaf_bookmark_t must be 4 bytes");

#if CONFIG_BOARD_MUSHAF_ENABLE || BOARD_MUSHAF_ENABLE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "board_config.h"
#include "sdcard.h"
#include "GUI_Paint.h"
#include "text_pager.h" /* reading font + TEXT_PAGER_FOOTER: same frame as the book reader */
#include "text_source.h"
#include "text_layout.h"
#include "ui_pager.h"

#define TAG "mushaf_reader"
#define MUSHAF_NVS_NAMESPACE "mushaf"
#define MUSHAF_NVS_KEY_STATE "state"

#define MUSHAF_TEXT_BUF_MAX 2048 /* raise if RAM allows; truncation is logged */
#define MUSHAF_TEXT_MAX_PAGES 48

/* Same geometry as the book reader page. */
static const ui_pager_chrome_t k_text_geometry = {.footer = TEXT_PAGER_FOOTER};

static bqm_header_t s_header;
static bool s_has_bqm = false;
static char s_bqm_path[64] = {0};
static bqm_surah_entry_t s_surahs[MUSHAF_TOTAL_SURAHS];
static bool s_surah_available[MUSHAF_TOTAL_SURAHS] = {false};
static mushaf_state_t s_state;
static bool s_clip_warned = false;
static bool s_state_dirty = false; /* RULES.md 2.5 #4: flushed at sleep entry */

static void mark_dirty(void)
{
    s_state_dirty = true;
}
/* ---------- text layer cache: one ayah, keyed by layer and geometry ---------- */

typedef struct
{
    bool valid;
    uint8_t surah;
    uint16_t ayah;
    uint8_t layer;
    uint16_t cols;
    uint16_t lines;
    size_t len;
    uint8_t page_count;
    size_t page_starts[MUSHAF_TEXT_MAX_PAGES];
} text_cache_t;

static text_cache_t s_text_cache;
static char s_text_buf[MUSHAF_TEXT_BUF_MAX]; /* compiled only when Mushaf is enabled */
static text_page_t s_text_scratch;

static text_layout_cfg_t text_cfg(void)
{
    return ui_pager_layout_cfg(text_pager_get_style(), &k_text_geometry, TEXT_PAGER_KEEP_BLANK);
}

static bool layer_blob(const bqm_ayah_entry_t *ayah, uint8_t layer,
                       uint32_t *out_off, uint8_t *out_stored)
{
    switch (layer)
    {
    case MUSHAF_LAYER_LATIN:
        *out_off = ayah->latin_off;
        *out_stored = ayah->latin_subpages;
        return true;
    case MUSHAF_LAYER_TRANSLATE:
        *out_off = ayah->trans_off;
        *out_stored = ayah->trans_subpages;
        return true;
    case MUSHAF_LAYER_TAFSIR:
        *out_off = ayah->tafsir_off;
        *out_stored = ayah->tafsir_subpages;
        return true;
    default:
        return false;
    }
}

/* Joins the stored (pre-wrapped) lines of one text layer with single spaces. */
static size_t read_layer_text(const bqm_ayah_entry_t *ayah, uint8_t layer, char *buf, size_t cap)
{
    uint32_t layer_off = 0;
    uint8_t stored = 0;
    if (!layer_blob(ayah, layer, &layer_off, &stored) || stored == 0 || cap < 2)
    {
        return 0;
    }

    FILE *fp = fopen(s_bqm_path, "rb");
    if (!fp)
    {
        return 0;
    }

    size_t len = 0;
    bool truncated = false;
    uint32_t base = s_header.text_blob_off + layer_off;
    buf[0] = '\0';

    for (uint8_t sp = 0; sp < stored; sp++)
    {
        uint16_t rel = 0;
        if (fseek(fp, base + ((uint32_t)sp * sizeof(uint16_t)), SEEK_SET) != 0 ||
            fread(&rel, sizeof(uint16_t), 1, fp) != 1 ||
            fseek(fp, base + rel, SEEK_SET) != 0)
        {
            break;
        }
        uint8_t line_count = 0;
        if (fread(&line_count, 1, 1, fp) != 1)
        {
            break;
        }
        for (uint8_t l = 0; l < line_count; l++)
        {
            uint8_t line_len = 0;
            if (fread(&line_len, 1, 1, fp) != 1)
            {
                break;
            }
            if (line_len == 0)
            {
                continue;
            }
            if (len > 0 && len + 1 < cap - 1)
            {
                buf[len++] = ' ';
            }
            size_t avail = (cap - 1) - len;
            size_t to_read = (line_len < avail) ? line_len : avail;
            if (to_read > 0)
            {
                if (fread(&buf[len], 1, to_read, fp) != to_read)
                {
                    break;
                }
                len += to_read;
            }
            if (line_len > to_read)
            {
                truncated = true;
                fseek(fp, line_len - to_read, SEEK_CUR);
            }
        }
    }
    buf[len] = '\0';
    fclose(fp);

    if (truncated)
    {
        ESP_LOGW(TAG, "layer %u text cut at %u bytes (MUSHAF_TEXT_BUF_MAX)",
                 (unsigned)layer, (unsigned)(cap - 1));
    }
    return len;
}

/* Loads (or reuses) one ayah's text layer; returns its page count. */
static uint8_t text_page_count(uint8_t surah, uint16_t ayah, uint8_t layer,
                               const bqm_ayah_entry_t *entry)
{
    text_layout_cfg_t cfg = text_cfg();
    text_cache_t *c = &s_text_cache;

    if (c->valid && c->surah == surah && c->ayah == ayah && c->layer == layer &&
        c->cols == cfg.cols && c->lines == cfg.lines)
    {
        return c->page_count;
    }

    c->valid = false;
    c->surah = surah;
    c->ayah = ayah;
    c->layer = layer;
    c->cols = cfg.cols;
    c->lines = cfg.lines;
    c->page_count = 0;
    c->len = read_layer_text(entry, layer, s_text_buf, MUSHAF_TEXT_BUF_MAX);

    if (c->len > 0)
    {
        text_source_t src;
        text_source_from_mem(&src, s_text_buf, c->len);
        size_t offset = 0;
        while (c->page_count < MUSHAF_TEXT_MAX_PAGES)
        {
            if (text_layout_page(&src, offset, &cfg, &s_text_scratch) != ESP_OK)
            {
                break;
            }
            if (s_text_scratch.line_count == 0 && s_text_scratch.is_eof)
            {
                break;
            }
            c->page_starts[c->page_count++] = offset;
            if (s_text_scratch.is_eof)
            {
                break;
            }
            offset = s_text_scratch.end;
        }
        if (c->page_count == MUSHAF_TEXT_MAX_PAGES && !s_text_scratch.is_eof)
        {
            ESP_LOGW(TAG, "%u:%u layer %u exceeds %d pages", surah, ayah, layer, MUSHAF_TEXT_MAX_PAGES);
        }
    }
    c->valid = true;
    return c->page_count;
}

/* ---------- cursor helpers ---------- */

static uint8_t get_subpage_count_for_layer(uint8_t surah, uint16_t ayah, uint8_t layer)
{
    if (!s_has_bqm || surah < 1 || surah > MUSHAF_TOTAL_SURAHS || !s_surah_available[surah - 1])
    {
        return 0;
    }

    bqm_ayah_entry_t ayah_entry;
    if (mushaf_reader_get_ayah(surah, ayah, &ayah_entry, NULL) != ESP_OK)
    {
        return 0;
    }

    if (layer == MUSHAF_LAYER_MUSHAF)
    {
        return ayah_entry.mushaf_subpages;
    }
    if (layer == MUSHAF_LAYER_LATIN || layer == MUSHAF_LAYER_TRANSLATE || layer == MUSHAF_LAYER_TAFSIR)
    {
        return text_page_count(surah, ayah, layer, &ayah_entry);
    }
    return 0;
}

static void ensure_valid_cursor(void)
{
    if (!s_has_bqm)
    {
        return;
    }

    if (s_state.surah >= 1 && s_state.surah <= MUSHAF_TOTAL_SURAHS &&
        s_surah_available[s_state.surah - 1])
    {
        if (get_subpage_count_for_layer(s_state.surah, s_state.ayah, s_state.layer) > 0)
        {
            return;
        }
        if (get_subpage_count_for_layer(s_state.surah, s_state.ayah, MUSHAF_LAYER_MUSHAF) > 0)
        {
            s_state.layer = MUSHAF_LAYER_MUSHAF;
            s_state.subpage = 0;
            return;
        }
    }

    for (uint8_t s = 1; s <= MUSHAF_TOTAL_SURAHS; s++)
    {
        if (!s_surah_available[s - 1])
        {
            continue;
        }
        uint16_t count = s_surahs[s - 1].ayah_count;
        for (uint16_t a = 1; a <= count; a++)
        {
            if (get_subpage_count_for_layer(s, a, MUSHAF_LAYER_MUSHAF) > 0)
            {
                s_state.surah = s;
                s_state.ayah = a;
                s_state.layer = MUSHAF_LAYER_MUSHAF;
                s_state.subpage = 0;
                return;
            }
        }
    }
}

/* ---------- BQM loading ---------- */

static bool try_load_bqm(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
    {
        return false;
    }

    bqm_header_t hdr;
    if (fread(&hdr, 1, sizeof(bqm_header_t), fp) != sizeof(bqm_header_t))
    {
        fclose(fp);
        return false;
    }

    if (memcmp(hdr.magic, "BQM1", 4) != 0 ||
        hdr.version != BQM_VERSION ||
        hdr.display_width != BOARD_DISPLAY_WIDTH ||
        hdr.display_height != BOARD_DISPLAY_HEIGHT ||
        hdr.total_surahs != MUSHAF_TOTAL_SURAHS ||
        hdr.total_ayahs != MUSHAF_TOTAL_AYAHS)
    {
        ESP_LOGE(TAG, "BQM header invalid: %s", path);
        fclose(fp);
        return false;
    }

    if (fseek(fp, hdr.surah_table_off, SEEK_SET) != 0 ||
        fread(s_surahs, sizeof(bqm_surah_entry_t), MUSHAF_TOTAL_SURAHS, fp) != MUSHAF_TOTAL_SURAHS)
    {
        ESP_LOGE(TAG, "Failed to read surah table: %s", path);
        fclose(fp);
        return false;
    }

    /* Fast surah availability table from the first ayah of each surah */
    memset(s_surah_available, 0, sizeof(s_surah_available));
    for (uint8_t i = 0; i < MUSHAF_TOTAL_SURAHS; i++)
    {
        if (s_surahs[i].ayah_count == 0)
        {
            continue;
        }
        uint16_t global_idx = s_surahs[i].first_global_ayah;
        if (global_idx >= MUSHAF_TOTAL_AYAHS)
        {
            continue;
        }
        uint32_t offset = hdr.ayah_table_off + ((uint32_t)global_idx * sizeof(bqm_ayah_entry_t));
        bqm_ayah_entry_t first_ayah;
        if (fseek(fp, offset, SEEK_SET) == 0 &&
            fread(&first_ayah, sizeof(bqm_ayah_entry_t), 1, fp) == 1 &&
            (first_ayah.mushaf_subpages > 0 || first_ayah.latin_subpages > 0 ||
             first_ayah.trans_subpages > 0 || first_ayah.tafsir_subpages > 0))
        {
            s_surah_available[i] = true;
        }
    }
    fclose(fp);

    s_header = hdr;
    strncpy(s_bqm_path, path, sizeof(s_bqm_path) - 1);
    s_bqm_path[sizeof(s_bqm_path) - 1] = '\0';
    s_has_bqm = true;
    s_text_cache.valid = false;
    ESP_LOGI(TAG, "Loaded BQM: %s (edition: %.20s)", path, hdr.edition_tag);
    return true;
}

/* ---------- public: lifecycle and state ---------- */

esp_err_t mushaf_reader_init(const mushaf_state_t *rtc_state)
{
    bool state_loaded = false;

    if (rtc_state != NULL && rtc_state->surah >= 1 && rtc_state->surah <= MUSHAF_TOTAL_SURAHS)
    {
        memcpy(&s_state, rtc_state, sizeof(mushaf_state_t));
        state_loaded = true;
    }
    else
    {
        nvs_handle_t nvs_h;
        if (nvs_open(MUSHAF_NVS_NAMESPACE, NVS_READONLY, &nvs_h) == ESP_OK)
        {
            size_t req_size = sizeof(mushaf_state_t);
            if (nvs_get_blob(nvs_h, MUSHAF_NVS_KEY_STATE, &s_state, &req_size) == ESP_OK &&
                req_size == sizeof(mushaf_state_t) &&
                s_state.surah >= 1 && s_state.surah <= MUSHAF_TOTAL_SURAHS)
            {
                state_loaded = true;
            }
            nvs_close(nvs_h);
        }
    }

    if (!state_loaded)
    {
        s_state.started = false;
        s_state.surah = 1;
        s_state.ayah = 1;
        s_state.layer = MUSHAF_LAYER_MUSHAF;
        s_state.subpage = 0;
        s_state.bookmark_count = 0;
        memset(s_state.bookmarks, 0, sizeof(s_state.bookmarks));
    }

    s_has_bqm = false;
    s_bqm_path[0] = '\0';
    s_text_cache.valid = false;

    static const char *const candidates[] = {
        FLASH_MOUNT_POINT "/books/mushaf.bqm",
        SDCARD_MOUNT_POINT "/books/mushaf.bqm"};

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
    {
        if (try_load_bqm(candidates[i]))
        {
            break;
        }
    }

    if (!s_has_bqm)
    {
        ESP_LOGW(TAG, "No valid mushaf.bqm found");
    }
    else
    {
        ensure_valid_cursor();
    }
    return ESP_OK;
}

bool mushaf_reader_is_available(void)
{
    return s_has_bqm;
}

const bqm_header_t *mushaf_reader_get_header(void)
{
    return s_has_bqm ? &s_header : NULL;
}

const mushaf_state_t *mushaf_reader_get_state(void)
{
    return &s_state;
}

esp_err_t mushaf_reader_save_state(void)
{
    if (!s_state_dirty)
    {
        return ESP_OK;
    }
    nvs_handle_t nvs_h;
    esp_err_t err = nvs_open(MUSHAF_NVS_NAMESPACE, NVS_READWRITE, &nvs_h);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_blob(nvs_h, MUSHAF_NVS_KEY_STATE, &s_state, sizeof(mushaf_state_t));
    if (err == ESP_OK)
    {
        err = nvs_commit(nvs_h);
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS save failed: %s", esp_err_to_name(err));
    }
    else
    {
        s_state_dirty = false;
    }
    nvs_close(nvs_h);
    return err;
}

/* ---------- public: table lookups ---------- */

esp_err_t mushaf_reader_get_surah(uint8_t surah, bqm_surah_entry_t *out)
{
    if (!s_has_bqm || surah < 1 || surah > MUSHAF_TOTAL_SURAHS || out == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_surah_available[surah - 1])
    {
        return ESP_ERR_NOT_FOUND;
    }
    *out = s_surahs[surah - 1];
    return ESP_OK;
}

esp_err_t mushaf_reader_get_ayah(uint8_t surah, uint16_t ayah, bqm_ayah_entry_t *out,
                                 uint16_t *out_global_ayah)
{
    if (!s_has_bqm || surah < 1 || surah > MUSHAF_TOTAL_SURAHS || out == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_surah_available[surah - 1])
    {
        return ESP_ERR_NOT_FOUND;
    }

    const bqm_surah_entry_t *s = &s_surahs[surah - 1];
    if (ayah < 1 || ayah > s->ayah_count)
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t global_idx = s->first_global_ayah + (ayah - 1);
    if (global_idx >= MUSHAF_TOTAL_AYAHS)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (out_global_ayah != NULL)
    {
        *out_global_ayah = global_idx;
    }

    FILE *fp = fopen(s_bqm_path, "rb");
    if (!fp)
    {
        return ESP_ERR_NOT_FOUND;
    }
    uint32_t offset = s_header.ayah_table_off + ((uint32_t)global_idx * sizeof(bqm_ayah_entry_t));
    esp_err_t err = ESP_OK;
    if (fseek(fp, offset, SEEK_SET) != 0 || fread(out, sizeof(bqm_ayah_entry_t), 1, fp) != 1)
    {
        err = ESP_FAIL;
    }
    fclose(fp);
    return err;
}

uint8_t mushaf_reader_get_subpage_count(void)
{
    return get_subpage_count_for_layer(s_state.surah, s_state.ayah, s_state.layer);
}

/* ---------- public: cursor and bookmarks ---------- */

esp_err_t mushaf_reader_set_cursor(uint8_t surah, uint16_t ayah, uint8_t layer)
{
    if (surah < 1 || surah > MUSHAF_TOTAL_SURAHS || layer >= MUSHAF_LAYER_COUNT)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_has_bqm)
    {
        if (!s_surah_available[surah - 1])
        {
            return ESP_ERR_NOT_FOUND;
        }
        if (ayah < 1 || ayah > s_surahs[surah - 1].ayah_count)
        {
            return ESP_ERR_INVALID_ARG;
        }
        if (get_subpage_count_for_layer(surah, ayah, layer) == 0)
        {
            return ESP_ERR_NOT_FOUND;
        }
    }

    s_state.started = true;
    s_state.surah = surah;
    s_state.ayah = ayah;
    s_state.layer = layer;
    s_state.subpage = 0;
    mark_dirty();
    return mushaf_reader_save_state();
}

esp_err_t mushaf_reader_set_bookmarks(const mushaf_bookmark_t *list, uint8_t count)
{
    if (count > MUSHAF_MAX_BOOKMARKS || (count > 0 && list == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t valid_count = 0;
    for (uint8_t i = 0; i < count; i++)
    {
        uint8_t s = list[i].surah;
        uint16_t a = list[i].ayah;
        if (s < 1 || s > MUSHAF_TOTAL_SURAHS)
        {
            continue;
        }
        if (s_has_bqm && (!s_surah_available[s - 1] || a < 1 || a > s_surahs[s - 1].ayah_count))
        {
            continue;
        }
        s_state.bookmarks[valid_count++] = list[i];
    }
    s_state.bookmark_count = valid_count;
    mark_dirty();
    return mushaf_reader_save_state();
}

bool mushaf_reader_is_bookmarked(uint8_t surah, uint16_t ayah)
{
    for (uint8_t i = 0; i < s_state.bookmark_count; i++)
    {
        if (s_state.bookmarks[i].surah == surah && s_state.bookmarks[i].ayah == ayah)
        {
            return true;
        }
    }
    return false;
}

bool mushaf_reader_toggle_bookmark(void)
{
    for (uint8_t i = 0; i < s_state.bookmark_count; i++)
    {
        if (s_state.bookmarks[i].surah == s_state.surah && s_state.bookmarks[i].ayah == s_state.ayah)
        {
            mushaf_reader_delete_bookmark(i);
            return false;
        }
    }
    return mushaf_reader_add_bookmark(s_state.surah, s_state.ayah) == ESP_OK;
}

esp_err_t mushaf_reader_add_bookmark(uint8_t surah, uint16_t ayah)
{
    if (surah == 0)
    {
        surah = s_state.surah;
        ayah = s_state.ayah;
    }
    if (surah < 1 || surah > MUSHAF_TOTAL_SURAHS)
    {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t i = 0; i < s_state.bookmark_count; i++)
    {
        if (s_state.bookmarks[i].surah == surah && s_state.bookmarks[i].ayah == ayah)
        {
            return ESP_OK;
        }
    }
    if (s_state.bookmark_count >= MUSHAF_MAX_BOOKMARKS)
    {
        ESP_LOGW(TAG, "Bookmark list full (%d max)", MUSHAF_MAX_BOOKMARKS);
        return ESP_ERR_NO_MEM;
    }

    uint8_t juz = 1;
    if (s_has_bqm)
    {
        if (!s_surah_available[surah - 1])
        {
            return ESP_ERR_NOT_FOUND;
        }
        bqm_ayah_entry_t ayah_entry;
        if (mushaf_reader_get_ayah(surah, ayah, &ayah_entry, NULL) != ESP_OK)
        {
            return ESP_ERR_NOT_FOUND;
        }
        if (ayah_entry.mushaf_subpages == 0 &&
            get_subpage_count_for_layer(surah, ayah, s_state.layer) == 0)
        {
            return ESP_ERR_NOT_FOUND;
        }
        juz = ayah_entry.juz;
    }

    s_state.bookmarks[s_state.bookmark_count].surah = surah;
    s_state.bookmarks[s_state.bookmark_count].ayah = ayah;
    s_state.bookmarks[s_state.bookmark_count].juz = juz;
    s_state.bookmark_count++;
    mark_dirty();
    return mushaf_reader_save_state();
}

esp_err_t mushaf_reader_delete_bookmark(uint8_t index)
{
    if (index >= s_state.bookmark_count)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (index < s_state.bookmark_count - 1)
    {
        memmove(&s_state.bookmarks[index], &s_state.bookmarks[index + 1],
                (s_state.bookmark_count - 1 - index) * sizeof(mushaf_bookmark_t));
    }
    s_state.bookmark_count--;
    mark_dirty();
    return mushaf_reader_save_state();
}

/* ---------- public: navigation ---------- */

esp_err_t mushaf_reader_step_page(int8_t dir)
{
    if (dir == 0)
    {
        return ESP_OK;
    }
    if (!s_has_bqm)
    {
        return ESP_ERR_INVALID_STATE;
    }

    ensure_valid_cursor();

    uint8_t current_surah = s_state.surah;
    uint16_t current_ayah = s_state.ayah;
    uint8_t current_subpage = s_state.subpage;

    uint8_t subpage_count = get_subpage_count_for_layer(current_surah, current_ayah, s_state.layer);
    if (current_subpage >= subpage_count && subpage_count > 0)
    {
        current_subpage = subpage_count - 1;
    }

    if (dir > 0)
    {
        if (subpage_count > 0 && (current_subpage + 1 < subpage_count))
        {
            s_state.subpage = current_subpage + 1;
            s_state.started = true;
            mark_dirty();
            return ESP_OK;
        }

        while (current_surah <= MUSHAF_TOTAL_SURAHS)
        {
            if (!s_surah_available[current_surah - 1])
            {
                current_surah++;
                current_ayah = 0;
                continue;
            }
            uint16_t ayah_count = s_surahs[current_surah - 1].ayah_count;
            current_ayah++;
            if (ayah_count == 0 || current_ayah > ayah_count)
            {
                current_surah++;
                current_ayah = 0;
                continue;
            }
            if (get_subpage_count_for_layer(current_surah, current_ayah, s_state.layer) > 0)
            {
                s_state.surah = current_surah;
                s_state.ayah = current_ayah;
                s_state.subpage = 0;
                s_state.started = true;
                mark_dirty();
                return ESP_OK;
            }
        }
    }
    else
    {
        if (subpage_count > 0 && current_subpage > 0)
        {
            s_state.subpage = current_subpage - 1;
            s_state.started = true;
            mark_dirty();
            return ESP_OK;
        }

        while (current_surah >= 1)
        {
            if (!s_surah_available[current_surah - 1])
            {
                if (current_surah <= 1)
                {
                    return ESP_ERR_NOT_FOUND;
                }
                current_surah--;
                current_ayah = s_surahs[current_surah - 1].ayah_count + 1;
                continue;
            }

            if (current_ayah > 1)
            {
                current_ayah--;
            }
            else
            {
                if (current_surah <= 1)
                {
                    return ESP_ERR_NOT_FOUND;
                }
                current_surah--;
                while (current_surah >= 1 && !s_surah_available[current_surah - 1])
                {
                    if (current_surah == 1)
                    {
                        return ESP_ERR_NOT_FOUND;
                    }
                    current_surah--;
                }
                current_ayah = s_surahs[current_surah - 1].ayah_count;
                if (current_ayah == 0)
                {
                    continue;
                }
            }

            uint8_t prev_count = get_subpage_count_for_layer(current_surah, current_ayah, s_state.layer);
            if (prev_count > 0)
            {
                s_state.surah = current_surah;
                s_state.ayah = current_ayah;
                s_state.subpage = prev_count - 1;
                s_state.started = true;
                mark_dirty();
                return ESP_OK;
            }
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t mushaf_reader_cycle_layer(void)
{
    uint8_t mask = s_has_bqm ? s_header.layer_mask : 0x0F;
    if (mask == 0)
    {
        mask = 0x0F;
    }

    uint8_t next = s_state.layer;
    for (int i = 0; i < MUSHAF_LAYER_COUNT; i++)
    {
        next = (next + 1) % MUSHAF_LAYER_COUNT;
        if ((mask & (1 << next)) &&
            (!s_has_bqm || get_subpage_count_for_layer(s_state.surah, s_state.ayah, next) > 0))
        {
            s_state.layer = next;
            s_state.subpage = 0;
            s_state.started = true;
            mark_dirty();
            return ESP_OK;
        }
    }
    return ESP_OK;
}

/* ---------- public: page content ---------- */

esp_err_t mushaf_reader_get_text_page(text_page_t *out)
{
    if (!out)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_has_bqm)
    {
        return ESP_ERR_INVALID_STATE;
    }

    ensure_valid_cursor(); /* may switch to the mushaf layer */
    if (s_state.layer == MUSHAF_LAYER_MUSHAF || s_state.layer >= MUSHAF_LAYER_COUNT)
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    bqm_ayah_entry_t ayah;
    esp_err_t err = mushaf_reader_get_ayah(s_state.surah, s_state.ayah, &ayah, NULL);
    if (err != ESP_OK)
    {
        return err;
    }

    uint8_t count = text_page_count(s_state.surah, s_state.ayah, s_state.layer, &ayah);
    if (count == 0)
    {
        memset(out, 0, sizeof(*out));
        out->is_bof = true;
        out->is_eof = true;
        return ESP_OK;
    }
    if (s_state.subpage >= count)
    {
        s_state.subpage = count - 1;
    }

    text_source_t src;
    text_source_from_mem(&src, s_text_buf, s_text_cache.len);
    text_layout_cfg_t cfg = text_cfg();
    return text_layout_page(&src, s_text_cache.page_starts[s_state.subpage], &cfg, out);
}

esp_err_t mushaf_reader_draw_mushaf_page(int clip_top, int clip_bottom)
{
    if (!s_has_bqm || Paint.Image == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    ensure_valid_cursor();
    if (s_state.layer != MUSHAF_LAYER_MUSHAF)
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    bqm_ayah_entry_t ayah;
    esp_err_t err = mushaf_reader_get_ayah(s_state.surah, s_state.ayah, &ayah, NULL);
    if (err != ESP_OK)
    {
        return err;
    }
    if (ayah.mushaf_subpages == 0)
    {
        return ESP_OK;
    }
    if (s_state.subpage >= ayah.mushaf_subpages)
    {
        s_state.subpage = ayah.mushaf_subpages - 1;
    }

    FILE *fp = fopen(s_bqm_path, "rb");
    if (!fp)
    {
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t base = s_header.mushaf_pages_off + ayah.mushaf_off;
    uint16_t rel = 0;
    uint8_t word_count = 0;
    if (fseek(fp, base + ((uint32_t)s_state.subpage * sizeof(uint16_t)), SEEK_SET) != 0 ||
        fread(&rel, sizeof(uint16_t), 1, fp) != 1 ||
        fseek(fp, base + rel, SEEK_SET) != 0 ||
        fread(&word_count, 1, 1, fp) != 1)
    {
        fclose(fp);
        return ESP_FAIL;
    }

    bqm_placed_word_t words[64];
    uint8_t count = (word_count > 64) ? 64 : word_count;
    if (count > 0 && fread(words, sizeof(bqm_placed_word_t), count, fp) != count)
    {
        fclose(fp);
        return ESP_FAIL;
    }

    uint32_t clipped = 0;
    uint8_t row_buf[32]; /* one row of a <=255 px wide sprite */

    for (uint8_t i = 0; i < count; i++)
    {
        const bqm_placed_word_t *pw = &words[i];

        bqm_word_sprite_t sprite;
        uint32_t entry_off = s_header.word_dict_off + ((uint32_t)pw->word_id * sizeof(bqm_word_sprite_t));
        if (fseek(fp, entry_off, SEEK_SET) != 0 || fread(&sprite, sizeof(sprite), 1, fp) != 1)
        {
            continue;
        }
        if (sprite.stride_bytes == 0 || sprite.stride_bytes > sizeof(row_buf) ||
            fseek(fp, s_header.sprite_blob_off + sprite.bitmap_off, SEEK_SET) != 0)
        {
            continue;
        }

        /* Row by row: no size limit on the sprite (finding 2). */
        for (uint8_t r = 0; r < sprite.height; r++)
        {
            if (fread(row_buf, 1, sprite.stride_bytes, fp) != sprite.stride_bytes)
            {
                break;
            }
            int py = (int)pw->y + (int)sprite.y_offset + (int)r;
            bool row_in = (py >= clip_top && py < clip_bottom);

            for (uint8_t col = 0; col < sprite.width; col++)
            {
                if ((row_buf[col >> 3] & (0x80 >> (col & 7))) == 0)
                {
                    continue;
                }
                int px = (int)pw->x + (int)col;
                if (row_in && px >= 0 && px < BOARD_DISPLAY_WIDTH)
                {
                    Paint_SetPixel((UWORD)px, (UWORD)py, BLACK);
                }
                else
                {
                    clipped++;
                }
            }
        }
    }
    fclose(fp);

    /* One-time check: if this fires with your real mushaf.bqm, adjust the BQM builder
     * to place pages inside rows clip_top..clip_bottom-1 (reader body). */
    if (clipped > 0 && !s_clip_warned)
    {
        ESP_LOGW(TAG, "%u:%u sub %u: %u sprite px outside body rows %d..%d",
                 s_state.surah, s_state.ayah, s_state.subpage, (unsigned)clipped,
                 clip_top, clip_bottom - 1);
        s_clip_warned = true;
    }
    return ESP_OK;
}

#endif /* CONFIG_BOARD_MUSHAF_ENABLE || BOARD_MUSHAF_ENABLE */