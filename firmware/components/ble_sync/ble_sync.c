/**
 * @file ble_sync.c
 * @brief NimBLE GATT Server Implementation for BacaIn Book Synchronization
 */

#include "ble_sync.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_idf_version.h"
#include "esp_vfs_fat.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#if __has_include("esp_ringbuf.h")
#include "esp_ringbuf.h"
#else
#include "freertos/ringbuf.h"
#endif

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#if __has_include("esp_nimble_hci.h") && (ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0))
#include "esp_nimble_hci.h"
#endif

#include "power_mgr.h"
#include "sdcard.h"
#include "book_library.h"
#include "app_settings.h"
#include "status_led.h"
#include "board_config.h"
#if CONFIG_BOARD_MUSHAF_ENABLE || BOARD_MUSHAF_ENABLE
#include "mushaf_reader.h"
#endif

_Static_assert(sizeof(ble_sync_config_response_t) == 16, "GET_CONFIG payload");
_Static_assert(sizeof(ble_sync_status_payload_t) == 12, "status payload");

static const char *TAG = "ble_sync";

#define RINGBUF_SIZE (64 * 1024)
#define CMD_QUEUE_LEN 16
#define WORKER_TASK_STACK_SIZE 8192

/* 128-bit Service and Characteristic UUIDs (stored little-endian in NimBLE) */
/* Service: 0000fe60-0000-1000-8000-00805f9b34fb */
static const ble_uuid128_t s_svc_uuid = {
    .u = {.type = BLE_UUID_TYPE_128},
    .value = {0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
              0x00, 0x10, 0x00, 0x00, 0x60, 0xfe, 0x00, 0x00}};

/* Control Characteristic: 0000fe61-0000-1000-8000-00805f9b34fb */
static const ble_uuid128_t s_chr_control_uuid = {
    .u = {.type = BLE_UUID_TYPE_128},
    .value = {0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
              0x00, 0x10, 0x00, 0x00, 0x61, 0xfe, 0x00, 0x00}};

/* Data Characteristic: 0000fe62-0000-1000-8000-00805f9b34fb */
static const ble_uuid128_t s_chr_data_uuid = {
    .u = {.type = BLE_UUID_TYPE_128},
    .value = {0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
              0x00, 0x10, 0x00, 0x00, 0x62, 0xfe, 0x00, 0x00}};

/* Status Characteristic: 0000fe63-0000-1000-8000-00805f9b34fb */
static const ble_uuid128_t s_chr_status_uuid = {
    .u = {.type = BLE_UUID_TYPE_128},
    .value = {0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
              0x00, 0x10, 0x00, 0x00, 0x63, 0xfe, 0x00, 0x00}};

/* Characteristic Value Handles */
static uint16_t s_control_val_handle;
static uint16_t s_data_val_handle;
static uint16_t s_status_val_handle;

/* BLE Connection State */
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool s_is_connected = false;
static bool s_sync_running = false;
static uint16_t s_current_mtu = 23;

/* Storage Target State */
static storage_target_t s_storage_target = STORAGE_TARGET_FLASH;

/* File Upload State */
static FILE *s_upload_fp = NULL;
static bool s_is_uploading = false;
static bool s_upload_error = false;
static uint8_t s_upload_err_status = BLE_SYNC_STATUS_OK;
static uint32_t s_bytes_received = 0;
static uint32_t s_expected_file_size = 0;
static uint16_t s_expected_seq = 0;
static char s_upload_filename[128] = {0};

/* Transfer snapshot for the UI (written by worker/host task, read by UI task) */
static portMUX_TYPE s_xfer_mux = portMUX_INITIALIZER_UNLOCKED;
static ble_sync_xfer_state_t s_xfer_state = BLE_SYNC_XFER_IDLE;
static uint8_t s_xfer_status = BLE_SYNC_STATUS_OK;
static uint32_t s_xfer_total = 0;
static uint32_t s_xfer_final_bytes = 0;
static char s_xfer_name[64] = {0};

static void xfer_begin(const char *name, uint32_t total)
{
    taskENTER_CRITICAL(&s_xfer_mux);
    strncpy(s_xfer_name, name ? name : "", sizeof(s_xfer_name) - 1);
    s_xfer_name[sizeof(s_xfer_name) - 1] = '\0';
    s_xfer_total = total;
    s_xfer_final_bytes = 0;
    s_xfer_status = BLE_SYNC_STATUS_OK;
    s_xfer_state = BLE_SYNC_XFER_TRANSFERRING;
    taskEXIT_CRITICAL(&s_xfer_mux);
}

static void xfer_end(ble_sync_xfer_state_t st, uint8_t status, uint32_t bytes)
{
    taskENTER_CRITICAL(&s_xfer_mux);
    s_xfer_state = st;
    s_xfer_status = status;
    s_xfer_final_bytes = bytes;
    taskEXIT_CRITICAL(&s_xfer_mux);
}

static void xfer_reset(void)
{
    taskENTER_CRITICAL(&s_xfer_mux);
    s_xfer_state = BLE_SYNC_XFER_IDLE;
    s_xfer_status = BLE_SYNC_STATUS_OK;
    s_xfer_total = 0;
    s_xfer_final_bytes = 0;
    s_xfer_name[0] = '\0';
    taskEXIT_CRITICAL(&s_xfer_mux);
}

/* Ringbuffer & Worker Task */
static RingbufHandle_t s_data_ringbuf = NULL;
static QueueHandle_t s_cmd_queue = NULL;
static TaskHandle_t s_worker_task_hdl = NULL;

typedef enum
{
    CMD_LIST_BOOKS,
    CMD_START_UPLOAD,
    CMD_FINISH_UPLOAD,
    CMD_DELETE_BOOK,
    CMD_ABORT_UPLOAD,
    CMD_SET_STORAGE_TARGET,
    CMD_SET_FONT,
    CMD_FORCE_REFRESH,
    CMD_SET_SLEEP_TIMEOUT,
    CMD_SET_NAV_HINTS,
    CMD_SET_ROTATION,
    CMD_SET_BUTTON_INVERT,
    CMD_GET_BOOKMARKS,
    CMD_SET_REFRESH_INTERVAL,
    CMD_DELETE_BOOKMARK,
    CMD_ADD_BOOKMARK,
    CMD_SET_BOOK_OFFSET,
    CMD_SET_STATUS_LED,
    CMD_SET_MUSHAF_STATE,
} ble_sync_cmd_type_t;

typedef struct
{
    ble_sync_cmd_type_t type;
    char filename[128];
    uint32_t file_size;
    uint32_t offset;
    uint8_t storage_target;
    uint16_t param16;
    uint8_t param8;
    uint8_t raw[40]; /* opcode payload without the opcode byte (SET_MUSHAF_STATE: max 34) */
    uint8_t raw_len;
} ble_sync_cmd_msg_t;

/* Optional Event Callback */
static ble_sync_event_cb_t s_event_cb = NULL;
static void *s_event_cb_arg = NULL;

/* Forward Declarations */
static void ble_sync_advertise(void);
static int ble_sync_gap_event_cb(struct ble_gap_event *event, void *arg);
static int ble_sync_control_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                      struct ble_gatt_access_ctxt *ctxt, void *arg);
static int ble_sync_data_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg);
static int ble_sync_status_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                     struct ble_gatt_access_ctxt *ctxt, void *arg);
static esp_err_t ble_sync_notify(uint16_t val_handle, const void *data, uint16_t len);

/* Queue a command for the worker. On failure log it and answer the client,
 * so it never waits for a reply that will not come (RULES.md 2.4). */
static void enqueue_cmd(const ble_sync_cmd_msg_t *cmd, uint8_t opcode)
{
    if (s_cmd_queue == NULL || xQueueSend(s_cmd_queue, cmd, pdMS_TO_TICKS(50)) != pdTRUE)
    {
        ESP_LOGE(TAG, "Command queue full, opcode 0x%02x dropped", opcode);
        uint8_t resp[2] = {opcode, BLE_SYNC_STATUS_ERROR};
        ble_sync_notify(s_control_val_handle, resp, sizeof(resp));
    }
}

/* Two-byte reply [opcode, status] from an esp_err_t. */
static void reply_status(uint8_t opcode, esp_err_t err)
{
    uint8_t resp[2] = {opcode, (err == ESP_OK) ? BLE_SYNC_STATUS_OK : BLE_SYNC_STATUS_ERROR};
    ble_sync_notify(s_control_val_handle, resp, sizeof(resp));
}

/* GATT Service Definitions */
static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                /* Control Characteristic (0xFE61): Write, Notify */
                .uuid = &s_chr_control_uuid.u,
                .access_cb = ble_sync_control_access_cb,
                .val_handle = &s_control_val_handle,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                /* Data Characteristic (0xFE62): Write, Write Without Response */
                .uuid = &s_chr_data_uuid.u,
                .access_cb = ble_sync_data_access_cb,
                .val_handle = &s_data_val_handle,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                /* Status Characteristic (0xFE63): Read, Notify */
                .uuid = &s_chr_status_uuid.u,
                .access_cb = ble_sync_status_access_cb,
                .val_handle = &s_status_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                0, /* End of characteristics */
            }},
    },
    {
        0, /* End of services */
    },
};

/* --- Helpers --- */

static void sanitize_filename(const char *in, char *out, size_t max_len)
{
    if (!in || !out || max_len == 0)
        return;

    // Strip leading path components
    const char *p = strrchr(in, '/');
    if (p)
        in = p + 1;
    p = strrchr(in, '\\');
    if (p)
        in = p + 1;

    // Strip leading dots to prevent hidden or traversal files
    while (*in == '.')
        in++;

    size_t i = 0;
    while (*in && i < max_len - 1)
    {
        char c = *in++;
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|' || (unsigned char)c < 32)
        {
            c = '_';
        }
        out[i++] = c;
    }
    out[i] = '\0';

    if (i == 0)
    {
        strncpy(out, "unnamed.txt", max_len - 1);
        out[max_len - 1] = '\0';
    }
}

static const char *ble_sync_get_books_dir(uint8_t *out_status)
{
    if (s_storage_target == STORAGE_TARGET_FLASH)
    {
        if (!flash_storage_is_mounted())
        {
            esp_err_t err = flash_storage_init();
            if (err != ESP_OK || !flash_storage_is_mounted())
            {
                ESP_LOGE(TAG, "SPI Flash storage is not mounted and init failed (err 0x%x)", err);
                if (out_status)
                    *out_status = BLE_SYNC_STATUS_STORAGE_ERR;
                return NULL;
            }
        }
        if (out_status)
            *out_status = BLE_SYNC_STATUS_OK;
        return "/spiflash/books";
    }
    else
    { // STORAGE_TARGET_SDCARD
        if (!sdcard_is_mounted())
        {
            ESP_LOGE(TAG, "SD card is not mounted");
            if (out_status)
                *out_status = BLE_SYNC_STATUS_STORAGE_ERR;
            return NULL;
        }
        if (out_status)
            *out_status = BLE_SYNC_STATUS_OK;
        return "/sdcard/books";
    }
}

static void ensure_books_dir(const char *dir_path)
{
    if (!dir_path)
        return;
    struct stat st;
    if (stat(dir_path, &st) != 0)
    {
        mkdir(dir_path, 0755);
    }
}

static void ble_sync_get_status_payload(ble_sync_status_payload_t *payload)
{
    memset(payload, 0, sizeof(*payload));
    payload->battery_pct = power_mgr_get_battery_percentage();
    uint32_t mv = power_mgr_get_battery_voltage_mv();
    payload->battery_mv = (uint16_t)(mv > 0xFFFF ? 0xFFFF : mv);
    payload->is_charging = power_mgr_is_charging() ? 1 : 0;

    uint64_t free_bytes = 0;
    uint64_t total_bytes = 0;

    if (s_storage_target == STORAGE_TARGET_FLASH)
    {
        if (!flash_storage_is_mounted())
        {
            flash_storage_init();
        }
        if (flash_storage_is_mounted())
        {
            free_bytes = flash_storage_get_free_bytes();
            total_bytes = flash_storage_get_total_bytes();
        }
    }
    else
    {
        if (sdcard_is_mounted())
        {
            free_bytes = sdcard_get_free_bytes();
            total_bytes = sdcard_get_total_bytes();
        }
    }

    payload->free_kb = (uint32_t)(free_bytes / 1024ULL);
    payload->total_kb = (uint32_t)(total_bytes / 1024ULL);
}

static esp_err_t ble_sync_notify(uint16_t val_handle, const void *data, uint16_t len)
{
    if (!s_is_connected || s_conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        return ESP_ERR_INVALID_STATE;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om)
    {
        ESP_LOGE(TAG, "Failed to allocate os_mbuf for notification");
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, val_handle, om);
    if (rc != 0)
    {
        if (rc == BLE_HS_EDISABLED)
        {
            ESP_LOGD(TAG, "Notifications disabled by client for handle %u", val_handle);
        }
        else
        {
            ESP_LOGW(TAG, "ble_gatts_notify_custom failed: rc=%d", rc);
        }
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void sanitize_rel_path(const char *in, char *out, size_t max_len)
{
    if (!in || !out || max_len == 0)
        return;

    while (*in == '/' || *in == '\\' || *in == '.')
    {
        in++;
    }

    size_t i = 0;
    bool seen_slash = false;
    while (*in && i < max_len - 1)
    {
        char c = *in++;
        if (c == '\\')
            c = '/';
        if (c == '/')
        {
            if (seen_slash || i == 0 || *in == '.' || *in == '/' || *in == '\0')
            {
                c = '_';
            }
            else
            {
                seen_slash = true;
            }
        }
        else if (c == ':' || c == '*' || c == '?' ||
                 c == '"' || c == '<' || c == '>' || c == '|' || (unsigned char)c < 32)
        {
            c = '_';
        }
        out[i++] = c;
    }
    out[i] = '\0';
}

static void send_list_book_entry(const char *name, uint32_t size, uint8_t is_last)
{
    uint8_t resp[256];
    resp[0] = BLE_SYNC_OP_LIST_BOOKS;
    resp[1] = BLE_SYNC_STATUS_OK;
    resp[2] = is_last;
    resp[3] = (uint8_t)(size & 0xFF);
    resp[4] = (uint8_t)((size >> 8) & 0xFF);
    resp[5] = (uint8_t)((size >> 16) & 0xFF);
    resp[6] = (uint8_t)((size >> 24) & 0xFF);

    size_t name_len = strlen(name);
    if (name_len > sizeof(resp) - 14)
    {
        name_len = sizeof(resp) - 14;
    }
    memcpy(&resp[7], name, name_len);
    resp[7 + name_len] = '\0';

    size_t total_len = 7 + name_len + 1;
    if (name_len > 0 && name[name_len - 1] != '/')
    {
        size_t book_offset = 0;
        size_t total_bytes = 0;
        size_t bm_count = 0;
        if (book_library_get_book_state(name, &book_offset, &total_bytes, NULL, &bm_count) != ESP_OK)
        {
            book_offset = 0;
            bm_count = 0;
        }
        uint32_t le_offset = (uint32_t)book_offset;
        size_t meta_pos = 7 + name_len + 1;
        resp[meta_pos + 0] = (uint8_t)(le_offset & 0xFF);
        resp[meta_pos + 1] = (uint8_t)((le_offset >> 8) & 0xFF);
        resp[meta_pos + 2] = (uint8_t)((le_offset >> 16) & 0xFF);
        resp[meta_pos + 3] = (uint8_t)((le_offset >> 24) & 0xFF);
        resp[meta_pos + 4] = (uint8_t)(bm_count > 0xFF ? 0xFF : bm_count);
        total_len += 5;
    }

    ble_sync_notify(s_control_val_handle, resp, (uint16_t)total_len);
    vTaskDelay(pdMS_TO_TICKS(15));
}

/* --- Worker Command Handlers --- */

static void handle_cmd_list_books(void)
{
    uint8_t storage_err = BLE_SYNC_STATUS_OK;
    const char *books_dir = ble_sync_get_books_dir(&storage_err);
    if (!books_dir || storage_err != BLE_SYNC_STATUS_OK)
    {
        ESP_LOGE(TAG, "Storage target not available for listing books");
        uint8_t err_resp[8] = {BLE_SYNC_OP_LIST_BOOKS, BLE_SYNC_STATUS_STORAGE_ERR, 1, 0, 0, 0, 0, 0};
        ble_sync_notify(s_control_val_handle, err_resp, sizeof(err_resp));
        return;
    }

    ESP_LOGI(TAG, "Listing books in %s", books_dir);
    ensure_books_dir(books_dir);

    DIR *dir = opendir(books_dir);
    if (!dir)
    {
        ESP_LOGE(TAG, "Failed to open directory %s", books_dir);
        uint8_t err_resp[8] = {BLE_SYNC_OP_LIST_BOOKS, BLE_SYNC_STATUS_ERROR, 1, 0, 0, 0, 0, 0};
        ble_sync_notify(s_control_val_handle, err_resp, sizeof(err_resp));
        return;
    }

    char pending_name[256];
    uint32_t pending_size = 0;
    bool has_pending = false;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (!s_is_connected)
        {
            break;
        }

        if (entry->d_name[0] == '.')
            continue; // skip hidden files

        size_t nlen = strlen(entry->d_name);
        if (nlen < 4)
            continue;
        if (strcasecmp(entry->d_name + nlen - 4, ".txt") != 0)
            continue;

        char fpath[512];
        snprintf(fpath, sizeof(fpath), "%s/%s", books_dir, entry->d_name);

        struct stat st;
        if (stat(fpath, &st) == 0 && S_ISREG(st.st_mode))
        {
            if (has_pending)
            {
                send_list_book_entry(pending_name, pending_size, 0);
            }
            strncpy(pending_name, entry->d_name, sizeof(pending_name) - 1);
            pending_name[sizeof(pending_name) - 1] = '\0';
            pending_size = (uint32_t)st.st_size;
            has_pending = true;
        }
    }
    closedir(dir);

    if (has_pending)
    {
        send_list_book_entry(pending_name, pending_size, 1);
    }
    else
    {
        // Empty list notification
        uint8_t empty_resp[8] = {BLE_SYNC_OP_LIST_BOOKS, BLE_SYNC_STATUS_OK, 1, 0, 0, 0, 0, 0};
        ble_sync_notify(s_control_val_handle, empty_resp, sizeof(empty_resp));
    }
}

static void handle_cmd_start_upload(const ble_sync_cmd_msg_t *cmd)
{
    ESP_LOGI(TAG, "Start upload: name='%s', size=%u", cmd->filename, (unsigned)cmd->file_size);

    if (s_upload_fp != NULL)
    {
        fclose(s_upload_fp);
        s_upload_fp = NULL;
    }

    // Flush leftover data in ringbuffer if any
    if (s_data_ringbuf != NULL)
    {
        size_t dummy_sz = 0;
        void *dummy = NULL;
        while ((dummy = xRingbufferReceiveUpTo(s_data_ringbuf, &dummy_sz, 0, 4096)) != NULL)
        {
            vRingbufferReturnItem(s_data_ringbuf, dummy);
        }
    }

    uint8_t storage_err = BLE_SYNC_STATUS_OK;
    const char *books_dir = ble_sync_get_books_dir(&storage_err);
    if (!books_dir || storage_err != BLE_SYNC_STATUS_OK)
    {
        ESP_LOGE(TAG, "Target storage not available for start upload");
        s_is_uploading = false;
        xfer_begin(cmd->filename, cmd->file_size);
        xfer_end(BLE_SYNC_XFER_FAILED, BLE_SYNC_STATUS_STORAGE_ERR, 0);
        uint8_t resp[2] = {BLE_SYNC_OP_START_UPLOAD, BLE_SYNC_STATUS_STORAGE_ERR};
        ble_sync_notify(s_control_val_handle, resp, sizeof(resp));
        return;
    }

    sanitize_filename(cmd->filename, s_upload_filename, sizeof(s_upload_filename));
    s_expected_file_size = cmd->file_size;
    s_upload_err_status = BLE_SYNC_STATUS_OK;
    s_bytes_received = 0;
    s_expected_seq = 0;
    s_upload_error = false;

    ensure_books_dir(books_dir);

    char fpath[256];
    snprintf(fpath, sizeof(fpath), "%s/%s", books_dir, s_upload_filename);

    unlink(fpath);

    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    const char *mount_path = (s_storage_target == STORAGE_TARGET_SDCARD) ? "/sdcard" : "/spiflash";
    esp_err_t info_err = esp_vfs_fat_info(mount_path, &total_bytes, &free_bytes);
    if (info_err != ESP_OK)
    {
        /* Free-space check is skipped; the write itself still reports WRITE_ERR if full. */
        ESP_LOGE(TAG, "esp_vfs_fat_info(%s) failed: %s", mount_path, esp_err_to_name(info_err));
    }
    if (cmd->file_size > 0 && free_bytes > 0 && ((uint64_t)cmd->file_size + 16384ULL) > free_bytes)
    {
        ESP_LOGE(TAG, "Upload rejected: file size %u B exceeds free space %llu B",
                 (unsigned)cmd->file_size, (unsigned long long)free_bytes);
        s_is_uploading = false;
        xfer_begin(s_upload_filename, cmd->file_size);
        xfer_end(BLE_SYNC_XFER_FAILED, BLE_SYNC_STATUS_SIZE_ERR, 0);
        uint8_t resp[2] = {BLE_SYNC_OP_START_UPLOAD, BLE_SYNC_STATUS_SIZE_ERR};
        ble_sync_notify(s_control_val_handle, resp, sizeof(resp));
        return;
    }

    s_upload_fp = fopen(fpath, "wb");
    uint8_t status = BLE_SYNC_STATUS_OK;
    if (!s_upload_fp)
    {
        ESP_LOGE(TAG, "Failed to open '%s' for write", fpath);
        status = BLE_SYNC_STATUS_ERROR;
        s_is_uploading = false;
        xfer_begin(s_upload_filename, cmd->file_size);
        xfer_end(BLE_SYNC_XFER_FAILED, status, 0);
    }
    else
    {
        s_is_uploading = true;
        xfer_begin(s_upload_filename, cmd->file_size);
        ESP_LOGI(TAG, "Upload target file opened: %s", fpath);
    }

    uint8_t resp[2] = {BLE_SYNC_OP_START_UPLOAD, status};
    ble_sync_notify(s_control_val_handle, resp, sizeof(resp));

    if (status == BLE_SYNC_STATUS_OK && s_event_cb)
    {
        s_event_cb(BLE_SYNC_EVT_UPLOAD_START, s_upload_filename, s_event_cb_arg);
    }
}

static void handle_cmd_finish_upload(void)
{
    ESP_LOGI(TAG, "Finish upload requested");

    // Drain all remaining data from the ringbuffer into the file
    if (s_data_ringbuf != NULL && s_upload_fp != NULL)
    {
        size_t item_sz = 0;
        uint8_t *item = NULL;
        while ((item = (uint8_t *)xRingbufferReceiveUpTo(s_data_ringbuf, &item_sz, 0, 4096)) != NULL)
        {
            if (fwrite(item, 1, item_sz, s_upload_fp) != item_sz)
            {
                ESP_LOGE(TAG, "fwrite failed while draining ringbuffer");
                if (s_upload_err_status == BLE_SYNC_STATUS_OK)
                    s_upload_err_status = BLE_SYNC_STATUS_WRITE_ERR;
                s_upload_error = true;
            }
            vRingbufferReturnItem(s_data_ringbuf, item);
        }
    }

    if (s_upload_fp != NULL)
    {
        fflush(s_upload_fp);
        fsync(fileno(s_upload_fp));
        fclose(s_upload_fp);
        s_upload_fp = NULL;
    }

    s_is_uploading = false;

    if (s_expected_file_size > 0 && s_bytes_received != s_expected_file_size)
    {
        ESP_LOGE(TAG, "Upload size mismatch: got %u, expected %u",
                 (unsigned)s_bytes_received, (unsigned)s_expected_file_size);
        if (s_upload_err_status == BLE_SYNC_STATUS_OK)
            s_upload_err_status = BLE_SYNC_STATUS_SIZE_ERR;
        s_upload_error = true;
    }

    uint8_t storage_err = BLE_SYNC_STATUS_OK;
    const char *books_dir = ble_sync_get_books_dir(&storage_err);

    uint8_t status = s_upload_error ? (s_upload_err_status != BLE_SYNC_STATUS_OK ? s_upload_err_status : BLE_SYNC_STATUS_ERROR) : BLE_SYNC_STATUS_OK;
    if (storage_err != BLE_SYNC_STATUS_OK)
    {
        status = BLE_SYNC_STATUS_STORAGE_ERR;
    }

    if (s_upload_error && s_upload_filename[0] != '\0' && books_dir != NULL)
    {
        char fpath[256];
        snprintf(fpath, sizeof(fpath), "%s/%s", books_dir, s_upload_filename);
        unlink(fpath);
        ESP_LOGW(TAG, "Deleted corrupted/incomplete file on error: %s", fpath);
    }

#if CONFIG_BOARD_MUSHAF_ENABLE || BOARD_MUSHAF_ENABLE
    if (!s_upload_error && strcasecmp(s_upload_filename, "mushaf.bqm") == 0)
    {
        /* Runs on the worker task; the main task shows the sync screen during transfers. */
        mushaf_reader_init(mushaf_reader_get_state());
        if (!mushaf_reader_is_available())
        {
            ESP_LOGE(TAG, "Uploaded %s failed BQM header validation", s_upload_filename);
            if (s_upload_err_status == BLE_SYNC_STATUS_OK)
                s_upload_err_status = BLE_SYNC_STATUS_BQM_INVALID;
            s_upload_error = true;
            if (books_dir != NULL)
            {
                char fpath[256];
                snprintf(fpath, sizeof(fpath), "%s/%s", books_dir, s_upload_filename);
                unlink(fpath);
                ESP_LOGW(TAG, "Deleted corrupted/incomplete file on error: %s", fpath);
            }
        }
    }
#endif

    status = s_upload_error ? (s_upload_err_status != BLE_SYNC_STATUS_OK ? s_upload_err_status : BLE_SYNC_STATUS_ERROR) : BLE_SYNC_STATUS_OK;
    if (storage_err != BLE_SYNC_STATUS_OK)
    {
        status = BLE_SYNC_STATUS_STORAGE_ERR;
    }
    xfer_end(status == BLE_SYNC_STATUS_OK ? BLE_SYNC_XFER_COMPLETE : BLE_SYNC_XFER_FAILED,
             status, s_bytes_received);

    uint8_t resp[6] = {
        BLE_SYNC_OP_FINISH_UPLOAD,
        status,
        (uint8_t)(s_bytes_received & 0xFF),
        (uint8_t)((s_bytes_received >> 8) & 0xFF),
        (uint8_t)((s_bytes_received >> 16) & 0xFF),
        (uint8_t)((s_bytes_received >> 24) & 0xFF)};
    ble_sync_notify(s_control_val_handle, resp, sizeof(resp));

    ESP_LOGI(TAG, "Upload finished: %u bytes received, status=%d",
             (unsigned)s_bytes_received, status);

    if (s_event_cb)
    {
        s_event_cb(BLE_SYNC_EVT_UPLOAD_COMPLETE, s_upload_filename, s_event_cb_arg);
    }
}

static void handle_cmd_delete_book(const ble_sync_cmd_msg_t *cmd)
{
    ESP_LOGI(TAG, "Delete book requested: '%s'", cmd->filename);

    char safe_name[128];
    sanitize_filename(cmd->filename, safe_name, sizeof(safe_name));

    if (s_upload_fp && strcmp(s_upload_filename, safe_name) == 0)
    {
        fclose(s_upload_fp);
        s_upload_fp = NULL;
        s_is_uploading = false;
        xfer_end(BLE_SYNC_XFER_ABORTED, BLE_SYNC_STATUS_OK, s_bytes_received);
    }

    uint8_t storage_err = BLE_SYNC_STATUS_OK;
    const char *books_dir = ble_sync_get_books_dir(&storage_err);
    if (!books_dir || storage_err != BLE_SYNC_STATUS_OK)
    {
        ESP_LOGE(TAG, "Target storage not available for delete book");
        uint8_t resp[2] = {BLE_SYNC_OP_DELETE_BOOK, BLE_SYNC_STATUS_STORAGE_ERR};
        ble_sync_notify(s_control_val_handle, resp, sizeof(resp));
        return;
    }

    char fpath[256];
    snprintf(fpath, sizeof(fpath), "%s/%s", books_dir, safe_name);

    int rc = unlink(fpath);
    uint8_t status = (rc == 0) ? BLE_SYNC_STATUS_OK : BLE_SYNC_STATUS_ERROR;

    if (rc == 0)
    {
        ESP_LOGI(TAG, "Deleted file: %s", fpath);
        book_library_delete_book_state(safe_name);
    }
    else
    {
        ESP_LOGW(TAG, "Failed to delete file: %s", fpath);
    }

    uint8_t resp[2] = {BLE_SYNC_OP_DELETE_BOOK, status};
    ble_sync_notify(s_control_val_handle, resp, sizeof(resp));

    if (rc == 0 && s_event_cb)
    {
        s_event_cb(BLE_SYNC_EVT_FILE_DELETED, safe_name, s_event_cb_arg);
    }
}

static void handle_cmd_abort_upload(void)
{
    ESP_LOGW(TAG, "Aborting file upload");
    // Client sends ABORT after a failed FINISH; keep the FAILED result visible.
    taskENTER_CRITICAL(&s_xfer_mux);
    bool was_transferring = (s_xfer_state == BLE_SYNC_XFER_TRANSFERRING);
    taskEXIT_CRITICAL(&s_xfer_mux);
    if (was_transferring)
    {
        xfer_end(BLE_SYNC_XFER_ABORTED, BLE_SYNC_STATUS_OK, s_bytes_received);
    }
    if (s_upload_fp != NULL)
    {
        fclose(s_upload_fp);
        s_upload_fp = NULL;
    }
    s_is_uploading = false;

    uint8_t storage_err = BLE_SYNC_STATUS_OK;
    const char *books_dir = ble_sync_get_books_dir(&storage_err);

    if (s_upload_filename[0] != '\0' && books_dir != NULL)
    {
        char fpath[256];
        snprintf(fpath, sizeof(fpath), "%s/%s", books_dir, s_upload_filename);
        unlink(fpath);
        ESP_LOGW(TAG, "Deleted partial file on abort: %s", fpath);
        s_upload_filename[0] = '\0';
    }

    if (s_data_ringbuf != NULL)
    {
        size_t dummy_sz = 0;
        void *dummy = NULL;
        while ((dummy = xRingbufferReceiveUpTo(s_data_ringbuf, &dummy_sz, 0, 4096)) != NULL)
        {
            vRingbufferReturnItem(s_data_ringbuf, dummy);
        }
    }

    s_bytes_received = 0;
    s_expected_file_size = 0;
    s_expected_seq = 0;
    s_upload_error = false;

    s_upload_err_status = BLE_SYNC_STATUS_OK;
    uint8_t resp[2] = {BLE_SYNC_OP_ABORT_UPLOAD, BLE_SYNC_STATUS_OK};
    ble_sync_notify(s_control_val_handle, resp, sizeof(resp));

    if (s_event_cb)
    {
        s_event_cb(BLE_SYNC_EVT_UPLOAD_ABORTED, NULL, s_event_cb_arg);
    }
}

static void handle_cmd_set_storage_target(const ble_sync_cmd_msg_t *cmd)
{
    ESP_LOGI(TAG, "Set storage target requested: %u", cmd->storage_target);
    uint8_t status = BLE_SYNC_STATUS_OK;

    if (cmd->storage_target != STORAGE_TARGET_SDCARD && cmd->storage_target != STORAGE_TARGET_FLASH)
    {
        ESP_LOGE(TAG, "Invalid storage target opcode payload: %u", cmd->storage_target);
        status = BLE_SYNC_STATUS_STORAGE_ERR;
    }
    else
    {
        esp_err_t err = ble_sync_set_storage_target((storage_target_t)cmd->storage_target);
        if (err != ESP_OK)
        {
            status = BLE_SYNC_STATUS_STORAGE_ERR;
        }
    }

    uint8_t resp[2] = {BLE_SYNC_OP_SET_STORAGE_TARGET, status};
    ble_sync_notify(s_control_val_handle, resp, sizeof(resp));

    if (status == BLE_SYNC_STATUS_OK)
    {
        ble_sync_notify_status();
    }
}

static void ble_sync_request_refresh(void)
{
    if (s_event_cb)
    {
        s_event_cb(BLE_SYNC_EVT_REQUEST_REFRESH, NULL, s_event_cb_arg);
    }
}

static void handle_cmd_force_refresh(void)
{
    ESP_LOGI(TAG, "Force display refresh requested");
    reply_status(BLE_SYNC_OP_FORCE_REFRESH, ESP_OK);
    ble_sync_request_refresh();
}

/* Settings: saved by app_settings on this (worker) task, applied by the main task
 * before it handles the next event (app_settings_take_pending_apply). */

static void handle_cmd_set_refresh_interval(uint8_t interval)
{
    ESP_LOGI(TAG, "Setting full refresh interval: %u", (unsigned)interval);
    reply_status(BLE_SYNC_OP_SET_REFRESH_INTERVAL, app_settings_set_full_refresh_interval(interval));
}
static void handle_cmd_set_status_led(uint8_t enabled)
{
    ESP_LOGI(TAG, "Setting status LED: %u", (unsigned)enabled);
    reply_status(BLE_SYNC_OP_SET_STATUS_LED, app_settings_set_status_led(enabled != 0));
}

static void handle_cmd_set_font(uint8_t font_id)
{
    ESP_LOGI(TAG, "Setting font: %u", (unsigned)font_id);
    /* PWA font id: 0 = 16 px (large), 1 = 12 px (small) */
    esp_err_t err = app_settings_set_font_size(font_id == 0 ? APP_FONT_LARGE : APP_FONT_SMALL);
    reply_status(BLE_SYNC_OP_SET_FONT, err);
    if (err == ESP_OK)
    {
        ble_sync_request_refresh();
    }
}

static void handle_cmd_set_sleep_timeout(uint16_t timeout_sec)
{
    ESP_LOGI(TAG, "Setting sleep timeout: %u sec", (unsigned)timeout_sec);
    reply_status(BLE_SYNC_OP_SET_SLEEP_TIMEOUT, app_settings_set_sleep_timeout(timeout_sec));
}

static void handle_cmd_set_nav_hints(uint8_t show)
{
    ESP_LOGI(TAG, "Setting nav hints: %u", (unsigned)show);
    esp_err_t err = app_settings_set_nav_hints(show != 0);
    reply_status(BLE_SYNC_OP_SET_NAV_HINTS, err);
    if (err == ESP_OK)
    {
        ble_sync_request_refresh();
    }
}

static void handle_cmd_set_rotation(uint8_t rot)
{
    ESP_LOGI(TAG, "Setting rotation: %u", (unsigned)rot);
    esp_err_t err = app_settings_set_rotation(rot);
    reply_status(BLE_SYNC_OP_SET_ROTATION, err);
    if (err == ESP_OK)
    {
        ble_sync_request_refresh();
    }
}

static void handle_cmd_set_button_invert(uint8_t inverted)
{
    ESP_LOGI(TAG, "Setting button invert: %u", (unsigned)inverted);
    esp_err_t err = app_settings_set_btn_invert(inverted != 0);
    reply_status(BLE_SYNC_OP_SET_BUTTON_INVERT, err);
    if (err == ESP_OK)
    {
        ble_sync_request_refresh(); /* wakes the main task so the change is applied */
    }
}

static void handle_cmd_get_bookmarks(const ble_sync_cmd_msg_t *cmd)
{
    char rel_path[128] = {0};
    sanitize_rel_path(cmd->filename, rel_path, sizeof(rel_path));
    size_t byte_offset = 0;
    size_t total_bytes = 0;
    size_t bookmarks[BOOK_MAX_BOOKMARKS] = {0};
    size_t bm_count = 0;
    esp_err_t err = book_library_get_book_state(rel_path, &byte_offset, &total_bytes, bookmarks, &bm_count);
    if (bm_count > BOOK_MAX_BOOKMARKS)
    {
        bm_count = BOOK_MAX_BOOKMARKS;
    }
    uint8_t resp[11 + BOOK_MAX_BOOKMARKS * 4];
    memset(resp, 0, sizeof(resp));
    resp[0] = BLE_CTRL_OP_GET_BOOKMARKS;
    resp[1] = (err == ESP_OK) ? BLE_SYNC_STATUS_OK : BLE_SYNC_STATUS_ERROR;
    uint32_t off_u32 = (uint32_t)byte_offset;
    uint32_t tot_u32 = (uint32_t)total_bytes;
    resp[2] = (uint8_t)(off_u32 & 0xFF);
    resp[3] = (uint8_t)((off_u32 >> 8) & 0xFF);
    resp[4] = (uint8_t)((off_u32 >> 16) & 0xFF);
    resp[5] = (uint8_t)((off_u32 >> 24) & 0xFF);
    resp[6] = (uint8_t)(tot_u32 & 0xFF);
    resp[7] = (uint8_t)((tot_u32 >> 8) & 0xFF);
    resp[8] = (uint8_t)((tot_u32 >> 16) & 0xFF);
    resp[9] = (uint8_t)((tot_u32 >> 24) & 0xFF);
    resp[10] = (uint8_t)bm_count;
    for (size_t i = 0; i < bm_count; i++)
    {
        uint32_t bm = (uint32_t)bookmarks[i];
        resp[11 + i * 4 + 0] = (uint8_t)(bm & 0xFF);
        resp[11 + i * 4 + 1] = (uint8_t)((bm >> 8) & 0xFF);
        resp[11 + i * 4 + 2] = (uint8_t)((bm >> 16) & 0xFF);
        resp[11 + i * 4 + 3] = (uint8_t)((bm >> 24) & 0xFF);
    }
    ble_sync_notify(s_control_val_handle, resp, (uint16_t)(11 + bm_count * 4));
}

static void handle_cmd_delete_bookmark(const ble_sync_cmd_msg_t *cmd)
{
    char rel_path[128] = {0};
    sanitize_rel_path(cmd->filename, rel_path, sizeof(rel_path));
    esp_err_t err = book_library_remove_book_bookmark(rel_path, (size_t)cmd->offset);
    reply_status(BLE_CTRL_OP_DELETE_BOOKMARK, err);
}

static void handle_cmd_add_bookmark(const ble_sync_cmd_msg_t *cmd)
{
    char rel_path[128] = {0};
    sanitize_rel_path(cmd->filename, rel_path, sizeof(rel_path));
    esp_err_t err = book_library_add_book_bookmark(rel_path, (size_t)cmd->offset);
    reply_status(BLE_CTRL_OP_ADD_BOOKMARK, err);
}

static void handle_cmd_set_book_offset(const ble_sync_cmd_msg_t *cmd)
{
    char rel_path[128] = {0};
    sanitize_rel_path(cmd->filename, rel_path, sizeof(rel_path));
    esp_err_t err = book_library_set_book_offset(rel_path, (size_t)cmd->offset);
    if (err == ESP_OK)
    {
        ble_sync_request_refresh();
    }
    reply_status(BLE_CTRL_OP_SET_BOOK_OFFSET, err);
}

#if CONFIG_BOARD_MUSHAF_ENABLE || BOARD_MUSHAF_ENABLE
static void handle_cmd_set_mushaf_state(const ble_sync_cmd_msg_t *cmd)
{
    const uint8_t *d = cmd->raw; /* d[0] = sub, then data */
    const uint8_t n = cmd->raw_len;
    esp_err_t err = ESP_ERR_INVALID_ARG;

    if (n >= 5 && d[0] == 0x01)
    {
        uint16_t ayah = (uint16_t)d[2] | ((uint16_t)d[3] << 8);
        err = mushaf_reader_set_cursor(d[1], ayah, d[4]);
    }
    else if (n >= 2 && d[0] == 0x02)
    {
        uint8_t count = d[1];
        if (count <= MUSHAF_MAX_BOOKMARKS && n >= 2 + count * 4)
        {
            mushaf_bookmark_t list[MUSHAF_MAX_BOOKMARKS];
            memset(list, 0, sizeof(list));
            for (uint8_t i = 0; i < count; i++)
            {
                const uint8_t *p = &d[2 + i * 4];
                list[i].surah = p[0];
                list[i].ayah = (uint16_t)p[1] | ((uint16_t)p[2] << 8);
                list[i].juz = p[3];
            }
            err = mushaf_reader_set_bookmarks(list, count);
        }
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "SET_MUSHAF_STATE failed: %s", esp_err_to_name(err));
    }
    else
    {
        ble_sync_request_refresh();
    }
    reply_status(BLE_SYNC_OP_SET_MUSHAF_STATE, err);
}
#endif

static void ble_sync_worker_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Worker task started");
    ble_sync_cmd_msg_t cmd;

    while (s_sync_running)
    {
        if (!s_is_uploading)
        {
            // Idle state: wait for command
            if (xQueueReceive(s_cmd_queue, &cmd, pdMS_TO_TICKS(1000)) == pdTRUE)
            {
                switch (cmd.type)
                {
                case CMD_LIST_BOOKS:
                    handle_cmd_list_books();
                    break;
                case CMD_START_UPLOAD:
                    handle_cmd_start_upload(&cmd);
                    break;
                case CMD_DELETE_BOOK:
                    handle_cmd_delete_book(&cmd);
                    break;
                case CMD_FINISH_UPLOAD:
                    handle_cmd_finish_upload();
                    break;
                case CMD_ABORT_UPLOAD:
                    handle_cmd_abort_upload();
                    break;
                case CMD_SET_STORAGE_TARGET:
                    handle_cmd_set_storage_target(&cmd);
                    break;
                case CMD_SET_FONT:
                    handle_cmd_set_font(cmd.param8);
                    break;
                case CMD_FORCE_REFRESH:
                    handle_cmd_force_refresh();
                    break;
#if CONFIG_BOARD_MUSHAF_ENABLE || BOARD_MUSHAF_ENABLE
                case CMD_SET_MUSHAF_STATE:
                    handle_cmd_set_mushaf_state(&cmd);
                    break;
#endif
                case CMD_SET_REFRESH_INTERVAL:
                    handle_cmd_set_refresh_interval(cmd.param8);
                    break;
                case CMD_SET_STATUS_LED:
                    handle_cmd_set_status_led(cmd.param8);
                    break;
                case CMD_SET_SLEEP_TIMEOUT:
                    handle_cmd_set_sleep_timeout(cmd.param16);
                    break;
                case CMD_SET_NAV_HINTS:
                    handle_cmd_set_nav_hints(cmd.param8);
                    break;
                case CMD_SET_ROTATION:
                    handle_cmd_set_rotation(cmd.param8);
                    break;
                case CMD_SET_BUTTON_INVERT:
                    handle_cmd_set_button_invert(cmd.param8);
                    break;
                case CMD_GET_BOOKMARKS:
                    handle_cmd_get_bookmarks(&cmd);
                    break;
                case CMD_DELETE_BOOKMARK:
                    handle_cmd_delete_bookmark(&cmd);
                    break;
                case CMD_ADD_BOOKMARK:
                    handle_cmd_add_bookmark(&cmd);
                    break;
                case CMD_SET_BOOK_OFFSET:
                    handle_cmd_set_book_offset(&cmd);
                    break;
                default:
                    break;
                }
            }
        }
        else
        {
            // Uploading state: check for commands first
            if (xQueueReceive(s_cmd_queue, &cmd, 0) == pdTRUE)
            {
                switch (cmd.type)
                {
                case CMD_FINISH_UPLOAD:
                    handle_cmd_finish_upload();
                    continue;
                case CMD_ABORT_UPLOAD:
                    handle_cmd_abort_upload();
                    continue;
                case CMD_START_UPLOAD:
                    handle_cmd_start_upload(&cmd);
                    continue;
                case CMD_SET_STORAGE_TARGET:
                    handle_cmd_set_storage_target(&cmd);
                    continue;
                case CMD_SET_FONT:
                    handle_cmd_set_font(cmd.param8);
                    continue;
                case CMD_FORCE_REFRESH:
                    handle_cmd_force_refresh();
                    continue;
                case CMD_SET_SLEEP_TIMEOUT:
                    handle_cmd_set_sleep_timeout(cmd.param16);
                    continue;
#if CONFIG_BOARD_MUSHAF_ENABLE || BOARD_MUSHAF_ENABLE
                case CMD_SET_MUSHAF_STATE:
                    handle_cmd_set_mushaf_state(&cmd);
                    continue;
#endif
                case CMD_SET_STATUS_LED:
                    handle_cmd_set_status_led(cmd.param8);
                    continue;
                case CMD_SET_REFRESH_INTERVAL:
                    handle_cmd_set_refresh_interval(cmd.param8);
                    continue;
                case CMD_SET_NAV_HINTS:
                    handle_cmd_set_nav_hints(cmd.param8);
                    continue;
                case CMD_SET_ROTATION:
                    handle_cmd_set_rotation(cmd.param8);
                    continue;
                case CMD_SET_BUTTON_INVERT:
                    handle_cmd_set_button_invert(cmd.param8);
                    continue;
                case CMD_GET_BOOKMARKS:
                    handle_cmd_get_bookmarks(&cmd);
                    continue;
                case CMD_DELETE_BOOKMARK:
                    handle_cmd_delete_bookmark(&cmd);
                    continue;
                case CMD_ADD_BOOKMARK:
                    handle_cmd_add_bookmark(&cmd);
                    continue;
                case CMD_SET_BOOK_OFFSET:
                    handle_cmd_set_book_offset(&cmd);
                    continue;
                default:
                    break;
                }
            }

            // Stream data from ringbuffer into open file
            if (s_data_ringbuf != NULL && s_upload_fp != NULL)
            {
                size_t item_sz = 0;
                uint8_t *item = (uint8_t *)xRingbufferReceiveUpTo(s_data_ringbuf, &item_sz,
                                                                  pdMS_TO_TICKS(20), 4096);
                if (item != NULL)
                {
                    size_t written = fwrite(item, 1, item_sz, s_upload_fp);
                    if (written != item_sz)
                    {
                        ESP_LOGE(TAG, "fwrite failed: written %u != %u",
                                 (unsigned)written, (unsigned)item_sz);
                        if (s_upload_err_status == BLE_SYNC_STATUS_OK)
                            s_upload_err_status = BLE_SYNC_STATUS_WRITE_ERR;
                        s_upload_error = true;
                    }
                    vRingbufferReturnItem(s_data_ringbuf, item);

                    if (s_event_cb)
                    {
                        s_event_cb(BLE_SYNC_EVT_UPLOAD_PROGRESS,
                                   (void *)(uintptr_t)s_bytes_received,
                                   s_event_cb_arg);
                    }
                }
            }
            else
            {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
    }

    ESP_LOGI(TAG, "Worker task terminating");
    if (s_upload_fp != NULL)
    {
        fclose(s_upload_fp);
        s_upload_fp = NULL;
    }
    s_worker_task_hdl = NULL;
    vTaskDelete(NULL);
}

/* --- GATT Callbacks --- */

/* Runs in the NimBLE host task (RULES.md 2.1): no flash writes, no rendering, no sleep.
 * Work that writes flash or touches shared reader state is queued for the worker task. */
static int ble_sync_control_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0)
    {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    uint8_t buf[256];
    uint16_t copied = 0;
    int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf) - 1, &copied);
    if (rc != 0 || copied == 0)
    {
        return BLE_ATT_ERR_UNLIKELY;
    }
    buf[copied] = '\0';

    uint8_t opcode = buf[0];
    ESP_LOGI(TAG, "Control write: opcode=0x%02x, len=%u", opcode, copied);

    ble_sync_cmd_msg_t cmd;
    memset(&cmd, 0, sizeof(cmd));

    switch (opcode)
    {
    case BLE_SYNC_OP_GET_MUSHAF_STATE:
    {
#if CONFIG_BOARD_MUSHAF_ENABLE || BOARD_MUSHAF_ENABLE
        const mushaf_state_t *qst = mushaf_reader_get_state();
        uint8_t resp[40];
        memset(resp, 0, sizeof(resp));
        resp[0] = BLE_SYNC_OP_GET_MUSHAF_STATE;
        resp[1] = BLE_SYNC_STATUS_OK;
        if (qst != NULL)
        {
            resp[2] = qst->started ? 1 : 0;
            resp[3] = qst->surah;
            resp[4] = (uint8_t)(qst->ayah & 0xFF);
            resp[5] = (uint8_t)((qst->ayah >> 8) & 0xFF);
            resp[6] = qst->layer;
            resp[7] = qst->bookmark_count;
            for (uint8_t i = 0; i < qst->bookmark_count && i < MUSHAF_MAX_BOOKMARKS; i++)
            {
                size_t base = 8 + (size_t)i * 4;
                resp[base + 0] = qst->bookmarks[i].surah;
                resp[base + 1] = (uint8_t)(qst->bookmarks[i].ayah & 0xFF);
                resp[base + 2] = (uint8_t)((qst->bookmarks[i].ayah >> 8) & 0xFF);
                resp[base + 3] = qst->bookmarks[i].juz;
            }
        }
        ble_sync_notify(s_control_val_handle, resp, sizeof(resp));
#else
        uint8_t resp[2] = {BLE_SYNC_OP_GET_MUSHAF_STATE, BLE_SYNC_STATUS_NOT_SUPPORTED};
        ble_sync_notify(s_control_val_handle, resp, sizeof(resp));
#endif
        break;
    }

    case BLE_SYNC_OP_SET_MUSHAF_STATE:
    {
#if CONFIG_BOARD_MUSHAF_ENABLE || BOARD_MUSHAF_ENABLE
        if (copied < 2 || (size_t)(copied - 1) > sizeof(cmd.raw))
        {
            uint8_t resp[2] = {BLE_SYNC_OP_SET_MUSHAF_STATE, BLE_SYNC_STATUS_ERROR};
            ble_sync_notify(s_control_val_handle, resp, sizeof(resp));
            break;
        }
        cmd.type = CMD_SET_MUSHAF_STATE;
        cmd.raw_len = (uint8_t)(copied - 1);
        memcpy(cmd.raw, &buf[1], cmd.raw_len);
        enqueue_cmd(&cmd, BLE_SYNC_OP_SET_MUSHAF_STATE);
#else
        uint8_t resp[2] = {BLE_SYNC_OP_SET_MUSHAF_STATE, BLE_SYNC_STATUS_NOT_SUPPORTED};
        ble_sync_notify(s_control_val_handle, resp, sizeof(resp));
#endif
        break;
    }

    case BLE_SYNC_OP_LIST_BOOKS:
        cmd.type = CMD_LIST_BOOKS;
        if (s_cmd_queue == NULL || xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(50)) != pdTRUE)
        {
            /* LIST_BOOKS replies carry is_last at byte 2: answer with a terminating error entry. */
            ESP_LOGE(TAG, "Command queue full for LIST_BOOKS");
            uint8_t err_resp[8] = {BLE_SYNC_OP_LIST_BOOKS, BLE_SYNC_STATUS_ERROR, 1, 0, 0, 0, 0, 0};
            ble_sync_notify(s_control_val_handle, err_resp, sizeof(err_resp));
        }
        break;

    case BLE_SYNC_OP_START_UPLOAD:
        if (copied < 6)
        {
            ESP_LOGE(TAG, "START_UPLOAD payload too short: %u bytes", copied);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        cmd.type = CMD_START_UPLOAD;
        cmd.file_size = (uint32_t)buf[1] |
                        ((uint32_t)buf[2] << 8) |
                        ((uint32_t)buf[3] << 16) |
                        ((uint32_t)buf[4] << 24);
        strncpy(cmd.filename, (char *)&buf[5], sizeof(cmd.filename) - 1);
        cmd.filename[sizeof(cmd.filename) - 1] = '\0';
        enqueue_cmd(&cmd, BLE_SYNC_OP_START_UPLOAD);
        break;

    case BLE_SYNC_OP_FINISH_UPLOAD:
        cmd.type = CMD_FINISH_UPLOAD;
        enqueue_cmd(&cmd, BLE_SYNC_OP_FINISH_UPLOAD);
        break;

    case BLE_SYNC_OP_DELETE_BOOK:
        if (copied < 2)
        {
            ESP_LOGE(TAG, "DELETE_BOOK payload too short: %u bytes", copied);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        cmd.type = CMD_DELETE_BOOK;
        strncpy(cmd.filename, (char *)&buf[1], sizeof(cmd.filename) - 1);
        cmd.filename[sizeof(cmd.filename) - 1] = '\0';
        enqueue_cmd(&cmd, BLE_SYNC_OP_DELETE_BOOK);
        break;

    case BLE_SYNC_OP_ABORT_UPLOAD:
        cmd.type = CMD_ABORT_UPLOAD;
        enqueue_cmd(&cmd, BLE_SYNC_OP_ABORT_UPLOAD);
        break;

    case BLE_SYNC_OP_SET_STORAGE_TARGET:
        if (copied < 2)
        {
            ESP_LOGE(TAG, "SET_STORAGE_TARGET payload too short: %u bytes", copied);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        cmd.type = CMD_SET_STORAGE_TARGET;
        cmd.storage_target = buf[1];
        enqueue_cmd(&cmd, BLE_SYNC_OP_SET_STORAGE_TARGET);
        break;

    case BLE_CTRL_OP_GET_BOOKMARKS:
    {
        if (copied < 2)
        {
            uint8_t err_resp[11] = {BLE_CTRL_OP_GET_BOOKMARKS, BLE_SYNC_STATUS_ERROR};
            ble_sync_notify(s_control_val_handle, err_resp, sizeof(err_resp));
            break;
        }
        cmd.type = CMD_GET_BOOKMARKS;
        strncpy(cmd.filename, (char *)&buf[1], sizeof(cmd.filename) - 1);
        cmd.filename[sizeof(cmd.filename) - 1] = '\0';
        if (s_cmd_queue == NULL || xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(50)) != pdTRUE)
        {
            ESP_LOGE(TAG, "Command queue full for GET_BOOKMARKS");
            uint8_t err_resp[11] = {BLE_CTRL_OP_GET_BOOKMARKS, BLE_SYNC_STATUS_ERROR};
            ble_sync_notify(s_control_val_handle, err_resp, sizeof(err_resp));
        }
        break;
    }

    case BLE_CTRL_OP_DELETE_BOOKMARK:
    {
        if (copied <= 5)
        {
            uint8_t resp[2] = {BLE_CTRL_OP_DELETE_BOOKMARK, BLE_SYNC_STATUS_ERROR};
            ble_sync_notify(s_control_val_handle, resp, sizeof(resp));
            break;
        }
        cmd.type = CMD_DELETE_BOOKMARK;
        cmd.offset = (uint32_t)buf[1] |
                     ((uint32_t)buf[2] << 8) |
                     ((uint32_t)buf[3] << 16) |
                     ((uint32_t)buf[4] << 24);
        strncpy(cmd.filename, (char *)&buf[5], sizeof(cmd.filename) - 1);
        cmd.filename[sizeof(cmd.filename) - 1] = '\0';
        enqueue_cmd(&cmd, BLE_CTRL_OP_DELETE_BOOKMARK);
        break;
    }

    case BLE_CTRL_OP_ADD_BOOKMARK:
    {
        if (copied <= 5)
        {
            uint8_t resp[2] = {BLE_CTRL_OP_ADD_BOOKMARK, BLE_SYNC_STATUS_ERROR};
            ble_sync_notify(s_control_val_handle, resp, sizeof(resp));
            break;
        }
        cmd.type = CMD_ADD_BOOKMARK;
        cmd.offset = (uint32_t)buf[1] |
                     ((uint32_t)buf[2] << 8) |
                     ((uint32_t)buf[3] << 16) |
                     ((uint32_t)buf[4] << 24);
        strncpy(cmd.filename, (char *)&buf[5], sizeof(cmd.filename) - 1);
        cmd.filename[sizeof(cmd.filename) - 1] = '\0';
        enqueue_cmd(&cmd, BLE_CTRL_OP_ADD_BOOKMARK);
        break;
    }

    case BLE_CTRL_OP_SET_BOOK_OFFSET:
    {
        if (copied <= 5)
        {
            uint8_t resp[2] = {BLE_CTRL_OP_SET_BOOK_OFFSET, BLE_SYNC_STATUS_ERROR};
            ble_sync_notify(s_control_val_handle, resp, sizeof(resp));
            break;
        }
        cmd.type = CMD_SET_BOOK_OFFSET;
        cmd.offset = (uint32_t)buf[1] |
                     ((uint32_t)buf[2] << 8) |
                     ((uint32_t)buf[3] << 16) |
                     ((uint32_t)buf[4] << 24);
        strncpy(cmd.filename, (char *)&buf[5], sizeof(cmd.filename) - 1);
        cmd.filename[sizeof(cmd.filename) - 1] = '\0';
        enqueue_cmd(&cmd, BLE_CTRL_OP_SET_BOOK_OFFSET);
        break;
    }

    case BLE_SYNC_OP_SET_FONT:
        if (copied < 2)
        {
            ESP_LOGE(TAG, "SET_FONT payload too short: %u bytes", copied);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        cmd.type = CMD_SET_FONT;
        cmd.param8 = buf[1];
        enqueue_cmd(&cmd, BLE_SYNC_OP_SET_FONT);
        break;

    case BLE_SYNC_OP_FORCE_REFRESH:
        cmd.type = CMD_FORCE_REFRESH;
        enqueue_cmd(&cmd, BLE_SYNC_OP_FORCE_REFRESH);
        break;

    case BLE_SYNC_OP_SET_SLEEP_TIMEOUT:
        if (copied < 3)
        {
            ESP_LOGE(TAG, "SET_SLEEP_TIMEOUT payload too short: %u bytes", copied);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        cmd.type = CMD_SET_SLEEP_TIMEOUT;
        cmd.param16 = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
        enqueue_cmd(&cmd, BLE_SYNC_OP_SET_SLEEP_TIMEOUT);
        break;

    case BLE_SYNC_OP_SET_NAV_HINTS:
        if (copied < 2)
        {
            ESP_LOGE(TAG, "SET_NAV_HINTS payload too short: %u bytes", copied);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        cmd.type = CMD_SET_NAV_HINTS;
        cmd.param8 = (buf[1] != 0) ? 1 : 0;
        enqueue_cmd(&cmd, BLE_SYNC_OP_SET_NAV_HINTS);
        break;

    case BLE_SYNC_OP_SET_ROTATION:
        if (copied < 2)
        {
            ESP_LOGE(TAG, "SET_ROTATION payload too short: %u bytes", copied);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        cmd.type = CMD_SET_ROTATION;
        cmd.param8 = buf[1];
        enqueue_cmd(&cmd, BLE_SYNC_OP_SET_ROTATION);
        break;

    case BLE_SYNC_OP_SET_BUTTON_INVERT:
        if (copied < 2)
        {
            ESP_LOGE(TAG, "SET_BUTTON_INVERT payload too short: %u bytes", copied);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        cmd.type = CMD_SET_BUTTON_INVERT;
        cmd.param8 = (buf[1] != 0) ? 1 : 0;
        enqueue_cmd(&cmd, BLE_SYNC_OP_SET_BUTTON_INVERT);
        break;

    case BLE_SYNC_OP_SET_STATUS_LED:
    {
        if (copied < 2)
        {
            ESP_LOGE(TAG, "SET_STATUS_LED payload too short: %u bytes", copied);
            reply_status(BLE_SYNC_OP_SET_STATUS_LED, ESP_ERR_INVALID_SIZE);
            break;
        }
        if (!status_led_is_available()) /* read-only check: fine on the host task */
        {
            uint8_t resp[2] = {BLE_SYNC_OP_SET_STATUS_LED, BLE_SYNC_STATUS_NOT_SUPPORTED};
            ble_sync_notify(s_control_val_handle, resp, sizeof(resp));
            break;
        }
        cmd.type = CMD_SET_STATUS_LED;
        cmd.param8 = (buf[1] != 0) ? 1 : 0;
        enqueue_cmd(&cmd, BLE_SYNC_OP_SET_STATUS_LED);
        break;
    }

    case BLE_SYNC_OP_SET_REFRESH_INTERVAL:
    {
        if (copied < 2 || buf[1] > 100)
        {
            ESP_LOGE(TAG, "SET_REFRESH_INTERVAL invalid payload (len=%u, val=%u)", copied, (copied >= 2) ? buf[1] : 0);
            uint8_t resp[2] = {BLE_SYNC_OP_SET_REFRESH_INTERVAL, BLE_SYNC_STATUS_ERROR};
            ble_sync_notify(s_control_val_handle, resp, sizeof(resp));
            break;
        }
        cmd.type = CMD_SET_REFRESH_INTERVAL;
        cmd.param8 = buf[1];
        enqueue_cmd(&cmd, BLE_SYNC_OP_SET_REFRESH_INTERVAL);
        break;
    }

    case BLE_SYNC_OP_GET_CONFIG:
    {
        const app_settings_t *cfg = app_settings_get();

        uint8_t pwa_font_id = (cfg->font_size == APP_FONT_LARGE) ? 0 : 1; /* 0 = 16 px, 1 = 12 px */
        uint8_t status_led = (status_led_is_available() && cfg->status_led_enabled) ? 1 : 0;
        uint8_t storage_target = (s_storage_target == STORAGE_TARGET_FLASH) ? 1 : 0;

        uint8_t caps = BLE_SYNC_CAP_REFRESH_INTERVAL;
#if CONFIG_BOARD_MUSHAF_ENABLE || BOARD_MUSHAF_ENABLE
        caps |= BLE_SYNC_CAP_MUSHAF;
#endif
        uint8_t resp[16] = {
            BLE_SYNC_OP_GET_CONFIG,
            BLE_SYNC_STATUS_OK,
            pwa_font_id,
            cfg->show_nav_hints ? 1 : 0,
            cfg->rotation,
            cfg->btn_invert ? 1 : 0,
            status_led,
            storage_target,
            (uint8_t)(cfg->sleep_timeout_sec & 0xFF),
            (uint8_t)(cfg->sleep_timeout_sec >> 8),
            caps,
            (uint8_t)(BOARD_DISPLAY_WIDTH & 0xFF),
            (uint8_t)((BOARD_DISPLAY_WIDTH >> 8) & 0xFF),
            (uint8_t)(BOARD_DISPLAY_HEIGHT & 0xFF),
            (uint8_t)((BOARD_DISPLAY_HEIGHT >> 8) & 0xFF),
            cfg->full_refresh_interval};
        ble_sync_notify(s_control_val_handle, resp, sizeof(resp));
        break;
    }

    default:
        ESP_LOGW(TAG, "Unknown control opcode: 0x%02x", opcode);
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0;
}

static int ble_sync_data_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t total_len = OS_MBUF_PKTLEN(ctxt->om);
    if (total_len < 2)
    {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    if (!s_is_uploading || s_data_ringbuf == NULL)
    {
        ESP_LOGW(TAG, "Data packet received while not uploading");
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint8_t chunk_buf[512];
    uint16_t copied = 0;
    int rc = ble_hs_mbuf_to_flat(ctxt->om, chunk_buf, sizeof(chunk_buf), &copied);
    if (rc != 0 || copied < 2)
    {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t seq = (uint16_t)chunk_buf[0] | ((uint16_t)chunk_buf[1] << 8);
    const uint8_t *payload = &chunk_buf[2];
    size_t payload_len = copied - 2;

    if (seq != s_expected_seq)
    {
        ESP_LOGE(TAG, "Seq mismatch: expected %u, got %u", s_expected_seq, seq);
        if (s_upload_err_status == BLE_SYNC_STATUS_OK)
            s_upload_err_status = BLE_SYNC_STATUS_SEQ_ERR;
        s_upload_error = true;
    }
    s_expected_seq = seq + 1;

    if (payload_len > 0)
    {
        BaseType_t sent = xRingbufferSend(s_data_ringbuf, payload, payload_len, pdMS_TO_TICKS(250));
        if (sent != pdTRUE)
        {
            ESP_LOGE(TAG, "Ringbuffer full, dropped %u bytes", (unsigned)payload_len);
            if (s_upload_err_status == BLE_SYNC_STATUS_OK)
                s_upload_err_status = BLE_SYNC_STATUS_OVERFLOW;
            s_upload_error = true;
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        s_bytes_received += payload_len;
    }

    return 0;
}

static int ble_sync_status_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        ble_sync_status_payload_t status;
        ble_sync_get_status_payload(&status);
        int rc = os_mbuf_append(ctxt->om, &status, sizeof(status));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/* --- GAP Events & Advertising --- */

static int ble_sync_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "BLE connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);

        if (event->connect.status == 0)
        {
            s_conn_handle = event->connect.conn_handle;
            s_is_connected = true;
            xfer_reset();

            // Set preferred MTU to 512 (client initiates MTU exchange)
            ble_att_set_preferred_mtu(512);

            // Request optimal connection interval (15ms - 30ms) for high throughput
            struct ble_gap_upd_params params = {
                .itvl_min = 12, // 15 ms (12 * 1.25)
                .itvl_max = 24, // 30 ms (24 * 1.25)
                .latency = 0,
                .supervision_timeout = 400, // 4000 ms
                .min_ce_len = 0,
                .max_ce_len = 0,
            };
            ble_gap_update_params(s_conn_handle, &params);

            if (s_event_cb)
            {
                s_event_cb(BLE_SYNC_EVT_CONNECTED, NULL, s_event_cb_arg);
            }
        }
        else
        {
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_is_connected = false;
            if (s_sync_running)
            {
                ble_sync_advertise();
            }
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE disconnected; reason=%d", event->disconnect.reason);
        s_is_connected = false;
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

        if (s_is_uploading)
        {
            /* No client to answer: a failed send only needs a log line. */
            ble_sync_cmd_msg_t cmd = {.type = CMD_ABORT_UPLOAD};
            if (xQueueSend(s_cmd_queue, &cmd, 0) != pdTRUE)
            {
                ESP_LOGE(TAG, "Command queue full, abort on disconnect dropped");
            }
        }

        if (s_event_cb)
        {
            s_event_cb(BLE_SYNC_EVT_DISCONNECTED, NULL, s_event_cb_arg);
        }

        if (s_sync_running)
        {
            ble_sync_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "BLE MTU updated: conn_handle=%d, mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        s_current_mtu = event->mtu.value;
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "BLE subscribe event: attr_handle=%d, cur_notify=%d",
                 event->subscribe.attr_handle, event->subscribe.cur_notify);
        if (event->subscribe.attr_handle == s_status_val_handle && event->subscribe.cur_notify)
        {
            ble_sync_notify_status();
        }
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertising complete; reason=%d", event->adv_complete.reason);
        if (s_sync_running && !s_is_connected)
        {
            ble_sync_advertise();
        }
        return 0;

    default:
        return 0;
    }
}

static void ble_sync_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields rsp_fields;
    int rc;

    // Primary advertisement data
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    fields.uuids128 = &s_svc_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to set adv fields: rc=%d", rc);
        return;
    }

    // Scan response data (complete device name)
    memset(&rsp_fields, 0, sizeof(rsp_fields));
    const char *dev_name = ble_svc_gap_device_name();
    if (!dev_name)
    {
        dev_name = BLE_SYNC_DEVICE_NAME;
    }
    rsp_fields.name = (uint8_t *)dev_name;
    rsp_fields.name_len = strlen(dev_name);
    rsp_fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to set scan rsp fields: rc=%d", rc);
        return;
    }

    // Start advertising
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    uint8_t own_addr_type;
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to determine own address type: rc=%d", rc);
        own_addr_type = BLE_OWN_ADDR_PUBLIC;
    }

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_sync_gap_event_cb, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to start advertising: rc=%d", rc);
        return;
    }

    ESP_LOGI(TAG, "BLE advertising started as '%s'", dev_name);
    if (s_event_cb)
    {
        s_event_cb(BLE_SYNC_EVT_ADVERTISING_STARTED, NULL, s_event_cb_arg);
    }
}

static void ble_sync_on_sync(void)
{
    ESP_LOGI(TAG, "NimBLE host synced");
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr failed: %d", rc);
    }
    ble_sync_advertise();
}

static void ble_sync_on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE host reset; reason=%d", reason);
}

static void ble_sync_gatts_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];
    switch (ctxt->op)
    {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGD(TAG, "Registered service %s with handle=%d",
                 ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                 ctxt->svc.handle);
        break;
    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGD(TAG, "Registered char %s def_handle=%d val_handle=%d",
                 ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                 ctxt->chr.def_handle,
                 ctxt->chr.val_handle);
        break;
    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGD(TAG, "Registered desc %s handle=%d",
                 ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                 ctxt->dsc.handle);
        break;
    default:
        break;
    }
}

static void ble_sync_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task running");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* --- Public API --- */

esp_err_t ble_sync_start(void)
{
    if (s_sync_running)
    {
        ESP_LOGI(TAG, "BLE sync already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting BLE sync component");

    s_storage_target = STORAGE_TARGET_FLASH;

    // Initialize NVS flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_flash_init failed: %d", ret);
        return ret;
    }

    // Create command queue if needed
    if (s_cmd_queue == NULL)
    {
        s_cmd_queue = xQueueCreate(CMD_QUEUE_LEN, sizeof(ble_sync_cmd_msg_t));
        if (s_cmd_queue == NULL)
        {
            ESP_LOGE(TAG, "Failed to create command queue");
            return ESP_ERR_NO_MEM;
        }
    }

    // Create data ringbuffer if needed
    if (s_data_ringbuf == NULL)
    {
        s_data_ringbuf = xRingbufferCreate(RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF);
        if (s_data_ringbuf == NULL)
        {
            ESP_LOGE(TAG, "Failed to create ringbuffer");
            return ESP_ERR_NO_MEM;
        }
    }

    s_sync_running = true;

    // Start worker task if needed
    if (s_worker_task_hdl == NULL)
    {
        BaseType_t task_ret = xTaskCreate(ble_sync_worker_task,
                                          "ble_sync_wrk",
                                          WORKER_TASK_STACK_SIZE,
                                          NULL,
                                          5,
                                          &s_worker_task_hdl);
        if (task_ret != pdPASS)
        {
            ESP_LOGE(TAG, "Failed to create worker task");
            s_sync_running = false;
            return ESP_ERR_NO_MEM;
        }
    }

    // Initialize NimBLE host stack
    static bool s_nimble_inited = false;
    if (!s_nimble_inited)
    {
#if __has_include("esp_nimble_hci.h") && (ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0))
        ret = esp_nimble_hci_and_controller_init();
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_nimble_hci_and_controller_init failed: %d", ret);
            return ret;
        }
#endif
        ret = nimble_port_init();
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "nimble_port_init failed: %d", ret);
            return ret;
        }

        ble_hs_cfg.reset_cb = ble_sync_on_reset;
        ble_hs_cfg.sync_cb = ble_sync_on_sync;
        ble_hs_cfg.gatts_register_cb = ble_sync_gatts_register_cb;
        ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

        ble_svc_gap_init();
        ble_svc_gatt_init();

        int rc = ble_gatts_count_cfg(s_gatt_svcs);
        if (rc != 0)
        {
            ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
            return ESP_FAIL;
        }

        rc = ble_gatts_add_svcs(s_gatt_svcs);
        if (rc != 0)
        {
            ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
            return ESP_FAIL;
        }

        rc = ble_svc_gap_device_name_set(BLE_SYNC_DEVICE_NAME);
        if (rc != 0)
        {
            ESP_LOGE(TAG, "ble_svc_gap_device_name_set failed: %d", rc);
            return ESP_FAIL;
        }

        nimble_port_freertos_init(ble_sync_host_task);
        s_nimble_inited = true;
    }
    else
    {
        ble_sync_advertise();
    }

    return ESP_OK;
}

void ble_sync_stop(void)
{
    if (!s_sync_running)
    {
        return;
    }
    ESP_LOGI(TAG, "Stopping BLE sync");
    s_sync_running = false;

    ble_gap_adv_stop();

    if (s_is_connected && s_conn_handle != BLE_HS_CONN_HANDLE_NONE)
    {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    s_is_connected = false;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

    if (s_is_uploading || s_upload_fp != NULL)
    {
        if (s_upload_fp != NULL)
        {
            fclose(s_upload_fp);
            s_upload_fp = NULL;
        }
        if (s_upload_filename[0] != '\0')
        {
            const char *books_dir = (s_storage_target == STORAGE_TARGET_FLASH) ? "/spiflash/books" : "/sdcard/books";
            char fpath[256];
            snprintf(fpath, sizeof(fpath), "%s/%s", books_dir, s_upload_filename);
            unlink(fpath);
            ESP_LOGW(TAG, "Deleted partial file on stop: %s", fpath);
            s_upload_filename[0] = '\0';
        }
    }

    s_is_uploading = false;
    s_upload_error = false;
    s_bytes_received = 0;
    s_expected_file_size = 0;
    s_expected_seq = 0;
    s_upload_err_status = BLE_SYNC_STATUS_OK;
    s_current_mtu = 23;
    xfer_reset();

    if (s_data_ringbuf != NULL)
    {
        size_t dummy_sz = 0;
        void *dummy = NULL;
        while ((dummy = xRingbufferReceiveUpTo(s_data_ringbuf, &dummy_sz, 0, 4096)) != NULL)
        {
            vRingbufferReturnItem(s_data_ringbuf, dummy);
        }
    }
}

bool ble_sync_is_connected(void)
{
    return s_is_connected;
}

uint32_t ble_sync_get_bytes_received(void)
{
    return s_bytes_received;
}

void ble_sync_get_xfer_info(ble_sync_xfer_info_t *out)
{
    if (!out)
        return;
    taskENTER_CRITICAL(&s_xfer_mux);
    out->state = s_xfer_state;
    out->status = s_xfer_status;
    out->total = s_xfer_total;
    out->bytes = (s_xfer_state == BLE_SYNC_XFER_TRANSFERRING) ? s_bytes_received
                                                              : s_xfer_final_bytes;
    memcpy(out->filename, s_xfer_name, sizeof(out->filename));
    taskEXIT_CRITICAL(&s_xfer_mux);
}

esp_err_t ble_sync_notify_status(void)
{
    ble_sync_status_payload_t status;
    ble_sync_get_status_payload(&status);
    return ble_sync_notify(s_status_val_handle, &status, sizeof(status));
}

esp_err_t ble_sync_set_storage_target(storage_target_t target)
{
    if (target != STORAGE_TARGET_SDCARD && target != STORAGE_TARGET_FLASH)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (target == STORAGE_TARGET_SDCARD)
    {
        if (!sdcard_is_mounted())
        {
            ESP_LOGE(TAG, "Cannot set storage target to SD card: not mounted");
            return ESP_ERR_INVALID_STATE;
        }
    }
    else if (target == STORAGE_TARGET_FLASH)
    {
        if (!flash_storage_is_mounted())
        {
            esp_err_t err = flash_storage_init();
            if (err != ESP_OK || !flash_storage_is_mounted())
            {
                ESP_LOGE(TAG, "Cannot set storage target to Flash: flash init failed (0x%x)", err);
                return ESP_ERR_INVALID_STATE;
            }
        }
    }

    s_storage_target = target;
    ESP_LOGI(TAG, "BLE sync storage target set to %s",
             target == STORAGE_TARGET_FLASH ? "FLASH" : "SDCARD");
    return ESP_OK;
}

storage_target_t ble_sync_get_storage_target(void)
{
    return s_storage_target;
}

void ble_sync_register_callback(ble_sync_event_cb_t cb, void *user_data)
{
    s_event_cb = cb;
    s_event_cb_arg = user_data;
}