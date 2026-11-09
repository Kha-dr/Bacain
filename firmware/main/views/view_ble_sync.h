#ifndef VIEW_BLE_SYNC_H
#define VIEW_BLE_SYNC_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "reader_ui.h"
#include "button.h"

#ifdef __cplusplus
extern "C" {
#endif

void view_ble_sync_enter(void);
void view_ble_sync_render(bool is_connected, uint32_t bytes_received, const char *status_override);
reader_view_t view_ble_sync_on_wake(void);
reader_view_t view_ble_sync_handle_event(button_event_t evt);
TickType_t view_ble_sync_get_wait_ticks(void);
reader_view_t view_ble_sync_check_timeout(int64_t *last_activity_us);

#ifdef __cplusplus
}
#endif

#endif // VIEW_BLE_SYNC_H
