#ifndef VIEWS_H
#define VIEWS_H

#include <stdbool.h>
#include <stdint.h>
#include "reader_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

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
