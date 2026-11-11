#ifndef VIEW_READING_H
#define VIEW_READING_H

#include <stdbool.h>
#include "reader_ui.h"
#include "button.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t view_reading_ensure_book_open(void);
void view_reading_render(bool full_refresh);
void view_reading_render_page_turn(void);
reader_view_t view_reading_on_wake(void);
reader_view_t view_reading_handle_event(button_event_t evt);

#ifdef __cplusplus
}
#endif

#endif // VIEW_READING_H
