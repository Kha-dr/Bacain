#ifndef VIEW_NAV_HINTS_H
#define VIEW_NAV_HINTS_H

#include "reader_ui.h"
#include "power_mgr.h"
#include "button.h"

#ifdef __cplusplus
extern "C" {
#endif

void view_nav_hints_render(void);
reader_view_t view_nav_hints_on_wake(wakeup_reason_t wakeup);
reader_view_t view_nav_hints_handle_event(button_event_t evt);

#ifdef __cplusplus
}
#endif

#endif // VIEW_NAV_HINTS_H
