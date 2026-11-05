#ifndef VIEW_BOOKMARKS_H
#define VIEW_BOOKMARKS_H

#include <stdbool.h>
#include "reader_ui.h"
#include "power_mgr.h"
#include "button.h"

#ifdef __cplusplus
extern "C" {
#endif

int view_bookmarks_get_index(void);
void view_bookmarks_set_index(int index);
void view_bookmarks_render(bool full_refresh);
reader_view_t view_bookmarks_on_wake(wakeup_reason_t wakeup);
reader_view_t view_bookmarks_handle_event(button_event_t evt);

#ifdef __cplusplus
}
#endif

#endif // VIEW_BOOKMARKS_H
