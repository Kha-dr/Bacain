/**
 * @file ble_sync.h
 * @brief NimBLE GATT Server for BacaIn Book Synchronization
 */

#ifndef BLE_SYNC_H
#define BLE_SYNC_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* BLE UUID Definitions */
#define BLE_SYNC_SERVICE_UUID_STR        "0000fe60-0000-1000-8000-00805f9b34fb"
#define BLE_SYNC_CHR_CONTROL_UUID_STR    "0000fe61-0000-1000-8000-00805f9b34fb"
#define BLE_SYNC_CHR_DATA_UUID_STR       "0000fe62-0000-1000-8000-00805f9b34fb"
#define BLE_SYNC_CHR_STATUS_UUID_STR     "0000fe63-0000-1000-8000-00805f9b34fb"

#define BLE_SYNC_DEVICE_NAME             "BacaIn"

#ifndef STORAGE_TARGET_T_DEFINED
#define STORAGE_TARGET_T_DEFINED
typedef enum {
    STORAGE_TARGET_SDCARD = 0,
    STORAGE_TARGET_FLASH  = 1,
} storage_target_t;
#endif

/* Control Characteristic OpCodes */
#define BLE_SYNC_OP_LIST_BOOKS           0x01
#define BLE_SYNC_OP_START_UPLOAD         0x02
#define BLE_SYNC_OP_FINISH_UPLOAD        0x03
#define BLE_SYNC_OP_DELETE_BOOK          0x04
#define BLE_SYNC_OP_ABORT_UPLOAD         0x05
#define BLE_SYNC_OP_SET_STORAGE_TARGET   0x06
#define BLE_SYNC_OP_GET_BOOKMARKS        0x07
#define BLE_SYNC_OP_DELETE_BOOKMARK      0x08
#define BLE_SYNC_OP_ADD_BOOKMARK         0x09
#define BLE_SYNC_OP_SET_BOOK_OFFSET      0x0A
#define BLE_CTRL_OP_GET_BOOKMARKS        0x07
#define BLE_CTRL_OP_DELETE_BOOKMARK      0x08
#define BLE_CTRL_OP_ADD_BOOKMARK         0x09
#define BLE_CTRL_OP_SET_BOOK_OFFSET      0x0A
#define BLE_SYNC_OP_SET_FONT             0x10
#define BLE_SYNC_OP_FORCE_REFRESH        0x12
#define BLE_SYNC_OP_SET_SLEEP_TIMEOUT    0x13
#define BLE_SYNC_OP_TRIGGER_SLEEP        0x14
#define BLE_SYNC_OP_SET_NAV_HINTS        0x15
#define BLE_SYNC_OP_SET_ROTATION         0x16
#define BLE_SYNC_OP_SET_BUTTON_INVERT    0x17
#define BLE_SYNC_OP_SET_STATUS_LED       0x18
#define BLE_SYNC_OP_GET_CONFIG           0x19
#define BLE_SYNC_OP_SET_REFRESH_INTERVAL 0x1A
#define BLE_SYNC_OP_GET_MUSHAF_STATE     0x20

#define BLE_SYNC_OP_SET_MUSHAF_STATE     0x21

#define BLE_SYNC_CAP_MUSHAF              (1 << 0)
#define BLE_SYNC_CAP_REFRESH_INTERVAL    (1 << 1)

/* Control Response Status Codes */
#define BLE_SYNC_STATUS_OK               0x00
#define BLE_SYNC_STATUS_ERROR            0x01
#define BLE_SYNC_STATUS_STORAGE_ERR      0x07
#define BLE_SYNC_STATUS_SEQ_ERR          0x03
#define BLE_SYNC_STATUS_OVERFLOW         0x04
#define BLE_SYNC_STATUS_WRITE_ERR        0x05
#define BLE_SYNC_STATUS_SIZE_ERR         0x06
#define BLE_SYNC_STATUS_BQM_INVALID      0x09
#define BLE_SYNC_STATUS_NOT_SUPPORTED    0x08

/**
 * @brief GET_CONFIG Control Characteristic 16-byte response payload layout (OpCode 0x19)
 */
typedef struct __attribute__((packed)) {
    uint8_t  opcode;           /*!< BLE_SYNC_OP_GET_CONFIG (0x19) */
    uint8_t  status;           /*!< BLE_SYNC_STATUS_OK (0x00) */
    uint8_t  font_id;          /*!< 0 = Large (16px), 1 = Small (12px) */
    uint8_t  nav_hints;        /*!< 0 = disabled, 1 = enabled */
    uint8_t  rotation;         /*!< Display rotation (0-3) */
    uint8_t  btn_invert;       /*!< 0 = normal, 1 = inverted */
    uint8_t  status_led;       /*!< 0 = disabled, 1 = enabled */
    uint8_t  storage_target;   /*!< 0 = SD card, 1 = Flash */
    uint16_t sleep_timeout;    /*!< Sleep timeout in seconds (little-endian) */
    uint8_t  capabilities;     /*!< Bit 0: BLE_SYNC_CAP_MUSHAF, Bit 1: BLE_SYNC_CAP_REFRESH_INTERVAL */
    uint16_t display_width;    /*!< BOARD_DISPLAY_WIDTH (little-endian) */
    uint16_t display_height;   /*!< BOARD_DISPLAY_HEIGHT (little-endian) */
    uint8_t  refresh_interval; /*!< Full refresh interval (0 = never, 1-100 pages) */
} ble_sync_config_response_t;

/**
 * @brief Status Characteristic 12-byte packed binary payload layout
 */
typedef struct __attribute__((packed)) {
    uint8_t  battery_pct;   /*!< Battery percentage (0-100) */
    uint8_t  is_charging;   /*!< 1 if charging, 0 otherwise */
    uint16_t battery_mv;    /*!< Battery voltage in millivolts (little-endian) */
    uint32_t free_kb;       /*!< MicroSD free space in kilobytes (little-endian) */
    uint32_t total_kb;      /*!< MicroSD total space in kilobytes (little-endian) */
} ble_sync_status_payload_t;

/**
 * @brief Event types emitted during BLE synchronization
 */
typedef enum {
    BLE_SYNC_EVT_ADVERTISING_STARTED,
    BLE_SYNC_EVT_CONNECTED,
    BLE_SYNC_EVT_DISCONNECTED,
    BLE_SYNC_EVT_UPLOAD_START,
    BLE_SYNC_EVT_UPLOAD_PROGRESS,
    BLE_SYNC_EVT_UPLOAD_COMPLETE,
    BLE_SYNC_EVT_UPLOAD_ABORTED,
    BLE_SYNC_EVT_FILE_DELETED,
    BLE_SYNC_EVT_REQUEST_REFRESH,
    BLE_SYNC_EVT_REQUEST_SLEEP,
} ble_sync_event_t;

typedef void (*ble_sync_event_cb_t)(ble_sync_event_t event, const void *param, void *user_data);

/**
 * @brief Initialize and start the NimBLE GATT server and advertise book sync service.
 *
 * Configures NimBLE stack, registers service 0000fe60-..., sets preferred MTU to 512,
 * and advertises as "BacaIn".
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t ble_sync_start(void);

/**
 * @brief Stop BLE synchronization.
 *
 * Stops advertising and terminates any active connection, aborting pending transfers.
 */
void ble_sync_stop(void);

/**
 * @brief Check if a BLE client is currently connected.
 *
 * @return true if connected, false otherwise.
 */
bool ble_sync_is_connected(void);

/**
 * @brief Get total bytes received during the current/latest file upload session.
 *
 * @return Number of bytes received.
 */
uint32_t ble_sync_get_bytes_received(void);

/**
 * @brief Manually send a Status Characteristic notification to the connected client.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t ble_sync_notify_status(void);

/**
 * @brief Set current storage target for book sync operations (SD card or SPI flash).
 *
 * @param target Storage target (STORAGE_TARGET_SDCARD or STORAGE_TARGET_FLASH).
 * @return ESP_OK on success, or ESP_ERR_INVALID_STATE if target media is not mounted/ready.
 */
esp_err_t ble_sync_set_storage_target(storage_target_t target);

/**
 * @brief Get current storage target.
 *
 * @return Current storage target.
 */
storage_target_t ble_sync_get_storage_target(void);

/**
 * @brief Get current sleep inactivity timeout in seconds.
 *
 * @return Inactivity timeout in seconds (0 = disabled).
 */
uint16_t ble_sync_get_sleep_timeout(void);

/**
 * @brief Set sleep inactivity timeout in seconds.
 *
 * @param sec Inactivity timeout in seconds (0 = disabled).
 */
void ble_sync_set_sleep_timeout(uint16_t sec);

/**
 * @brief Register an optional callback for BLE synchronization events.
 *
 * @param cb Callback function.
 * @param user_data User context pointer passed to callback.
 */
void ble_sync_register_callback(ble_sync_event_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif // BLE_SYNC_H
