#include "view_menu.h"
#include <stdio.h>
#include "esp_attr.h"
#include "epaper_port.h"
#include "GUI_Paint.h"
#include "fonts.h"
#include "text_pager.h"
#include "book_library.h"
#include "app_config.h"
#include "power_mgr.h"
#include "view_reading.h"
#include "view_library.h"
#include "view_ble_sync.h"
#include "view_nav_hints.h"

#if CONFIG_BOARD_MUSHAF_ENABLE
#include "view_mushaf.h"
#endif

typedef enum {
    MENU_ITEM_CONTINUE = 0,
    MENU_ITEM_LIBRARY,
#if CONFIG_BOARD_MUSHAF_ENABLE
    MENU_ITEM_MUSHAF,
#endif
    MENU_ITEM_FONT_SIZE,
    MENU_ITEM_SYNC,
    MENU_ITEM_NAV_HINTS,
    MENU_ITEM_REFRESH,
    MENU_ITEM_POWER_OFF,
    MENU_ITEM_COUNT
} menu_item_id_t;

RTC_DATA_ATTR static int s_rtc_menu_index = 0;
static size_t s_menu_list_start_idx = 0;

int view_menu_get_index(void) {
    return s_rtc_menu_index;
}

void view_menu_set_index(int index) {
    s_rtc_menu_index = index;
}

static bool menu_range_fits(const char *const *items, size_t start, size_t target, int max_width, int bottom_limit) {
    int cur_y = 24;
    for (size_t i = start; i <= target; i++) {
        int lines = ui_get_wrapped_line_count(items[i], &Font12, max_width, 3);
        int text_h = lines * Font12.Height;
        if (cur_y + text_h + 2 >= bottom_limit) {
            return false;
        }
        cur_y += (lines > 1) ? (text_h + 6) : 22;
    }
    return true;
}

void view_menu_render(bool full_refresh) {
    uint8_t *canvas = reader_ui_get_canvas();
    if (!canvas) return;

    epaper_power_on();

    Paint_SelectImage(canvas);
    Paint_Clear(WHITE);

    // Title Bar - Solid black box with white text
    Paint_DrawRectangle(0, 0, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(8, 3, "READER MENU", &Font12, WHITE, BLACK);

    char bat_str[16];
    snprintf(bat_str, sizeof(bat_str), "%u%%", power_mgr_get_battery_percentage());
    Paint_DrawString_EN(165, 3, bat_str, &Font12, WHITE, BLACK);

    Paint_DrawLine(0, UI_HEADER_HEIGHT + 1, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT + 1, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    char font_str[32];
    if (text_pager_get_font_size() == TEXT_PAGER_FONT_12) {
        snprintf(font_str, sizeof(font_str), "Font Size: [12px]");
    } else {
        snprintf(font_str, sizeof(font_str), "Font Size: [16px]");
    }

    const char *items[MENU_ITEM_COUNT] = {
        [MENU_ITEM_CONTINUE]  = "Continue Reading",
        [MENU_ITEM_LIBRARY]   = "Book Library",
#if CONFIG_BOARD_MUSHAF_ENABLE
        [MENU_ITEM_MUSHAF]    = "Mushaf",
#endif
        [MENU_ITEM_FONT_SIZE] = font_str,
        [MENU_ITEM_SYNC]      = "Sync Books",
        [MENU_ITEM_NAV_HINTS] = "Nav Hints",
        [MENU_ITEM_REFRESH]   = "Refresh Screen",
        [MENU_ITEM_POWER_OFF] = "Power Off"
    };

    int selected_index = s_rtc_menu_index;
    if (selected_index < 0) {
        selected_index = 0;
    } else if (selected_index >= MENU_ITEM_COUNT) {
        selected_index = MENU_ITEM_COUNT - 1;
    }
    s_rtc_menu_index = selected_index;

    int max_width = DISPLAY_WIDTH - 24;
    int bottom_limit = reader_ui_get_show_nav_hints() ? (UI_FOOTER_Y - 2) : (DISPLAY_HEIGHT - 2);

    if (s_menu_list_start_idx >= MENU_ITEM_COUNT) {
        s_menu_list_start_idx = 0;
    }
    if ((size_t)selected_index < s_menu_list_start_idx) {
        s_menu_list_start_idx = (size_t)selected_index;
    }
    while (s_menu_list_start_idx < (size_t)selected_index &&
           !menu_range_fits(items, s_menu_list_start_idx, (size_t)selected_index, max_width, bottom_limit)) {
        s_menu_list_start_idx++;
    }

    int y = 24;
    for (size_t i = s_menu_list_start_idx; i < MENU_ITEM_COUNT; i++) {
        int lines = ui_get_wrapped_line_count(items[i], &Font12, max_width, 3);
        int text_h = lines * Font12.Height;

        if (y + text_h + 2 >= bottom_limit) {
            break;
        }

        if ((int)i == selected_index) {
            Paint_DrawRectangle(4, y - 2, DISPLAY_WIDTH - 4, y + text_h + 2, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
            Paint_DrawString_EN(8, y, ">", &Font12, BLACK, WHITE);
        }
        ui_draw_wrapped_text_clamped(22, y, items[i], &Font12, max_width, 3, BLACK, WHITE);
        y += (lines > 1) ? (text_h + 6) : 22;
    }

    // Footer
    if (reader_ui_get_show_nav_hints()) {
        Paint_DrawLine(0, UI_FOOTER_Y - 2, DISPLAY_WIDTH - 1, UI_FOOTER_Y - 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        ui_draw_centered_string(UI_FOOTER_Y + 1, "▲/▼ Move | 2x▲/▼ Select", &Font12);
    }

    reader_ui_flush_canvas(full_refresh);
}

void reader_ui_render_menu(int selected_index) {
    s_rtc_menu_index = selected_index;
    view_menu_render(false);
}

void reader_ui_render_menu_full(int selected_index) {
    s_rtc_menu_index = selected_index;
    view_menu_render(true);
}

reader_view_t view_menu_on_wake(void) {
    if (s_rtc_menu_index < 0 || s_rtc_menu_index >= MENU_ITEM_COUNT) {
        s_rtc_menu_index = 0;
    }
    return VIEW_MENU;
}

reader_view_t view_menu_handle_event(button_event_t evt) {
    if (evt.type == BTN_EVENT_CUSTOM_REFRESH) {
        view_menu_render(false);
        return VIEW_MENU;
    }

    if (evt.type == BTN_CLICK) {
        if (evt.button == BTN_ID_BOOT) {
            s_rtc_menu_index = (s_rtc_menu_index - 1 + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
            view_menu_render(false);
        } else if (evt.button == BTN_ID_PWR) {
            s_rtc_menu_index = (s_rtc_menu_index + 1) % MENU_ITEM_COUNT;
            view_menu_render(false);
        }
    } else if (evt.type == BTN_DOUBLE_CLICK) {
        switch ((menu_item_id_t)s_rtc_menu_index) {
            case MENU_ITEM_CONTINUE:
                view_reading_render(false);
                return VIEW_READING;
            case MENU_ITEM_LIBRARY:
                book_library_scan();
                view_library_set_index(0);
                view_library_render(false);
                return VIEW_BOOK_LIST;
#if CONFIG_BOARD_MUSHAF_ENABLE
            case MENU_ITEM_MUSHAF:
                view_mushaf_enter_menu();
                return VIEW_MUSHAF_MENU;
#endif
            case MENU_ITEM_FONT_SIZE: {
                text_pager_font_size_t f = text_pager_get_font_size();
                text_pager_font_size_t new_font = (f == TEXT_PAGER_FONT_16 ? TEXT_PAGER_FONT_12 : TEXT_PAGER_FONT_16);
                text_pager_set_font_size(new_font);
                app_settings_save_font_size(new_font == TEXT_PAGER_FONT_12 ? 0 : 1);
                view_menu_render(false);
                return VIEW_MENU;
            }
            case MENU_ITEM_SYNC:
                book_library_scan();
                view_ble_sync_enter();
                return VIEW_BLE_SYNC;
            case MENU_ITEM_NAV_HINTS:
                view_nav_hints_render();
                return VIEW_NAV_HINTS;
            case MENU_ITEM_REFRESH:
                view_reading_reset_redraw_counter();
                view_menu_render(true);
                return VIEW_MENU;
            case MENU_ITEM_POWER_OFF:
                reader_ui_render_sleeping();
                app_enter_deep_sleep();
                return VIEW_MENU;
            default:
                break;
        }
    }

    return VIEW_MENU;
}
