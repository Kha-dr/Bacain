#include "view_library.h"
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "fonts.h"
#include "book_library.h"
#include "text_pager.h"
#include "ui_display.h"
#include "ui_widgets.h"
#include "ui_list.h"

static const char *TAG = "view_library";

#define LIB_PATH_MAX 256 /* matches text_pager_t.active_book_path */

RTC_DATA_ATTR static int s_rtc_book_index = 0;

/* get_text buffer: valid until the next call (ui_list uses each result immediately). */
static char s_name_buf[BOOK_FILENAME_MAX_LEN + 16];

int view_library_get_index(void) { return s_rtc_book_index; }
void view_library_set_index(int index) { s_rtc_book_index = index; }

static const char *book_name(void *ctx, size_t index)
{
    (void)ctx;
    const book_info_t *book = book_library_get_book(index);
    if (!book)
    {
        s_name_buf[0] = '\0';
    }
    else if (book->type == LIBRARY_ENTRY_UP)
    {
        snprintf(s_name_buf, sizeof(s_name_buf), "[.. Back]");
    }
    else if (book->type == LIBRARY_ENTRY_DIR)
    {
        snprintf(s_name_buf, sizeof(s_name_buf), "[D] %s", book->filename);
    }
    else
    {
        snprintf(s_name_buf, sizeof(s_name_buf), "%s", book->filename);
    }
    return s_name_buf;
}

static void format_title(char *out, size_t len)
{
    if (book_library_is_root_dir())
    {
        snprintf(out, len, "SELECT BOOK [/]");
    }
    else
    {
        const char *rel = book_library_get_current_rel_path();
        snprintf(out, len, "DIR: %.16s", (rel && rel[0]) ? rel : "/");
    }
}

static void render(reader_view_t self, bool full_refresh)
{
    (void)self;
    if (!ui_begin(true))
    {
        return;
    }

    char title[32];
    format_title(title, sizeof(title));
    int top = ui_draw_header(title, false);

    size_t count = book_library_get_count();
    if (count == 0)
    {
        static const char *const k_empty[] = {"No books in folder!", "Start sync books to add"};
        ui_draw_message(k_empty, 2, NULL, false);
    }
    else
    {
        ui_row_style_t style = {.font = NULL, .max_lines = (ui_body_font() == &Font16) ? 2 : 3};

        s_rtc_book_index = ui_list_clamp(s_rtc_book_index, (int)count);
        ui_list_t list = {
            .count = count,
            .selected = s_rtc_book_index,
            .get_text = book_name,
            .style = &style,
            .top = top,
            .bottom = ui_body_bottom(false),
        };
        ui_draw_list(&list);
    }

    ui_draw_footer("2x▲/▼ Open | Hold ▲/▼ Back", false);
    ui_end(full_refresh ? UI_REFRESH_FULL : UI_REFRESH_PARTIAL);
}

static void enter(reader_view_t self)
{
    (void)self;
    book_library_scan(); /* was done by the menu before entering */
    s_rtc_book_index = 0;
}

static reader_view_t wake(reader_view_t self)
{
    if (book_library_get_count() == 0)
    {
        book_library_scan();
    }
    s_rtc_book_index = ui_list_clamp(s_rtc_book_index, (int)book_library_get_count());
    return self;
}

static reader_view_t open_file(const char *path)
{
    /* Set active first: the resume offset comes from the new active book's state. */
    book_library_set_active_book(path, false);
    const reading_state_t *st = book_library_get_state();
    size_t resume = st ? st->byte_offset : 0;

    if (text_pager_open_book(path, resume) == ESP_OK)
    {
        return VIEW_READING;
    }
    ESP_LOGW(TAG, "failed to open %s", path);
    return VIEW_BOOK_LIST;
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

    size_t count = book_library_get_count();
    if (ui_list_step(&s_rtc_book_index, (int)count, evt))
    { /* finding 1: own index */
        render(self, false);
        return self;
    }
    if (evt.type != BTN_DOUBLE_CLICK || count == 0)
    {
        return self;
    }

    const book_info_t *book =
        book_library_get_book((size_t)ui_list_clamp(s_rtc_book_index, (int)count));
    if (!book)
    {
        return self;
    }

    /* Copy before calling into the library: navigation may rebuild the entry table. */
    char name[LIB_PATH_MAX];
    switch (book->type)
    {
    case LIBRARY_ENTRY_UP:
        book_library_navigate_up();
        s_rtc_book_index = 0;
        render(self, false);
        return self;
    case LIBRARY_ENTRY_DIR:
        strncpy(name, book->filename, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        book_library_enter_dir(name);
        s_rtc_book_index = 0;
        render(self, false);
        return self;
    case LIBRARY_ENTRY_FILE:
        strncpy(name, book->path, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        return open_file(name);
    default:
        return self;
    }
}

const view_def_t VIEW_DEF_LIBRARY = {
    .first = VIEW_BOOK_LIST,
    .last = VIEW_BOOK_LIST,
    .full_on_refresh = false,
    .enter = enter,
    .wake = wake,
    .render = render,
    .event = event,
};