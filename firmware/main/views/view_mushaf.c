#define view_mushaf_on_wake __legacy_view_mushaf_on_wake
#include "view_mushaf.h"
#undef view_mushaf_on_wake
#include "views.h"

#if CONFIG_BOARD_MUSHAF_ENABLE || BOARD_MUSHAF_ENABLE

#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "epaper_port.h"
#include "GUI_Paint.h"
#include "fonts.h"
#include "app_config.h"
#include "view_menu.h"
#include "power_mgr.h"

#define TAG "view_mushaf"

#ifndef MUSHAF_TOTAL_AYAHS
#define MUSHAF_TOTAL_AYAHS 6236
#endif

#ifndef MUSHAF_LAYER_COUNT
#define MUSHAF_LAYER_COUNT 4
#endif

RTC_DATA_ATTR static mushaf_state_t s_rtc_mushaf_state;
RTC_DATA_ATTR static int s_rtc_mushaf_menu_idx = 0;
RTC_DATA_ATTR static int s_rtc_mushaf_jump_idx = 0;
RTC_DATA_ATTR static int s_rtc_mushaf_bm_idx = 0;
RTC_DATA_ATTR static uint32_t s_mushaf_redraw_counter = 0;
RTC_DATA_ATTR static reader_view_t s_rtc_mushaf_sub_view = VIEW_MUSHAF_MENU;

static const struct {
    const char *label;
    uint8_t surah;
    uint16_t ayah;
} k_sunnah_presets[5] = {
    { "Al-Kahf (18:1) - Friday", 18, 1 },
    { "Yasin (36:1) - Daily", 36, 1 },
    { "Ar-Rahman (55:1)", 55, 1 },
    { "Al-Waqiah (56:1) - Night", 56, 1 },
    { "Al-Mulk (67:1) - Sleep", 67, 1 }
};

static inline void get_juz_start(uint8_t juz, uint8_t *out_surah, uint16_t *out_ayah) {
    static const struct { uint8_t s; uint16_t a; } k_juz[30] = {
        {1, 1},   {2, 142}, {2, 253}, {3, 93},  {4, 24},
        {4, 148}, {5, 82},  {6, 111}, {7, 88},  {8, 41},
        {9, 93},  {11, 6},  {12, 53}, {15, 1},  {17, 1},
        {18, 75}, {21, 1},  {23, 1},  {25, 21}, {27, 56},
        {29, 46}, {33, 31}, {36, 28}, {39, 32}, {41, 47},
        {46, 1},  {51, 31}, {58, 1},  {67, 1},  {78, 1}
    };
    if (juz >= 1 && juz <= 30) {
        if (out_surah) *out_surah = k_juz[juz - 1].s;
        if (out_ayah) *out_ayah = k_juz[juz - 1].a;
    }
}

static void view_mushaf_render_menu(bool full_refresh) {
    uint8_t *canvas = reader_ui_get_canvas();
    if (!canvas) return;

    epaper_power_on();
    Paint_SelectImage(canvas);
    Paint_Clear(WHITE);

    Paint_DrawRectangle(0, 0, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(8, 3, "Mushaf Menu", &Font12, WHITE, BLACK);
    char bat_str[16];
    snprintf(bat_str, sizeof(bat_str), "%u%%", power_mgr_get_battery_percentage());
    Paint_DrawString_EN(165, 3, bat_str, &Font12, WHITE, BLACK);
    Paint_DrawLine(0, UI_HEADER_HEIGHT + 1, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT + 1, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    if (!mushaf_reader_has_bqm()) {
        Paint_DrawString_EN(20, 60, "No Mushaf data (.bqm)", &Font12, BLACK, WHITE);
        Paint_DrawString_EN(20, 84, "Connect via Sync Books", &Font12, BLACK, WHITE);
        Paint_DrawString_EN(20, 108, "Build & upload via Web", &Font12, BLACK, WHITE);
        if (reader_ui_get_show_nav_hints()) {
            Paint_DrawLine(0, UI_FOOTER_Y - 2, DISPLAY_WIDTH - 1, UI_FOOTER_Y - 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
            ui_draw_centered_string(UI_FOOTER_Y + 1, "Hold ▲/▼ Back", &Font12);
        }
        reader_ui_flush_canvas(full_refresh);
        return;
    }

    const mushaf_state_t *st = mushaf_reader_get_state();
    char item0[32];
    if (!st || !st->started) {
        snprintf(item0, sizeof(item0), "Start Tilawah");
    } else {
        bqm_ayah_entry_t a_info;
        uint8_t juz = 1;
        if (mushaf_reader_get_ayah_info(st->surah, st->ayah, &a_info, NULL) == ESP_OK) {
            juz = a_info.juz;
        }
        snprintf(item0, sizeof(item0), "Resume S.%u:%u (Juz %u)", st->surah, st->ayah, juz);
    }

    const char *layer_names[MUSHAF_LAYER_COUNT] = { "Mushaf", "Latin", "Translate", "Tafsir" };
    uint8_t cur_layer = (st && st->layer < MUSHAF_LAYER_COUNT) ? st->layer : 0;
    char item1[32];
    snprintf(item1, sizeof(item1), "Layer: [%s]", layer_names[cur_layer]);

    char item3[32];
    snprintf(item3, sizeof(item3), "Bookmarks (%u/8)", st ? st->bookmark_count : 0);

    const char *items[4] = {
        item0,
        item1,
        "Sunnah & Juz Jump",
        item3
    };

    if (s_rtc_mushaf_menu_idx < 0) s_rtc_mushaf_menu_idx = 0;
    if (s_rtc_mushaf_menu_idx >= 4) s_rtc_mushaf_menu_idx = 3;

    int max_width = DISPLAY_WIDTH - 24;
    int bottom_limit = reader_ui_get_show_nav_hints() ? (UI_FOOTER_Y - 2) : (DISPLAY_HEIGHT - 2);
    int y = 24;
    for (int i = 0; i < 4; i++) {
        int lines = ui_get_wrapped_line_count(items[i], &Font12, max_width, 3);
        int text_h = lines * Font12.Height;
        if (y + text_h + 2 >= bottom_limit) break;

        if (i == s_rtc_mushaf_menu_idx) {
            Paint_DrawRectangle(4, y - 2, DISPLAY_WIDTH - 4, y + text_h + 2, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
            Paint_DrawString_EN(8, y, ">", &Font12, BLACK, WHITE);
        }
        ui_draw_wrapped_text_clamped(22, y, items[i], &Font12, max_width, 3, BLACK, WHITE);
        y += (lines > 1) ? (text_h + 6) : 22;
    }

    if (reader_ui_get_show_nav_hints()) {
        Paint_DrawLine(0, UI_FOOTER_Y - 2, DISPLAY_WIDTH - 1, UI_FOOTER_Y - 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        ui_draw_centered_string(UI_FOOTER_Y + 1, "2x▲/▼ Select | Hold ▲/▼ Back", &Font12);
    }

    reader_ui_flush_canvas(full_refresh);
}

static void view_mushaf_render_reading(bool full_refresh) {
    uint8_t *canvas = reader_ui_get_canvas();
    if (!canvas) return;

    epaper_power_on();
    Paint_SelectImage(canvas);
    Paint_Clear(WHITE);

    const mushaf_state_t *st = mushaf_reader_get_state();
    if (!st || !mushaf_reader_has_bqm()) {
        Paint_DrawString_EN(20, 80, "No Mushaf data (.bqm)", &Font12, BLACK, WHITE);
        ui_draw_string(20, 104, "Sync via Web Companion", &Font12, BLACK, WHITE);
        reader_ui_flush_canvas(full_refresh);
        return;
    }

    mushaf_reader_render_subpage(canvas);

    bqm_surah_entry_t s_info;
    char header_left[32] = {0};
    if (mushaf_reader_get_surah_info(st->surah, &s_info) == ESP_OK) {
        snprintf(header_left, sizeof(header_left), "%02u:%u %.9s", st->surah, st->ayah, s_info.name_latin);
    } else {
        snprintf(header_left, sizeof(header_left), "%02u:%u", st->surah, st->ayah);
    }
    Paint_DrawString_EN(4, 2, header_left, &Font12, BLACK, WHITE);

    const char *layer_tags[MUSHAF_LAYER_COUNT] = { "[M]", "[LAT]", "[TRN]", "[TAF]" };
    const char *ltag = (st->layer < MUSHAF_LAYER_COUNT) ? layer_tags[st->layer] : "[?]";
    bool bookmarked = mushaf_reader_is_bookmarked(st->surah, st->ayah);
    char tag_str[16];
    snprintf(tag_str, sizeof(tag_str), "%s%s", ltag, bookmarked ? "*" : "");
    Paint_DrawString_EN(128, 2, tag_str, &Font12, BLACK, WHITE);

    char bat_str[16];
    snprintf(bat_str, sizeof(bat_str), "%u%%", power_mgr_get_battery_percentage());
    Paint_DrawString_EN(165, 2, bat_str, &Font12, BLACK, WHITE);
    Paint_DrawLine(0, 15, DISPLAY_WIDTH - 1, 15, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    Paint_DrawLine(0, DISPLAY_HEIGHT - 16, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 16, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    if (reader_ui_get_show_nav_hints()) {
        ui_draw_centered_string(DISPLAY_HEIGHT - 14, "2x▲ Layer | 2x▼ Mark", &Font12);
    } else {
        bqm_ayah_entry_t ayah_info;
        uint16_t global_ayah = 0;
        char footer_left[32] = {0};
        char footer_right[32] = {0};
        if (mushaf_reader_get_ayah_info(st->surah, st->ayah, &ayah_info, &global_ayah) == ESP_OK) {
            snprintf(footer_left, sizeof(footer_left), "Juz %u", ayah_info.juz);
            uint8_t subpage_count = mushaf_reader_get_active_subpage_count();
            if (st->started) {
                uint32_t pct = (uint32_t)((uint64_t)global_ayah * 100 / MUSHAF_TOTAL_AYAHS);
                if (subpage_count > 1) {
                    if (pct > 0) {
                        snprintf(footer_right, sizeof(footer_right), "%u/%u %u%%", st->subpage + 1, subpage_count, (unsigned)pct);
                    } else {
                        snprintf(footer_right, sizeof(footer_right), "%u/%u", st->subpage + 1, subpage_count);
                    }
                } else if (pct > 0) {
                    snprintf(footer_right, sizeof(footer_right), "%u%%", (unsigned)pct);
                }
            } else {
                if (subpage_count > 1) {
                    snprintf(footer_right, sizeof(footer_right), "%u/%u", st->subpage + 1, subpage_count);
                }
            }
        }
        Paint_DrawString_EN(4, DISPLAY_HEIGHT - 14, footer_left, &Font12, BLACK, WHITE);
        if (footer_right[0] != '\0') {
            int w = ui_get_string_width(footer_right, &Font12);
            Paint_DrawString_EN(DISPLAY_WIDTH - w - 4, DISPLAY_HEIGHT - 14, footer_right, &Font12, BLACK, WHITE);
        }
    }

    reader_ui_flush_canvas(full_refresh);
}

static void view_mushaf_render_jump(bool full_refresh) {
    uint8_t *canvas = reader_ui_get_canvas();
    if (!canvas) return;

    epaper_power_on();
    Paint_SelectImage(canvas);
    Paint_Clear(WHITE);

    Paint_DrawRectangle(0, 0, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(8, 3, "SUNNAH & JUZ JUMP", &Font12, WHITE, BLACK);
    char bat_str[16];
    snprintf(bat_str, sizeof(bat_str), "%u%%", power_mgr_get_battery_percentage());
    Paint_DrawString_EN(165, 3, bat_str, &Font12, WHITE, BLACK);
    Paint_DrawLine(0, UI_HEADER_HEIGHT + 1, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT + 1, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    int selected = s_rtc_mushaf_jump_idx;
    if (selected < 0) selected = 0;
    if (selected >= 35) selected = 34;
    s_rtc_mushaf_jump_idx = selected;

    size_t start_idx = 0;
    if (selected >= 5) {
        start_idx = (size_t)(selected - 4);
    }
    if (start_idx + 6 > 35) {
        start_idx = 35 - 6;
    }
    size_t end_idx = start_idx + 6;

    int y = 28;
    for (size_t i = start_idx; i < end_idx; i++) {
        char line[48] = {0};
        if (i < 5) {
            strncpy(line, k_sunnah_presets[i].label, sizeof(line) - 1);
        } else {
            uint8_t juz = (uint8_t)(i - 4);
            uint8_t s = 1;
            uint16_t a = 1;
            get_juz_start(juz, &s, &a);
            bqm_surah_entry_t s_info;
            if (mushaf_reader_get_surah_info(s, &s_info) == ESP_OK) {
                snprintf(line, sizeof(line), "Juz %u: %.10s %u:%u", juz, s_info.name_latin, s, a);
            } else {
                snprintf(line, sizeof(line), "Juz %u (%u:%u)", juz, s, a);
            }
        }

        if ((int)i == selected) {
            Paint_DrawRectangle(4, y - 2, DISPLAY_WIDTH - 4, y + 16, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
            Paint_DrawString_EN(8, y, ">", &Font12, BLACK, WHITE);
        }
        Paint_DrawString_EN(22, y, line, &Font12, BLACK, WHITE);
        y += 24;
    }

    if (reader_ui_get_show_nav_hints()) {
        Paint_DrawLine(0, UI_FOOTER_Y - 2, DISPLAY_WIDTH - 1, UI_FOOTER_Y - 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        ui_draw_centered_string(UI_FOOTER_Y + 1, "2x▲/▼ Jump | Hold ▲/▼ Back", &Font12);
    }

    reader_ui_flush_canvas(full_refresh);
}

static void view_mushaf_render_bookmarks(bool full_refresh) {
    uint8_t *canvas = reader_ui_get_canvas();
    if (!canvas) return;

    epaper_power_on();
    Paint_SelectImage(canvas);
    Paint_Clear(WHITE);

    const mushaf_state_t *st = mushaf_reader_get_state();
    uint8_t count = (st != NULL) ? st->bookmark_count : 0;

    Paint_DrawRectangle(0, 0, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    char title_buf[32];
    snprintf(title_buf, sizeof(title_buf), "BOOKMARKS (%u/8)", count);
    Paint_DrawString_EN(8, 3, title_buf, &Font12, WHITE, BLACK);
    char bat_str[16];
    snprintf(bat_str, sizeof(bat_str), "%u%%", power_mgr_get_battery_percentage());
    Paint_DrawString_EN(165, 3, bat_str, &Font12, WHITE, BLACK);
    Paint_DrawLine(0, UI_HEADER_HEIGHT + 1, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT + 1, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    if (count == 0) {
        Paint_DrawString_EN(20, 80, "No bookmarks yet.", &Font12, BLACK, WHITE);
        ui_draw_string(14, 104, "2xTap ▼ in ayah to add", &Font12, BLACK, WHITE);
    } else {
        int selected = s_rtc_mushaf_bm_idx;
        if (selected < 0) selected = 0;
        if (selected >= count) selected = count - 1;
        s_rtc_mushaf_bm_idx = selected;

        size_t start_idx = 0;
        if (count > 6) {
            if (selected >= 5) {
                start_idx = (size_t)(selected - 4);
            }
            if (start_idx + 6 > count) {
                start_idx = count - 6;
            }
        }
        size_t end_idx = start_idx + 6;
        if (end_idx > count) end_idx = count;

        int y = 28;
        for (size_t i = start_idx; i < end_idx; i++) {
            uint8_t s = st->bookmarks[i].surah;
            uint16_t a = st->bookmarks[i].ayah;
            uint8_t j = st->bookmarks[i].juz;
            bqm_surah_entry_t s_info;
            char name[12] = {0};
            if (mushaf_reader_get_surah_info(s, &s_info) == ESP_OK) {
                strncpy(name, s_info.name_latin, sizeof(name) - 1);
            }
            char line[48];
            snprintf(line, sizeof(line), "%u. %s %u:%u (J%u)", (unsigned)(i + 1), name, s, a, j);

            if ((int)i == selected) {
                Paint_DrawRectangle(4, y - 2, DISPLAY_WIDTH - 4, y + 16, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
                Paint_DrawString_EN(8, y, ">", &Font12, BLACK, WHITE);
            }
            Paint_DrawString_EN(22, y, line, &Font12, BLACK, WHITE);
            y += 24;
        }
    }

    if (reader_ui_get_show_nav_hints()) {
        Paint_DrawLine(0, UI_FOOTER_Y - 2, DISPLAY_WIDTH - 1, UI_FOOTER_Y - 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        ui_draw_centered_string(UI_FOOTER_Y + 1, "2x▲/▼ Jump | Hold ▲/▼ Back", &Font12);
    }

    reader_ui_flush_canvas(full_refresh);
}

void view_mushaf_init(bool is_cold_boot) {
    if (is_cold_boot) {
        s_rtc_mushaf_sub_view = VIEW_MUSHAF_MENU;
        mushaf_reader_init(NULL);
    } else {
        mushaf_reader_init(&s_rtc_mushaf_state);
    }
    const mushaf_state_t *st = mushaf_reader_get_state();
    if (st != NULL) {
        memcpy(&s_rtc_mushaf_state, st, sizeof(mushaf_state_t));
    }
}

void view_mushaf_save_before_sleep(void) {
    mushaf_reader_save_state();
    const mushaf_state_t *st = mushaf_reader_get_state();
    if (st != NULL) {
        memcpy(&s_rtc_mushaf_state, st, sizeof(mushaf_state_t));
    }
}

void view_mushaf_enter_menu(void) {
    s_rtc_mushaf_menu_idx = 0;
    s_rtc_mushaf_sub_view = VIEW_MUSHAF_MENU;
    view_mushaf_render_menu(false);
}

reader_view_t view_mushaf_on_wake(void) {
    switch (s_rtc_mushaf_sub_view) {
    case VIEW_MUSHAF_READING:
    case VIEW_MUSHAF_JUMP:
    case VIEW_MUSHAF_BOOKMARKS:
        return s_rtc_mushaf_sub_view;
    case VIEW_MUSHAF_MENU:
    default:
        s_rtc_mushaf_sub_view = VIEW_MUSHAF_MENU;
        return VIEW_MUSHAF_MENU;
    }
}

static reader_view_t mushaf_handle_event_internal(reader_view_t view, button_event_t evt) {
    if (view == VIEW_MUSHAF_MENU) {
        if (evt.type == BTN_EVENT_CUSTOM_REFRESH) {
            view_mushaf_render_menu(false);
            return VIEW_MUSHAF_MENU;
        }

        if (!mushaf_reader_has_bqm()) {
            if (evt.type == BTN_LONG_PRESS || evt.type == BTN_DOUBLE_CLICK) {
                view_menu_render(false);
                return VIEW_MENU;
            }
            return VIEW_MUSHAF_MENU;
        }

        if (evt.type == BTN_CLICK) {
            if (evt.button == BTN_ID_BOOT) {
                s_rtc_mushaf_menu_idx = (s_rtc_mushaf_menu_idx - 1 + 4) % 4;
            } else if (evt.button == BTN_ID_PWR) {
                s_rtc_mushaf_menu_idx = (s_rtc_mushaf_menu_idx + 1) % 4;
            }
            view_mushaf_render_menu(false);
            return VIEW_MUSHAF_MENU;
        } else if (evt.type == BTN_DOUBLE_CLICK) {
            if (s_rtc_mushaf_menu_idx == 0) {
                view_mushaf_render_reading(false);
                return VIEW_MUSHAF_READING;
            } else if (s_rtc_mushaf_menu_idx == 1) {
                mushaf_reader_cycle_layer();
                view_mushaf_render_menu(false);
                return VIEW_MUSHAF_MENU;
            } else if (s_rtc_mushaf_menu_idx == 2) {
                s_rtc_mushaf_jump_idx = 0;
                view_mushaf_render_jump(false);
                return VIEW_MUSHAF_JUMP;
            } else if (s_rtc_mushaf_menu_idx == 3) {
                s_rtc_mushaf_bm_idx = 0;
                view_mushaf_render_bookmarks(false);
                return VIEW_MUSHAF_BOOKMARKS;
            }
        } else if (evt.type == BTN_LONG_PRESS) {
            view_menu_render(false);
            return VIEW_MENU;
        }
        return VIEW_MUSHAF_MENU;
    }

    if (view == VIEW_MUSHAF_READING) {
        if (evt.type == BTN_EVENT_CUSTOM_REFRESH) {
            view_mushaf_render_reading(false);
            return VIEW_MUSHAF_READING;
        }

        if (evt.type == BTN_CLICK) {
            if (evt.button == BTN_ID_PWR) {
                mushaf_reader_step_page(1);
            } else if (evt.button == BTN_ID_BOOT) {
                mushaf_reader_step_page(-1);
            }
            s_mushaf_redraw_counter++;
            bool full = (APP_FULL_REFRESH_INTERVAL > 0 && s_mushaf_redraw_counter >= APP_FULL_REFRESH_INTERVAL);
            if (full) s_mushaf_redraw_counter = 0;
            view_mushaf_render_reading(full);
            return VIEW_MUSHAF_READING;
        } else if (evt.type == BTN_DOUBLE_CLICK) {
            if (evt.button == BTN_ID_BOOT) {
                mushaf_reader_cycle_layer();
                view_mushaf_render_reading(false);
                return VIEW_MUSHAF_READING;
            } else if (evt.button == BTN_ID_PWR) {
                mushaf_reader_toggle_bookmark();
                view_mushaf_render_reading(false);
                return VIEW_MUSHAF_READING;
            }
        } else if (evt.type == BTN_LONG_PRESS) {
            view_mushaf_save_before_sleep();
            view_mushaf_render_menu(false);
            return VIEW_MUSHAF_MENU;
        }
        return VIEW_MUSHAF_READING;
    }

    if (view == VIEW_MUSHAF_JUMP) {
        if (evt.type == BTN_EVENT_CUSTOM_REFRESH) {
            view_mushaf_render_jump(false);
            return VIEW_MUSHAF_JUMP;
        }

        if (evt.type == BTN_CLICK) {
            if (evt.button == BTN_ID_BOOT) {
                s_rtc_mushaf_jump_idx = (s_rtc_mushaf_jump_idx - 1 + 35) % 35;
            } else if (evt.button == BTN_ID_PWR) {
                s_rtc_mushaf_jump_idx = (s_rtc_mushaf_jump_idx + 1) % 35;
            }
            view_mushaf_render_jump(false);
            return VIEW_MUSHAF_JUMP;
        } else if (evt.type == BTN_DOUBLE_CLICK) {
            uint8_t surah = 1;
            uint16_t ayah = 1;
            if (s_rtc_mushaf_jump_idx < 5) {
                surah = k_sunnah_presets[s_rtc_mushaf_jump_idx].surah;
                ayah = k_sunnah_presets[s_rtc_mushaf_jump_idx].ayah;
            } else {
                uint8_t juz = (uint8_t)(s_rtc_mushaf_jump_idx - 4);
                get_juz_start(juz, &surah, &ayah);
            }
            const mushaf_state_t *st = mushaf_reader_get_state();
            uint8_t layer = st ? st->layer : 0;
            mushaf_reader_set_cursor(surah, ayah, layer);
            view_mushaf_render_reading(true);
            return VIEW_MUSHAF_READING;
        } else if (evt.type == BTN_LONG_PRESS) {
            view_mushaf_render_menu(false);
            return VIEW_MUSHAF_MENU;
        }
        return VIEW_MUSHAF_JUMP;
    }

    if (view == VIEW_MUSHAF_BOOKMARKS) {
        if (evt.type == BTN_EVENT_CUSTOM_REFRESH) {
            view_mushaf_render_bookmarks(false);
            return VIEW_MUSHAF_BOOKMARKS;
        }

        const mushaf_state_t *st = mushaf_reader_get_state();
        uint8_t count = (st != NULL) ? st->bookmark_count : 0;

        if (count == 0) {
            if (evt.type == BTN_LONG_PRESS || evt.type == BTN_DOUBLE_CLICK) {
                view_mushaf_render_menu(false);
                return VIEW_MUSHAF_MENU;
            }
            return VIEW_MUSHAF_BOOKMARKS;
        }

        if (evt.type == BTN_CLICK) {
            if (evt.button == BTN_ID_BOOT) {
                s_rtc_mushaf_bm_idx = (s_rtc_mushaf_bm_idx - 1 + count) % count;
            } else if (evt.button == BTN_ID_PWR) {
                s_rtc_mushaf_bm_idx = (s_rtc_mushaf_bm_idx + 1) % count;
            }
            view_mushaf_render_bookmarks(false);
            return VIEW_MUSHAF_BOOKMARKS;
        } else if (evt.type == BTN_DOUBLE_CLICK) {
            if (s_rtc_mushaf_bm_idx >= count) s_rtc_mushaf_bm_idx = 0;
            uint8_t surah = st->bookmarks[s_rtc_mushaf_bm_idx].surah;
            uint16_t ayah = st->bookmarks[s_rtc_mushaf_bm_idx].ayah;
            mushaf_reader_set_cursor(surah, ayah, st->layer);
            view_mushaf_render_reading(true);
            return VIEW_MUSHAF_READING;
        } else if (evt.type == BTN_LONG_PRESS) {
            view_mushaf_render_menu(false);
            return VIEW_MUSHAF_MENU;
        }
        return VIEW_MUSHAF_BOOKMARKS;
    }

    return view;
}

reader_view_t view_mushaf_handle_event(reader_view_t view, button_event_t evt) {
    reader_view_t next = mushaf_handle_event_internal(view, evt);
    if (next >= VIEW_MUSHAF_MENU && next <= VIEW_MUSHAF_BOOKMARKS) {
        s_rtc_mushaf_sub_view = next;
    }
    return next;
}

#endif // CONFIG_BOARD_MUSHAF_ENABLE || BOARD_MUSHAF_ENABLE
