#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "driver/gpio.h"

#include "app_config.h"
#include "board_config.h"
#include "power_mgr.h"
#include "sdcard.h"
#include "button.h"
#include "epaper_port.h"
#include "book_library.h"
#include "text_pager.h"
#include "ble_sync.h"
#include "reader_ui.h"
#include "status_led.h"

#include "views.h"
#include "view_reading.h"
#include "view_menu.h"
#include "view_library.h"
#include "view_ble_sync.h"
#include "view_bookmarks.h"
#include "view_nav_hints.h"
#if CONFIG_BOARD_MUSHAF_ENABLE
#include "view_mushaf.h"
#endif

#define TAG "MAIN"

#define LOOP_MAX_WAIT_MS 60000       // keeps pdMS_TO_TICKS within 32 bits; bounds staleness of BLE-changed timeout
#define BLE_RX_REFRESH_MIN_US 500000

/* ---------------- WAKE PROFILE ----------------
 * Define WAKE_PROFILE to record timing marks during boot/wake.
 * Marks are buffered in RAM and printed once, after the first event
 * is rendered, so UART output does not skew the measurements.
 * Printed with ESP_LOGW so they survive CONFIG_LOG_DEFAULT_LEVEL_WARN.
 */
// #define WAKE_PROFILE 1
#ifdef WAKE_PROFILE
#define WAKE_MARK_MAX 24
typedef struct
{
    const char *label;
    int64_t us;
} wake_mark_t;
static wake_mark_t s_wake_marks[WAKE_MARK_MAX];
static int s_wake_mark_count = 0;

static void wake_mark(const char *label)
{
    if (s_wake_mark_count < WAKE_MARK_MAX)
    {
        s_wake_marks[s_wake_mark_count].label = label;
        s_wake_marks[s_wake_mark_count].us = esp_timer_get_time();
        s_wake_mark_count++;
    }
}

static void wake_marks_dump(void)
{
    int64_t prev = (s_wake_mark_count > 0) ? s_wake_marks[0].us : 0;
    for (int i = 0; i < s_wake_mark_count; i++)
    {
        ESP_LOGW("WAKE", "%-24s %9lld us  +%8lld us",
                 s_wake_marks[i].label, s_wake_marks[i].us, s_wake_marks[i].us - prev);
        prev = s_wake_marks[i].us;
    }
}
#define WAKE_MARK(label) wake_mark(label)
#define WAKE_DUMP() wake_marks_dump()
#else
#define WAKE_MARK(label) ((void)0)
#define WAKE_DUMP() ((void)0)
#endif
/* ---------------- END WAKE PROFILE ---------------- */

static QueueHandle_t s_btn_queue = NULL;
static atomic_int s_current_view = VIEW_MENU; // written by main task, read by BLE task
static int64_t s_last_activity_us = 0;
static bool s_nvs_ready = false;             // main task only
static bool s_first_event_handled = false;   // main task only

RTC_DATA_ATTR static reader_view_t s_rtc_last_view = VIEW_MENU;
RTC_DATA_ATTR static bool s_rtc_has_sdcard = false;
RTC_DATA_ATTR static storage_target_t s_rtc_storage_target = STORAGE_TARGET_FLASH;

static inline reader_view_t get_view(void)
{
    return (reader_view_t)atomic_load(&s_current_view);
}

static inline void set_view(reader_view_t view)
{
    atomic_store(&s_current_view, (int)view);
    s_rtc_last_view = view;
}

static inline void update_activity(void)
{
    s_last_activity_us = esp_timer_get_time();
}

static void ensure_nvs(void)
{
    if (s_nvs_ready)
    {
        return;
    }
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    s_nvs_ready = true;
}

static void post_refresh_event(void)
{
    if (s_btn_queue)
    {
        const button_event_t evt = {.button = BTN_ID_BOOT, .type = BTN_EVENT_CUSTOM_REFRESH};
        xQueueSend(s_btn_queue, &evt, 0);
    }
}

void app_enter_deep_sleep(void)
{
    ensure_nvs(); // no-op unless sleep happens before deferred init ran
    s_rtc_last_view = get_view();

    reading_state_t state;
    if (book_library_get_current_state(&state) == ESP_OK && state.active_book_path[0] != '\0')
    {
        s_rtc_storage_target =
            (strncmp(state.active_book_path, SDCARD_MOUNT_POINT, sizeof(SDCARD_MOUNT_POINT) - 1) == 0)
                ? STORAGE_TARGET_SDCARD
                : STORAGE_TARGET_FLASH;
    }
    else
    {
        s_rtc_storage_target = book_library_get_storage_target();
    }
    book_library_update_offset(text_pager_get_current_offset());
    book_library_flush_state(); // no write if state not dirty
#if CONFIG_BOARD_MUSHAF_ENABLE
    view_mushaf_save_before_sleep();
#endif
    status_led_trigger(STATUS_LED_EVT_SLEEP); // synchronous 150 ms; no-op if LED disabled
    epaper_port_sleep();
    epaper_power_off();
    esp_deep_sleep_disable_rom_logging(); // silences ROM banner on next wake only; no eFuse burn
    power_mgr_enter_deep_sleep(0);
}

// Runs in the NimBLE host task: never touch peripherals here, only signal the main task.
static void on_ble_sync_event(ble_sync_event_t event, const void *param, void *user_data)
{
    (void)param;
    (void)user_data;

    if (event == BLE_SYNC_EVT_REQUEST_REFRESH)
    {
        post_refresh_event();
        return;
    }

    const reader_view_t view = get_view(); // single snapshot
    const bool in_ble = (view == VIEW_BLE_SYNC);
    bool in_mushaf_menu = false;
#if CONFIG_BOARD_MUSHAF_ENABLE
    in_mushaf_menu = (view == VIEW_MUSHAF_MENU);
#endif
    if (!in_ble && !in_mushaf_menu)
    {
        return;
    }

    bool should_refresh = false;
    switch (event)
    {
    case BLE_SYNC_EVT_CONNECTED:
    case BLE_SYNC_EVT_DISCONNECTED:
    case BLE_SYNC_EVT_UPLOAD_COMPLETE:
    case BLE_SYNC_EVT_FILE_DELETED:
        should_refresh = true;
        break;
    case BLE_SYNC_EVT_UPLOAD_START:
    case BLE_SYNC_EVT_UPLOAD_ABORTED:
        should_refresh = in_ble;
        break;
    case BLE_SYNC_EVT_UPLOAD_PROGRESS:
        if (in_ble)
        {
            static int64_t s_last_rx_refresh_us = 0; // BLE task only
            int64_t now_us = esp_timer_get_time();
            if (now_us - s_last_rx_refresh_us >= BLE_RX_REFRESH_MIN_US)
            {
                s_last_rx_refresh_us = now_us;
                should_refresh = true;
            }
        }
        break;
    default:
        break;
    }
    if (should_refresh)
    {
        post_refresh_event();
    }
}

static reader_view_t dispatch_wake_view(reader_view_t last_view)
{
    switch (last_view)
    {
    case VIEW_READING:
        return view_reading_on_wake();
    case VIEW_MENU:
        return view_menu_on_wake();
    case VIEW_BOOK_LIST:
        return view_library_on_wake();
    case VIEW_BOOKMARKS:
        return view_bookmarks_on_wake();
    case VIEW_NAV_HINTS:
        return view_nav_hints_on_wake();
    case VIEW_BLE_SYNC:
        return view_ble_sync_on_wake();
#if CONFIG_BOARD_MUSHAF_ENABLE
    case VIEW_MUSHAF_MENU:
    case VIEW_MUSHAF_READING:
    case VIEW_MUSHAF_JUMP:
    case VIEW_MUSHAF_BOOKMARKS:
        return view_mushaf_on_wake();
#endif
    default:
        view_menu_set_index(0);
        return VIEW_MENU;
    }
}

static reader_view_t dispatch_view_event(reader_view_t view, button_event_t evt)
{
    switch (view)
    {
    case VIEW_READING:
        return view_reading_handle_event(evt);
    case VIEW_MENU:
        return view_menu_handle_event(evt);
    case VIEW_BOOK_LIST:
        return view_library_handle_event(evt);
    case VIEW_BLE_SYNC:
        return view_ble_sync_handle_event(evt);
    case VIEW_BOOKMARKS:
        return view_bookmarks_handle_event(evt);
    case VIEW_NAV_HINTS:
        return view_nav_hints_handle_event(evt);
#if CONFIG_BOARD_MUSHAF_ENABLE
    case VIEW_MUSHAF_MENU:
    case VIEW_MUSHAF_READING:
    case VIEW_MUSHAF_JUMP:
    case VIEW_MUSHAF_BOOKMARKS:
        return view_mushaf_handle_event(view, evt);
#endif
    default:
        return view;
    }
}

static void restore_or_open_default_book(void)
{
    reading_state_t state;
    if (book_library_load_state(&state) == ESP_OK && state.active_book_path[0] != '\0' &&
        text_pager_open_book(state.active_book_path, state.byte_offset) == ESP_OK)
    {
        return;
    }
    size_t count = book_library_get_count();
    for (size_t i = 0; i < count; i++)
    {
        const book_info_t *book = book_library_get_book(i);
        if (book && book->type == LIBRARY_ENTRY_FILE &&
            text_pager_open_book(book->path, 0) == ESP_OK)
        {
            book_library_set_active_book(book->path, true);
            return;
        }
    }
}

static bool inactivity_expired(void)
{
    uint16_t timeout_sec = ble_sync_get_sleep_timeout();
    if (timeout_sec == 0)
    {
        return false;
    }
    return (esp_timer_get_time() - s_last_activity_us) >= (int64_t)timeout_sec * 1000000LL;
}

static TickType_t compute_wait_ticks(reader_view_t view)
{
    if (view == VIEW_BLE_SYNC)
    {
        return view_ble_sync_get_wait_ticks();
    }
    int64_t wait_ms = LOOP_MAX_WAIT_MS;
    uint16_t timeout_sec = ble_sync_get_sleep_timeout();
    if (timeout_sec > 0)
    {
        int64_t remaining_us = (int64_t)timeout_sec * 1000000LL - (esp_timer_get_time() - s_last_activity_us);
        if (remaining_us <= 0)
        {
            return 0;
        }
        int64_t remaining_ms = (remaining_us + 999) / 1000; // round up: avoid 0-tick spins
        if (remaining_ms < wait_ms)
        {
            wait_ms = remaining_ms;
        }
    }
    TickType_t ticks = pdMS_TO_TICKS((uint32_t)wait_ms);
    return ticks > 0 ? ticks : 1;
}

static void wait_for_splash_dismiss(void)
{
    button_event_t evt;
    for (;;)
    {
        uint16_t timeout_sec = ble_sync_get_sleep_timeout();
        // seconds * tick rate: avoids the 32-bit overflow of pdMS_TO_TICKS(sec * 1000)
        TickType_t wait = (timeout_sec > 0)
                              ? (TickType_t)timeout_sec * configTICK_RATE_HZ
                              : portMAX_DELAY;
        if (xQueueReceive(s_btn_queue, &evt, wait) != pdTRUE)
        {
            ESP_LOGI(TAG, "No input on splash. Entering deep sleep...");
            app_enter_deep_sleep();
        }
        if (evt.type != BTN_EVENT_CUSTOM_REFRESH)
        {
            break; // only a real button press dismisses the splash
        }
    }
    while (gpio_get_level((gpio_num_t)BOARD_BTN_UP_PIN) == 0 ||
           gpio_get_level((gpio_num_t)BOARD_BTN_DOWN_PIN) == 0)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    xQueueReset(s_btn_queue);
}

void app_main(void)
{
    // 1. Latch board power and cut audio rail (must stay first)
    power_mgr_early_init();
    WAKE_MARK("power_mgr_early_init");
    ensure_nvs();
    WAKE_MARK("nvs");
    status_led_init();
    status_led_trigger(STATUS_LED_EVT_WAKEUP); // background task, returns immediately

    wakeup_reason_t raw_wakeup = power_mgr_get_wakeup_cause();
    bool is_cold_boot = (raw_wakeup == WAKEUP_REASON_COLD_BOOT ||
                         esp_reset_reason() != ESP_RST_DEEPSLEEP);

    // 2. Buttons: the wake stub's classified gesture is pushed to the queue here
    if (!is_cold_boot)
    {
        if (raw_wakeup == WAKEUP_REASON_BOOT_BUTTON)
        {
            button_seed_wakeup(BTN_ID_BOOT);
        }
        else if (raw_wakeup == WAKEUP_REASON_PWR_BUTTON)
        {
            button_seed_wakeup(BTN_ID_PWR);
        }
    }
    ESP_ERROR_CHECK(button_init(&s_btn_queue));
    WAKE_MARK("button_init");

    // 3. Settings, once. text_pager_init() first (font is applied to the pager);
    //    must run before reader_ui_init() (rotation is read there).
    text_pager_init();
    app_settings_load(is_cold_boot);
    WAKE_MARK("settings");

    ESP_LOGI(TAG, "Starting BacaIn firmware (wakeup=%d, cold=%d)...", raw_wakeup, is_cold_boot);

    // 4. Storage
    esp_err_t flash_ret = flash_storage_init();
    if (flash_ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Internal flash storage init failed (0x%x)", flash_ret);
    }
    WAKE_MARK("flash_storage_init");

    // Retry SD whenever it is the target: no card-detect pin, so a reinserted
    // card is only found by probing (~20 ms when absent).
    if (is_cold_boot || s_rtc_storage_target == STORAGE_TARGET_SDCARD)
    {
        s_rtc_has_sdcard = (sdcard_init() == ESP_OK);
        if (!s_rtc_has_sdcard)
        {
            ESP_LOGI(TAG, "MicroSD not detected, using internal flash storage");
        }
    }
    WAKE_MARK("sdcard_init");

    // 5. Display
    ESP_ERROR_CHECK(reader_ui_init(is_cold_boot));
    WAKE_MARK("reader_ui_init");

    // Show splash before the slow library scan so the screen updates sooner
    if (is_cold_boot)
    {
        reader_ui_render_splash(NULL, NULL);
    }

    // 6. Reader core
    book_library_init();
#if CONFIG_BOARD_MUSHAF_ENABLE
    view_mushaf_init(is_cold_boot);
#endif
    storage_target_t active_target =
        (s_rtc_has_sdcard && s_rtc_storage_target == STORAGE_TARGET_SDCARD)
            ? STORAGE_TARGET_SDCARD
            : STORAGE_TARGET_FLASH;
    book_library_set_storage_target(active_target);
    ble_sync_set_storage_target(active_target);
    if (is_cold_boot)
    {
        book_library_scan();
    }
    ble_sync_register_callback(on_ble_sync_event, NULL);
    WAKE_MARK("library/mushaf init");

    // 8. Restore reading state or open first available book
    //    (always opened: app_enter_deep_sleep() saves the pager offset unconditionally)
    restore_or_open_default_book();
    WAKE_MARK("book open");

    // 9. Initial view
    if (is_cold_boot)
    {
        wait_for_splash_dismiss();
        set_view(VIEW_MENU);
        view_menu_set_index(0);
        view_menu_render(false);
    }
    else
    {
        reader_view_t view = dispatch_wake_view(s_rtc_last_view);
        bool is_button_wake = (raw_wakeup == WAKEUP_REASON_BOOT_BUTTON ||
                               raw_wakeup == WAKEUP_REASON_PWR_BUTTON);
        if (!is_button_wake)
        {
            // Defensive: power_mgr currently enables only EXT1 button wake
            const button_event_t refresh_evt = {.button = BTN_ID_BOOT, .type = BTN_EVENT_CUSTOM_REFRESH};
            view = dispatch_view_event(view, refresh_evt);
        }
        set_view(view);
    }
    WAKE_MARK("wake view restored");

    update_activity();

    // 10. Event loop (main task is the only writer of s_current_view)
    for (;;)
    {
        reader_view_t view = get_view();
        if (view != VIEW_BLE_SYNC && inactivity_expired())
        {
            ESP_LOGI(TAG, "Inactivity timeout reached. Entering deep sleep...");
            app_enter_deep_sleep();
        }

        button_event_t evt;
        if (xQueueReceive(s_btn_queue, &evt, compute_wait_ticks(view)) == pdTRUE)
        {
            bool is_refresh = (evt.type == BTN_EVENT_CUSTOM_REFRESH);
            // Background refreshes count as activity only during BLE sync (keeps uploads alive)
            if (!is_refresh || view == VIEW_BLE_SYNC)
            {
                update_activity();
            }
            ESP_LOGD(TAG, "Btn evt: button=%d, type=%d, view=%d", evt.button, evt.type, view);
            view = dispatch_view_event(view, evt);
            set_view(view);

            // First event after boot/wake: the page is now rendered.
            if (!s_first_event_handled)
            {
                s_first_event_handled = true;
                WAKE_MARK("first event rendered");
                WAKE_DUMP();
            }
            if (is_refresh)
            {
                continue;
            }
        }

        if (view == VIEW_BLE_SYNC)
        {
            set_view(view_ble_sync_check_timeout(&s_last_activity_us));
        }
    }
}