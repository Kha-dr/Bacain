#include "views.h"
#include <stddef.h>
#include "view_reading.h"
#include "view_menu.h"
#include "view_library.h"
#include "view_bookmarks.h"
#include "view_nav_hints.h"
#include "view_ble_sync.h"
#if VIEWS_HAS_MUSHAF
#include "view_mushaf.h"
#endif

static const view_def_t *const k_views[] = {
    &VIEW_DEF_READING,
    &VIEW_DEF_MENU,
    &VIEW_DEF_LIBRARY,
    &VIEW_DEF_BOOKMARKS,
    &VIEW_DEF_NAV_HINTS,
    &VIEW_DEF_BLE_SYNC,
#if VIEWS_HAS_MUSHAF
    &VIEW_DEF_MUSHAF,
#endif
};

static bool s_next_full = false;

static const view_def_t *find(reader_view_t id)
{
    for (size_t i = 0; i < sizeof(k_views) / sizeof(k_views[0]); i++)
    {
        if (id >= k_views[i]->first && id <= k_views[i]->last)
        {
            return k_views[i];
        }
    }
    return NULL;
}

static bool consume_full(void)
{
    bool full = s_next_full;
    s_next_full = false;
    return full;
}

void views_request_full_refresh(void)
{
    s_next_full = true;
}

void views_render(reader_view_t view, bool full_refresh)
{
    const view_def_t *d = find(view);
    if (d && d->render)
    {
        d->render(view, full_refresh);
    }
}

reader_view_t views_navigate(reader_view_t from, reader_view_t to)
{
    if (from == to)
    {
        return to;
    }
    const view_def_t *f = find(from);
    const view_def_t *t = find(to);

    if (f && f != t && f->leave)
    {
        f->leave(from);
    }
    if (!t)
    {
        s_next_full = false; /* e.g. VIEW_REQUEST_POWER_OFF: caller takes over */
        return to;
    }
    if (t->enter)
    {
        t->enter(to);
    }
    if (t->render)
    {
        t->render(to, consume_full());
    }
    return to;
}

reader_view_t views_dispatch_event(reader_view_t view, button_event_t evt)
{
    const view_def_t *d = find(view);
    if (!d || !d->event)
    {
        return view;
    }

    reader_view_t next = d->event(view, evt);
    if (next != view)
    {
        return views_navigate(view, next);
    }
    if (evt.type == BTN_EVENT_CUSTOM_REFRESH && d->render)
    {
        d->render(view, d->full_on_refresh || consume_full());
    }
    return view;
}

reader_view_t views_dispatch_wake(reader_view_t last_view)
{
    const view_def_t *d = find(last_view);
    if (!d || !d->wake)
    {
        view_menu_set_index(0);
        return VIEW_MENU;
    }
    return d->wake(last_view);
}