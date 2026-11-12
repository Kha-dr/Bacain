#include "view_menu.h"
#include <stdio.h>
#include "esp_attr.h"
#include "fonts.h"
#include "app_settings.h"
#include "ui_display.h"
#include "ui_widgets.h"
#include "ui_list.h"
#include "ui_screens.h"

typedef enum
{
    MENU_ITEM_CONTINUE = 0,
    MENU_ITEM_LIBRARY,
#if VIEWS_HAS_MUSHAF
    MENU_ITEM_MUSHAF,
#endif
    MENU_ITEM_FONT_SIZE,
    MENU_ITEM_SYNC,
    MENU_ITEM_NAV_HINTS,
    MENU_ITEM_REFRESH,
    MENU_ITEM_POWER_OFF,
    MENU_ITEM_COUNT
} menu_item_id_t;

static const ui_row_style_t k_menu_row = {.font = NULL, .max_lines = 3};

RTC_DATA_ATTR static int s_rtc_menu_index = 0;
static char s_font_label[24];

int view_menu_get_index(void) { return s_rtc_menu_index; }
void view_menu_set_index(int index) { s_rtc_menu_index = index; }

static const char *item_text(void *ctx, size_t index)
{
    (void)ctx;
    switch ((menu_item_id_t)index)
    {
    case MENU_ITEM_CONTINUE:
        return "Continue Reading";
    case MENU_ITEM_LIBRARY:
        return "Book Library";
#if VIEWS_HAS_MUSHAF
    case MENU_ITEM_MUSHAF:
        return "Mushaf";
#endif
    case MENU_ITEM_FONT_SIZE:
        return s_font_label;
    case MENU_ITEM_SYNC:
        return "Sync Books";
    case MENU_ITEM_NAV_HINTS:
        return "Nav Hints";
    case MENU_ITEM_REFRESH:
        return "Refresh Screen";
    case MENU_ITEM_POWER_OFF:
        return "Power Off";
    default:
        return "";
    }
}

static void render(reader_view_t self, bool full_refresh)
{
    (void)self;
    if (!ui_begin(true))
    {
        return;
    }

    snprintf(s_font_label, sizeof(s_font_label), "Font Size: [%s]",
             app_settings_get()->font_size == APP_FONT_LARGE ? "16px" : "12px");
    s_rtc_menu_index = ui_list_clamp(s_rtc_menu_index, MENU_ITEM_COUNT);

    int top = ui_draw_header("READER MENU", true);
    ui_list_t list = {
        .count = MENU_ITEM_COUNT,
        .selected = s_rtc_menu_index,
        .get_text = item_text,
        .style = &k_menu_row,
        .top = top,
        .bottom = ui_body_bottom(false),
    };
    ui_draw_list(&list);
    ui_draw_footer("▲/▼ Move | 2x▲/▼ Select", false);

    ui_end(full_refresh ? UI_REFRESH_FULL : UI_REFRESH_PARTIAL);
}

static reader_view_t wake(reader_view_t self)
{
    if (s_rtc_menu_index < 0 || s_rtc_menu_index >= MENU_ITEM_COUNT)
    {
        s_rtc_menu_index = 0;
    }
    return self;
}

static void toggle_font_size(void)
{
    app_font_size_t next = (app_settings_get()->font_size == APP_FONT_LARGE) ? APP_FONT_SMALL
                                                                             : APP_FONT_LARGE;
    app_settings_set_font_size(next);
    app_settings_apply(); /* single place that maps settings -> pager font */
}

static reader_view_t event(reader_view_t self, button_event_t evt)
{
    if (evt.type == BTN_EVENT_CUSTOM_REFRESH)
    {
        return self;
    }
    if (ui_list_step(&s_rtc_menu_index, MENU_ITEM_COUNT, evt))
    {
        render(self, false);
        return self;
    }
    if (evt.type != BTN_DOUBLE_CLICK)
    {
        return self;
    }

    switch ((menu_item_id_t)s_rtc_menu_index)
    {
    case MENU_ITEM_CONTINUE:
        return VIEW_READING;
    case MENU_ITEM_LIBRARY:
        return VIEW_BOOK_LIST;
#if VIEWS_HAS_MUSHAF
    case MENU_ITEM_MUSHAF:
        return VIEW_MUSHAF_MENU;
#endif
    case MENU_ITEM_FONT_SIZE:
        toggle_font_size();
        render(self, false);
        return self;
    case MENU_ITEM_SYNC:
        return VIEW_BLE_SYNC;
    case MENU_ITEM_NAV_HINTS:
        return VIEW_NAV_HINTS;
    case MENU_ITEM_REFRESH:
        render(self, true);
        return self;
    case MENU_ITEM_POWER_OFF:
        ui_screen_power_off(app_settings_get()->btn_invert);
        return VIEW_REQUEST_POWER_OFF;
    default:
        return self;
    }
}

const view_def_t VIEW_DEF_MENU = {
    .first = VIEW_MENU,
    .last = VIEW_MENU,
    .full_on_refresh = false,
    .wake = wake,
    .render = render,
    .event = event,
};