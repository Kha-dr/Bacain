#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_system.h"
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

static QueueHandle_t s_btn_queue = NULL;
static reader_view_t s_current_view = VIEW_MENU;
static int64_t s_last_activity_us = 0;

RTC_DATA_ATTR static reader_view_t s_rtc_last_view = VIEW_MENU;
RTC_DATA_ATTR static bool s_rtc_has_sdcard = false;
RTC_DATA_ATTR static storage_target_t s_rtc_storage_target = STORAGE_TARGET_FLASH;

static void update_activity(void) {
    s_last_activity_us = esp_timer_get_time();
}

void app_enter_deep_sleep(void) {
    s_rtc_last_view = s_current_view;
    reading_state_t state;
    if (book_library_get_current_state(&state) == ESP_OK && state.active_book_path[0] != '\0') {
        if (strncmp(state.active_book_path, "/sdcard", 7) == 0) {
            s_rtc_storage_target = STORAGE_TARGET_SDCARD;
        } else {
            s_rtc_storage_target = STORAGE_TARGET_FLASH;
        }
    } else {
        s_rtc_storage_target = book_library_get_storage_target();
    }
    book_library_update_offset(text_pager_get_current_offset());
    book_library_flush_state();
#if CONFIG_BOARD_MUSHAF_ENABLE
    view_mushaf_save_before_sleep();
#endif
    status_led_trigger(STATUS_LED_EVT_SLEEP);
    epaper_port_sleep();
    epaper_power_off();
    power_mgr_enter_deep_sleep(0);
}

static void on_ble_sync_event(ble_sync_event_t event, const void *param, void *user_data) {
    (void)param;
    (void)user_data;

    if (event == BLE_SYNC_EVT_REQUEST_REFRESH) {
        if (s_btn_queue) {
            button_event_t evt = { .button = BTN_ID_BOOT, .type = BTN_EVENT_CUSTOM_REFRESH };
            xQueueSend(s_btn_queue, &evt, 0);
        }
        return;
    }

    if (event == BLE_SYNC_EVT_REQUEST_SLEEP) {
        app_enter_deep_sleep();
        return;
    }

    if (s_btn_queue && (s_current_view == VIEW_BLE_SYNC
#if CONFIG_BOARD_MUSHAF_ENABLE
        || s_current_view == VIEW_MUSHAF_MENU
#endif
    )) {
        bool should_refresh = false;
        if (event == BLE_SYNC_EVT_CONNECTED || event == BLE_SYNC_EVT_DISCONNECTED ||
            event == BLE_SYNC_EVT_UPLOAD_COMPLETE || event == BLE_SYNC_EVT_FILE_DELETED) {
            should_refresh = true;
        } else if (s_current_view == VIEW_BLE_SYNC) {
            if (event == BLE_SYNC_EVT_UPLOAD_START || event == BLE_SYNC_EVT_UPLOAD_ABORTED) {
                should_refresh = true;
            } else if (event == BLE_SYNC_EVT_UPLOAD_PROGRESS) {
                static int64_t s_last_rx_refresh_us = 0;
                int64_t now_us = esp_timer_get_time();
                if (now_us - s_last_rx_refresh_us >= 500000) {
                    s_last_rx_refresh_us = now_us;
                    should_refresh = true;
                }
            }
        }
        if (should_refresh) {
            button_event_t evt = { .button = BTN_ID_BOOT, .type = BTN_EVENT_CUSTOM_REFRESH };
            xQueueSend(s_btn_queue, &evt, 0);
        }
    }
}

static reader_view_t dispatch_wake_view(reader_view_t last_view) {
    switch (last_view) {
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

static reader_view_t dispatch_view_event(reader_view_t view, button_event_t evt) {
    switch (view) {
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

void app_main(void) {
    // 1. Critical: Latch board power and cut audio rail
    power_mgr_early_init();
    status_led_init();
    esp_reset_reason_t rst_reason = esp_reset_reason();
    if (rst_reason != ESP_RST_DEEPSLEEP) {
        status_led_trigger(STATUS_LED_EVT_WAKEUP);
    }

    // 2. Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Starting BacaIn firmware...");

    wakeup_reason_t raw_wakeup = power_mgr_get_wakeup_cause();
    bool is_cold_boot = (raw_wakeup == WAKEUP_REASON_COLD_BOOT || esp_reset_reason() != ESP_RST_DEEPSLEEP);
    app_settings_load(is_cold_boot);

    // 3. Initialize Peripherals
    esp_err_t flash_ret = flash_storage_init();
    if (flash_ret == ESP_OK) {
        ESP_LOGI(TAG, "Flash storage ready at /spiflash");
    } else {
        ESP_LOGE(TAG, "Internal Flash storage initialization failed (0x%x)", flash_ret);
    }

    ESP_LOGI(TAG, "Wakeup reason: %d", raw_wakeup);

    if (is_cold_boot) {
        ret = sdcard_init();
        if (ret == ESP_OK) {
            s_rtc_has_sdcard = true;
        } else {
            s_rtc_has_sdcard = false;
            ESP_LOGI(TAG, "MicroSD not detected, using internal Flash storage");
        }
    } else if (s_rtc_has_sdcard && s_rtc_storage_target == STORAGE_TARGET_SDCARD) {
        ret = sdcard_init();
        if (ret != ESP_OK) {
            s_rtc_has_sdcard = false;
            ESP_LOGI(TAG, "MicroSD not detected, using internal Flash storage");
        }
    }

    ESP_ERROR_CHECK(reader_ui_init(is_cold_boot));
    if (!is_cold_boot) {
        if (raw_wakeup == WAKEUP_REASON_BOOT_BUTTON) {
            button_seed_wakeup(BTN_ID_BOOT);
        } else if (raw_wakeup == WAKEUP_REASON_PWR_BUTTON) {
            button_seed_wakeup(BTN_ID_PWR);
        }
    }
    ESP_ERROR_CHECK(button_init(&s_btn_queue));

    // 4. Initialize Reader Core
    book_library_init();
#if CONFIG_BOARD_MUSHAF_ENABLE
    view_mushaf_init(is_cold_boot);
#endif
    storage_target_t active_target = (s_rtc_has_sdcard && s_rtc_storage_target == STORAGE_TARGET_SDCARD) ? STORAGE_TARGET_SDCARD : STORAGE_TARGET_FLASH;
    book_library_set_storage_target(active_target);
    ble_sync_set_storage_target(active_target);
    if (is_cold_boot) {
        book_library_scan();
    }
    text_pager_init();
    app_settings_load(false);

    // 5. Register BLE sync callback
    ble_sync_register_callback(on_ble_sync_event, NULL);

    // 6. Restore reading state or open first available book
    reading_state_t state;
    bool opened = false;
    if (book_library_load_state(&state) == ESP_OK && strlen(state.active_book_path) > 0) {
        if (text_pager_open_book(state.active_book_path, state.byte_offset) == ESP_OK) {
            opened = true;
        }
    }

    if (!opened) {
        size_t count = book_library_get_count();
        for (size_t i = 0; i < count; i++) {
            const book_info_t *first_book = book_library_get_book(i);
            if (first_book && first_book->type == LIBRARY_ENTRY_FILE) {
                if (text_pager_open_book(first_book->path, 0) == ESP_OK) {
                    book_library_set_active_book(first_book->path, true);
                    opened = true;
                    break;
                }
            }
        }
    }

    if (is_cold_boot) {
        reader_ui_render_splash(NULL, NULL);

        button_event_t boot_evt;
        xQueueReceive(s_btn_queue, &boot_evt, portMAX_DELAY);
        while (gpio_get_level((gpio_num_t)BOARD_BTN_UP_PIN) == 0 || gpio_get_level((gpio_num_t)BOARD_BTN_DOWN_PIN) == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (s_btn_queue) {
            xQueueReset(s_btn_queue);
        }

        s_current_view = VIEW_MENU;
        view_menu_set_index(0);
        view_menu_render(true);
        ESP_LOGI(TAG, "Cold boot started into view %d", s_current_view);
    } else {
        s_current_view = dispatch_wake_view(s_rtc_last_view);
        bool is_button_wake = (raw_wakeup == WAKEUP_REASON_BOOT_BUTTON || raw_wakeup == WAKEUP_REASON_PWR_BUTTON);
        if (!is_button_wake) {
            button_event_t refresh_evt = { .button = BTN_ID_BOOT, .type = BTN_EVENT_CUSTOM_REFRESH };
            s_current_view = dispatch_view_event(s_current_view, refresh_evt);
        }
        s_rtc_last_view = s_current_view;
        ESP_LOGI(TAG, "Wake view %d restored.", s_current_view);
    }

    update_activity();

    // 7. Event Loop
    button_event_t evt;
    for (;;) {
        TickType_t wait_ticks = pdMS_TO_TICKS(5000);
        if (s_current_view == VIEW_BLE_SYNC) {
            wait_ticks = view_ble_sync_get_wait_ticks();
        } else {
            uint16_t timeout_sec = ble_sync_get_sleep_timeout();
            if (timeout_sec > 0) {
                int64_t elapsed_us = esp_timer_get_time() - s_last_activity_us;
                int64_t total_us = (int64_t)timeout_sec * 1000000LL;
                if (elapsed_us >= total_us) {
                    ESP_LOGI(TAG, "Inactivity timeout reached. Entering deep sleep...");
                    app_enter_deep_sleep();
                }
                int64_t remaining_ms = (total_us - elapsed_us) / 1000LL;
                if (remaining_ms <= 0) {
                    wait_ticks = 0;
                } else if (remaining_ms > 5000) {
                    wait_ticks = pdMS_TO_TICKS(5000);
                } else {
                    wait_ticks = pdMS_TO_TICKS(remaining_ms);
                }
            }
        }

        if (xQueueReceive(s_btn_queue, &evt, wait_ticks) == pdTRUE) {
            update_activity();
            ESP_LOGI(TAG, "Btn evt: button=%d, type=%d, view=%d", evt.button, evt.type, s_current_view);
            s_current_view = dispatch_view_event(s_current_view, evt);
            s_rtc_last_view = s_current_view;
            if (evt.type == BTN_EVENT_CUSTOM_REFRESH) {
                continue;
            }
        }

        if (s_current_view == VIEW_BLE_SYNC) {
            s_current_view = view_ble_sync_check_timeout(&s_last_activity_us);
            s_rtc_last_view = s_current_view;
        } else {
            int64_t elapsed_sec = (esp_timer_get_time() - s_last_activity_us) / 1000000LL;
            uint16_t timeout_sec = ble_sync_get_sleep_timeout();
            if (timeout_sec > 0 && elapsed_sec >= timeout_sec) {
                ESP_LOGI(TAG, "Inactivity timeout (%llds >= %us). Entering deep sleep...", elapsed_sec, timeout_sec);
                app_enter_deep_sleep();
            }
        }
    }
}
