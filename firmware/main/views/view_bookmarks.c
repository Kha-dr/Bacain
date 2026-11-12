#include "view_bookmarks.h"
#include <stdio.h>
#include "esp_attr.h"
#include "fonts.h"
#include "book_library.h"
#include "text_pager.h"
#include "ui_display.h"
#include "ui_widgets.h"
#include "ui_list.h"
#include "view_reading.h"

static const ui_row_style_t k_row = {.font = NULL, .max_lines = 2}; /* NULL = body font */

RTC_DATA_ATTR static int s_rtc_bookmark_index = 0;
static char s_item_buf[32];

int view_bookmarks_get_index(void) { return s_rtc_bookmark_index; }
void view_bookmarks_set_index(int index) { s_rtc_bookmark_index = index; }

static size_t bookmark_count(const reading_state_t *st)
{
    return st ? st->bookmark_count : 0;
}

static const char *bookmark_text(void *ctx, size_t index)
{
    const reading_state_t *st = (const reading_state_t *)ctx;
    uint32_t pct = (st->total_bytes > 0)
                       ? (uint32_t)(((uint64_t)st->bookmarks[index] * 100) / st->total_bytes)
                       : 0;
    snprintf(s_item_buf, sizeof(s_item_buf), "%u. Bookmark (%u%%)",
             (unsigned)(index + 1), (unsigned)pct);
    return s_item_buf;
}

static void render(reader_view_t self, bool full_refresh)
{
    (void)self;
    if (!ui_begin(true))
    {
        return;
    }

    const reading_state_t *st = book_library_get_state();
    size_t count = bookmark_count(st);

    char title[48];
    snprintf(title, sizeof(title), "BOOKMARKS (%u)", (unsigned)count);
    int top = ui_draw_header(title, false);

    if (count == 0)
    {
        static const char *const k_empty[] = {"No bookmarks yet.", "2x▲ in book to add"};
        ui_draw_message(k_empty, 2, NULL, false);
    }
    else
    {
        s_rtc_bookmark_index = ui_list_clamp(s_rtc_bookmark_index, (int)count);
        ui_list_t list = {
            .count = count,
            .selected = s_rtc_bookmark_index,
            .get_text = bookmark_text,
            .ctx = (void *)st,
            .style = &k_row,
            .top = top,
            .bottom = ui_body_bottom(false),
        };
        ui_draw_list(&list);
    }

    ui_draw_footer("2x▲/▼ Jump | Hold ▲/▼ Back", false);
    ui_end(full_refresh ? UI_REFRESH_FULL : UI_REFRESH_PARTIAL);
}

static void enter(reader_view_t self)
{
    (void)self;
    s_rtc_bookmark_index = 0; /* was set by the reading view before entering */
}

static reader_view_t wake(reader_view_t self)
{
    s_rtc_bookmark_index =
        ui_list_clamp(s_rtc_bookmark_index, (int)bookmark_count(book_library_get_state()));
    return self;
}

static reader_view_t event(reader_view_t self, button_event_t evt)
{
    if (evt.type == BTN_EVENT_CUSTOM_REFRESH)
    {
        return self;
    }
    if (evt.type == BTN_LONG_PRESS)
    {
        return VIEW_READING;
    }

    const reading_state_t *st = book_library_get_state();
    size_t count = bookmark_count(st);

    if (ui_list_step(&s_rtc_bookmark_index, (int)count, evt))
    {
        render(self, false);
        return self;
    }
    if (evt.type != BTN_DOUBLE_CLICK)
    {
        return self;
    }

    if (count > 0 && s_rtc_bookmark_index >= 0 && (size_t)s_rtc_bookmark_index < count)
    {
        size_t target = st->bookmarks[s_rtc_bookmark_index]; /* read before other library calls */
        view_reading_ensure_book_open();
        text_pager_seek(target); /* pager listener persists the offset */
    }
    return VIEW_READING;
}

const view_def_t VIEW_DEF_BOOKMARKS = {
    .first = VIEW_BOOKMARKS,
    .last = VIEW_BOOKMARKS,
    .full_on_refresh = false,
    .enter = enter,
    .wake = wake,
    .render = render,
    .event = event,
};