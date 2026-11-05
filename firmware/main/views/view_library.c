#include "view_library.h"
#include <stdio.h>
#include "esp_attr.h"
#include "epaper_port.h"
#include "GUI_Paint.h"
#include "fonts.h"
#include "book_library.h"
#include "text_pager.h"
#include "app_config.h"
#include "view_reading.h"
#include "view_menu.h"

RTC_DATA_ATTR static int s_rtc_book_index = 0;
static size_t s_book_list_start_idx = 0;

int view_library_get_index(void) {
    return s_rtc_book_index;
}

void view_library_set_index(int index) {
    s_rtc_book_index = index;
}

static void get_book_display_name(size_t index, char *buf, size_t buf_size) {
    const book_info_t *book = book_library_get_book(index);
    if (!book) {
        buf[0] = '\0';
        return;
    }
    if (book->type == LIBRARY_ENTRY_UP) {
        snprintf(buf, buf_size, "[.. Back]");
    } else if (book->type == LIBRARY_ENTRY_DIR) {
        snprintf(buf, buf_size, "[D] %s", book->filename);
    } else {
        snprintf(buf, buf_size, "%s", book->filename);
    }
}

static int get_book_item_height(size_t index, sFONT *font, int max_width, int max_lines) {
    char name[BOOK_FILENAME_MAX_LEN + 16];
    get_book_display_name(index, name, sizeof(name));
    int lines = ui_get_wrapped_line_count(name, font, max_width, max_lines);
    return lines * font->Height;
}

static bool book_range_fits(size_t start, size_t target, sFONT *font, int max_width, int max_lines) {
    int cur_y = 26;
    int bottom_limit = reader_ui_get_show_nav_hints() ? (UI_FOOTER_Y - 2) : (DISPLAY_HEIGHT - 2);
    for (size_t i = start; i <= target; i++) {
        int text_h = get_book_item_height(i, font, max_width, max_lines);
        if (cur_y + text_h + 2 >= bottom_limit) {
            return false;
        }
        cur_y += text_h + 8;
    }
    return true;
}

void view_library_render(bool full_refresh) {
    uint8_t *canvas = reader_ui_get_canvas();
    if (!canvas) return;

    epaper_power_on();

    Paint_SelectImage(canvas);
    Paint_Clear(WHITE);

    // Title Bar - Solid black box with white text
    Paint_DrawRectangle(0, 0, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    char title_buf[32];
    if (book_library_is_root_dir()) {
        snprintf(title_buf, sizeof(title_buf), "SELECT BOOK [/]");
    } else {
        const char *rel_path = book_library_get_current_rel_path();
        snprintf(title_buf, sizeof(title_buf), "DIR: %.16s", (rel_path && rel_path[0]) ? rel_path : "/");
    }
    Paint_DrawString_EN(8, 3, title_buf, &Font12, WHITE, BLACK);
    Paint_DrawLine(0, UI_HEADER_HEIGHT + 1, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT + 1, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    size_t count = book_library_get_count();
    if (count == 0) {
        s_book_list_start_idx = 0;
        Paint_DrawString_EN(20, 80, "No books in folder!", &Font12, BLACK, WHITE);
        Paint_DrawString_EN(20, 100, "Open BacaIn Web to add", &Font12, BLACK, WHITE);
    } else {
        int selected_index = s_rtc_book_index;
        if (selected_index < 0) {
            selected_index = 0;
        } else if (selected_index >= (int)count) {
            selected_index = (int)count - 1;
        }
        s_rtc_book_index = selected_index;

        text_pager_font_size_t font_size = text_pager_get_font_size();
        sFONT *font = (font_size == TEXT_PAGER_FONT_16) ? &Font16 : &Font12;
        int max_lines = (font_size == TEXT_PAGER_FONT_16) ? 2 : 3;
        int max_width = DISPLAY_WIDTH - 24;

        if (s_book_list_start_idx >= count) {
            s_book_list_start_idx = 0;
        }
        if ((size_t)selected_index < s_book_list_start_idx) {
            s_book_list_start_idx = (size_t)selected_index;
        }
        while (s_book_list_start_idx < (size_t)selected_index &&
               !book_range_fits(s_book_list_start_idx, (size_t)selected_index, font, max_width, max_lines)) {
            s_book_list_start_idx++;
        }

        int bottom_limit = reader_ui_get_show_nav_hints() ? (UI_FOOTER_Y - 2) : (DISPLAY_HEIGHT - 2);
        int y = 26;
        for (size_t i = s_book_list_start_idx; i < count; i++) {
            char display_name[BOOK_FILENAME_MAX_LEN + 16];
            get_book_display_name(i, display_name, sizeof(display_name));

            int text_h = get_book_item_height(i, font, max_width, max_lines);
            if (y + text_h + 2 >= bottom_limit) {
                break;
            }

            if ((int)i == selected_index) {
                Paint_DrawRectangle(4, y - 2, DISPLAY_WIDTH - 4, y + text_h + 2, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
                Paint_DrawString_EN(8, y, ">", font, BLACK, WHITE);
            }

            ui_draw_wrapped_text_clamped(22, y, display_name, font, max_width, max_lines, BLACK, WHITE);
            y += text_h + 8;
        }
    }

    // Footer
    if (reader_ui_get_show_nav_hints()) {
        Paint_DrawLine(0, UI_FOOTER_Y - 2, DISPLAY_WIDTH - 1, UI_FOOTER_Y - 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        ui_draw_centered_string(UI_FOOTER_Y + 1, "2x▲/▼ Open | Hold ▲/▼ Back", &Font12);
    }

    reader_ui_flush_canvas(full_refresh);
}

void reader_ui_render_book_list(int selected_index) {
    s_rtc_book_index = selected_index;
    view_library_render(false);
}

void reader_ui_render_book_list_full(int selected_index) {
    s_rtc_book_index = selected_index;
    view_library_render(true);
}

reader_view_t view_library_on_wake(wakeup_reason_t wakeup) {
    if (book_library_get_count() == 0) {
        book_library_scan();
    }
    int count = (int)book_library_get_count();
    if (count > 0) {
        if (s_rtc_book_index < 0 || s_rtc_book_index >= count) {
            s_rtc_book_index = 0;
        }
        if (wakeup == WAKEUP_REASON_BOOT_BUTTON) {
            s_rtc_book_index = (s_rtc_book_index - 1 + count) % count;
        } else if (wakeup == WAKEUP_REASON_PWR_BUTTON) {
            s_rtc_book_index = (s_rtc_book_index + 1) % count;
        }
    } else {
        s_rtc_book_index = 0;
    }
    view_library_render(false);
    return VIEW_BOOK_LIST;
}

reader_view_t view_library_handle_event(button_event_t evt) {
    if (evt.type == BTN_EVENT_CUSTOM_REFRESH) {
        view_library_render(false);
        return VIEW_BOOK_LIST;
    }

    size_t count = book_library_get_count();
    if (evt.type == BTN_CLICK) {
        if (count > 0) {
            if (evt.button == BTN_ID_BOOT) {
                s_rtc_book_index = (s_rtc_book_index - 1 + (int)count) % (int)count;
            } else if (evt.button == BTN_ID_PWR) {
                s_rtc_book_index = (s_rtc_book_index + 1) % (int)count;
            }
            view_library_render(false);
        }
    } else if (evt.type == BTN_DOUBLE_CLICK) {
        if (count > 0) {
            const book_info_t *book = book_library_get_book(s_rtc_book_index);
            if (book) {
                if (book->type == LIBRARY_ENTRY_UP) {
                    book_library_navigate_up();
                    s_rtc_book_index = 0;
                    view_library_render(false);
                } else if (book->type == LIBRARY_ENTRY_DIR) {
                    book_library_enter_dir(book->filename);
                    s_rtc_book_index = 0;
                    view_library_render(false);
                } else if (book->type == LIBRARY_ENTRY_FILE) {
                    if (text_pager_open_book(book->path, 0) == ESP_OK) {
                        book_library_set_active_book(book->path, true);
                        view_reading_render(false);
                        return VIEW_READING;
                    }
                }
            }
        }
    } else if (evt.type == BTN_LONG_PRESS) {
        view_menu_render(false);
        return VIEW_MENU;
    }

    return VIEW_BOOK_LIST;
}
