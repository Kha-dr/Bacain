#ifndef VIEW_READING_H
#define VIEW_READING_H

#include <stdbool.h>
#include "reader_ui.h"
#include "power_mgr.h"
#include "button.h"

#ifdef __cplusplus
extern "C" {
#endif

void view_reading_reset_redraw_counter(void);
void view_reading_render(bool full_refresh);
void view_reading_render_page_turn(void);
reader_view_t view_reading_on_wake(wakeup_reason_t wakeup, bool book_opened);
reader_view_t view_reading_handle_event(button_event_t evt);

#ifdef __cplusplus
}
#endif

#endif // VIEW_READING_H
