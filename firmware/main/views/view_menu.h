#ifndef VIEW_MENU_H
#define VIEW_MENU_H

#include <stdbool.h>
#include "reader_ui.h"
#include "power_mgr.h"
#include "button.h"

#ifdef __cplusplus
extern "C" {
#endif

int view_menu_get_index(void);
void view_menu_set_index(int index);
void view_menu_render(bool full_refresh);
reader_view_t view_menu_on_wake(wakeup_reason_t wakeup);
reader_view_t view_menu_handle_event(button_event_t evt);

#ifdef __cplusplus
}
#endif

#endif // VIEW_MENU_H
