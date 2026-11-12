#ifndef TEXT_PAGER_H
#define TEXT_PAGER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "text_layout.h"
#include "ui_pager.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define TEXT_PAGER_HISTORY_DEPTH 16
#define TEXT_PAGER_MAX_LINES TEXT_LAYOUT_MAX_LINES     /* 16 */
#define TEXT_PAGER_MAX_LINE_CHARS TEXT_LAYOUT_MAX_COLS /* 48 */

/* Geometry contract: layout and renderer must use the same footer kind. */
#define TEXT_PAGER_FOOTER UI_PAGER_FOOTER_STATUS
#define TEXT_PAGER_KEEP_BLANK false /* books: old behavior, blank lines dropped */

    typedef enum
    {
        TEXT_PAGER_FONT_16 = 0, /* Font16 11x16: 16 cols x 8 lines on 200x200  */
        TEXT_PAGER_FONT_12 = 1, /* Font12  7x12: 26 cols x 11 lines on 200x200 */
    } text_pager_font_size_t;

    typedef struct
    {
        char lines[TEXT_PAGER_MAX_LINES][TEXT_PAGER_MAX_LINE_CHARS + 1]; /* ASCII after folding */
        size_t line_count;
        size_t page_start_offset;
        size_t page_end_offset;
        uint32_t progress_pct;
        bool is_eof;
        bool is_bof;
    } text_pager_page_t;

    typedef struct
    {
        char active_book_path[256];
        char book_title[128];
        size_t total_bytes;
        size_t current_offset; /* always == current_page.page_start_offset */
        text_pager_font_size_t font_size;

        size_t history[TEXT_PAGER_HISTORY_DEPTH];
        size_t history_count;

        text_pager_page_t current_page;
        bool book_open;
    } text_pager_t;

    /* Notifications instead of direct book_library calls. Called outside the pager mutex. */
    typedef struct
    {
        void (*on_open)(const char *path, size_t offset, void *ctx);
        void (*on_offset)(size_t offset, void *ctx);
        void *ctx;
    } text_pager_listener_t;

    esp_err_t text_pager_init(void);
    void text_pager_deinit(void);
    void text_pager_set_listener(const text_pager_listener_t *listener);

    esp_err_t text_pager_open_book(const char *path, size_t byte_offset);
    void text_pager_close_book(void);
    bool text_pager_is_book_open(void);

    /* Re-lays out the current page. Does NOT render. */
    esp_err_t text_pager_set_font_size(text_pager_font_size_t font_size);
    text_pager_font_size_t text_pager_get_font_size(void);

    esp_err_t text_pager_seek(size_t byte_offset);
    esp_err_t text_pager_next_page(void); /* ESP_ERR_NOT_FOUND at EOF */
    esp_err_t text_pager_prev_page(void); /* ESP_ERR_NOT_FOUND at BOF */

    const text_pager_page_t *text_pager_get_current_page(void); /* NULL if no book open */
    size_t text_pager_get_current_offset(void);
    size_t text_pager_get_total_bytes(void);

    /* For renderers: layout-native page, title, style. Valid until the next pager call. */
    const text_page_t *text_pager_get_layout_page(void); /* NULL if no book open */
    const char *text_pager_get_title(void);
    const ui_pager_style_t *text_pager_get_style(void);

#ifdef __cplusplus
}
#endif

#endif /* TEXT_PAGER_H */