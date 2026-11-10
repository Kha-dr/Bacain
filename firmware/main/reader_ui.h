#ifndef READER_UI_H
#define READER_UI_H

#include <stdint.h>
#include <stdbool.h>
#include "board_config.h"
#include "esp_err.h"
#include "GUI_Paint.h"
#include "fonts.h"
#include "book_library.h"
#include "text_pager.h"
#include "views.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef file_size
#define file_size total_bytes
#endif

typedef enum {
    MENU_ACTION_NONE = 0,
    MENU_ACTION_RESUME,
    MENU_ACTION_BOOK_LIST,
#if CONFIG_BOARD_MUSHAF_ENABLE || BOARD_MUSHAF_ENABLE
    MENU_ACTION_MUSHAF,
#endif
    MENU_ACTION_TOGGLE_FONT,
    MENU_ACTION_BLE_SYNC,
    MENU_ACTION_NAV_HINTS,
    MENU_ACTION_REFRESH_SCREEN,
    MENU_ACTION_SLEEP,
} menu_action_t;

esp_err_t reader_ui_init(bool is_cold_boot);
uint8_t  *reader_ui_get_canvas(void);
void      reader_ui_flush_canvas(bool full_refresh);

void      app_settings_load(bool force_nvs_reload);
void      app_settings_save_font_size(uint8_t font_size);
void      app_settings_save_nav_hints(bool show);
uint8_t   reader_ui_get_battery_percentage(void);

void      reader_ui_set_rotation(uint8_t rotation);
uint8_t   reader_ui_get_rotation(void);
bool      reader_ui_get_show_nav_hints(void);
void      reader_ui_set_show_nav_hints(bool show);

void      ui_draw_string(UWORD x, UWORD y, const char *str, sFONT *font, UWORD fg, UWORD bg);
int       ui_get_string_width(const char *str, sFONT *font);
void      ui_draw_centered_string(int y, const char *str, sFONT *font);
int       ui_get_wrapped_line_count(const char *text, sFONT *font, int max_width, int max_lines);
int       ui_draw_wrapped_text_clamped(UWORD x, UWORD y, const char *text, sFONT *font, int max_width, int max_lines, UWORD fg, UWORD bg);
void      ui_draw_bacain_icon(int center_x, int top_y);

void      reader_ui_render_reading(void);
void      reader_ui_render_reading_full(void);
void      reader_ui_render_reading_force_full(void);
void      reader_ui_render_menu(int selected_index);
void      reader_ui_render_menu_full(int selected_index);
void      reader_ui_render_book_list(int selected_index);
void      reader_ui_render_book_list_full(int selected_index);
void      reader_ui_render_bookmarks(int selected_index);
void      reader_ui_render_bookmarks_full(int selected_index);
void      reader_ui_render_nav_hints(void);
void      reader_ui_render_ble_sync(bool is_connected, uint32_t bytes_received, const char *status_override);
/**
 * @brief Render splash screen with official BacaIn icon and title/subtitle.
 */
void      reader_ui_render_splash(const char *title, const char *subtitle);

/**
 * @brief Render power off screen with official BacaIn icon and turn-on guide.
 */
void      reader_ui_render_power_off(void);

/**
 * @brief Get copy of current reading state.
 */
const reading_state_t *book_library_get_state(void);

/**
 * @brief Compatibility helper to seek text pager to offset.
 */
static inline esp_err_t text_pager_open_offset(size_t byte_offset) {
    return text_pager_seek(byte_offset);
}

#ifdef __cplusplus
}
#endif

#endif // READER_UI_H
