#ifndef VIEW_LIBRARY_H
#define VIEW_LIBRARY_H

#include <stdbool.h>
#include "reader_ui.h"
#include "button.h"

#ifdef __cplusplus
extern "C" {
#endif

int view_library_get_index(void);
void view_library_set_index(int index);
void view_library_render(bool full_refresh);
reader_view_t view_library_on_wake(void);
reader_view_t view_library_handle_event(button_event_t evt);

#ifdef __cplusplus
}
#endif

#endif // VIEW_LIBRARY_H
