#ifndef VIEWS_H
#define VIEWS_H

#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "board_config.h"
#include "button.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Single feature switch for mushaf (finding 3). */
#if CONFIG_BOARD_MUSHAF_ENABLE || BOARD_MUSHAF_ENABLE
#define VIEWS_HAS_MUSHAF 1
#else
#define VIEWS_HAS_MUSHAF 0
#endif

    typedef enum
    {
        VIEW_READING = 0,
        VIEW_MENU,
        VIEW_BOOK_LIST,
        VIEW_BLE_SYNC,
        VIEW_BOOKMARKS,
        VIEW_NAV_HINTS,
#if VIEWS_HAS_MUSHAF
        VIEW_MUSHAF_MENU,
        VIEW_MUSHAF_READING,
        VIEW_MUSHAF_JUMP,
        VIEW_MUSHAF_BOOKMARKS,
#endif
        VIEW_REQUEST_POWER_OFF, /* not a screen: handled by the caller */
    } reader_view_t;

    /* One descriptor per view module. A module may own a range of ids (mushaf).
     *
     * Rules:
     *  - event() returns the next view id. It renders itself only when staying on
     *    the same view and its content changed. It never renders another view.
     *  - On a change of view, the dispatcher calls leave(old) when the module
     *    changes, then enter(new), then render(new).
     *  - BTN_EVENT_CUSTOM_REFRESH: the dispatcher calls event() (for side effects),
     *    then render(view, full_on_refresh).
     *  - wake() restores state after deep sleep and returns the view to resume.
     *    It does not render: the panel still shows the last frame. */
    typedef struct
    {
        reader_view_t first;
        reader_view_t last;
        bool full_on_refresh;
        void (*enter)(reader_view_t self); /* optional */
        reader_view_t (*wake)(reader_view_t self);
        void (*render)(reader_view_t self, bool full_refresh);
        reader_view_t (*event)(reader_view_t self, button_event_t evt);
        void (*leave)(reader_view_t self); /* optional */
    } view_def_t;

    reader_view_t views_dispatch_wake(reader_view_t last_view);
    reader_view_t views_dispatch_event(reader_view_t view, button_event_t evt);

    /* Change of view from outside an event handler (e.g. a timeout in the main loop). */
    reader_view_t views_navigate(reader_view_t from, reader_view_t to);

    void views_render(reader_view_t view, bool full_refresh);

    /* The next view entered through the dispatcher renders with a full refresh. */
    void views_request_full_refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* VIEWS_H */