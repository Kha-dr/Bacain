#ifndef READER_UI_H
#define READER_UI_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "book_library.h"
#include "text_pager.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef file_size
#define file_size total_bytes
#endif

typedef enum {
    VIEW_READING = 0,
    VIEW_MENU,
    VIEW_BOOK_LIST,
    VIEW_BLE_SYNC,
    VIEW_BOOKMARKS,
    VIEW_NAV_HINTS,
} reader_view_t;

typedef enum {
    MENU_ACTION_NONE = 0,
    MENU_ACTION_RESUME,
    MENU_ACTION_BOOK_LIST,
    MENU_ACTION_TOGGLE_FONT,
    MENU_ACTION_BLE_SYNC,
    MENU_ACTION_NAV_HINTS,
    MENU_ACTION_REFRESH_SCREEN,
    MENU_ACTION_SLEEP,
} menu_action_t;

esp_err_t reader_ui_init(bool is_cold_boot);
void      reader_ui_set_rotation(uint8_t rotation);
uint8_t   reader_ui_get_rotation(void);
bool      reader_ui_get_show_nav_hints(void);
void      reader_ui_set_show_nav_hints(bool show);
void      reader_ui_render_reading(void);
void      reader_ui_render_reading_full(void);
void      reader_ui_render_reading_force_full(void);
void      reader_ui_render_menu(int selected_index);
void      reader_ui_render_menu_full(int selected_index);
void      reader_ui_render_book_list(int selected_index);
void      reader_ui_render_bookmarks(int selected_index);
void      reader_ui_render_nav_hints(void);
void      reader_ui_render_ble_sync(bool is_connected, uint32_t bytes_received);
/**
 * @brief Render splash screen with official BacaIn icon and title/subtitle.
 */
void      reader_ui_render_splash(const char *title, const char *subtitle);

/**
 * @brief Render sleeping screen with official BacaIn icon and wake guide.
 */
void      reader_ui_render_sleeping(void);

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
