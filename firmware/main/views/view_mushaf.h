#ifndef VIEW_MUSHAF_H
#define VIEW_MUSHAF_H

#include "sdkconfig.h"
#include "board_config.h"

#if CONFIG_BOARD_MUSHAF_ENABLE || BOARD_MUSHAF_ENABLE

#include <stdbool.h>
#include <stdint.h>
#include "reader_ui.h"
#include "button.h"
#include "mushaf_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

void          view_mushaf_init(bool is_cold_boot);
void          view_mushaf_save_before_sleep(void);
void          view_mushaf_enter_menu(void);
reader_view_t view_mushaf_on_wake(void);
reader_view_t view_mushaf_handle_event(reader_view_t view, button_event_t evt);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_BOARD_MUSHAF_ENABLE || BOARD_MUSHAF_ENABLE

#endif // VIEW_MUSHAF_H
