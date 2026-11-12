#include "view_nav_hints.h"
#include <stdio.h>
#include "esp_attr.h"
#include "app_settings.h"
#include "text_source.h"
#include "text_layout.h"
#include "ui_display.h"
#include "ui_widgets.h"
#include "ui_pager.h"

#define HINTS_MAX_PAGES 8

/* Paragraphs separated by blank lines; leading spaces are kept as indentation. */
static const char k_hints_text[] =
    "Reading\n"
    " \xE2\x96\xB2/\xE2\x96\xBC  Prev / next page\n"
    " 2x\xE2\x96\xB2  Bookmark this page\n"
    " 2x\xE2\x96\xBC  Bookmark list\n"
    " Hold  Menu\n"
    "\n"
    "Menu and lists\n"
    " \xE2\x96\xB2/\xE2\x96\xBC  Move\n"
    " 2x\xE2\x96\xB2/\xE2\x96\xBC  Select / open\n"
    " Hold  Back\n"
    "\n"
#if VIEWS_HAS_MUSHAF
    "Mushaf\n"
    " \xE2\x96\xB2/\xE2\x96\xBC  Prev / next\n"
    " 2x\xE2\x96\xB2  Change layer\n"
    " 2x\xE2\x96\xBC  Bookmark ayah\n"
    " Hold  Mushaf menu\n"
    "\n"
#endif
    "Sync\n"
    " Hold  Cancel / back\n"
    "\n"
    "Global\n"
    " Hold \xE2\x96\xB2+\xE2\x96\xBC (2s)  Reboot\n"
    "\n"
    "This screen\n"
    " \xE2\x96\xB2/\xE2\x96\xBC  Page\n"
    " 2x  Toggle hints\n"
    " Hold  Back\n";

static const ui_pager_chrome_t k_chrome = {
    .title = "NAVIGATION HINTS",
    .header_inverted = true,
    .show_battery = false,
    .footer = UI_PAGER_FOOTER_STATUS,
};

RTC_DATA_ATTR static int s_rtc_page = 0;

static text_source_t s_src;
static text_page_t s_page;
static size_t s_page_starts[HINTS_MAX_PAGES];
static int s_page_count = 1;

/* Text is a few hundred bytes: re-paginating on each render keeps it correct
 * after rotation changes, at negligible cost. */
static void paginate(const text_layout_cfg_t *cfg)
{
    size_t offset = 0;
    s_page_count = 0;
    while (s_page_count < HINTS_MAX_PAGES)
    {
        s_page_starts[s_page_count++] = offset;
        if (text_layout_page(&s_src, offset, cfg, &s_page) != ESP_OK || s_page.is_eof)
        {
            break;
        }
        offset = s_page.end;
    }
}

static void render(reader_view_t self, bool full_refresh)
{
    (void)self;
    if (!ui_begin(true))
    {
        return;
    }

    if (!text_source_is_open(&s_src))
    {
        text_source_from_str(&s_src, k_hints_text);
    }

    const ui_pager_style_t *style = ui_pager_body_style();
    text_layout_cfg_t cfg = ui_pager_layout_cfg(style, &k_chrome, true);
    paginate(&cfg);
    if (s_rtc_page < 0 || s_rtc_page >= s_page_count)
    {
        s_rtc_page = 0;
    }
    text_layout_page(&s_src, s_page_starts[s_rtc_page], &cfg, &s_page);

    char right[24];
    snprintf(right, sizeof(right), "%d/%d", s_rtc_page + 1, s_page_count);

    ui_pager_chrome_t chrome = k_chrome;
    chrome.footer_left = ui_nav_hints_visible() ? "Hints: ON" : "Hints: OFF";
    chrome.footer_right = (s_page_count > 1) ? right : NULL;
    ui_pager_draw(&s_page, style, &chrome);

    ui_end(full_refresh ? UI_REFRESH_FULL : UI_REFRESH_PARTIAL);
}

static void enter(reader_view_t self)
{
    (void)self;
    s_rtc_page = 0;
}

static reader_view_t wake(reader_view_t self)
{
    return self; /* page index restored from RTC; clamped on render */
}

static void toggle_hints(void)
{
    bool show = !app_settings_get()->show_nav_hints;
    app_settings_set_nav_hints(show);
    ui_set_nav_hints_visible(show);
}

static reader_view_t event(reader_view_t self, button_event_t evt)
{
    if (evt.type == BTN_EVENT_CUSTOM_REFRESH)
    {
        return self;
    }
    if (evt.type == BTN_LONG_PRESS)
    {
        return VIEW_MENU;
    }

    if (evt.type == BTN_CLICK)
    {
        int before = s_rtc_page;
        if (evt.button == BTN_ID_BOOT && s_rtc_page > 0)
        {
            s_rtc_page--;
        }
        else if (evt.button == BTN_ID_PWR && s_rtc_page + 1 < s_page_count)
        {
            s_rtc_page++;
        }
        if (s_rtc_page != before)
        {
            render(self, false);
        }
    }
    else if (evt.type == BTN_DOUBLE_CLICK)
    {
        toggle_hints();
        render(self, false);
    }
    return self;
}

const view_def_t VIEW_DEF_NAV_HINTS = {
    .first = VIEW_NAV_HINTS,
    .last = VIEW_NAV_HINTS,
    .full_on_refresh = false,
    .enter = enter,
    .wake = wake,
    .render = render,
    .event = event,
};