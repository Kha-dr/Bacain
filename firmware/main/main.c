#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
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
static reader_view_t s_current_view = VIEW_READING;
static int s_menu_index = 0;
static int s_book_index = 0;
static int s_bookmark_index = 0;
static int64_t s_last_activity_us = 0;

static void update_activity(void) {
    s_last_activity_us = esp_timer_get_time();
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
        if (s_current_view != VIEW_READING) {
            reader_ui_render_sleeping();
        }
        book_library_update_offset(text_pager_get_current_offset());
        book_library_flush_state();
        vTaskDelay(pdMS_TO_TICKS(500));
        power_mgr_enter_deep_sleep(0);
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

    nvs_handle_t nvs_handle;
    uint8_t rot = BOARD_DISPLAY_ROTATION;
    uint8_t btn_invert = 0;
    if (nvs_open("settings", NVS_READWRITE, &nvs_handle) == ESP_OK) {
        if (nvs_get_u8(nvs_handle, "rotation", &rot) != ESP_OK) {
            rot = BOARD_DISPLAY_ROTATION;
        }
        if (nvs_get_u8(nvs_handle, "btn_invert", &btn_invert) != ESP_OK) {
            btn_invert = 0;
        }
        nvs_close(nvs_handle);
    }
    reader_ui_set_rotation(rot);
    button_set_inverted(btn_invert != 0);

    // 3. Initialize Peripherals
    esp_err_t flash_ret = flash_storage_init();
    if (flash_ret == ESP_OK) {
        ESP_LOGI(TAG, "Flash storage ready at /spiflash");
    } else {
        ESP_LOGE(TAG, "Internal Flash storage initialization failed (0x%x)", flash_ret);
    }

    ret = sdcard_init();
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "MicroSD not detected, using internal Flash storage");
    }

    ESP_ERROR_CHECK(reader_ui_init());
    ESP_ERROR_CHECK(button_init(&s_btn_queue));

    wakeup_reason_t wakeup = power_mgr_get_wakeup_cause();
    if (button_get_inverted()) {
        if (wakeup == WAKEUP_REASON_BOOT_BUTTON) {
            wakeup = WAKEUP_REASON_PWR_BUTTON;
        } else if (wakeup == WAKEUP_REASON_PWR_BUTTON) {
            wakeup = WAKEUP_REASON_BOOT_BUTTON;
        }
    }
    ESP_LOGI(TAG, "Wakeup reason: %d", wakeup);

    // Show splash screen with official BacaIn icon on cold boot before library scan
    if (wakeup == WAKEUP_REASON_NONE) {
        reader_ui_render_splash("BacaIn", "Loading library...");
    }

    // 4. Initialize Reader Core
    book_library_init();
    book_library_set_storage_target(STORAGE_TARGET_FLASH);
    ble_sync_set_storage_target(STORAGE_TARGET_FLASH);
    book_library_scan();
    text_pager_init();

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
        s_current_view = VIEW_READING;
        if (wakeup == WAKEUP_REASON_BOOT_BUTTON) {
            text_pager_prev_page();
            book_library_update_offset(text_pager_get_current_offset());
        } else if (wakeup == WAKEUP_REASON_PWR_BUTTON) {
            text_pager_next_page();
            book_library_update_offset(text_pager_get_current_offset());
        }
        reader_ui_render_reading();
        ESP_LOGI(TAG, "Display force-render initiated successfully.");

        if (wakeup == WAKEUP_REASON_BOOT_BUTTON || wakeup == WAKEUP_REASON_PWR_BUTTON) {
            while (gpio_get_level(GPIO_NUM_0) == 0 || gpio_get_level(GPIO_NUM_18) == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            if (s_btn_queue) {
                xQueueReset(s_btn_queue);
            }
        }
    } else {
        s_current_view = VIEW_BLE_SYNC;
        ble_sync_start();
        reader_ui_render_ble_sync(false, 0);
        ESP_LOGI(TAG, "Display force-render initiated successfully.");
    }

    update_activity();

    // 7. Event Loop
    button_event_t evt;
    for (;;) {
        // Wait for button event with 500ms timeout to monitor sleep inactivity
        if (xQueueReceive(s_btn_queue, &evt, pdMS_TO_TICKS(500)) == pdTRUE) {
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
                }
                continue;
            }

            switch (s_current_view) {
                case VIEW_READING:
                    if (evt.button == BTN_ID_BOOT) {
                        if (evt.type == BTN_CLICK) {
                            if (text_pager_prev_page() == ESP_OK) {
                                reader_ui_render_reading();
                                book_library_update_offset(text_pager_get_current_offset());
                            }
                        } else if (evt.type == BTN_DOUBLE_CLICK) {
                            size_t off = text_pager_get_current_offset();
                            if (book_library_is_bookmarked(off)) {
                                book_library_remove_bookmark(off);
                            } else {
                                book_library_add_bookmark(off);
                            }
                            reader_ui_render_reading();
                        } else if (evt.type == BTN_LONG_PRESS) {
                            s_current_view = VIEW_MENU;
                            s_menu_index = 0;
                            reader_ui_render_menu(s_menu_index);
                        }
                    } else if (evt.button == BTN_ID_PWR) {
                        if (evt.type == BTN_CLICK) {
                            if (text_pager_next_page() == ESP_OK) {
                                reader_ui_render_reading();
                                book_library_update_offset(text_pager_get_current_offset());
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
                            s_menu_index = (s_menu_index - 1 + 6) % 6;
                            reader_ui_render_menu(s_menu_index);
                        } else if (evt.button == BTN_ID_PWR) {
                            s_menu_index = (s_menu_index + 1) % 6;
                            reader_ui_render_menu(s_menu_index);
                        }
                    } else if (evt.type == BTN_DOUBLE_CLICK) {
                        if (s_menu_index == 0) { // Continue Reading
                            s_current_view = VIEW_READING;
                            reader_ui_render_reading();
                        } else if (s_menu_index == 1) { // Book Library
                            s_current_view = VIEW_BOOK_LIST;
                            s_book_index = 0;
                            reader_ui_render_book_list(s_book_index);
                        } else if (s_menu_index == 2) { // Font Size Toggle
                            text_pager_font_size_t f = text_pager_get_font_size();
                            text_pager_set_font_size(f == TEXT_PAGER_FONT_16 ? TEXT_PAGER_FONT_12 : TEXT_PAGER_FONT_16);
                            reader_ui_render_menu(s_menu_index);
                        } else if (s_menu_index == 3) { // Sync BLE
                            s_current_view = VIEW_BLE_SYNC;
                            ble_sync_start();
                            reader_ui_render_ble_sync(ble_sync_is_connected(), ble_sync_get_bytes_received());
                        } else if (s_menu_index == 4) { // Nav Hints Toggle
                            reader_ui_set_show_nav_hints(!reader_ui_get_show_nav_hints());
                            reader_ui_render_menu(s_menu_index);
                        } else if (s_menu_index == 5) { // Power Off / Sleep
                            reader_ui_render_sleeping();
                            book_library_update_offset(text_pager_get_current_offset());
                            book_library_flush_state();
                            vTaskDelay(pdMS_TO_TICKS(500));
                            power_mgr_enter_deep_sleep(0);
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
                            if (evt.button == BTN_ID_BOOT) {
                                s_current_view = VIEW_MENU;
                                reader_ui_render_menu(s_menu_index);
                            } else if (evt.button == BTN_ID_PWR) {
                                reader_ui_render_sleeping();
                                book_library_update_offset(text_pager_get_current_offset());
                                book_library_flush_state();
                                vTaskDelay(pdMS_TO_TICKS(500));
                                power_mgr_enter_deep_sleep(0);
                            }
                        }
                    }
                    break;

                case VIEW_BLE_SYNC:
                    if (evt.button == BTN_ID_BOOT && evt.type == BTN_LONG_PRESS) {
                        ble_sync_stop();
                        book_library_scan();
                        const text_pager_page_t *p = text_pager_get_current_page();
                        if (!p && book_library_get_count() > 0) {
                            reading_state_t st;
                            if (book_library_load_state(&st) == ESP_OK && strlen(st.active_book_path) > 0) {
                                text_pager_open_book(st.active_book_path, st.byte_offset);
                            } else {
                                size_t bcount = book_library_get_count();
                                for (size_t bi = 0; bi < bcount; bi++) {
                                    const book_info_t *b = book_library_get_book(bi);
                                    if (b && b->type == LIBRARY_ENTRY_FILE) {
                                        if (text_pager_open_book(b->path, 0) == ESP_OK) {
                                            book_library_set_active_book(b->path, true);
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        if (book_library_get_count() > 0) {
                            s_current_view = VIEW_READING;
                            reader_ui_render_reading();
                        } else {
                            s_current_view = VIEW_BLE_SYNC;
                            ble_sync_start();
                            reader_ui_render_ble_sync(false, 0);
                        }
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
            }
        }

        // Auto-sleep check for inactivity (active in all views except active BLE transfer)
        if (s_current_view != VIEW_BLE_SYNC) {
            int64_t elapsed_sec = (esp_timer_get_time() - s_last_activity_us) / 1000000LL;
            uint16_t timeout_sec = ble_sync_get_sleep_timeout();
            if (timeout_sec > 0 && elapsed_sec >= timeout_sec) {
                ESP_LOGI(TAG, "Inactivity timeout (%llds >= %us). Entering deep sleep...", elapsed_sec, timeout_sec);
                if (s_current_view != VIEW_READING) {
                    reader_ui_render_sleeping();
                }
                book_library_update_offset(text_pager_get_current_offset());
                book_library_flush_state();
                vTaskDelay(pdMS_TO_TICKS(500));
                power_mgr_enter_deep_sleep(0);
            }
        }
    }
}
