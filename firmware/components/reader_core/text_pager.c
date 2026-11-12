#include "text_pager.h"
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "text_source.h"

static const char *TAG = "text_pager";

_Static_assert(TEXT_PAGER_MAX_LINES == TEXT_LAYOUT_MAX_LINES, "line count mismatch");
_Static_assert(sizeof(((text_pager_page_t *)0)->lines[0]) ==
                   sizeof(((text_page_t *)0)->lines[0]),
               "line width mismatch");

static text_pager_t s_pager;
static text_source_t s_src;
static text_page_t s_layout_page; /* layout-native copy of the current page */
static text_layout_cfg_t s_cfg;   /* geometry the history was built with */
static text_pager_listener_t s_listener;
static bool s_warned_no_listener;
static SemaphoreHandle_t s_mutex;

#define LOCK() xSemaphoreTake(s_mutex, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_mutex)

/* ---------- internal (call with mutex held) ---------- */

static const ui_pager_style_t *style_for(text_pager_font_size_t f)
{
    return (f == TEXT_PAGER_FONT_16) ? &UI_PAGER_STYLE_LARGE : &UI_PAGER_STYLE_SMALL;
}

/* Geometry from current font + screen. A change (font, rotation) invalidates history,
 * because stored offsets were page starts under the old geometry. */
static text_layout_cfg_t current_cfg(void)
{
    static const ui_pager_chrome_t k_geometry = {.footer = TEXT_PAGER_FOOTER};
    text_layout_cfg_t cfg = ui_pager_layout_cfg(style_for(s_pager.font_size), &k_geometry,
                                                TEXT_PAGER_KEEP_BLANK);
    if (cfg.cols != s_cfg.cols || cfg.lines != s_cfg.lines)
    {
        s_pager.history_count = 0;
        s_cfg = cfg;
    }
    return cfg;
}

static void copy_to_public(const text_page_t *src, text_pager_page_t *dst)
{
    dst->line_count = src->line_count;
    for (size_t i = 0; i < src->line_count; i++)
    {
        memcpy(dst->lines[i], src->lines[i], sizeof(dst->lines[i]));
    }
    dst->page_start_offset = src->start;
    dst->page_end_offset = src->end;
    dst->progress_pct = src->progress_pct;
    dst->is_eof = src->is_eof;
    dst->is_bof = src->is_bof;
}

static esp_err_t layout_at(size_t offset)
{
    text_layout_cfg_t cfg = current_cfg();
    esp_err_t err = text_layout_page(&s_src, offset, &cfg, &s_layout_page);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "layout at %u failed: %s", (unsigned)offset, esp_err_to_name(err));
        return err;
    }
    copy_to_public(&s_layout_page, &s_pager.current_page);
    s_pager.current_offset = s_layout_page.start; /* canonical offset (finding 1) */
    return ESP_OK;
}

static void history_push(size_t offset)
{
    if (s_pager.history_count < TEXT_PAGER_HISTORY_DEPTH)
    {
        s_pager.history[s_pager.history_count++] = offset;
    }
    else
    {
        memmove(&s_pager.history[0], &s_pager.history[1],
                sizeof(size_t) * (TEXT_PAGER_HISTORY_DEPTH - 1));
        s_pager.history[TEXT_PAGER_HISTORY_DEPTH - 1] = offset;
    }
}

static void extract_clean_title(const char *path, char *out, size_t max_len)
{
    if (!path || !out || max_len == 0)
    {
        return;
    }
    const char *slash = strrchr(path, '/');
    char temp[128];
    strncpy(temp, slash ? slash + 1 : path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char *dot = strrchr(temp, '.');
    if (dot && strcasecmp(dot, ".txt") == 0)
    {
        *dot = '\0';
    }

    bool cap_next = true;
    for (size_t i = 0; temp[i] != '\0'; i++)
    {
        if (temp[i] == '_' || temp[i] == '-')
        {
            temp[i] = ' ';
            cap_next = true;
        }
        else if (cap_next && isalpha((unsigned char)temp[i]))
        {
            temp[i] = (char)toupper((unsigned char)temp[i]);
            cap_next = false;
        }
        else
        {
            cap_next = false;
        }
    }
    /* No fixed 28-char cut: the header clips to the space left by the battery. */
    strncpy(out, temp, max_len - 1);
    out[max_len - 1] = '\0';
}

/* ---------- notifications (call without mutex) ---------- */

static void notify_offset(size_t offset)
{
    if (s_listener.on_offset)
    {
        s_listener.on_offset(offset, s_listener.ctx);
    }
    else if (!s_warned_no_listener)
    {
        ESP_LOGW(TAG, "no listener set: reading position is not persisted");
        s_warned_no_listener = true;
    }
}

/* ---------- public ---------- */

esp_err_t text_pager_init(void)
{
    if (!s_mutex)
    {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex)
        {
            ESP_LOGE(TAG, "failed to create mutex");
            return ESP_ERR_NO_MEM;
        }
    }
    LOCK();
    memset(&s_pager, 0, sizeof(s_pager));
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_pager.font_size = TEXT_PAGER_FONT_16;
    UNLOCK();
    ESP_LOGI(TAG, "initialized");
    return ESP_OK;
}

void text_pager_deinit(void)
{
    text_pager_close_book();
    if (s_mutex)
    {
        LOCK();
        memset(&s_pager, 0, sizeof(s_pager));
        UNLOCK();
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
}

void text_pager_set_listener(const text_pager_listener_t *listener)
{
    if (listener)
    {
        s_listener = *listener;
    }
    else
    {
        memset(&s_listener, 0, sizeof(s_listener));
    }
}

esp_err_t text_pager_open_book(const char *path, size_t byte_offset)
{
    if (!path || !s_mutex)
    {
        return ESP_ERR_INVALID_ARG;
    }

    LOCK();
    text_source_close(&s_src);
    s_pager.book_open = false;

    esp_err_t err = text_source_open_file(&s_src, path);
    if (err != ESP_OK)
    {
        UNLOCK();
        ESP_LOGE(TAG, "failed to open book: %s", path);
        return err;
    }

    s_pager.total_bytes = text_source_size(&s_src);
    strncpy(s_pager.active_book_path, path, sizeof(s_pager.active_book_path) - 1);
    s_pager.active_book_path[sizeof(s_pager.active_book_path) - 1] = '\0';
    extract_clean_title(path, s_pager.book_title, sizeof(s_pager.book_title));
    s_pager.history_count = 0;

    err = layout_at((byte_offset < s_pager.total_bytes) ? byte_offset : 0);
    if (err != ESP_OK)
    {
        text_source_close(&s_src);
        UNLOCK();
        return err;
    }
    s_pager.book_open = true;
    size_t offset = s_pager.current_offset;
    UNLOCK();

    if (s_listener.on_open)
    {
        s_listener.on_open(path, offset, s_listener.ctx);
    }
    ESP_LOGI(TAG, "opened '%s' (title '%s', %u bytes, offset %u)", path,
             s_pager.book_title, (unsigned)s_pager.total_bytes, (unsigned)offset);
    return ESP_OK;
}

void text_pager_close_book(void)
{
    if (!s_mutex)
    {
        return;
    }
    LOCK();
    text_source_close(&s_src);
    s_pager.book_open = false;
    s_pager.history_count = 0;
    s_pager.active_book_path[0] = '\0';
    s_pager.book_title[0] = '\0';
    s_pager.total_bytes = 0;
    s_pager.current_offset = 0;
    memset(&s_pager.current_page, 0, sizeof(s_pager.current_page));
    memset(&s_layout_page, 0, sizeof(s_layout_page));
    UNLOCK();
}

bool text_pager_is_book_open(void)
{
    return s_pager.book_open;
}

esp_err_t text_pager_set_font_size(text_pager_font_size_t font_size)
{
    if (!s_mutex)
    {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_OK;
    LOCK();
    if (s_pager.font_size != font_size)
    {
        s_pager.font_size = font_size;
        s_pager.history_count = 0;
        if (s_pager.book_open)
        {
            err = layout_at(s_pager.current_offset);
        }
    }
    UNLOCK();
    return err;
}

text_pager_font_size_t text_pager_get_font_size(void)
{
    text_pager_font_size_t size = TEXT_PAGER_FONT_16;
    if (s_mutex)
    {
        LOCK();
        size = s_pager.font_size;
        UNLOCK();
    }
    return size;
}

esp_err_t text_pager_seek(size_t byte_offset)
{
    if (!s_mutex || !s_pager.book_open)
    {
        return ESP_ERR_INVALID_STATE;
    }
    LOCK();
    if (byte_offset >= s_pager.total_bytes)
    {
        byte_offset = (s_pager.total_bytes > 0) ? s_pager.total_bytes - 1 : 0;
    }
    s_pager.history_count = 0;
    esp_err_t err = layout_at(byte_offset);
    size_t offset = s_pager.current_offset;
    UNLOCK();

    if (err == ESP_OK)
    {
        notify_offset(offset);
    }
    return err;
}

esp_err_t text_pager_next_page(void)
{
    if (!s_mutex || !s_pager.book_open)
    {
        return ESP_ERR_INVALID_STATE;
    }
    LOCK();
    if (s_pager.current_page.is_eof)
    {
        UNLOCK();
        return ESP_ERR_NOT_FOUND;
    }

    size_t from = s_pager.current_offset;
    size_t saved_count = s_pager.history_count;
    (void)current_cfg(); /* drop stale history before pushing */
    history_push(from);

    esp_err_t err = layout_at(s_pager.current_page.page_end_offset);
    if (err != ESP_OK)
    {
        s_pager.history_count = saved_count < s_pager.history_count ? saved_count
                                                                    : s_pager.history_count;
        UNLOCK();
        return err;
    }
    size_t offset = s_pager.current_offset;
    UNLOCK();

    notify_offset(offset);
    return ESP_OK;
}

esp_err_t text_pager_prev_page(void)
{
    if (!s_mutex || !s_pager.book_open)
    {
        return ESP_ERR_INVALID_STATE;
    }
    LOCK();
    text_layout_cfg_t cfg = current_cfg(); /* may clear history on geometry change */

    size_t target;
    if (s_pager.history_count > 0)
    {
        target = s_pager.history[--s_pager.history_count];
    }
    else if (s_pager.current_offset > 0)
    {
        target = text_layout_prev_offset(&s_src, s_pager.current_offset, &cfg);
    }
    else
    {
        UNLOCK();
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = layout_at(target);
    size_t offset = s_pager.current_offset;
    UNLOCK();

    if (err == ESP_OK)
    {
        notify_offset(offset);
    }
    return err;
}

const text_pager_page_t *text_pager_get_current_page(void)
{
    return s_pager.book_open ? &s_pager.current_page : NULL;
}

size_t text_pager_get_current_offset(void)
{
    return s_pager.current_offset;
}

size_t text_pager_get_total_bytes(void)
{
    return s_pager.total_bytes;
}

const text_page_t *text_pager_get_layout_page(void)
{
    return s_pager.book_open ? &s_layout_page : NULL;
}

const char *text_pager_get_title(void)
{
    return s_pager.book_title;
}

const ui_pager_style_t *text_pager_get_style(void)
{
    return style_for(s_pager.font_size);
}