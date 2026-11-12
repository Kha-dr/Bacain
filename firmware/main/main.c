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
#include "esp_pm.h"
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
#include "status_led.h"

#include "app_settings.h" /* CHANGED: was reader_ui.h */
#include "app_links.h"    /* CHANGED */
#include "ui_display.h"   /* CHANGED */
#include "ui_screens.h"   /* CHANGED */

#include "views.h"
#include "view_menu.h"
#include "view_ble_sync.h"
#if VIEWS_HAS_MUSHAF /* CHANGED: single mushaf switch */
#include "view_mushaf.h"
#endif

#define TAG "MAIN"

#define LOOP_MAX_WAIT_MS 60000 // keeps pdMS_TO_TICKS within 32 bits; bounds staleness of BLE-changed timeout
#define BLE_RX_REFRESH_MIN_US 500000

static QueueHandle_t s_btn_queue = NULL;
static atomic_int s_current_view = VIEW_MENU; // written by main task, read by BLE task
static int64_t s_last_activity_us = 0;
static bool s_nvs_ready = false; // main task only

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

static void prepare_power_down(void)
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
    if (text_pager_is_book_open())
    {
        book_library_update_offset(text_pager_get_current_offset());
    }
    book_library_flush_state(); // no write if state not dirty
#if VIEWS_HAS_MUSHAF            /* CHANGED */
    view_mushaf_save_before_sleep();
#endif
    status_led_trigger(STATUS_LED_EVT_SLEEP); // synchronous 150 ms; no-op if LED disabled
    epaper_port_sleep();
    epaper_power_off();
    esp_deep_sleep_disable_rom_logging(); // silences ROM banner on next wake only; no eFuse burn
}

void app_enter_deep_sleep(void)
{
    prepare_power_down();
    power_mgr_enter_deep_sleep(0);
}

static void app_power_off(void)
{
    prepare_power_down();
    power_mgr_power_off(); // falls back to deep sleep if the rail stays up (USB)
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
#if VIEWS_HAS_MUSHAF /* CHANGED */
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

static bool inactivity_expired(void)
{
    uint16_t timeout_sec = app_settings_get()->sleep_timeout_sec;
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
    uint16_t timeout_sec = app_settings_get()->sleep_timeout_sec;
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
        uint16_t timeout_sec = app_settings_get()->sleep_timeout_sec;
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
    ensure_nvs();

    wakeup_reason_t raw_wakeup = power_mgr_get_wakeup_cause();
    bool is_cold_boot = (raw_wakeup == WAKEUP_REASON_COLD_BOOT ||
                         esp_reset_reason() != ESP_RST_DEEPSLEEP);

    // 2. Settings (NVS -> RTC cache, no side effects). Read before the LED wake blink.
    app_settings_read(is_cold_boot);
    status_led_init(app_settings_get()->status_led_enabled);
    status_led_trigger(STATUS_LED_EVT_WAKEUP); // background task, returns immediately

    // 3. Buttons: the wake stub's classified gesture is pushed to the queue here
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

    // 4. Pager, module links, apply settings (already read in step 2).
    //    text_pager_init() before apply: apply sets the pager font.
    //    apply before ui_display_init(): it stores the rotation used to build the canvas.
    text_pager_init();
    app_links_init();
    app_settings_apply();

    ESP_LOGI(TAG, "Starting BacaIn firmware (wakeup=%d, cold=%d)...", raw_wakeup, is_cold_boot);

    // 4. Storage
    esp_err_t flash_ret = flash_storage_init();
    if (flash_ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Internal flash storage init failed (0x%x)", flash_ret);
    }

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

    // 5. Display
    ESP_ERROR_CHECK(ui_display_init(is_cold_boot)); /* CHANGED: was reader_ui_init() */

    // Protect all board output lines from floating in light sleep before enabling PM
    board_light_sleep_pins_init();

    esp_pm_config_t pm = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 80, // APB stays 80 MHz: SPI/UART unaffected
        .light_sleep_enable = true,
    };
    esp_err_t pm_err = esp_pm_configure(&pm);
    if (pm_err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_pm_configure failed (0x%x), running without light sleep", pm_err);
    }

    // Show splash before the slow library scan so the screen updates sooner
    if (is_cold_boot)
    {
        ui_screen_splash(NULL, NULL); /* CHANGED: was reader_ui_render_splash() */
    }

    // 6. Reader core
    book_library_init();
#if VIEWS_HAS_MUSHAF /* CHANGED */
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

    // 8. Restore reading state metadata
    reading_state_t state;
    (void)book_library_load_state(&state);

    // 9. Initial view
    if (is_cold_boot)
    {
        wait_for_splash_dismiss();
        set_view(VIEW_MENU);
        view_menu_set_index(0);
        views_render(VIEW_MENU, false); /* CHANGED: was view_menu_render(false) */
    }
    else
    {
        reader_view_t view = views_dispatch_wake(s_rtc_last_view);
        bool is_button_wake = (raw_wakeup == WAKEUP_REASON_BOOT_BUTTON ||
                               raw_wakeup == WAKEUP_REASON_PWR_BUTTON);
        if (!is_button_wake)
        {
            // Defensive: power_mgr currently enables only EXT1 button wake
            const button_event_t refresh_evt = {.button = BTN_ID_BOOT, .type = BTN_EVENT_CUSTOM_REFRESH};
            view = views_dispatch_event(view, refresh_evt);
            if (view == VIEW_REQUEST_POWER_OFF)
            {
                app_power_off();
            }
        }
        set_view(view);
    }

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
            view = views_dispatch_event(view, evt);
            if (view == VIEW_REQUEST_POWER_OFF)
            {
                app_power_off();
            }
            set_view(view);

            if (is_refresh)
            {
                continue;
            }
        }

        if (view == VIEW_BLE_SYNC)
        {
            /* Returns the next view; on timeout it has already navigated to the menu. */
            set_view(view_ble_sync_check_timeout(&s_last_activity_us));
        }
    }
}