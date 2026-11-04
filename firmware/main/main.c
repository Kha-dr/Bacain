#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "driver/gpio.h"

#include "app_config.h"
#include "power_mgr.h"
#include "sdcard.h"
#include "button.h"
#include "epaper_port.h"
#include "book_library.h"
#include "text_pager.h"
#include "ble_sync.h"
#include "reader_ui.h"

extern void reader_ui_set_rotation(uint8_t rotation);

#define TAG "MAIN"

static QueueHandle_t s_btn_queue = NULL;
static reader_view_t s_current_view = VIEW_MENU;
static int s_menu_index = 0;
static int s_book_index = 0;
static int s_bookmark_index = 0;
static int64_t s_last_activity_us = 0;

RTC_DATA_ATTR static reader_view_t s_rtc_last_view = VIEW_MENU;
RTC_DATA_ATTR static int s_rtc_menu_index = 0;
RTC_DATA_ATTR static int s_rtc_book_index = 0;
RTC_DATA_ATTR static int s_rtc_bookmark_index = 0;
RTC_DATA_ATTR static uint32_t s_redraw_counter = 0;
RTC_DATA_ATTR static bool s_rtc_has_sdcard = false;
RTC_DATA_ATTR static bool s_rtc_settings_cached = false;
RTC_DATA_ATTR static uint8_t s_rtc_rot = BOARD_DISPLAY_ROTATION;
RTC_DATA_ATTR static uint8_t s_rtc_btn_invert = 0;
RTC_DATA_ATTR static uint8_t s_rtc_font_size = 1;
RTC_DATA_ATTR static uint8_t s_rtc_show_hints = 1;

static void load_settings(bool force_nvs_reload) {
    if (!s_rtc_settings_cached || force_nvs_reload) {
        nvs_handle_t nvs_handle;
        if (nvs_open("settings", NVS_READWRITE, &nvs_handle) == ESP_OK) {
            if (nvs_get_u8(nvs_handle, "rotation", &s_rtc_rot) != ESP_OK) {
                s_rtc_rot = BOARD_DISPLAY_ROTATION;
            }
            if (nvs_get_u8(nvs_handle, "btn_invert", &s_rtc_btn_invert) != ESP_OK) {
                s_rtc_btn_invert = 0;
            }
            if (nvs_get_u8(nvs_handle, "font_size", &s_rtc_font_size) != ESP_OK) {
                s_rtc_font_size = 1;
            }
            if (nvs_get_u8(nvs_handle, "nav_hints", &s_rtc_show_hints) != ESP_OK) {
                s_rtc_show_hints = 1;
            }
            nvs_close(nvs_handle);
        } else {
            s_rtc_rot = BOARD_DISPLAY_ROTATION;
            s_rtc_btn_invert = 0;
            s_rtc_font_size = 1;
            s_rtc_show_hints = 1;
        }
        s_rtc_settings_cached = true;
    }
    reader_ui_set_rotation(s_rtc_rot);
    reader_ui_set_show_nav_hints(s_rtc_show_hints != 0);
    button_set_inverted(s_rtc_btn_invert != 0);
    text_pager_set_font_size(s_rtc_font_size == 0 ? TEXT_PAGER_FONT_12 : TEXT_PAGER_FONT_16);
}

static void update_activity(void) {
    s_last_activity_us = esp_timer_get_time();
}

static void enter_deep_sleep_with_state(void) {
    s_rtc_last_view = s_current_view;
    s_rtc_menu_index = s_menu_index;
    s_rtc_book_index = s_book_index;
    s_rtc_bookmark_index = s_bookmark_index;
    book_library_update_offset(text_pager_get_current_offset());
    book_library_flush_state();
    epaper_port_sleep();
    epaper_power_off();
    power_mgr_enter_deep_sleep(0);
}

static void render_reading_page(void) {
    s_redraw_counter++;
    if (APP_FULL_REFRESH_INTERVAL > 0 && s_redraw_counter >= APP_FULL_REFRESH_INTERVAL) {
        s_redraw_counter = 0;
        reader_ui_render_reading_full();
    } else {
        reader_ui_render_reading();
    }
}

static void on_ble_sync_event(ble_sync_event_t event, const void *param, void *user_data) {
    if (event == BLE_SYNC_EVT_REQUEST_REFRESH) {
        if (s_btn_queue) {
            button_event_t evt = { .button = BTN_ID_BOOT, .type = BTN_EVENT_CUSTOM_REFRESH };
            xQueueSend(s_btn_queue, &evt, 0);
        }
        return;
    }

    if (event == BLE_SYNC_EVT_REQUEST_SLEEP) {
        enter_deep_sleep_with_state();
        return;
    }

    if (s_btn_queue && s_current_view == VIEW_BLE_SYNC) {
        if (event == BLE_SYNC_EVT_CONNECTED || event == BLE_SYNC_EVT_DISCONNECTED ||
            event == BLE_SYNC_EVT_UPLOAD_COMPLETE || event == BLE_SYNC_EVT_FILE_DELETED) {
            button_event_t evt = { .button = BTN_ID_BOOT, .type = BTN_EVENT_CUSTOM_REFRESH };
            xQueueSend(s_btn_queue, &evt, 0);
        }
    }
}

void app_main(void) {
    // 1. Critical: Latch board power HIGH (GPIO 17) and cut audio rail
    power_mgr_early_init();

    // 2. Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Starting BacaIn firmware...");

    wakeup_reason_t wakeup = power_mgr_get_wakeup_cause();
    load_settings(wakeup == WAKEUP_REASON_COLD_BOOT);

    // 3. Initialize Peripherals
    esp_err_t flash_ret = flash_storage_init();
    if (flash_ret == ESP_OK) {
        ESP_LOGI(TAG, "Flash storage ready at /spiflash");
    } else {
        ESP_LOGE(TAG, "Internal Flash storage initialization failed (0x%x)", flash_ret);
    }

    if (button_get_inverted()) {
        if (wakeup == WAKEUP_REASON_BOOT_BUTTON) {
            wakeup = WAKEUP_REASON_PWR_BUTTON;
        } else if (wakeup == WAKEUP_REASON_PWR_BUTTON) {
            wakeup = WAKEUP_REASON_BOOT_BUTTON;
        }
    }
    ESP_LOGI(TAG, "Wakeup reason: %d", wakeup);

    if (wakeup == WAKEUP_REASON_COLD_BOOT) {
        ret = sdcard_init();
        if (ret == ESP_OK) {
            s_rtc_has_sdcard = true;
        } else {
            s_rtc_has_sdcard = false;
            ESP_LOGI(TAG, "MicroSD not detected, using internal Flash storage");
        }
    } else if (s_rtc_has_sdcard) {
        ret = sdcard_init();
        if (ret != ESP_OK) {
            s_rtc_has_sdcard = false;
            ESP_LOGI(TAG, "MicroSD not detected, using internal Flash storage");
        }
    }

    ESP_ERROR_CHECK(reader_ui_init(wakeup == WAKEUP_REASON_COLD_BOOT));
    ESP_ERROR_CHECK(button_init(&s_btn_queue));

    // Show splash screen with official BacaIn icon on cold boot before library scan
    if (wakeup == WAKEUP_REASON_COLD_BOOT) {
        reader_ui_render_splash("BacaIn", "Loading library...");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // 4. Initialize Reader Core
    book_library_init();
    book_library_set_storage_target(STORAGE_TARGET_FLASH);
    ble_sync_set_storage_target(STORAGE_TARGET_FLASH);
    if (wakeup == WAKEUP_REASON_COLD_BOOT) {
        book_library_scan();
    }
    text_pager_init();
    text_pager_set_font_size(s_rtc_font_size == 0 ? TEXT_PAGER_FONT_12 : TEXT_PAGER_FONT_16);

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

    if (opened) {
        if (wakeup == WAKEUP_REASON_BOOT_BUTTON || wakeup == WAKEUP_REASON_PWR_BUTTON) {
            switch (s_rtc_last_view) {
                case VIEW_READING:
                    s_current_view = VIEW_READING;
                    if (wakeup == WAKEUP_REASON_BOOT_BUTTON) {
                        text_pager_prev_page();
                    } else if (wakeup == WAKEUP_REASON_PWR_BUTTON) {
                        text_pager_next_page();
                    }
                    book_library_update_offset(text_pager_get_current_offset());
                    render_reading_page();
                    break;

                case VIEW_MENU:
                    s_current_view = VIEW_MENU;
                    s_menu_index = s_rtc_menu_index;
                    if (wakeup == WAKEUP_REASON_BOOT_BUTTON) {
                        s_menu_index = (s_menu_index - 1 + 7) % 7;
                    } else if (wakeup == WAKEUP_REASON_PWR_BUTTON) {
                        s_menu_index = (s_menu_index + 1) % 7;
                    }
                    reader_ui_render_menu(s_menu_index);
                    break;

                case VIEW_BOOK_LIST:
                    s_current_view = VIEW_BOOK_LIST;
                    s_book_index = s_rtc_book_index;
                    {
                        int count = (int)book_library_get_count();
                        if (count > 0) {
                            if (wakeup == WAKEUP_REASON_BOOT_BUTTON) {
                                s_book_index = (s_book_index - 1 + count) % count;
                            } else if (wakeup == WAKEUP_REASON_PWR_BUTTON) {
                                s_book_index = (s_book_index + 1) % count;
                            }
                        }
                    }
                    reader_ui_render_book_list(s_book_index);
                    break;

                case VIEW_BOOKMARKS:
                    s_current_view = VIEW_BOOKMARKS;
                    s_bookmark_index = s_rtc_bookmark_index;
                    {
                        const reading_state_t *st = book_library_get_state();
                        int count = (st != NULL) ? (int)st->bookmark_count : 0;
                        if (count > 0) {
                            if (wakeup == WAKEUP_REASON_BOOT_BUTTON) {
                                s_bookmark_index = (s_bookmark_index - 1 + count) % count;
                            } else if (wakeup == WAKEUP_REASON_PWR_BUTTON) {
                                s_bookmark_index = (s_bookmark_index + 1) % count;
                            }
                        }
                    }
                    reader_ui_render_bookmarks(s_bookmark_index);
                    break;

                case VIEW_NAV_HINTS:
                    s_current_view = VIEW_NAV_HINTS;
                    reader_ui_render_nav_hints();
                    break;

                case VIEW_BLE_SYNC:
                    s_current_view = VIEW_BLE_SYNC;
                    ble_sync_start();
                    reader_ui_render_ble_sync(ble_sync_is_connected(), ble_sync_get_bytes_received());
                    break;

                default:
                    s_rtc_last_view = VIEW_MENU;
                    s_rtc_menu_index = 0;
                    s_current_view = VIEW_MENU;
                    s_menu_index = 0;
                    reader_ui_render_menu(s_menu_index);
                    break;
            }
        } else {
            s_rtc_last_view = VIEW_MENU;
            s_rtc_menu_index = 0;
            s_current_view = VIEW_MENU;
            s_menu_index = 0;
            reader_ui_render_menu(s_menu_index);
        }
        ESP_LOGI(TAG, "Display force-render initiated successfully.");
    } else {
        s_rtc_last_view = VIEW_MENU;
        s_rtc_menu_index = 0;
        s_current_view = VIEW_BLE_SYNC;
        ble_sync_start();
        reader_ui_render_ble_sync(false, 0);
        ESP_LOGI(TAG, "Display force-render initiated successfully.");
    }

    if (wakeup == WAKEUP_REASON_BOOT_BUTTON || wakeup == WAKEUP_REASON_PWR_BUTTON) {
        while (gpio_get_level(GPIO_NUM_0) == 0 || gpio_get_level(GPIO_NUM_18) == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (s_btn_queue) {
            xQueueReset(s_btn_queue);
        }
    }

    update_activity();

    // 7. Event Loop
    button_event_t evt;
    for (;;) {
        TickType_t wait_ticks = pdMS_TO_TICKS(5000);
        if (s_current_view != VIEW_BLE_SYNC) {
            uint16_t timeout_sec = ble_sync_get_sleep_timeout();
            if (timeout_sec > 0) {
                int64_t elapsed_us = esp_timer_get_time() - s_last_activity_us;
                int64_t total_us = (int64_t)timeout_sec * 1000000LL;
                if (elapsed_us >= total_us) {
                    ESP_LOGI(TAG, "Inactivity timeout reached. Entering deep sleep...");
                    enter_deep_sleep_with_state();
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
            if (evt.type == BTN_EVENT_CUSTOM_REFRESH) {
                if (s_current_view == VIEW_READING) {
                    reader_ui_render_reading_force_full();
                } else if (s_current_view == VIEW_BLE_SYNC) {
                    reader_ui_render_ble_sync(ble_sync_is_connected(), ble_sync_get_bytes_received());
                } else if (s_current_view == VIEW_BOOKMARKS) {
                    reader_ui_render_bookmarks(s_bookmark_index);
                } else if (s_current_view == VIEW_MENU) {
                    reader_ui_render_menu(s_menu_index);
                } else if (s_current_view == VIEW_BOOK_LIST) {
                    reader_ui_render_book_list(s_book_index);
                } else if (s_current_view == VIEW_NAV_HINTS) {
                    reader_ui_render_nav_hints();
                }
                continue;
            }

            switch (s_current_view) {
                case VIEW_READING:
                    if (evt.type == BTN_LONG_PRESS) {
                        s_current_view = VIEW_MENU;
                        s_menu_index = 0;
                        reader_ui_render_menu(s_menu_index);
                    } else if (evt.button == BTN_ID_BOOT) {
                        if (evt.type == BTN_CLICK) {
                            if (text_pager_prev_page() == ESP_OK) {
                                book_library_update_offset(text_pager_get_current_offset());
                                render_reading_page();
                            }
                        } else if (evt.type == BTN_DOUBLE_CLICK) {
                            size_t off = text_pager_get_current_offset();
                            if (book_library_is_bookmarked(off)) {
                                book_library_remove_bookmark(off);
                            } else {
                                book_library_add_bookmark(off);
                            }
                            reader_ui_render_reading();
                        }
                    } else if (evt.button == BTN_ID_PWR) {
                        if (evt.type == BTN_CLICK) {
                            if (text_pager_next_page() == ESP_OK) {
                                book_library_update_offset(text_pager_get_current_offset());
                                render_reading_page();
                            }
                        } else if (evt.type == BTN_DOUBLE_CLICK) {
                            s_current_view = VIEW_BOOKMARKS;
                            s_bookmark_index = 0;
                            reader_ui_render_bookmarks(s_bookmark_index);
                        }
                    }
                    break;

                case VIEW_MENU:
                    if (evt.type == BTN_CLICK) {
                        if (evt.button == BTN_ID_BOOT) {
                            s_menu_index = (s_menu_index - 1 + 7) % 7;
                            reader_ui_render_menu(s_menu_index);
                        } else if (evt.button == BTN_ID_PWR) {
                            s_menu_index = (s_menu_index + 1) % 7;
                            reader_ui_render_menu(s_menu_index);
                        }
                    } else if (evt.type == BTN_DOUBLE_CLICK) {
                        if (s_menu_index == 0) { // Continue Reading
                            s_current_view = VIEW_READING;
                            reader_ui_render_reading();
                        } else if (s_menu_index == 1) { // Book Library
                            book_library_scan();
                            s_current_view = VIEW_BOOK_LIST;
                            s_book_index = 0;
                            reader_ui_render_book_list(s_book_index);
                        } else if (s_menu_index == 2) { // Font Size Toggle
                            text_pager_font_size_t f = text_pager_get_font_size();
                            text_pager_font_size_t new_font = (f == TEXT_PAGER_FONT_16 ? TEXT_PAGER_FONT_12 : TEXT_PAGER_FONT_16);
                            text_pager_set_font_size(new_font);
                            s_rtc_font_size = (new_font == TEXT_PAGER_FONT_12 ? 0 : 1);
                            nvs_handle_t h;
                            if (nvs_open("settings", NVS_READWRITE, &h) == ESP_OK) {
                                nvs_set_u8(h, "font_size", s_rtc_font_size);
                                nvs_commit(h);
                                nvs_close(h);
                            } else {
                                ESP_LOGE(TAG, "Failed to open NVS settings to save font_size");
                            }
                            reader_ui_render_menu(s_menu_index);
                        } else if (s_menu_index == 3) { // Sync BLE
                            book_library_scan();
                            s_current_view = VIEW_BLE_SYNC;
                            ble_sync_start();
                            reader_ui_render_ble_sync(ble_sync_is_connected(), ble_sync_get_bytes_received());
                        } else if (s_menu_index == 4) { // Nav Hints Screen
                            s_current_view = VIEW_NAV_HINTS;
                            reader_ui_render_nav_hints();
                        } else if (s_menu_index == 5) { // Refresh Screen
                            s_redraw_counter = 0;
                            reader_ui_render_menu_full(s_menu_index);
                        } else if (s_menu_index == 6) { // Power Off
                            reader_ui_render_sleeping();
                            enter_deep_sleep_with_state();
                        }
                    }
                    break;

                case VIEW_BOOK_LIST:
                    {
                        size_t count = book_library_get_count();
                        if (evt.type == BTN_CLICK) {
                            if (count > 0) {
                                if (evt.button == BTN_ID_BOOT) {
                                    s_book_index = (s_book_index - 1 + count) % count;
                                } else if (evt.button == BTN_ID_PWR) {
                                    s_book_index = (s_book_index + 1) % count;
                                }
                                reader_ui_render_book_list(s_book_index);
                            }
                        } else if (evt.type == BTN_DOUBLE_CLICK) {
                            if (count > 0) {
                                const book_info_t *book = book_library_get_book(s_book_index);
                                if (book) {
                                    if (book->type == LIBRARY_ENTRY_UP) {
                                        book_library_navigate_up();
                                        s_book_index = 0;
                                        reader_ui_render_book_list(s_book_index);
                                    } else if (book->type == LIBRARY_ENTRY_DIR) {
                                        book_library_enter_dir(book->filename);
                                        s_book_index = 0;
                                        reader_ui_render_book_list(s_book_index);
                                    } else if (book->type == LIBRARY_ENTRY_FILE) {
                                        if (text_pager_open_book(book->path, 0) == ESP_OK) {
                                            book_library_set_active_book(book->path, true);
                                            s_current_view = VIEW_READING;
                                            reader_ui_render_reading();
                                        }
                                    }
                                }
                            }
                        } else if (evt.type == BTN_LONG_PRESS) {
                            s_current_view = VIEW_MENU;
                            reader_ui_render_menu(s_menu_index);
                        }
                    }
                    break;

                case VIEW_BLE_SYNC:
                    if (evt.type == BTN_LONG_PRESS) {
                        ble_sync_stop();
                        book_library_invalidate_cache();
                        book_library_scan();
                        load_settings(true);
                        s_current_view = VIEW_MENU;
                        reader_ui_render_menu(s_menu_index);
                    }
                    break;

                case VIEW_BOOKMARKS:
                    {
                        const reading_state_t *st = book_library_get_state();
                        size_t count = (st != NULL) ? st->bookmark_count : 0;
                        if (evt.type == BTN_CLICK) {
                            if (count > 0) {
                                if (evt.button == BTN_ID_BOOT) {
                                    s_bookmark_index = (s_bookmark_index - 1 + count) % count;
                                } else if (evt.button == BTN_ID_PWR) {
                                    s_bookmark_index = (s_bookmark_index + 1) % count;
                                }
                                reader_ui_render_bookmarks(s_bookmark_index);
                            }
                        } else if (evt.type == BTN_DOUBLE_CLICK) {
                            if (count > 0 && s_bookmark_index >= 0 && s_bookmark_index < (int)count) {
                                size_t target_offset = st->bookmarks[s_bookmark_index];
                                text_pager_open_offset(target_offset);
                                book_library_update_offset(target_offset);
                            }
                            s_current_view = VIEW_READING;
                            reader_ui_render_reading();
                        } else if (evt.type == BTN_LONG_PRESS) {
                            s_current_view = VIEW_READING;
                            reader_ui_render_reading();
                        }
                    }
                    break;

                case VIEW_NAV_HINTS:
                    if (evt.type == BTN_DOUBLE_CLICK) {
                        bool new_hints = !reader_ui_get_show_nav_hints();
                        reader_ui_set_show_nav_hints(new_hints);
                        s_rtc_show_hints = new_hints ? 1 : 0;
                        nvs_handle_t h;
                        if (nvs_open("settings", NVS_READWRITE, &h) == ESP_OK) {
                            nvs_set_u8(h, "nav_hints", s_rtc_show_hints);
                            nvs_commit(h);
                            nvs_close(h);
                        } else {
                            ESP_LOGE(TAG, "Failed to open NVS settings to save nav_hints");
                        }
                        reader_ui_render_nav_hints();
                    } else if (evt.type == BTN_LONG_PRESS) {
                        s_current_view = VIEW_MENU;
                        reader_ui_render_menu(s_menu_index);
                    }
                    break;
            }
        }

        if (s_current_view == VIEW_BLE_SYNC) {
            s_last_activity_us = esp_timer_get_time();
        } else {
            int64_t elapsed_sec = (esp_timer_get_time() - s_last_activity_us) / 1000000LL;
            uint16_t timeout_sec = ble_sync_get_sleep_timeout();
            if (timeout_sec > 0 && elapsed_sec >= timeout_sec) {
                ESP_LOGI(TAG, "Inactivity timeout (%llds >= %us). Entering deep sleep...", elapsed_sec, timeout_sec);
                enter_deep_sleep_with_state();
            }
        }
    }
}
