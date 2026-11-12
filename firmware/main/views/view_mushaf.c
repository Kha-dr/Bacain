#include "view_mushaf.h"

#if VIEWS_HAS_MUSHAF

#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "fonts.h"
#include "mushaf_reader.h"
#include "text_pager.h" /* reading font + TEXT_PAGER_FOOTER: same frame as the book reader */
#include "ui_display.h"
#include "ui_widgets.h"
#include "ui_list.h"
#include "ui_pager.h"

#define MENU_COUNT 4
#define PRESET_COUNT 5
#define JUZ_COUNT 30
#define JUMP_COUNT (PRESET_COUNT + JUZ_COUNT)

/* Print at most `limit` chars of a fixed-size, possibly unterminated name (finding 1). */
#define NAME_FIELD_SIZE sizeof(((bqm_surah_entry_t *)0)->name_latin)
#define NAME_PREC(limit) ((int)(NAME_FIELD_SIZE < (size_t)(limit) ? NAME_FIELD_SIZE : (size_t)(limit)))

/* ---------- state ---------- */

RTC_DATA_ATTR static mushaf_state_t s_rtc_state;
RTC_DATA_ATTR static int s_rtc_menu_idx = 0;
RTC_DATA_ATTR static int s_rtc_jump_idx = 0;
RTC_DATA_ATTR static int s_rtc_bm_idx = 0;
RTC_DATA_ATTR static reader_view_t s_rtc_sub_view = VIEW_MUSHAF_MENU;

static char s_item_buf[48]; /* list get_text buffer, valid until next call */
static char s_menu_item0[32];
static char s_menu_item1[32];
static char s_menu_item3[32];
static text_page_t s_text_page;

static const ui_row_style_t k_menu_row = {.font = NULL, .max_lines = 3};
static const ui_row_style_t k_line_row = {.font = NULL, .max_lines = 1};

static const struct
{
    const char *label;
    uint8_t surah;
    uint16_t ayah;
} k_presets[PRESET_COUNT] = {
    {"Al-Kahf (18:1) - Friday", 18, 1},
    {"Yasin (36:1) - Daily", 36, 1},
    {"Ar-Rahman (55:1)", 55, 1},
    {"Al-Waqiah (56:1) - Night", 56, 1},
    {"Al-Mulk (67:1) - Sleep", 67, 1},
};

static void get_juz_start(uint8_t juz, uint8_t *out_surah, uint16_t *out_ayah)
{
    static const struct
    {
        uint8_t s;
        uint16_t a;
    } k_juz[JUZ_COUNT] = {
        {1, 1}, {2, 142}, {2, 253}, {3, 93}, {4, 24}, {4, 148}, {5, 82}, {6, 111}, {7, 88}, {8, 41}, {9, 93}, {11, 6}, {12, 53}, {15, 1}, {17, 1}, {18, 75}, {21, 1}, {23, 1}, {25, 21}, {27, 56}, {29, 46}, {33, 31}, {36, 28}, {39, 32}, {41, 47}, {46, 1}, {51, 31}, {58, 1}, {67, 1}, {78, 1}};
    if (juz >= 1 && juz <= JUZ_COUNT)
    {
        *out_surah = k_juz[juz - 1].s;
        *out_ayah = k_juz[juz - 1].a;
    }
}

static ui_refresh_t refresh_mode(bool full)
{
    return full ? UI_REFRESH_FULL : UI_REFRESH_PARTIAL;
}

static void draw_no_data(void)
{
    static const char *const k_lines[] = {
        "No Mushaf data (.bqm)", "Connect via Sync Books", "Build & upload via Web"};
    ui_draw_message(k_lines, 3, NULL, false);
    ui_draw_footer("Hold ▲/▼ Back", false);
}

/* ---------- menu ---------- */

static const char *menu_text(void *ctx, size_t i)
{
    (void)ctx;
    switch (i)
    {
    case 0:
        return s_menu_item0;
    case 1:
        return s_menu_item1;
    case 2:
        return "Sunnah & Juz Jump";
    case 3:
        return s_menu_item3;
    default:
        return "";
    }
}

static void build_menu_items(void)
{
    static const char *const k_layer_names[MUSHAF_LAYER_COUNT] = {"Mushaf", "Latin", "Translate", "Tafsir"};
    const mushaf_state_t *st = mushaf_reader_get_state();

    if (!st->started)
    {
        snprintf(s_menu_item0, sizeof(s_menu_item0), "Start Tilawah");
    }
    else
    {
        bqm_ayah_entry_t info;
        uint8_t juz = 1;
        if (mushaf_reader_get_ayah(st->surah, st->ayah, &info, NULL) == ESP_OK)
        {
            juz = info.juz;
        }
        snprintf(s_menu_item0, sizeof(s_menu_item0), "Resume %u:%u (Juz %u)", st->surah, st->ayah, juz);
    }

    uint8_t layer = (st->layer < MUSHAF_LAYER_COUNT) ? st->layer : 0;
    snprintf(s_menu_item1, sizeof(s_menu_item1), "Layer: [%s]", k_layer_names[layer]);
    snprintf(s_menu_item3, sizeof(s_menu_item3), "Bookmarks (%u/%u)",
             st->bookmark_count, (unsigned)MUSHAF_MAX_BOOKMARKS);
}

static void render_menu(bool full)
{
    if (!ui_begin(true))
    {
        return;
    }
    int top = ui_draw_header("MUSHAF MENU", true);

    if (!mushaf_reader_is_available())
    {
        draw_no_data();
    }
    else
    {
        build_menu_items();
        s_rtc_menu_idx = ui_list_clamp(s_rtc_menu_idx, MENU_COUNT);
        ui_list_t list = {
            .count = MENU_COUNT,
            .selected = s_rtc_menu_idx,
            .get_text = menu_text,
            .style = &k_menu_row,
            .top = top,
            .bottom = ui_body_bottom(false),
        };
        ui_draw_list(&list);
        ui_draw_footer("2x▲/▼ Select | Hold ▲/▼ Back", false);
    }
    ui_end(refresh_mode(full));
}

static reader_view_t event_menu(reader_view_t self, button_event_t evt)
{
    if (!mushaf_reader_is_available())
    {
        return (evt.type == BTN_LONG_PRESS || evt.type == BTN_DOUBLE_CLICK) ? VIEW_MENU : self;
    }
    if (evt.type == BTN_LONG_PRESS)
    {
        return VIEW_MENU;
    }
    if (ui_list_step(&s_rtc_menu_idx, MENU_COUNT, evt))
    {
        render_menu(false);
        return self;
    }
    if (evt.type != BTN_DOUBLE_CLICK)
    {
        return self;
    }
    switch (s_rtc_menu_idx)
    {
    case 0:
        return VIEW_MUSHAF_READING;
    case 1:
        mushaf_reader_cycle_layer();
        render_menu(false);
        return self;
    case 2:
        return VIEW_MUSHAF_JUMP;
    case 3:
        return VIEW_MUSHAF_BOOKMARKS;
    default:
        return self;
    }
}

/* ---------- reading (same frame as the book reader) ---------- */

static void format_title(const mushaf_state_t *st, char *out, size_t n)
{
    bqm_surah_entry_t info;
    if (mushaf_reader_get_surah(st->surah, &info) == ESP_OK)
    {
        snprintf(out, n, "%u:%u %.*s", st->surah, st->ayah, NAME_PREC(NAME_FIELD_SIZE), info.name_latin);
    }
    else
    {
        snprintf(out, n, "%u:%u", st->surah, st->ayah);
    }
}

/* left: "J3 LAT [BM]"   right: "2/5 40%" (same rules as the old mushaf footer) */
static void format_footer(const mushaf_state_t *st, char *left, size_t ln, char *right, size_t rn)
{
    static const char *const k_tags[MUSHAF_LAYER_COUNT] = {"MUS", "LAT", "TRN", "TAF"};
    const char *tag = (st->layer < MUSHAF_LAYER_COUNT) ? k_tags[st->layer] : "?";
    const char *bm = mushaf_reader_is_bookmarked(st->surah, st->ayah) ? " [BM]" : "";

    right[0] = '\0';
    bqm_ayah_entry_t info;
    uint16_t global = 0;
    if (mushaf_reader_get_ayah(st->surah, st->ayah, &info, &global) != ESP_OK)
    {
        snprintf(left, ln, "%s%s", tag, bm);
        return;
    }
    snprintf(left, ln, "J%u %s%s", info.juz, tag, bm);

    uint8_t sub = mushaf_reader_get_subpage_count();
    uint32_t pct = st->started ? (uint32_t)(((uint64_t)global * 100) / MUSHAF_TOTAL_AYAHS) : 0;
    if (sub > 1 && pct > 0)
    {
        snprintf(right, rn, "%u/%u %u%%", st->subpage + 1, sub, (unsigned)pct);
    }
    else if (sub > 1)
    {
        snprintf(right, rn, "%u/%u", st->subpage + 1, sub);
    }
    else if (pct > 0)
    {
        snprintf(right, rn, "%u%%", (unsigned)pct);
    }
}

static void render_reading(ui_refresh_t mode)
{
    if (!ui_begin(true))
    {
        return;
    }
    if (!mushaf_reader_is_available())
    {
        ui_draw_header_plain("MUSHAF", true);
        draw_no_data();
        ui_end(mode);
        return;
    }

    /* Fetch text first: it may normalize the cursor (layer/subpage) before labels are built. */
    const mushaf_state_t *st = mushaf_reader_get_state();
    esp_err_t text_err = ESP_ERR_NOT_SUPPORTED;
    if (st->layer != MUSHAF_LAYER_MUSHAF)
    {
        text_err = mushaf_reader_get_text_page(&s_text_page);
    }

    char title[40], left[24], right[24];
    format_title(st, title, sizeof(title));
    format_footer(st, left, sizeof(left), right, sizeof(right));

    ui_pager_chrome_t chrome = {
        .title = title,
        .show_battery = true,
        .footer = TEXT_PAGER_FOOTER,
        .footer_left = left,
        .footer_right = right[0] ? right : NULL,
    };

    if (text_err == ESP_ERR_NOT_SUPPORTED)
    {
        /* Mushaf image layer: frame with an empty body, then sprites clipped to the body. */
        chrome.empty_text = "";
        ui_pager_draw(NULL, text_pager_get_style(), &chrome);
        int top, bottom;
        ui_pager_body_rect(&chrome, &top, &bottom);
        mushaf_reader_draw_mushaf_page(top, bottom);
    }
    else
    {
        chrome.empty_text = "No text for this ayah.";
        ui_pager_draw((text_err == ESP_OK) ? &s_text_page : NULL, text_pager_get_style(), &chrome);
    }
    ui_end(mode);
}

static reader_view_t event_reading(reader_view_t self, button_event_t evt)
{
    if (evt.type == BTN_LONG_PRESS)
    {
        view_mushaf_save_before_sleep();
        return VIEW_MUSHAF_MENU;
    }
    if (evt.type == BTN_CLICK)
    {
        mushaf_reader_step_page(evt.button == BTN_ID_PWR ? 1 : -1);
        render_reading(UI_REFRESH_PAGE_TURN);
    }
    else if (evt.type == BTN_DOUBLE_CLICK)
    {
        if (evt.button == BTN_ID_BOOT)
        {
            mushaf_reader_cycle_layer();
        }
        else if (evt.button == BTN_ID_PWR)
        {
            mushaf_reader_toggle_bookmark();
        }
        render_reading(UI_REFRESH_PARTIAL);
    }
    return self;
}

/* ---------- jump list ---------- */

static const char *jump_text(void *ctx, size_t i)
{
    (void)ctx;
    if (i < PRESET_COUNT)
    {
        return k_presets[i].label;
    }
    uint8_t juz = (uint8_t)(i - PRESET_COUNT + 1);
    uint8_t s = 1;
    uint16_t a = 1;
    get_juz_start(juz, &s, &a);

    bqm_surah_entry_t info;
    if (mushaf_reader_get_surah(s, &info) == ESP_OK)
    {
        snprintf(s_item_buf, sizeof(s_item_buf), "Juz %u: %.*s %u:%u",
                 juz, NAME_PREC(10), info.name_latin, s, a);
    }
    else
    {
        snprintf(s_item_buf, sizeof(s_item_buf), "Juz %u (%u:%u)", juz, s, a);
    }
    return s_item_buf;
}

static void render_jump(bool full)
{
    if (!ui_begin(true))
    {
        return;
    }
    int top = ui_draw_header("SUNNAH & JUZ JUMP", true);
    s_rtc_jump_idx = ui_list_clamp(s_rtc_jump_idx, JUMP_COUNT);
    ui_list_t list = {
        .count = JUMP_COUNT,
        .selected = s_rtc_jump_idx,
        .get_text = jump_text,
        .style = &k_line_row,
        .top = top,
        .bottom = ui_body_bottom(false),
    };
    ui_draw_list(&list);
    ui_draw_footer("2x▲/▼ Jump | Hold ▲/▼ Back", false);
    ui_end(refresh_mode(full));
}

static reader_view_t event_jump(reader_view_t self, button_event_t evt)
{
    if (evt.type == BTN_LONG_PRESS)
    {
        return VIEW_MUSHAF_MENU;
    }
    if (ui_list_step(&s_rtc_jump_idx, JUMP_COUNT, evt))
    {
        render_jump(false);
        return self;
    }
    if (evt.type != BTN_DOUBLE_CLICK)
    {
        return self;
    }

    uint8_t surah = 1;
    uint16_t ayah = 1;
    if (s_rtc_jump_idx < PRESET_COUNT)
    {
        surah = k_presets[s_rtc_jump_idx].surah;
        ayah = k_presets[s_rtc_jump_idx].ayah;
    }
    else
    {
        get_juz_start((uint8_t)(s_rtc_jump_idx - PRESET_COUNT + 1), &surah, &ayah);
    }
    mushaf_reader_set_cursor(surah, ayah, mushaf_reader_get_state()->layer);
    views_request_full_refresh();
    return VIEW_MUSHAF_READING;
}

/* ---------- bookmarks ---------- */

static const char *bookmark_text(void *ctx, size_t i)
{
    const mushaf_state_t *st = (const mushaf_state_t *)ctx;
    const mushaf_bookmark_t *b = &st->bookmarks[i];

    bqm_surah_entry_t info;
    if (mushaf_reader_get_surah(b->surah, &info) == ESP_OK)
    {
        snprintf(s_item_buf, sizeof(s_item_buf), "%u. %.*s %u:%u (J%u)",
                 (unsigned)(i + 1), NAME_PREC(11), info.name_latin, b->surah, b->ayah, b->juz);
    }
    else
    {
        snprintf(s_item_buf, sizeof(s_item_buf), "%u. %u:%u (J%u)",
                 (unsigned)(i + 1), b->surah, b->ayah, b->juz);
    }
    return s_item_buf;
}

static void render_bookmarks(bool full)
{
    if (!ui_begin(true))
    {
        return;
    }
    const mushaf_state_t *st = mushaf_reader_get_state();
    uint8_t count = st->bookmark_count;

    char title[32];
    snprintf(title, sizeof(title), "BOOKMARKS (%u/%u)", count, (unsigned)MUSHAF_MAX_BOOKMARKS);
    int top = ui_draw_header(title, true);

    if (count == 0)
    {
        static const char *const k_empty[] = {"No bookmarks yet.", "2x▼ in ayah to add"};
        ui_draw_message(k_empty, 2, NULL, false);
    }
    else
    {
        s_rtc_bm_idx = ui_list_clamp(s_rtc_bm_idx, count);
        ui_list_t list = {
            .count = count,
            .selected = s_rtc_bm_idx,
            .get_text = bookmark_text,
            .ctx = (void *)st,
            .style = &k_line_row,
            .top = top,
            .bottom = ui_body_bottom(false),
        };
        ui_draw_list(&list);
    }
    ui_draw_footer("2x▲/▼ Jump | Hold ▲/▼ Back", false);
    ui_end(refresh_mode(full));
}

static reader_view_t event_bookmarks(reader_view_t self, button_event_t evt)
{
    const mushaf_state_t *st = mushaf_reader_get_state();
    uint8_t count = st->bookmark_count;

    if (evt.type == BTN_LONG_PRESS || (count == 0 && evt.type == BTN_DOUBLE_CLICK))
    {
        return VIEW_MUSHAF_MENU;
    }
    if (ui_list_step(&s_rtc_bm_idx, count, evt))
    {
        render_bookmarks(false);
        return self;
    }
    if (evt.type != BTN_DOUBLE_CLICK)
    {
        return self;
    }

    int idx = ui_list_clamp(s_rtc_bm_idx, count);
    mushaf_reader_set_cursor(st->bookmarks[idx].surah, st->bookmarks[idx].ayah, st->layer);
    views_request_full_refresh();
    return VIEW_MUSHAF_READING;
}

/* ---------- view_def_t ---------- */

static void enter(reader_view_t self)
{
    s_rtc_sub_view = self;
    if (self == VIEW_MUSHAF_JUMP)
    {
        s_rtc_jump_idx = 0;
    }
    else if (self == VIEW_MUSHAF_BOOKMARKS)
    {
        s_rtc_bm_idx = 0;
    }
    /* Menu index is reset in leave(), so returning from a sub-view keeps the selection. */
}

static void leave(reader_view_t self)
{
    (void)self;
    view_mushaf_save_before_sleep();
    s_rtc_menu_idx = 0;
    s_rtc_sub_view = VIEW_MUSHAF_MENU;
}

static reader_view_t wake(reader_view_t self)
{
    (void)self;
    switch (s_rtc_sub_view)
    {
    case VIEW_MUSHAF_READING:
    case VIEW_MUSHAF_JUMP:
    case VIEW_MUSHAF_BOOKMARKS:
        return s_rtc_sub_view;
    default:
        s_rtc_sub_view = VIEW_MUSHAF_MENU;
        return VIEW_MUSHAF_MENU;
    }
}

static void render(reader_view_t self, bool full)
{
    switch (self)
    {
    case VIEW_MUSHAF_READING:
        render_reading(refresh_mode(full));
        break;
    case VIEW_MUSHAF_JUMP:
        render_jump(full);
        break;
    case VIEW_MUSHAF_BOOKMARKS:
        render_bookmarks(full);
        break;
    default:
        render_menu(full);
        break;
    }
}

static reader_view_t event(reader_view_t self, button_event_t evt)
{
    if (evt.type == BTN_EVENT_CUSTOM_REFRESH)
    {
        return self; /* dispatcher renders */
    }
    switch (self)
    {
    case VIEW_MUSHAF_READING:
        return event_reading(self, evt);
    case VIEW_MUSHAF_JUMP:
        return event_jump(self, evt);
    case VIEW_MUSHAF_BOOKMARKS:
        return event_bookmarks(self, evt);
    default:
        return event_menu(self, evt);
    }
}

const view_def_t VIEW_DEF_MUSHAF = {
    .first = VIEW_MUSHAF_MENU,
    .last = VIEW_MUSHAF_BOOKMARKS,
    .full_on_refresh = false,
    .enter = enter,
    .wake = wake,
    .render = render,
    .event = event,
    .leave = leave,
};

/* ---------- lifecycle (called from main) ---------- */

void view_mushaf_init(bool is_cold_boot)
{
    if (is_cold_boot)
    {
        s_rtc_sub_view = VIEW_MUSHAF_MENU;
        mushaf_reader_init(NULL);
    }
    else
    {
        mushaf_reader_init(&s_rtc_state);
    }
    memcpy(&s_rtc_state, mushaf_reader_get_state(), sizeof(mushaf_state_t));
}

void view_mushaf_save_before_sleep(void)
{
    mushaf_reader_save_state();
    memcpy(&s_rtc_state, mushaf_reader_get_state(), sizeof(mushaf_state_t));
}

#endif /* VIEWS_HAS_MUSHAF */