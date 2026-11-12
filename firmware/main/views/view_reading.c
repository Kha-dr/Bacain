#include "view_reading.h"
#include "esp_log.h"
#include "text_pager.h"
#include "book_library.h"
#include "ui_display.h"
#include "ui_pager.h"
#include "view_menu.h"

static const char *TAG = "view_reading";

esp_err_t view_reading_ensure_book_open(void)
{
    if (text_pager_is_book_open())
    {
        return ESP_OK;
    }

    reading_state_t state;
    if (book_library_get_current_state(&state) == ESP_OK && state.active_book_path[0] != '\0' &&
        text_pager_open_book(state.active_book_path, state.byte_offset) == ESP_OK)
    {
        return ESP_OK;
    }

    size_t count = book_library_get_count();
    for (size_t i = 0; i < count; i++)
    {
        const book_info_t *book = book_library_get_book(i);
        if (book && book->type == LIBRARY_ENTRY_FILE &&
            text_pager_open_book(book->path, 0) == ESP_OK)
        {
            return ESP_OK; /* pager listener sets the active book */
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static void draw_page(void)
{
    const text_page_t *page = text_pager_get_layout_page();
    char pct[8] = "";
    bool bookmarked = false;
    if (page)
    {
        ui_pager_format_progress(page, pct, sizeof(pct));
        bookmarked = book_library_is_bookmarked(page->start);
    }

    ui_pager_chrome_t chrome = {
        .title = text_pager_get_title(),
        .header_inverted = false,
        .show_battery = true,
        .footer = TEXT_PAGER_FOOTER,
        .footer_left = bookmarked ? "[BM]" : NULL,
        .footer_right = page ? pct : NULL,
        .empty_text = "No book. Hold ▲/▼ for menu",
    };
    ui_pager_draw(page, text_pager_get_style(), &chrome);
}

static void render_mode(ui_refresh_t mode)
{
    if (!ui_begin(true))
    {
        return;
    }
    draw_page();
    ui_end(mode);
}

static void render(reader_view_t self, bool full_refresh)
{
    (void)self;
    render_mode(full_refresh ? UI_REFRESH_FULL : UI_REFRESH_PARTIAL);
}

static void enter(reader_view_t self)
{
    (void)self;
    if (view_reading_ensure_book_open() != ESP_OK)
    {
        ESP_LOGW(TAG, "no book to open");
    }
}

static reader_view_t wake(reader_view_t self)
{
    if (view_reading_ensure_book_open() == ESP_OK && text_pager_get_current_page() != NULL)
    {
        return self;
    }
    view_menu_set_index(0);
    return VIEW_MENU;
}

static void toggle_bookmark(void)
{
    size_t off = text_pager_get_current_offset(); /* == page start (Step 3) */
    if (book_library_is_bookmarked(off))
    {
        book_library_remove_bookmark(off);
    }
    else
    {
        book_library_add_bookmark(off);
    }
}

static reader_view_t event(reader_view_t self, button_event_t evt)
{
    if (evt.type == BTN_EVENT_CUSTOM_REFRESH)
    {
        return self;
    }
    if (evt.type == BTN_LONG_PRESS)
    {
        view_menu_set_index(0);
        return VIEW_MENU;
    }

    if (evt.button == BTN_ID_BOOT)
    {
        if (evt.type == BTN_CLICK)
        {
            /* offset persistence is done by the pager listener (app_links) */
            render_mode(text_pager_prev_page() == ESP_OK ? UI_REFRESH_PAGE_TURN : UI_REFRESH_PARTIAL);
        }
        else if (evt.type == BTN_DOUBLE_CLICK)
        {
            toggle_bookmark();
            render_mode(UI_REFRESH_PARTIAL);
        }
    }
    else if (evt.button == BTN_ID_PWR)
    {
        if (evt.type == BTN_CLICK)
        {
            render_mode(text_pager_next_page() == ESP_OK ? UI_REFRESH_PAGE_TURN : UI_REFRESH_PARTIAL);
        }
        else if (evt.type == BTN_DOUBLE_CLICK)
        {
            return VIEW_BOOKMARKS; /* bookmarks.enter resets its index */
        }
    }
    return self;
}

const view_def_t VIEW_DEF_READING = {
    .first = VIEW_READING,
    .last = VIEW_READING,
    .full_on_refresh = true,
    .enter = enter,
    .wake = wake,
    .render = render,
    .event = event,
};