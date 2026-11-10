#include "views.h"
#include "view_reading.h"
#include "view_menu.h"
#include "view_library.h"
#include "view_ble_sync.h"
#include "view_bookmarks.h"
#include "view_nav_hints.h"
#if CONFIG_BOARD_MUSHAF_ENABLE
#include "view_mushaf.h"
#endif

reader_view_t views_dispatch_wake(reader_view_t last_view)
{
    switch (last_view)
    {
    case VIEW_READING:
        return view_reading_on_wake();
    case VIEW_MENU:
        return view_menu_on_wake();
    case VIEW_BOOK_LIST:
        return view_library_on_wake();
    case VIEW_BOOKMARKS:
        return view_bookmarks_on_wake();
    case VIEW_NAV_HINTS:
        return view_nav_hints_on_wake();
    case VIEW_BLE_SYNC:
        return view_ble_sync_on_wake();
#if CONFIG_BOARD_MUSHAF_ENABLE
    case VIEW_MUSHAF_MENU:
    case VIEW_MUSHAF_READING:
    case VIEW_MUSHAF_JUMP:
    case VIEW_MUSHAF_BOOKMARKS:
        return view_mushaf_on_wake();
#endif
    default:
        view_menu_set_index(0);
        return VIEW_MENU;
    }
}

reader_view_t views_dispatch_event(reader_view_t view, button_event_t evt)
{
    switch (view)
    {
    case VIEW_READING:
        return view_reading_handle_event(evt);
    case VIEW_MENU:
        return view_menu_handle_event(evt);
    case VIEW_BOOK_LIST:
        return view_library_handle_event(evt);
    case VIEW_BLE_SYNC:
        return view_ble_sync_handle_event(evt);
    case VIEW_BOOKMARKS:
        return view_bookmarks_handle_event(evt);
    case VIEW_NAV_HINTS:
        return view_nav_hints_handle_event(evt);
#if CONFIG_BOARD_MUSHAF_ENABLE
    case VIEW_MUSHAF_MENU:
    case VIEW_MUSHAF_READING:
    case VIEW_MUSHAF_JUMP:
    case VIEW_MUSHAF_BOOKMARKS:
        return view_mushaf_handle_event(view, evt);
#endif
    default:
        return view;
    }
}
