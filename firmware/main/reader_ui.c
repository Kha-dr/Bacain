#include "reader_ui.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "epaper_port.h"
#include "GUI_Paint.h"
#include "fonts.h"
#include "text_pager.h"
#include "book_library.h"
#include "power_mgr.h"
#include "button.h"
#include "app_config.h"
#include "board_config.h"
#include "bacain_logo.h"

#define TAG "READER_UI"

static uint8_t *s_canvas = NULL;
static reading_state_t s_cached_reading_state;
static bool s_show_nav_hints = true;
static uint8_t s_rotation = BOARD_DISPLAY_ROTATION;
static RTC_DATA_ATTR uint8_t s_rtc_canvas[DISPLAY_BUFFER_SIZE];
static RTC_DATA_ATTR bool s_rtc_canvas_valid = false;
static bool s_is_wake_frame = false;

static RTC_DATA_ATTR bool s_rtc_settings_cached = false;
static RTC_DATA_ATTR uint8_t s_rtc_rot = BOARD_DISPLAY_ROTATION;
static RTC_DATA_ATTR uint8_t s_rtc_btn_invert = 0;
static RTC_DATA_ATTR uint8_t s_rtc_font_size = 0;
static RTC_DATA_ATTR uint8_t s_rtc_show_hints = 1;
static RTC_DATA_ATTR uint8_t s_rtc_full_refresh_interval = APP_FULL_REFRESH_INTERVAL;
RTC_DATA_ATTR static uint8_t s_rtc_page_turns_since_full = 0;

void app_settings_load(bool force_nvs_reload) {
    if (!s_rtc_settings_cached || force_nvs_reload) {
        nvs_handle_t nvs_handle;
        if (nvs_open("settings", NVS_READWRITE, &nvs_handle) == ESP_OK) {
            if (nvs_get_u8(nvs_handle, "rotation", &s_rtc_rot) != ESP_OK) {
                s_rtc_rot = BOARD_DISPLAY_ROTATION;
            }
            if (nvs_get_u8(nvs_handle, "btn_invert", &s_rtc_btn_invert) != ESP_OK) {
                s_rtc_btn_invert = 0;
            }
            if (nvs_get_u8(nvs_handle, "font_size", &s_rtc_font_size) != ESP_OK) {
                s_rtc_font_size = 0;
            }
            if (nvs_get_u8(nvs_handle, "nav_hints", &s_rtc_show_hints) != ESP_OK) {
                s_rtc_show_hints = 1;
            }
            if (nvs_get_u8(nvs_handle, "full_refresh_n", &s_rtc_full_refresh_interval) != ESP_OK) {
                s_rtc_full_refresh_interval = APP_FULL_REFRESH_INTERVAL;
            }
            nvs_close(nvs_handle);
        } else {
            s_rtc_rot = BOARD_DISPLAY_ROTATION;
            s_rtc_btn_invert = 0;
            s_rtc_font_size = 0;
            s_rtc_show_hints = 1;
            s_rtc_full_refresh_interval = APP_FULL_REFRESH_INTERVAL;
        }
        s_rtc_settings_cached = true;
    }
    reader_ui_set_rotation(s_rtc_rot);
    reader_ui_set_show_nav_hints(s_rtc_show_hints != 0);
    button_set_inverted(s_rtc_btn_invert != 0);
    text_pager_set_font_size(s_rtc_font_size == 0 ? TEXT_PAGER_FONT_12 : TEXT_PAGER_FONT_16);
}

void app_settings_save_font_size(uint8_t font_size) {
    s_rtc_font_size = font_size;
    nvs_handle_t h;
    if (nvs_open("settings", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "font_size", s_rtc_font_size);
        nvs_commit(h);
        nvs_close(h);
    } else {
        ESP_LOGE(TAG, "Failed to open NVS settings to save font_size");
    }
}

void app_settings_save_nav_hints(bool show) {
    reader_ui_set_show_nav_hints(show);
    s_rtc_show_hints = show ? 1 : 0;
    nvs_handle_t h;
    if (nvs_open("settings", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "nav_hints", s_rtc_show_hints);
        nvs_commit(h);
        nvs_close(h);
    } else {
        ESP_LOGE(TAG, "Failed to open NVS settings to save nav_hints");
    }
}

void app_settings_save_refresh_interval(uint8_t interval) {
    if (interval > 100) {
        interval = 100;
    }
    s_rtc_full_refresh_interval = interval;
    nvs_handle_t h;
    if (nvs_open("settings", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "full_refresh_n", s_rtc_full_refresh_interval);
        nvs_commit(h);
        nvs_close(h);
    } else {
        ESP_LOGE(TAG, "Failed to open NVS settings to save full_refresh_n");
    }
}

uint8_t reader_ui_get_full_refresh_interval(void) {
    return s_rtc_full_refresh_interval;
}

void reader_ui_note_full_refresh(void) {
    s_rtc_page_turns_since_full = 0;
}

bool reader_ui_page_turn_needs_full(void) {
    s_rtc_page_turns_since_full++;
    if (s_rtc_full_refresh_interval > 0 && s_rtc_page_turns_since_full >= s_rtc_full_refresh_interval) {
        s_rtc_page_turns_since_full = 0;
        return true;
    }
    return false;
}

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

uint8_t *reader_ui_get_canvas(void) {
    return s_canvas;
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

void ui_draw_string(UWORD x, UWORD y, const char *str, sFONT *font, UWORD fg, UWORD bg) {
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

int ui_get_string_width(const char *str, sFONT *font) {
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

void ui_draw_centered_string(int y, const char *str, sFONT *font) {
    if (!str || !font) return;
    int w = ui_get_string_width(str, font);
    int x = (DISPLAY_WIDTH - w) / 2;
    if (x < 4) x = 4;
    ui_draw_string((UWORD)x, (UWORD)y, str, font, BLACK, WHITE);
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

int ui_get_wrapped_line_count(const char *text, sFONT *font, int max_width, int max_lines) {
    if (!text || !font || font->Width == 0 || max_lines <= 0 || max_width <= 0) {
        return 1;
    }
    char lines[4][64];
    if (max_lines > 4) max_lines = 4;
    int chars_per_line = max_width / font->Width;
    int count = break_text_lines(text, chars_per_line, max_lines, lines);
    return count > 0 ? count : 1;
}

int ui_draw_wrapped_text_clamped(UWORD x, UWORD y, const char *text, sFONT *font,
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
        ui_draw_string(x, cur_y, lines[i], font, fg, bg);
    }
    return line_count;
}

void ui_draw_bacain_icon(int center_x, int top_y) {
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

esp_err_t reader_ui_init(bool is_cold_boot) {
    s_is_wake_frame = !is_cold_boot;
    if (!s_canvas) {
        s_canvas = (uint8_t *)heap_caps_malloc(DISPLAY_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (!s_canvas) {
            s_canvas = (uint8_t *)malloc(DISPLAY_BUFFER_SIZE);
        }
        if (!s_canvas) {
            s_canvas = (uint8_t *)heap_caps_malloc(DISPLAY_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
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
    if (is_cold_boot) {
        epaper_port_init();
        epaper_port_clear(WHITE);
        epaper_port_init_partial();
        vTaskDelay(pdMS_TO_TICKS(500));
    } else {
        epaper_port_init_partial();
    }

    return ESP_OK;
}

void reader_ui_flush_canvas(bool full_refresh) {
    if (!s_canvas) return;

    if (full_refresh) {
        reader_ui_note_full_refresh();
        epaper_port_display_full(s_canvas);
        s_is_wake_frame = false;
    } else if (s_is_wake_frame && s_rtc_canvas_valid) {
        epaper_port_display_part_diff(s_rtc_canvas, s_canvas);
        s_is_wake_frame = false;
    } else {
        epaper_port_display_part(s_canvas);
    }

    memcpy(s_rtc_canvas, s_canvas, DISPLAY_BUFFER_SIZE);
    s_rtc_canvas_valid = true;
}

void reader_ui_render_splash(const char *title, const char *subtitle) {
    if (!s_canvas) return;

    epaper_power_on();

    Paint_SelectImage(s_canvas);
    Paint_Clear(WHITE);

    // Elegant clean outer border
    Paint_DrawRectangle(8, 8, DISPLAY_WIDTH - 9, DISPLAY_HEIGHT - 9, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);

    // BacaIn open book icon
    ui_draw_bacain_icon(DISPLAY_WIDTH / 2, 28);

    // Title (Font16)
    ui_draw_centered_string(88, (title && title[0] != '\0') ? title : "BacaIn", &Font16);

    // Subtle divider accent
    Paint_DrawLine(65, 112, 135, 112, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    // Subtitle (Font12)
    const char *sub = (subtitle && subtitle[0] != '\0') ? subtitle : "Press ▲/▼";
    ui_draw_centered_string(126, sub, &Font12);

    reader_ui_note_full_refresh();
    epaper_port_display_full(s_canvas);
    memcpy(s_rtc_canvas, s_canvas, DISPLAY_BUFFER_SIZE);
    s_rtc_canvas_valid = true;
}

void reader_ui_render_power_off(void) {
    if (!s_canvas) return;

    epaper_power_on();

    Paint_SelectImage(s_canvas);
    Paint_Clear(WHITE);

    // Elegant clean outer border
    Paint_DrawRectangle(8, 8, DISPLAY_WIDTH - 9, DISPLAY_HEIGHT - 9, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);

    // BacaIn open book icon
    ui_draw_bacain_icon(DISPLAY_WIDTH / 2, 24);

    // Title (Font16)
    ui_draw_centered_string(80, "BacaIn", &Font16);

    // Divider line
    Paint_DrawLine(50, 126, 150, 126, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    // Turn-on instruction guide (Font12)
    const char *arrow = button_get_inverted() ? "▲" : "▼";
    char wake_str[32];
    snprintf(wake_str, sizeof(wake_str), "Hold %s to turn on", arrow);
    ui_draw_centered_string(146, wake_str, &Font12);

    reader_ui_note_full_refresh();
    epaper_port_display_full(s_canvas);
    memcpy(s_rtc_canvas, s_canvas, DISPLAY_BUFFER_SIZE);
    s_rtc_canvas_valid = true;
}

uint8_t reader_ui_get_battery_percentage(void) {
    return power_mgr_get_battery_percentage();
}
