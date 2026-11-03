#include "reader_ui.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "epaper_port.h"
#include "GUI_Paint.h"
#include "fonts.h"
#include "text_pager.h"
#include "book_library.h"
#include "power_mgr.h"
#include "app_config.h"

#define TAG "READER_UI"

static uint8_t *s_canvas = NULL;
static int s_reading_page_turns = 0;
static reading_state_t s_cached_reading_state;
static size_t s_book_list_start_idx = 0;
static bool s_show_nav_hints = true;
static uint8_t s_rotation = BOARD_DISPLAY_ROTATION;

void reader_ui_set_rotation(uint8_t rotation) {
    s_rotation = rotation % 4;
    UWORD mapped_rotate = ROTATE_0;
    switch (s_rotation) {
        case 1:
            mapped_rotate = ROTATE_90;
            break;
        case 2:
            mapped_rotate = ROTATE_180;
            break;
        case 3:
            mapped_rotate = ROTATE_270;
            break;
        case 0:
        default:
            mapped_rotate = ROTATE_0;
            break;
    }
    Paint_SetRotate(mapped_rotate);
}

uint8_t reader_ui_get_rotation(void) {
    return s_rotation;
}

bool reader_ui_get_show_nav_hints(void) {
    return s_show_nav_hints;
}

void reader_ui_set_show_nav_hints(bool show) {
    s_show_nav_hints = show;
}

const reading_state_t *book_library_get_state(void) {
    if (book_library_get_current_state(&s_cached_reading_state) == ESP_OK) {
        return &s_cached_reading_state;
    }
    return NULL;
}

static void draw_triangle_up(int center_x, int top_y, int h, UWORD color) {
    if (h <= 0) return;
    for (int i = 0; i < h; i++) {
        int span = i / 2;
        int x0 = center_x - span;
        int x1 = center_x + span;
        int y = top_y + i;
        if (x0 < 0) x0 = 0;
        if (x1 >= DISPLAY_WIDTH) x1 = DISPLAY_WIDTH - 1;
        if (y >= 0 && y < DISPLAY_HEIGHT && x0 <= x1) {
            Paint_DrawLine((UWORD)x0, (UWORD)y, (UWORD)x1, (UWORD)y, color, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        }
    }
}

static void draw_triangle_down(int center_x, int top_y, int h, UWORD color) {
    if (h <= 0) return;
    for (int i = 0; i < h; i++) {
        int span = (h - 1 - i) / 2;
        int x0 = center_x - span;
        int x1 = center_x + span;
        int y = top_y + i;
        if (x0 < 0) x0 = 0;
        if (x1 >= DISPLAY_WIDTH) x1 = DISPLAY_WIDTH - 1;
        if (y >= 0 && y < DISPLAY_HEIGHT && x0 <= x1) {
            Paint_DrawLine((UWORD)x0, (UWORD)y, (UWORD)x1, (UWORD)y, color, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        }
    }
}

static void draw_ui_string(UWORD x, UWORD y, const char *str, sFONT *font, UWORD fg, UWORD bg) {
    if (!str || !font) return;
    const unsigned char *p = (const unsigned char *)str;
    while (*p != '\0') {
        if (p[0] == 0xE2 && p[1] == 0x96 && p[2] == 0xB2) {
            int h = font->Height > 4 ? font->Height - 4 : font->Height;
            int top_y = (int)y + (font->Height - h) / 2;
            int center_x = (int)x + font->Width / 2;
            Paint_DrawRectangle(x, y, x + font->Width - 1, y + font->Height - 1, bg, DOT_PIXEL_1X1, DRAW_FILL_FULL);
            draw_triangle_up(center_x, top_y, h, fg);
            x += font->Width;
            p += 3;
        } else if (p[0] == 0xE2 && p[1] == 0x96 && p[2] == 0xBC) {
            int h = font->Height > 4 ? font->Height - 4 : font->Height;
            int top_y = (int)y + (font->Height - h) / 2;
            int center_x = (int)x + font->Width / 2;
            Paint_DrawRectangle(x, y, x + font->Width - 1, y + font->Height - 1, bg, DOT_PIXEL_1X1, DRAW_FILL_FULL);
            draw_triangle_down(center_x, top_y, h, fg);
            x += font->Width;
            p += 3;
        } else {
            Paint_DrawChar(x, y, (char)*p, font, fg, bg);
            x += font->Width;
            p++;
        }
    }
}

static int get_ui_string_width(const char *str, sFONT *font) {
    if (!str || !font) return 0;
    int glyph_count = 0;
    const unsigned char *p = (const unsigned char *)str;
    while (*p != '\0') {
        if (p[0] == 0xE2 && p[1] == 0x96 && (p[2] == 0xB2 || p[2] == 0xBC)) {
            glyph_count++;
            p += 3;
        } else {
            glyph_count++;
            p++;
        }
    }
    return glyph_count * font->Width;
}

esp_err_t reader_ui_init(void) {
    if (!s_canvas) {
        s_canvas = (uint8_t *)heap_caps_malloc(DISPLAY_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
        if (!s_canvas) {
            s_canvas = (uint8_t *)malloc(DISPLAY_BUFFER_SIZE);
        }
        if (!s_canvas) {
            ESP_LOGE(TAG, "Failed to allocate display canvas buffer");
            return ESP_ERR_NO_MEM;
        }
    }

    UWORD rotation = (s_rotation == 1) ? ROTATE_90 :
                     (s_rotation == 2) ? ROTATE_180 :
                     (s_rotation == 3) ? ROTATE_270 : ROTATE_0;
    Paint_NewImage(s_canvas, DISPLAY_WIDTH, DISPLAY_HEIGHT, rotation, WHITE);
    Paint_SetRotate(rotation);
    Paint_SetScale(2);
    Paint_SelectImage(s_canvas);
    Paint_Clear(WHITE);

    epaper_power_on();
    epaper_port_init();
    epaper_port_clear(WHITE);
    epaper_port_init_partial();
    vTaskDelay(pdMS_TO_TICKS(500));

    return ESP_OK;
}

void reader_ui_render_reading(void) {
    if (!s_canvas) return;

    epaper_power_on();

    Paint_SelectImage(s_canvas);
    text_pager_render_to_buffer(s_canvas);

    // Add Battery & Status to top header right corner
    char bat_str[16];
    snprintf(bat_str, sizeof(bat_str), "%u%%", power_mgr_get_battery_percentage());
    Paint_DrawString_EN(165, 2, bat_str, &Font12, BLACK, WHITE);

    s_reading_page_turns++;
    if (s_reading_page_turns >= 10) {
        epaper_port_init();
        epaper_port_display(s_canvas);
        epaper_port_init_partial();
        s_reading_page_turns = 0;
    } else {
        epaper_port_display_part(s_canvas);
    }
}

void reader_ui_render_reading_force_full(void) {
    if (!s_canvas) return;

    epaper_power_on();

    Paint_SelectImage(s_canvas);
    text_pager_render_to_buffer(s_canvas);

    // Add Battery & Status to top header right corner
    char bat_str[16];
    snprintf(bat_str, sizeof(bat_str), "%u%%", power_mgr_get_battery_percentage());
    Paint_DrawString_EN(165, 2, bat_str, &Font12, BLACK, WHITE);

    s_reading_page_turns = 0;
    epaper_port_init();
    epaper_port_display(s_canvas);
    epaper_port_init_partial();
}

static int break_text_lines(const char *text, int chars_per_line, int max_lines, char lines_out[][64]) {
    if (!text || chars_per_line <= 0 || max_lines <= 0) {
        return 0;
    }
    if (chars_per_line > 60) chars_per_line = 60;

    const char *p = text;
    int line_count = 0;

    while (*p != '\0' && line_count < max_lines) {
        while (*p == ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        bool is_last_line = (line_count == max_lines - 1);
        int rem_len = (int)strlen(p);

        if (!is_last_line) {
            if (rem_len <= chars_per_line) {
                int copy_len = rem_len;
                if (copy_len >= 63) copy_len = 63;
                memcpy(lines_out[line_count], p, copy_len);
                while (copy_len > 0 && lines_out[line_count][copy_len - 1] == ' ') {
                    copy_len--;
                }
                lines_out[line_count][copy_len] = '\0';
                line_count++;
                break;
            } else {
                int break_pos = -1;
                for (int i = chars_per_line; i > 0; i--) {
                    if (p[i] == ' ') {
                        break_pos = i;
                        break;
                    }
                }

                if (break_pos > 0) {
                    int copy_len = break_pos;
                    if (copy_len >= 63) copy_len = 63;
                    memcpy(lines_out[line_count], p, copy_len);
                    while (copy_len > 0 && lines_out[line_count][copy_len - 1] == ' ') {
                        copy_len--;
                    }
                    lines_out[line_count][copy_len] = '\0';
                    p += break_pos + 1;
                } else {
                    int copy_len = chars_per_line;
                    if (copy_len >= 63) copy_len = 63;
                    memcpy(lines_out[line_count], p, copy_len);
                    while (copy_len > 0 && lines_out[line_count][copy_len - 1] == ' ') {
                        copy_len--;
                    }
                    lines_out[line_count][copy_len] = '\0';
                    p += chars_per_line;
                }
                line_count++;
            }
        } else {
            if (rem_len <= chars_per_line) {
                int copy_len = rem_len;
                if (copy_len >= 63) copy_len = 63;
                memcpy(lines_out[line_count], p, copy_len);
                while (copy_len > 0 && lines_out[line_count][copy_len - 1] == ' ') {
                    copy_len--;
                }
                lines_out[line_count][copy_len] = '\0';
                line_count++;
                break;
            } else {
                int take = chars_per_line - 3;
                if (take < 0) take = 0;
                if (take >= 60) take = 60;
                memcpy(lines_out[line_count], p, take);
                while (take > 0 && lines_out[line_count][take - 1] == ' ') {
                    take--;
                }
                memcpy(lines_out[line_count] + take, "...", 4);
                line_count++;
                break;
            }
        }
    }

    return line_count;
}

static int get_wrapped_line_count(const char *text, sFONT *font, int max_width, int max_lines) {
    if (!text || !font || font->Width == 0 || max_lines <= 0 || max_width <= 0) {
        return 1;
    }
    char lines[4][64];
    if (max_lines > 4) max_lines = 4;
    int chars_per_line = max_width / font->Width;
    int count = break_text_lines(text, chars_per_line, max_lines, lines);
    return count > 0 ? count : 1;
}

static int draw_wrapped_text_clamped(UWORD x, UWORD y, const char *text, sFONT *font,
                                     int max_width, int max_lines, UWORD fg, UWORD bg) {
    if (!text || !font || font->Width == 0 || font->Height == 0 || max_lines <= 0 || max_width <= 0) {
        return 0;
    }
    char lines[4][64];
    if (max_lines > 4) max_lines = 4;
    int chars_per_line = max_width / font->Width;
    int line_count = break_text_lines(text, chars_per_line, max_lines, lines);
    for (int i = 0; i < line_count; i++) {
        UWORD cur_y = y + (UWORD)(i * font->Height);
        draw_ui_string(x, cur_y, lines[i], font, fg, bg);
    }
    return line_count;
}

static void get_book_display_name(size_t index, char *buf, size_t buf_size) {
    const book_info_t *book = book_library_get_book(index);
    if (!book) {
        buf[0] = '\0';
        return;
    }
    if (book->type == LIBRARY_ENTRY_UP) {
        snprintf(buf, buf_size, "[.. Kembali]");
    } else if (book->type == LIBRARY_ENTRY_DIR) {
        snprintf(buf, buf_size, "[D] %s", book->filename);
    } else {
        snprintf(buf, buf_size, "%s", book->filename);
    }
}

static int get_book_item_height(size_t index, sFONT *font, int max_width, int max_lines) {
    char name[BOOK_FILENAME_MAX_LEN + 16];
    get_book_display_name(index, name, sizeof(name));
    int lines = get_wrapped_line_count(name, font, max_width, max_lines);
    return lines * font->Height;
}

static bool book_range_fits(size_t start, size_t target, sFONT *font, int max_width, int max_lines) {
    int cur_y = 26;
    int bottom_limit = s_show_nav_hints ? (UI_FOOTER_Y - 2) : (DISPLAY_HEIGHT - 2);
    for (size_t i = start; i <= target; i++) {
        int text_h = get_book_item_height(i, font, max_width, max_lines);
        if (cur_y + text_h + 2 >= bottom_limit) {
            return false;
        }
        cur_y += text_h + 8;
    }
    return true;
}

void reader_ui_render_menu(int selected_index) {
    if (!s_canvas) return;

    epaper_power_on();

    Paint_SelectImage(s_canvas);
    Paint_Clear(WHITE);

    // Title Bar - Solid black box with white text
    Paint_DrawRectangle(0, 0, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(8, 3, "READER MENU", &Font12, WHITE, BLACK);

    char bat_str[16];
    snprintf(bat_str, sizeof(bat_str), "%u%%", power_mgr_get_battery_percentage());
    Paint_DrawString_EN(165, 3, bat_str, &Font12, WHITE, BLACK);

    Paint_DrawLine(0, UI_HEADER_HEIGHT + 1, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT + 1, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    // Menu Options
    const char *items[] = {
        "Continue Reading",
        "Book Library",
        "Font Size: [16px]",
        "Sync Books",
        "Nav Hints: [Show]",
        "Power Off / Sleep"
    };
    int count = 6;

    char font_str[32];
    if (text_pager_get_font_size() == TEXT_PAGER_FONT_12) {
        snprintf(font_str, sizeof(font_str), "Font Size: [12px]");
    } else {
        snprintf(font_str, sizeof(font_str), "Font Size: [16px]");
    }
    items[2] = font_str;

    char hints_str[32];
    if (s_show_nav_hints) {
        snprintf(hints_str, sizeof(hints_str), "Nav Hints: [Show]");
    } else {
        snprintf(hints_str, sizeof(hints_str), "Nav Hints: [Hide]");
    }
    items[4] = hints_str;

    int max_width = DISPLAY_WIDTH - 24;
    int bottom_limit = s_show_nav_hints ? (UI_FOOTER_Y - 2) : (DISPLAY_HEIGHT - 2);
    int y = 28;
    for (int i = 0; i < count; i++) {
        int lines = get_wrapped_line_count(items[i], &Font12, max_width, 3);
        int text_h = lines * Font12.Height;

        if (y + text_h + 2 >= bottom_limit) {
            break;
        }

        if (i == selected_index) {
            Paint_DrawRectangle(4, y - 2, DISPLAY_WIDTH - 4, y + text_h + 2, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
            Paint_DrawString_EN(8, y, ">", &Font12, BLACK, WHITE);
        }
        draw_wrapped_text_clamped(22, y, items[i], &Font12, max_width, 3, BLACK, WHITE);
        y += (lines > 1) ? (text_h + 8) : 24;
    }

    // Footer
    if (s_show_nav_hints) {
        Paint_DrawLine(0, UI_FOOTER_Y - 2, DISPLAY_WIDTH - 1, UI_FOOTER_Y - 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        draw_ui_string(6, UI_FOOTER_Y + 1, "▼ Down   ▲ Up", &Font12, BLACK, WHITE);
        Paint_DrawString_EN(112, UI_FOOTER_Y + 1, "2xTap:Select", &Font12, BLACK, WHITE);
    }

    epaper_port_display_part(s_canvas);
}

void reader_ui_render_book_list(int selected_index) {
    if (!s_canvas) return;

    epaper_power_on();

    Paint_SelectImage(s_canvas);
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
        if (selected_index < 0) {
            selected_index = 0;
        } else if (selected_index >= (int)count) {
            selected_index = (int)count - 1;
        }

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

        int bottom_limit = s_show_nav_hints ? (UI_FOOTER_Y - 2) : (DISPLAY_HEIGHT - 2);
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

            draw_wrapped_text_clamped(22, y, display_name, font, max_width, max_lines, BLACK, WHITE);
            y += text_h + 8;
        }
    }

    // Footer
    if (s_show_nav_hints) {
        Paint_DrawLine(0, UI_FOOTER_Y - 2, DISPLAY_WIDTH - 1, UI_FOOTER_Y - 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        draw_ui_string(6, UI_FOOTER_Y + 1, "▼ Down   ▲ Up", &Font12, BLACK, WHITE);
        Paint_DrawString_EN(112, UI_FOOTER_Y + 1, "2xTap:Open", &Font12, BLACK, WHITE);
    }

    epaper_port_display_part(s_canvas);
}

void reader_ui_render_bookmarks(int selected_index) {
    if (!s_canvas) return;

    epaper_power_on();

    Paint_SelectImage(s_canvas);
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
        draw_ui_string(14, 104, "2xTap ▲ in book to add", &Font12, BLACK, WHITE);
    } else {
        if (selected_index < 0) {
            selected_index = 0;
        } else if (selected_index >= (int)count) {
            selected_index = (int)count - 1;
        }

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
    if (s_show_nav_hints) {
        Paint_DrawLine(0, UI_FOOTER_Y - 2, DISPLAY_WIDTH - 1, UI_FOOTER_Y - 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        draw_ui_string(6, UI_FOOTER_Y + 1, "▼ Down", &Font12, BLACK, WHITE);
        draw_ui_string(72, UI_FOOTER_Y + 1, "▲ Jump", &Font12, BLACK, WHITE);
        draw_ui_string(132, UI_FOOTER_Y + 1, "Hold ▼ Exit", &Font12, BLACK, WHITE);
    }

    epaper_port_display_part(s_canvas);
}

void reader_ui_render_ble_sync(bool is_connected, uint32_t bytes_received) {
    if (!s_canvas) return;

    epaper_power_on();

    Paint_SelectImage(s_canvas);
    Paint_Clear(WHITE);

    // Title Bar - Solid black box with white text
    Paint_DrawRectangle(0, 0, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(8, 3, "SYNC BOOKS", &Font12, WHITE, BLACK);
    Paint_DrawLine(0, UI_HEADER_HEIGHT + 1, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT + 1, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    Paint_DrawString_EN(12, 35, "Open BacaIn Web on", &Font12, BLACK, WHITE);
    Paint_DrawString_EN(12, 50, "Chrome / Edge:", &Font12, BLACK, WHITE);

    Paint_DrawRectangle(10, 70, 190, 95, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
    Paint_DrawString_EN(16, 76, "BacaIn", &Font16, BLACK, WHITE);

    if (is_connected) {
        Paint_DrawString_EN(12, 110, "Status: CONNECTED", &Font12, BLACK, WHITE);
        char rx_buf[32];
        snprintf(rx_buf, sizeof(rx_buf), "Rx: %lu bytes", (unsigned long)bytes_received);
        Paint_DrawString_EN(12, 130, rx_buf, &Font12, BLACK, WHITE);
    } else {
        Paint_DrawString_EN(12, 110, "Waiting to connect...", &Font12, BLACK, WHITE);
        Paint_DrawString_EN(12, 130, "Ready for connection...", &Font12, BLACK, WHITE);
    }

    // Footer
    Paint_DrawLine(0, UI_FOOTER_Y - 2, DISPLAY_WIDTH - 1, UI_FOOTER_Y - 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    draw_ui_string(12, UI_FOOTER_Y + 1, "Hold ▼  Exit", &Font12, BLACK, WHITE);

    epaper_port_display_part(s_canvas);
}

#define BACA_ICON_WIDTH   48
#define BACA_ICON_HEIGHT  48

// Official BacaIn 1-bit monochrome icon (Monogram B with spine & ribbon)
static const uint8_t s_bacain_icon_48x48[288] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // row 0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // row 1
    0x0f, 0xef, 0xff, 0xff, 0xe0, 0x00, // row 2
    0x0f, 0xef, 0xff, 0xff, 0xf8, 0x00, // row 3
    0x07, 0xcf, 0xff, 0xff, 0xfe, 0x00, // row 4
    0x07, 0xcf, 0xff, 0xff, 0xff, 0x00, // row 5
    0x07, 0xcf, 0x80, 0x03, 0xff, 0x80, // row 6
    0x07, 0xcf, 0x80, 0x00, 0x1f, 0x80, // row 7
    0x07, 0xcf, 0x80, 0x00, 0x0f, 0x80, // row 8
    0x07, 0xcf, 0x80, 0x00, 0x0f, 0x80, // row 9
    0x07, 0xcf, 0x80, 0x00, 0x0f, 0x80, // row 10
    0x07, 0xcf, 0x83, 0xfc, 0x0f, 0x80, // row 11
    0x07, 0xcf, 0x80, 0x00, 0x0f, 0x80, // row 12
    0x07, 0xcf, 0x80, 0x00, 0x0f, 0x80, // row 13
    0x1f, 0xef, 0x80, 0x00, 0x1f, 0x00, // row 14
    0x1f, 0xef, 0x80, 0x00, 0x3f, 0x00, // row 15
    0x07, 0xcf, 0x80, 0x00, 0x7e, 0x00, // row 16
    0x07, 0xcf, 0x80, 0x03, 0xfc, 0x00, // row 17
    0x07, 0xcf, 0x80, 0x0f, 0xf8, 0x00, // row 18
    0x07, 0xcf, 0x80, 0x7f, 0xf0, 0x00, // row 19
    0x07, 0xcf, 0xff, 0xff, 0xe0, 0x00, // row 20
    0x07, 0xcf, 0xff, 0xff, 0xc0, 0x00, // row 21
    0x07, 0xcf, 0xff, 0xff, 0xe0, 0x00, // row 22
    0x07, 0xcf, 0x80, 0x7f, 0xf8, 0x00, // row 23
    0x07, 0xcf, 0x80, 0x0f, 0xfe, 0x00, // row 24
    0x07, 0xcf, 0x80, 0x01, 0xff, 0x00, // row 25
    0x07, 0xcf, 0x80, 0x00, 0x7f, 0x80, // row 26
    0x1f, 0xef, 0x80, 0x00, 0x1f, 0xc0, // row 27
    0x1f, 0xef, 0x80, 0x00, 0x0f, 0xc0, // row 28
    0x07, 0xcf, 0x80, 0x00, 0x0f, 0xe0, // row 29
    0x07, 0xcf, 0x83, 0xfe, 0x0f, 0xe0, // row 30
    0x07, 0xcf, 0x80, 0x00, 0x0f, 0xe0, // row 31
    0x07, 0xcf, 0x80, 0x00, 0x0f, 0xe0, // row 32
    0x07, 0xcf, 0x80, 0x00, 0x0f, 0xe0, // row 33
    0x07, 0xcf, 0x80, 0x00, 0x0f, 0xe0, // row 34
    0x07, 0xcf, 0x80, 0x00, 0x1f, 0xc0, // row 35
    0x07, 0xcf, 0x80, 0x00, 0x3f, 0xc0, // row 36
    0x07, 0xcf, 0xff, 0xff, 0xff, 0x80, // row 37
    0x07, 0xcf, 0xff, 0xff, 0xff, 0x00, // row 38
    0x0f, 0xef, 0xff, 0xff, 0xfe, 0x00, // row 39
    0x0f, 0xef, 0xff, 0xff, 0xf0, 0x00, // row 40
    0x03, 0xc0, 0x00, 0x00, 0x00, 0x00, // row 41
    0x03, 0xc0, 0x00, 0x00, 0x00, 0x00, // row 42
    0x03, 0xc0, 0x00, 0x00, 0x00, 0x00, // row 43
    0x03, 0xc0, 0x00, 0x00, 0x00, 0x00, // row 44
    0x02, 0x40, 0x00, 0x00, 0x00, 0x00, // row 45
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // row 46
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // row 47
};

static void draw_bacain_icon(int center_x, int top_y) {
    int start_x = center_x - (BACA_ICON_WIDTH / 2);
    for (int y = 0; y < BACA_ICON_HEIGHT; y++) {
        int py = top_y + y;
        if (py < 0 || py >= DISPLAY_HEIGHT) continue;
        for (int x = 0; x < BACA_ICON_WIDTH; x++) {
            int px = start_x + x;
            if (px < 0 || px >= DISPLAY_WIDTH) continue;
            int byte_idx = y * (BACA_ICON_WIDTH / 8) + (x / 8);
            if (s_bacain_icon_48x48[byte_idx] & (0x80 >> (x % 8))) {
                Paint_SetPixel(px, py, BLACK);
            }
        }
    }
}

static void draw_centered_string(int y, const char *str, sFONT *font) {
    if (!str || !font) return;
    int w = get_ui_string_width(str, font);
    int x = (DISPLAY_WIDTH - w) / 2;
    if (x < 4) x = 4;
    draw_ui_string((UWORD)x, (UWORD)y, str, font, BLACK, WHITE);
}

void reader_ui_render_splash(const char *title, const char *subtitle) {
    if (!s_canvas) return;

    epaper_power_on();

    Paint_SelectImage(s_canvas);
    Paint_Clear(WHITE);

    // Elegant clean outer border
    Paint_DrawRectangle(8, 8, DISPLAY_WIDTH - 9, DISPLAY_HEIGHT - 9, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);

    // BacaIn Monogram B icon
    draw_bacain_icon(DISPLAY_WIDTH / 2, 28);

    // Title (Font16)
    draw_centered_string(88, (title && title[0] != '\0') ? title : "BacaIn", &Font16);

    // Subtle divider accent
    Paint_DrawLine(65, 112, 135, 112, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    // Subtitle (Font12)
    if (subtitle && subtitle[0] != '\0') {
        draw_centered_string(126, subtitle, &Font12);
    }

    epaper_port_display(s_canvas);
}

void reader_ui_render_sleeping(void) {
    if (!s_canvas) return;

    epaper_power_on();

    Paint_SelectImage(s_canvas);
    Paint_Clear(WHITE);

    // Elegant clean outer border
    Paint_DrawRectangle(8, 8, DISPLAY_WIDTH - 9, DISPLAY_HEIGHT - 9, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);

    // BacaIn Monogram B icon
    draw_bacain_icon(DISPLAY_WIDTH / 2, 24);

    // Title (Font16)
    draw_centered_string(80, "BacaIn", &Font16);

    // Sleeping status (Font12)
    draw_centered_string(104, "SLEEPING...", &Font12);

    // Divider line
    Paint_DrawLine(50, 126, 150, 126, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    // Wake instruction guide (Font12)
    const char *wake_str = "Press ▲ to Wake";
    int wake_x = (DISPLAY_WIDTH - get_ui_string_width(wake_str, &Font12)) / 2;
    if (wake_x < 4) wake_x = 4;
    draw_ui_string((UWORD)wake_x, 146, wake_str, &Font12, BLACK, WHITE);

    epaper_port_display(s_canvas);
}
