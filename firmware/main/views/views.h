#ifndef VIEWS_H
#define VIEWS_H

#include <stdbool.h>
#include <stdint.h>
#include "board_config.h"
#include "button.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VIEW_READING = 0,
    VIEW_MENU,
    VIEW_BOOK_LIST,
    VIEW_BLE_SYNC,
    VIEW_BOOKMARKS,
    VIEW_NAV_HINTS,
#if CONFIG_BOARD_MUSHAF_ENABLE || BOARD_MUSHAF_ENABLE
    VIEW_MUSHAF_MENU,
    VIEW_MUSHAF_READING,
    VIEW_MUSHAF_JUMP,
    VIEW_MUSHAF_BOOKMARKS,
#endif
    VIEW_REQUEST_POWER_OFF,
} reader_view_t;

reader_view_t views_dispatch_wake(reader_view_t last_view);
reader_view_t views_dispatch_event(reader_view_t view, button_event_t evt);

reader_view_t view_reading_on_wake(void);
reader_view_t view_menu_on_wake(void);
reader_view_t view_library_on_wake(void);
reader_view_t view_bookmarks_on_wake(void);
reader_view_t view_mushaf_on_wake(void);
reader_view_t view_nav_hints_on_wake(void);
reader_view_t view_ble_sync_on_wake(void);

#ifdef __cplusplus
}
#endif

#endif // VIEWS_H
