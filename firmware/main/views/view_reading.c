#include "view_reading.h"
#include <stdio.h>
#include "esp_attr.h"
#include "epaper_port.h"
#include "GUI_Paint.h"
#include "fonts.h"
#include "text_pager.h"
#include "book_library.h"
#include "power_mgr.h"
#include "app_config.h"
#include "view_menu.h"
#include "view_bookmarks.h"

RTC_DATA_ATTR static uint32_t s_redraw_counter = 0;

void view_reading_reset_redraw_counter(void) {
    s_redraw_counter = 0;
}

void view_reading_render(bool full_refresh) {
    uint8_t *canvas = reader_ui_get_canvas();
    if (!canvas) return;

    epaper_power_on();

    Paint_SelectImage(canvas);
    text_pager_render_to_buffer(canvas);

    // Add Battery & Status to top header right corner
    char bat_str[16];
    snprintf(bat_str, sizeof(bat_str), "%u%%", power_mgr_get_battery_percentage());
    Paint_DrawString_EN(165, 2, bat_str, &Font12, BLACK, WHITE);

    reader_ui_flush_canvas(full_refresh);
}

void reader_ui_render_reading(void) {
    view_reading_render(false);
}

void reader_ui_render_reading_full(void) {
    view_reading_render(true);
}

void reader_ui_render_reading_force_full(void) {
    view_reading_render(true);
}

void view_reading_render_page_turn(void) {
    s_redraw_counter++;
    if (APP_FULL_REFRESH_INTERVAL > 0 && s_redraw_counter >= APP_FULL_REFRESH_INTERVAL) {
        s_redraw_counter = 0;
        view_reading_render(true);
    } else {
        view_reading_render(false);
    }
}

reader_view_t view_reading_on_wake(wakeup_reason_t wakeup, bool book_opened) {
    if (book_opened) {
        s_redraw_counter = 0;
        if (wakeup != WAKEUP_REASON_BOOT_BUTTON && wakeup != WAKEUP_REASON_PWR_BUTTON) {
            view_reading_render(false);
        }
        return VIEW_READING;
    }

    view_menu_set_index(0);
    if (wakeup != WAKEUP_REASON_BOOT_BUTTON && wakeup != WAKEUP_REASON_PWR_BUTTON) {
        view_menu_render(false);
    }
    return VIEW_MENU;
}

reader_view_t view_reading_handle_event(button_event_t evt) {
    if (evt.type == BTN_EVENT_CUSTOM_REFRESH) {
        view_reading_render(true);
        return VIEW_READING;
    }

    if (evt.type == BTN_LONG_PRESS) {
        view_menu_set_index(0);
        view_menu_render(false);
        return VIEW_MENU;
    } else if (evt.button == BTN_ID_BOOT) {
        if (evt.type == BTN_CLICK) {
            if (text_pager_prev_page() == ESP_OK) {
                book_library_update_offset(text_pager_get_current_offset());
                view_reading_render_page_turn();
            } else {
                view_reading_render(false);
            }
        } else if (evt.type == BTN_DOUBLE_CLICK) {
            size_t off = text_pager_get_current_offset();
            if (book_library_is_bookmarked(off)) {
                book_library_remove_bookmark(off);
            } else {
                book_library_add_bookmark(off);
            }
            view_reading_render(false);
        }
    } else if (evt.button == BTN_ID_PWR) {
        if (evt.type == BTN_CLICK) {
            if (text_pager_next_page() == ESP_OK) {
                book_library_update_offset(text_pager_get_current_offset());
                view_reading_render_page_turn();
            } else {
                view_reading_render(false);
            }
        } else if (evt.type == BTN_DOUBLE_CLICK) {
            view_bookmarks_set_index(0);
            view_bookmarks_render(false);
            return VIEW_BOOKMARKS;
        }
    }

    return VIEW_READING;
}
