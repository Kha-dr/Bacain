#ifndef VIEW_QURAN_H
#define VIEW_QURAN_H

#include "sdkconfig.h"
#include "board_config.h"

#if CONFIG_BOARD_ALQURAN_ENABLE

#include <stdbool.h>
#include <stdint.h>
#include "reader_ui.h"
#include "power_mgr.h"
#include "button.h"
#include "quran_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

void          view_quran_init(bool is_cold_boot);
void          view_quran_save_before_sleep(void);
void          view_quran_enter_menu(void);
reader_view_t view_quran_on_wake(reader_view_t view, wakeup_reason_t wakeup);
reader_view_t view_quran_handle_event(reader_view_t view, button_event_t evt);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_BOARD_ALQURAN_ENABLE

#endif // VIEW_QURAN_H
