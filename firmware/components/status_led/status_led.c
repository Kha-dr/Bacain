#include "status_led.h"
#include "board_config.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#if BOARD_HAS_STATUS_LED
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

static const char *TAG = "STATUS_LED";
static bool s_enabled = true;

bool status_led_is_available(void) {
#if BOARD_HAS_STATUS_LED
    return (BOARD_LED_RED_PIN >= 0 || BOARD_LED_GREEN_PIN >= 0 || BOARD_LED_BLUE_PIN >= 0);
#else
    return false;
#endif
}

bool status_led_is_enabled(void) {
    if (!status_led_is_available()) {
        return false;
    }
    return s_enabled;
}

esp_err_t status_led_set_enabled(bool enabled) {
    if (!status_led_is_available()) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    s_enabled = enabled;

    nvs_handle_t handle;
    esp_err_t err = nvs_open("settings", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u8(handle, "status_led", enabled ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write status_led: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    nvs_close(handle);
    return ESP_OK;
}

#if BOARD_HAS_STATUS_LED
static void delay_ms(uint32_t ms) {
    TickType_t ticks = pdMS_TO_TICKS(ms);
    vTaskDelay(ticks > 0 ? ticks : 1);
}

static void set_pin_level(int pin, bool active) {
    if (pin >= 0) {
        int level = active ? (BOARD_LED_ACTIVE_LEVEL ? 1 : 0) : (BOARD_LED_ACTIVE_LEVEL ? 0 : 1);
        gpio_set_level((gpio_num_t)pin, level);
    }
}

static void led_wake_task(void *param) {
    int pin = (int)(intptr_t)param;
    for (int i = 0; i < 2; i++) {
        set_pin_level(pin, true);
        delay_ms(150);
        set_pin_level(pin, false);
        if (i < 1) {
            delay_ms(120);
        }
    }
    vTaskDelete(NULL);
}
#endif

void status_led_init(void) {
    if (!status_led_is_available()) {
        s_enabled = false;
        return;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open("settings", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        uint8_t val = 1;
        err = nvs_get_u8(handle, "status_led", &val);
        if (err == ESP_OK) {
            s_enabled = (val != 0);
        } else {
            s_enabled = true;
        }
        nvs_close(handle);
    } else {
        s_enabled = true;
    }

#if BOARD_HAS_STATUS_LED
    const int pins[] = { BOARD_LED_RED_PIN, BOARD_LED_GREEN_PIN, BOARD_LED_BLUE_PIN };
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        int pin = pins[i];
        if (pin >= 0) {
            gpio_reset_pin((gpio_num_t)pin);
            gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
            set_pin_level(pin, false);
        }
    }
#endif
}

void status_led_trigger(status_led_event_t event) {
    if (!status_led_is_available() || !s_enabled) {
        return;
    }

#if BOARD_HAS_STATUS_LED
    switch (event) {
    case STATUS_LED_EVT_WAKEUP: {
        int pin = -1;
        if (BOARD_LED_BLUE_PIN >= 0) {
            pin = BOARD_LED_BLUE_PIN;
        } else if (BOARD_LED_GREEN_PIN >= 0) {
            pin = BOARD_LED_GREEN_PIN;
        }

        if (pin >= 0) {
            BaseType_t res = xTaskCreate(led_wake_task, "led_wake", 2048, (void *)(intptr_t)pin, 1, NULL);
            if (res != pdPASS) {
                for (int i = 0; i < 2; i++) {
                    set_pin_level(pin, true);
                    delay_ms(150);
                    set_pin_level(pin, false);
                    if (i < 1) {
                        delay_ms(120);
                    }
                }
            }
        }
        break;
    }

    case STATUS_LED_EVT_SLEEP: {
        if (BOARD_LED_RED_PIN >= 0 && BOARD_LED_GREEN_PIN >= 0) {
            set_pin_level(BOARD_LED_RED_PIN, true);
            set_pin_level(BOARD_LED_GREEN_PIN, true);
            delay_ms(500);
            set_pin_level(BOARD_LED_RED_PIN, false);
            set_pin_level(BOARD_LED_GREEN_PIN, false);
        } else {
            int sleep_pin = -1;
            if (BOARD_LED_GREEN_PIN >= 0) {
                sleep_pin = BOARD_LED_GREEN_PIN;
            } else if (BOARD_LED_RED_PIN >= 0) {
                sleep_pin = BOARD_LED_RED_PIN;
            } else if (BOARD_LED_BLUE_PIN >= 0) {
                sleep_pin = BOARD_LED_BLUE_PIN;
            }

            if (sleep_pin >= 0) {
                set_pin_level(sleep_pin, true);
                delay_ms(500);
                set_pin_level(sleep_pin, false);
            }
        }
        break;
    }

    case STATUS_LED_EVT_REBOOT: {
        if (BOARD_LED_RED_PIN >= 0) {
            set_pin_level(BOARD_LED_RED_PIN, true);
        } else {
            int reboot_pin = (BOARD_LED_GREEN_PIN >= 0) ? BOARD_LED_GREEN_PIN : BOARD_LED_BLUE_PIN;
            if (reboot_pin >= 0) {
                for (int i = 0; i < 4; i++) {
                    set_pin_level(reboot_pin, true);
                    delay_ms(50);
                    set_pin_level(reboot_pin, false);
                    delay_ms(50);
                }
            }
        }
        break;
    }

    default:
        break;
    }
#else
    (void)event;
#endif
}
