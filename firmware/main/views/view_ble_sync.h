#ifndef VIEW_BLE_SYNC_H
#define VIEW_BLE_SYNC_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "views.h"

#ifdef __cplusplus
extern "C"
{
#endif

  extern const view_def_t VIEW_DEF_BLE_SYNC;

  /* Main-loop hooks while VIEW_BLE_SYNC is active. */
  TickType_t view_ble_sync_get_wait_ticks(void);
  /* Polls transfer state; on timeout navigates to the menu itself and returns VIEW_MENU. */
  reader_view_t view_ble_sync_check_timeout(int64_t *last_activity_us);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_BLE_SYNC_H */