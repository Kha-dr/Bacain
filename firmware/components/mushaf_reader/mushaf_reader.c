#include "mushaf_reader.h"

_Static_assert(sizeof(bqm_header_t) == 64, "bqm_header_t must be 64 bytes");
_Static_assert(sizeof(bqm_surah_entry_t) == 24, "bqm_surah_entry_t must be 24 bytes");
_Static_assert(sizeof(bqm_ayah_entry_t) == 24, "bqm_ayah_entry_t must be 24 bytes");
_Static_assert(sizeof(bqm_word_sprite_t) == 8, "bqm_word_sprite_t must be 8 bytes");
_Static_assert(sizeof(bqm_placed_word_t) == 6, "bqm_placed_word_t must be 6 bytes");
_Static_assert(sizeof(mushaf_bookmark_t) == 4, "mushaf_bookmark_t must be 4 bytes");

#if CONFIG_BOARD_MUSHAF_ENABLE || BOARD_MUSHAF_ENABLE

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "board_config.h"
#include "sdcard.h"
#include "GUI_Paint.h"
#include "fonts.h"

#define TAG "mushaf_reader"
#define MUSHAF_NVS_NAMESPACE "mushaf"
#define MUSHAF_NVS_KEY_STATE "state"

static bqm_header_t s_header;
static bool s_has_bqm = false;
static char s_bqm_path[64] = {0};
static bqm_surah_entry_t s_surahs[MUSHAF_TOTAL_SURAHS];
static mushaf_state_t s_state;

static bool try_load_bqm(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return false;
    }

    bqm_header_t hdr;
    if (fread(&hdr, 1, sizeof(bqm_header_t), fp) != sizeof(bqm_header_t)) {
        fclose(fp);
        return false;
    }

    if (memcmp(hdr.magic, "BQM1", 4) != 0 ||
        hdr.version != BQM_VERSION ||
        hdr.display_width != BOARD_DISPLAY_WIDTH ||
        hdr.display_height != BOARD_DISPLAY_HEIGHT ||
        hdr.total_surahs != MUSHAF_TOTAL_SURAHS ||
        hdr.total_ayahs != MUSHAF_TOTAL_AYAHS) {
        ESP_LOGE(TAG, "BQM header invalid: %s", path);
        fclose(fp);
        return false;
    }

    if (fseek(fp, hdr.surah_table_off, SEEK_SET) != 0) {
        fclose(fp);
        return false;
    }

    if (fread(s_surahs, sizeof(bqm_surah_entry_t), MUSHAF_TOTAL_SURAHS, fp) != MUSHAF_TOTAL_SURAHS) {
        ESP_LOGE(TAG, "Failed to read surah table: %s", path);
        fclose(fp);
        return false;
    }

    fclose(fp);

    s_header = hdr;
    strncpy(s_bqm_path, path, sizeof(s_bqm_path) - 1);
    s_bqm_path[sizeof(s_bqm_path) - 1] = '\0';
    s_has_bqm = true;
    ESP_LOGI(TAG, "Loaded BQM: %s (edition: %.20s)", path, hdr.edition_tag);
    return true;
}

esp_err_t mushaf_reader_init(const mushaf_state_t *rtc_state) {
    bool state_loaded = false;

    if (rtc_state != NULL && rtc_state->surah >= 1 && rtc_state->surah <= MUSHAF_TOTAL_SURAHS) {
        memcpy(&s_state, rtc_state, sizeof(mushaf_state_t));
        state_loaded = true;
    } else {
        nvs_handle_t nvs_h;
        esp_err_t err = nvs_open(MUSHAF_NVS_NAMESPACE, NVS_READONLY, &nvs_h);
        if (err == ESP_OK) {
            size_t req_size = sizeof(mushaf_state_t);
            err = nvs_get_blob(nvs_h, MUSHAF_NVS_KEY_STATE, &s_state, &req_size);
            if (err == ESP_OK && req_size == sizeof(mushaf_state_t)) {
                if (s_state.surah >= 1 && s_state.surah <= MUSHAF_TOTAL_SURAHS) {
                    state_loaded = true;
                }
            }
            nvs_close(nvs_h);
        }
    }

    if (!state_loaded) {
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

    static const char * const candidates[] = {
        FLASH_MOUNT_POINT "/books/mushaf.bqm",
        SDCARD_MOUNT_POINT "/books/mushaf.bqm"
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (try_load_bqm(candidates[i])) {
            break;
        }
    }

    if (!s_has_bqm) {
        ESP_LOGW(TAG, "No valid mushaf.bqm found");
    }

    return ESP_OK;
}

bool mushaf_reader_is_available(void) {
    return s_has_bqm;
}

const bqm_header_t *mushaf_reader_get_header(void) {
    return s_has_bqm ? &s_header : NULL;
}

const mushaf_state_t *mushaf_reader_get_state(void) {
    return &s_state;
}

esp_err_t mushaf_reader_save_state(void) {
    nvs_handle_t nvs_h;
    esp_err_t err = nvs_open(MUSHAF_NVS_NAMESPACE, NVS_READWRITE, &nvs_h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_blob(nvs_h, MUSHAF_NVS_KEY_STATE, &s_state, sizeof(mushaf_state_t));
    if (err == ESP_OK) {
        err = nvs_commit(nvs_h);
    } else {
        ESP_LOGE(TAG, "NVS set blob failed: %s", esp_err_to_name(err));
    }
    nvs_close(nvs_h);
    return err;
}

esp_err_t mushaf_reader_get_surah(uint8_t surah, bqm_surah_entry_t *out) {
    if (!s_has_bqm || surah < 1 || surah > MUSHAF_TOTAL_SURAHS || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = s_surahs[surah - 1];
    return ESP_OK;
}

esp_err_t mushaf_reader_get_ayah(uint8_t surah, uint16_t ayah, bqm_ayah_entry_t *out, uint16_t *out_global_ayah) {
    if (!s_has_bqm || surah < 1 || surah > MUSHAF_TOTAL_SURAHS || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const bqm_surah_entry_t *s = &s_surahs[surah - 1];
    if (ayah < 1 || ayah > s->ayah_count) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t global_idx = s->first_global_ayah + (ayah - 1);
    if (global_idx >= MUSHAF_TOTAL_AYAHS) {
        return ESP_ERR_INVALID_ARG;
    }

    if (out_global_ayah != NULL) {
        *out_global_ayah = global_idx;
    }

    FILE *fp = fopen(s_bqm_path, "rb");
    if (!fp) {
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t offset = s_header.ayah_table_off + ((uint32_t)global_idx * sizeof(bqm_ayah_entry_t));
    if (fseek(fp, offset, SEEK_SET) != 0) {
        fclose(fp);
        return ESP_FAIL;
    }

    if (fread(out, sizeof(bqm_ayah_entry_t), 1, fp) != 1) {
        fclose(fp);
        return ESP_FAIL;
    }

    fclose(fp);
    return ESP_OK;
}

uint8_t mushaf_reader_get_subpage_count(void) {
    if (!s_has_bqm) {
        return 0;
    }

    bqm_ayah_entry_t ayah_entry;
    if (mushaf_reader_get_ayah(s_state.surah, s_state.ayah, &ayah_entry, NULL) != ESP_OK) {
        return 0;
    }

    switch (s_state.layer) {
    case MUSHAF_LAYER_MUSHAF:
        return ayah_entry.mushaf_subpages;
    case MUSHAF_LAYER_LATIN:
        return ayah_entry.latin_subpages;
    case MUSHAF_LAYER_TRANSLATE:
        return ayah_entry.trans_subpages;
    case MUSHAF_LAYER_TAFSIR:
        return ayah_entry.tafsir_subpages;
    default:
        return 0;
    }
}

esp_err_t mushaf_reader_set_cursor(uint8_t surah, uint16_t ayah, uint8_t layer) {
    if (surah < 1 || surah > MUSHAF_TOTAL_SURAHS || layer >= MUSHAF_LAYER_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_has_bqm) {
        if (ayah < 1 || ayah > s_surahs[surah - 1].ayah_count) {
            return ESP_ERR_INVALID_ARG;
        }

        uint8_t old_layer = s_state.layer;
        s_state.layer = layer;
        uint8_t subpages = mushaf_reader_get_subpage_count();
        s_state.layer = old_layer;

        if (subpages == 0) {
            return ESP_ERR_NOT_FOUND;
        }
    }

    s_state.started = true;
    s_state.surah = surah;
    s_state.ayah = ayah;
    s_state.layer = layer;
    s_state.subpage = 0;
    return mushaf_reader_save_state();
}

esp_err_t mushaf_reader_set_bookmarks(const mushaf_bookmark_t *list, uint8_t count) {
    if (count > MUSHAF_MAX_BOOKMARKS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (count > 0 && list == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_state.bookmark_count = count;
    if (count > 0) {
        memcpy(s_state.bookmarks, list, count * sizeof(mushaf_bookmark_t));
    }
    return mushaf_reader_save_state();
}

bool mushaf_reader_is_bookmarked(uint8_t surah, uint16_t ayah) {
    for (uint8_t i = 0; i < s_state.bookmark_count; i++) {
        if (s_state.bookmarks[i].surah == surah && s_state.bookmarks[i].ayah == ayah) {
            return true;
        }
    }
    return false;
}

bool mushaf_reader_toggle_bookmark(void) {
    for (uint8_t i = 0; i < s_state.bookmark_count; i++) {
        if (s_state.bookmarks[i].surah == s_state.surah &&
            s_state.bookmarks[i].ayah == s_state.ayah) {
            mushaf_reader_delete_bookmark(i);
            return false;
        }
    }

    if (s_state.bookmark_count >= MUSHAF_MAX_BOOKMARKS) {
        ESP_LOGW(TAG, "Bookmark list full (%d max)", MUSHAF_MAX_BOOKMARKS);
        return false;
    }

    uint8_t juz = 1;
    if (s_has_bqm) {
        bqm_ayah_entry_t ayah_entry;
        if (mushaf_reader_get_ayah(s_state.surah, s_state.ayah, &ayah_entry, NULL) == ESP_OK) {
            juz = ayah_entry.juz;
        }
    }

    s_state.bookmarks[s_state.bookmark_count].surah = s_state.surah;
    s_state.bookmarks[s_state.bookmark_count].ayah = s_state.ayah;
    s_state.bookmarks[s_state.bookmark_count].juz = juz;
    s_state.bookmark_count++;
    mushaf_reader_save_state();
    return true;
}

esp_err_t mushaf_reader_add_bookmark(uint8_t surah, uint16_t ayah) {
    if (surah == 0) {
        surah = s_state.surah;
        ayah = s_state.ayah;
    }
    if (surah < 1 || surah > MUSHAF_TOTAL_SURAHS) {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint8_t i = 0; i < s_state.bookmark_count; i++) {
        if (s_state.bookmarks[i].surah == surah && s_state.bookmarks[i].ayah == ayah) {
            return ESP_OK;
        }
    }

    if (s_state.bookmark_count >= MUSHAF_MAX_BOOKMARKS) {
        ESP_LOGW(TAG, "Bookmark list full (%d max)", MUSHAF_MAX_BOOKMARKS);
        return ESP_ERR_NO_MEM;
    }

    uint8_t juz = 1;
    if (s_has_bqm) {
        bqm_ayah_entry_t ayah_entry;
        if (mushaf_reader_get_ayah(surah, ayah, &ayah_entry, NULL) == ESP_OK) {
            juz = ayah_entry.juz;
            uint8_t old_surah = s_state.surah;
            uint16_t old_ayah = s_state.ayah;
            s_state.surah = surah;
            s_state.ayah = ayah;
            uint8_t subpages = mushaf_reader_get_subpage_count();
            s_state.surah = old_surah;
            s_state.ayah = old_ayah;
            if (subpages == 0) {
                return ESP_ERR_NOT_FOUND;
            }
        } else {
            return ESP_ERR_NOT_FOUND;
        }
    }

    s_state.bookmarks[s_state.bookmark_count].surah = surah;
    s_state.bookmarks[s_state.bookmark_count].ayah = ayah;
    s_state.bookmarks[s_state.bookmark_count].juz = juz;
    s_state.bookmark_count++;
    return mushaf_reader_save_state();
}

esp_err_t mushaf_reader_delete_bookmark(uint8_t index) {
    if (index >= s_state.bookmark_count) {
        return ESP_ERR_INVALID_ARG;
    }

    if (index < s_state.bookmark_count - 1) {
        memmove(&s_state.bookmarks[index],
                &s_state.bookmarks[index + 1],
                (s_state.bookmark_count - 1 - index) * sizeof(mushaf_bookmark_t));
    }
    s_state.bookmark_count--;
    return mushaf_reader_save_state();
}

static uint8_t get_subpage_count_for(uint8_t surah, uint16_t ayah) {
    uint8_t old_surah = s_state.surah;
    uint16_t old_ayah = s_state.ayah;

    s_state.surah = surah;
    s_state.ayah = ayah;
    uint8_t count = mushaf_reader_get_subpage_count();

    s_state.surah = old_surah;
    s_state.ayah = old_ayah;

    return count;
}

esp_err_t mushaf_reader_step_page(int8_t dir) {
    if (dir == 0) return ESP_OK;
    if (!s_has_bqm) return ESP_ERR_INVALID_STATE;

    if (s_state.surah < 1 || s_state.surah > MUSHAF_TOTAL_SURAHS) {
        s_state.surah = 1;
        s_state.ayah = 1;
        s_state.subpage = 0;
    }

    uint8_t current_surah = s_state.surah;
    uint16_t current_ayah = s_state.ayah;
    uint8_t current_subpage = s_state.subpage;

    uint8_t subpage_count = get_subpage_count_for(current_surah, current_ayah);

    if (subpage_count == 0) {
        subpage_count = 1;
    }

    if (dir > 0) {
        if (current_subpage + 1 < subpage_count) {
            s_state.subpage = current_subpage + 1;
            s_state.started = true;
            return mushaf_reader_save_state();
        }

        while (current_surah <= MUSHAF_TOTAL_SURAHS) {
            uint16_t ayah_count = s_surahs[current_surah - 1].ayah_count;
            current_ayah++;

            if (current_ayah > ayah_count) {
                current_surah++;
                current_ayah = 1;
                if (current_surah > MUSHAF_TOTAL_SURAHS) {
                    return ESP_ERR_NOT_FOUND;
                }
            }

            uint8_t next_subpage_count = get_subpage_count_for(current_surah, current_ayah);
            if (next_subpage_count > 0) {
                s_state.surah = current_surah;
                s_state.ayah = current_ayah;
                s_state.subpage = 0;
                s_state.started = true;
                return mushaf_reader_save_state();
            }
        }
    } else {
        if (current_subpage > 0) {
            s_state.subpage = current_subpage - 1;
            s_state.started = true;
            return mushaf_reader_save_state();
        }

        while (current_surah >= 1) {
            current_ayah--;

            if (current_ayah < 1) {
                current_surah--;
                if (current_surah < 1) {
                    return ESP_ERR_NOT_FOUND;
                }
                current_ayah = s_surahs[current_surah - 1].ayah_count;
            }

            uint8_t prev_subpage_count = get_subpage_count_for(current_surah, current_ayah);
            if (prev_subpage_count > 0) {
                s_state.surah = current_surah;
                s_state.ayah = current_ayah;
                s_state.subpage = prev_subpage_count - 1;
                s_state.started = true;
                return mushaf_reader_save_state();
            }
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t mushaf_reader_cycle_layer(void) {
    uint8_t mask = s_has_bqm ? s_header.layer_mask : 0x0F;
    if (mask == 0) {
        mask = 0x0F;
    }

    uint8_t next = s_state.layer;
    for (int i = 0; i < MUSHAF_LAYER_COUNT; i++) {
        next = (next + 1) % MUSHAF_LAYER_COUNT;
        if (mask & (1 << next)) {
            s_state.layer = next;
            s_state.subpage = 0;
            s_state.started = true;
            return mushaf_reader_save_state();
        }
    }
    return ESP_OK;
}

esp_err_t mushaf_reader_render_subpage(uint8_t *canvas_buf) {
    if (!s_has_bqm) {
        return ESP_ERR_INVALID_STATE;
    }

    if (canvas_buf != NULL) {
        Paint_SelectImage(canvas_buf);
    }

    if (Paint.Image == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    Paint_ClearWindows(0, 16, BOARD_DISPLAY_WIDTH - 1, BOARD_DISPLAY_HEIGHT - 17, WHITE);

    bqm_ayah_entry_t ayah;
    esp_err_t err = mushaf_reader_get_ayah(s_state.surah, s_state.ayah, &ayah, NULL);
    if (err != ESP_OK) {
        return err;
    }

    FILE *fp = fopen(s_bqm_path, "rb");
    if (!fp) {
        return ESP_ERR_NOT_FOUND;
    }

    if (s_state.layer == MUSHAF_LAYER_MUSHAF) {
        if (ayah.mushaf_subpages == 0) {
            fclose(fp);
            return ESP_OK;
        }
        if (s_state.subpage >= ayah.mushaf_subpages) {
            fclose(fp);
            return ESP_ERR_INVALID_ARG;
        }

        uint32_t ayah_mushaf_base = s_header.mushaf_pages_off + ayah.mushaf_off;
        uint16_t subpage_rel = 0;
        if (fseek(fp, ayah_mushaf_base + ((uint32_t)s_state.subpage * sizeof(uint16_t)), SEEK_SET) != 0 ||
            fread(&subpage_rel, sizeof(uint16_t), 1, fp) != 1) {
            fclose(fp);
            return ESP_FAIL;
        }

        if (fseek(fp, ayah_mushaf_base + subpage_rel, SEEK_SET) != 0) {
            fclose(fp);
            return ESP_FAIL;
        }

        uint8_t word_count = 0;
        if (fread(&word_count, 1, 1, fp) != 1) {
            fclose(fp);
            return ESP_FAIL;
        }

        bqm_placed_word_t words[64];
        uint8_t count = (word_count > 64) ? 64 : word_count;
        if (count > 0) {
            if (fread(words, sizeof(bqm_placed_word_t), count, fp) != count) {
                fclose(fp);
                return ESP_FAIL;
            }
        }

        for (uint8_t i = 0; i < count; i++) {
            const bqm_placed_word_t *pw = &words[i];
            uint32_t sprite_entry_off = s_header.word_dict_off + ((uint32_t)pw->word_id * sizeof(bqm_word_sprite_t));
            if (fseek(fp, sprite_entry_off, SEEK_SET) != 0) {
                continue;
            }

            bqm_word_sprite_t sprite;
            if (fread(&sprite, sizeof(bqm_word_sprite_t), 1, fp) != 1) {
                continue;
            }

            uint32_t total_bytes = (uint32_t)sprite.height * sprite.stride_bytes;
            uint8_t row_buf[256];
            size_t bytes_to_read = (total_bytes > sizeof(row_buf)) ? sizeof(row_buf) : total_bytes;

            if (fseek(fp, s_header.sprite_blob_off + sprite.bitmap_off, SEEK_SET) != 0 ||
                fread(row_buf, 1, bytes_to_read, fp) != bytes_to_read) {
                continue;
            }

            for (uint8_t r = 0; r < sprite.height; r++) {
                if ((size_t)(r * sprite.stride_bytes) >= bytes_to_read) {
                    break;
                }
                int py = (int)pw->y + (int)sprite.y_offset + (int)r;
                if (py < 16 || py >= BOARD_DISPLAY_HEIGHT - 16) {
                    continue;
                }

                for (uint8_t col = 0; col < sprite.width; col++) {
                    size_t byte_idx = (size_t)r * sprite.stride_bytes + (col >> 3);
                    if (byte_idx >= bytes_to_read) {
                        break;
                    }
                    if ((row_buf[byte_idx] & (0x80 >> (col & 7))) != 0) {
                        int px = (int)pw->x + (int)col;
                        if (px >= 0 && px < BOARD_DISPLAY_WIDTH) {
                            Paint_SetPixel((UWORD)px, (UWORD)py, BLACK);
                        }
                    }
                }
            }
        }
    } else if (s_state.layer == MUSHAF_LAYER_LATIN ||
               s_state.layer == MUSHAF_LAYER_TRANSLATE ||
               s_state.layer == MUSHAF_LAYER_TAFSIR) {
        uint32_t layer_off = 0;
        uint8_t layer_subpages = 0;

        if (s_state.layer == MUSHAF_LAYER_LATIN) {
            layer_off = ayah.latin_off;
            layer_subpages = ayah.latin_subpages;
        } else if (s_state.layer == MUSHAF_LAYER_TRANSLATE) {
            layer_off = ayah.trans_off;
            layer_subpages = ayah.trans_subpages;
        } else {
            layer_off = ayah.tafsir_off;
            layer_subpages = ayah.tafsir_subpages;
        }

        if (layer_subpages == 0) {
            fclose(fp);
            return ESP_OK;
        }
        if (s_state.subpage >= layer_subpages) {
            fclose(fp);
            return ESP_ERR_INVALID_ARG;
        }

        uint32_t text_ayah_base = s_header.text_blob_off + layer_off;
        uint16_t subpage_rel = 0;
        if (fseek(fp, text_ayah_base + ((uint32_t)s_state.subpage * sizeof(uint16_t)), SEEK_SET) != 0 ||
            fread(&subpage_rel, sizeof(uint16_t), 1, fp) != 1) {
            fclose(fp);
            return ESP_FAIL;
        }

        if (fseek(fp, text_ayah_base + subpage_rel, SEEK_SET) != 0) {
            fclose(fp);
            return ESP_FAIL;
        }

        uint8_t line_count = 0;
        if (fread(&line_count, 1, 1, fp) != 1) {
            fclose(fp);
            return ESP_FAIL;
        }

        text_pager_font_size_t font_size = text_pager_get_font_size();
        sFONT *font = (font_size == TEXT_PAGER_FONT_16) ? &Font16 : &Font12;

        UWORD y = 18;
        char line_buf[128];
        for (uint8_t l = 0; l < line_count; l++) {
            uint8_t line_len = 0;
            if (fread(&line_len, 1, 1, fp) != 1) {
                break;
            }
            size_t to_read = line_len;
            if (to_read >= sizeof(line_buf)) {
                to_read = sizeof(line_buf) - 1;
            }
            if (to_read > 0) {
                if (fread(line_buf, 1, to_read, fp) != to_read) {
                    break;
                }
            }
            line_buf[to_read] = '\0';
            if (line_len > to_read) {
                fseek(fp, line_len - to_read, SEEK_CUR);
            }
            if (y + font->Height < BOARD_DISPLAY_HEIGHT - 16) {
                Paint_DrawString_EN(4, y, line_buf, font, BLACK, WHITE);
            }
            y += font->Height + 2;
        }
    } else {
        fclose(fp);
        return ESP_ERR_INVALID_ARG;
    }

    fclose(fp);
    return ESP_OK;
}

#endif
