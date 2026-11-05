#include "view_menu.h"
#include <stdio.h>
#include "esp_attr.h"
#include "epaper_port.h"
#include "GUI_Paint.h"
#include "fonts.h"
#include "text_pager.h"
#include "book_library.h"
#include "power_mgr.h"
#include "app_config.h"
#include "view_reading.h"
#include "view_library.h"
#include "view_ble_sync.h"
#include "view_nav_hints.h"

#define MENU_ITEM_COUNT 7

RTC_DATA_ATTR static int s_rtc_menu_index = 0;

int view_menu_get_index(void) {
    return s_rtc_menu_index;
}

void view_menu_set_index(int index) {
    s_rtc_menu_index = index;
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

    // Menu Options
    const char *items[MENU_ITEM_COUNT] = {
        "Continue Reading",
        "Book Library",
        "Font Size: [16px]",
        "Sync Books",
        "Nav Hints",
        "Refresh Screen",
        "Power Off"
    };

    char font_str[32];
    if (text_pager_get_font_size() == TEXT_PAGER_FONT_12) {
        snprintf(font_str, sizeof(font_str), "Font Size: [12px]");
    } else {
        snprintf(font_str, sizeof(font_str), "Font Size: [16px]");
    }
    items[2] = font_str;

    int max_width = DISPLAY_WIDTH - 24;
    int bottom_limit = reader_ui_get_show_nav_hints() ? (UI_FOOTER_Y - 2) : (DISPLAY_HEIGHT - 2);
    int y = 24;
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        int lines = ui_get_wrapped_line_count(items[i], &Font12, max_width, 3);
        int text_h = lines * Font12.Height;

        if (y + text_h + 2 >= bottom_limit) {
            break;
        }

        if (i == s_rtc_menu_index) {
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

reader_view_t view_menu_on_wake(wakeup_reason_t wakeup) {
    if (s_rtc_menu_index < 0 || s_rtc_menu_index >= MENU_ITEM_COUNT) {
        s_rtc_menu_index = 0;
    }
    if (wakeup == WAKEUP_REASON_BOOT_BUTTON) {
        s_rtc_menu_index = (s_rtc_menu_index - 1 + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
    } else if (wakeup == WAKEUP_REASON_PWR_BUTTON) {
        s_rtc_menu_index = (s_rtc_menu_index + 1) % MENU_ITEM_COUNT;
    }
    view_menu_render(false);
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
        if (s_rtc_menu_index == 0) {
            view_reading_render(false);
            return VIEW_READING;
        } else if (s_rtc_menu_index == 1) {
            book_library_scan();
            view_library_set_index(0);
            view_library_render(false);
            return VIEW_BOOK_LIST;
        } else if (s_rtc_menu_index == 2) {
            text_pager_font_size_t f = text_pager_get_font_size();
            text_pager_font_size_t new_font = (f == TEXT_PAGER_FONT_16 ? TEXT_PAGER_FONT_12 : TEXT_PAGER_FONT_16);
            text_pager_set_font_size(new_font);
            app_settings_save_font_size(new_font == TEXT_PAGER_FONT_12 ? 0 : 1);
            view_menu_render(false);
            return VIEW_MENU;
        } else if (s_rtc_menu_index == 3) {
            book_library_scan();
            view_ble_sync_enter();
            return VIEW_BLE_SYNC;
        } else if (s_rtc_menu_index == 4) {
            view_nav_hints_render();
            return VIEW_NAV_HINTS;
        } else if (s_rtc_menu_index == 5) {
            view_reading_reset_redraw_counter();
            view_menu_render(true);
            return VIEW_MENU;
        } else if (s_rtc_menu_index == 6) {
            reader_ui_render_sleeping();
            app_enter_deep_sleep();
            return VIEW_MENU;
        }
    }

    return VIEW_MENU;
}
