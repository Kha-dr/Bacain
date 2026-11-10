#include "view_bookmarks.h"
#include <stdio.h>
#include "esp_attr.h"
#include "epaper_port.h"
#include "GUI_Paint.h"
#include "fonts.h"
#include "book_library.h"
#include "text_pager.h"
#include "app_config.h"
#include "view_reading.h"

RTC_DATA_ATTR static int s_rtc_bookmark_index = 0;

int view_bookmarks_get_index(void) {
    return s_rtc_bookmark_index;
}

void view_bookmarks_set_index(int index) {
    s_rtc_bookmark_index = index;
}

void view_bookmarks_render(bool full_refresh) {
    uint8_t *canvas = reader_ui_get_canvas();
    if (!canvas) return;

    epaper_power_on();

    Paint_SelectImage(canvas);
    Paint_Clear(WHITE);

    const reading_state_t *st = book_library_get_state();
    size_t count = st ? st->bookmark_count : 0;

    // Header: Solid black box at top with height UI_HEADER_HEIGHT
    Paint_DrawRectangle(0, 0, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    char title_buf[32];
    snprintf(title_buf, sizeof(title_buf), "BOOKMARKS (%u)", (unsigned)count);
    Paint_DrawString_EN(8, 3, title_buf, &Font12, WHITE, BLACK);
    Paint_DrawLine(0, UI_HEADER_HEIGHT + 1, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT + 1, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    if (count == 0) {
        Paint_DrawString_EN(20, 80, "No bookmarks yet.", &Font12, BLACK, WHITE);
        ui_draw_string(14, 104, "2xTap ▲ in book to add", &Font12, BLACK, WHITE);
    } else {
        int selected_index = s_rtc_bookmark_index;
        if (selected_index < 0) {
            selected_index = 0;
        } else if (selected_index >= (int)count) {
            selected_index = (int)count - 1;
        }
        s_rtc_bookmark_index = selected_index;

        size_t start_idx = 0;
        if (count > 6) {
            if (selected_index >= 5) {
                start_idx = (size_t)(selected_index - 4);
            }
            if (start_idx + 6 > count) {
                start_idx = count - 6;
            }
        }

        size_t end_idx = start_idx + 6;
        if (end_idx > count) {
            end_idx = count;
        }

        int y = 28;
        for (size_t i = start_idx; i < end_idx; i++) {
            uint32_t pct = (st->file_size > 0) ? (uint32_t)((uint64_t)st->bookmarks[i] * 100 / st->file_size) : 0;
            char item_str[32];
            snprintf(item_str, sizeof(item_str), "%u. Bookmark (%u%%)", (unsigned)(i + 1), (unsigned)pct);

            if ((int)i == selected_index) {
                Paint_DrawRectangle(4, y - 2, DISPLAY_WIDTH - 4, y + 16, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
                Paint_DrawString_EN(8, y, ">", &Font12, BLACK, WHITE);
            }
            Paint_DrawString_EN(22, y, item_str, &Font12, BLACK, WHITE);
            y += 24;
        }
    }

    // Footer
    if (reader_ui_get_show_nav_hints()) {
        Paint_DrawLine(0, UI_FOOTER_Y - 2, DISPLAY_WIDTH - 1, UI_FOOTER_Y - 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        ui_draw_centered_string(UI_FOOTER_Y + 1, "2x▲/▼ Jump | Hold ▲/▼ Back", &Font12);
    }

    reader_ui_flush_canvas(full_refresh);
}

void reader_ui_render_bookmarks(int selected_index) {
    s_rtc_bookmark_index = selected_index;
    view_bookmarks_render(false);
}

void reader_ui_render_bookmarks_full(int selected_index) {
    s_rtc_bookmark_index = selected_index;
    view_bookmarks_render(true);
}

reader_view_t view_bookmarks_on_wake(void) {
    const reading_state_t *st = book_library_get_state();
    int count = (st != NULL) ? (int)st->bookmark_count : 0;
    if (count > 0) {
        if (s_rtc_bookmark_index < 0 || s_rtc_bookmark_index >= count) {
            s_rtc_bookmark_index = 0;
        }
    } else {
        s_rtc_bookmark_index = 0;
    }
    return VIEW_BOOKMARKS;
}

reader_view_t view_bookmarks_handle_event(button_event_t evt) {
    if (evt.type == BTN_EVENT_CUSTOM_REFRESH) {
        view_bookmarks_render(false);
        return VIEW_BOOKMARKS;
    }

    const reading_state_t *st = book_library_get_state();
    size_t count = (st != NULL) ? st->bookmark_count : 0;
    if (evt.type == BTN_CLICK) {
        if (count > 0) {
            if (evt.button == BTN_ID_BOOT) {
                s_rtc_bookmark_index = (s_rtc_bookmark_index - 1 + (int)count) % (int)count;
            } else if (evt.button == BTN_ID_PWR) {
                s_rtc_bookmark_index = (s_rtc_bookmark_index + 1) % (int)count;
            }
            view_bookmarks_render(false);
        }
    } else if (evt.type == BTN_DOUBLE_CLICK) {
        if (count > 0 && s_rtc_bookmark_index >= 0 && s_rtc_bookmark_index < (int)count) {
            size_t target_offset = st->bookmarks[s_rtc_bookmark_index];
            view_reading_ensure_book_open();
            text_pager_open_offset(target_offset);
            book_library_update_offset(target_offset);
        }
        view_reading_render(false);
        return VIEW_READING;
    } else if (evt.type == BTN_LONG_PRESS) {
        view_reading_render(false);
        return VIEW_READING;
    }

    return VIEW_BOOKMARKS;
}
