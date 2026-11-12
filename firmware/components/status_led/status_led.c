#include "status_led.h"
#include "board_config.h"
#include "esp_log.h"

#if BOARD_HAS_STATUS_LED
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

static const char *TAG = "STATUS_LED";

/* Runtime only. Persistence is owned by app_settings ("status_led" key). */
static bool s_enabled = false;

#define WAKE_BLINKS 2
#define WAKE_ON_MS 150
#define WAKE_OFF_MS 120
#define REBOOT_BLINKS 4
#define REBOOT_ON_MS 50
#define REBOOT_OFF_MS 50
#define SLEEP_PULSE_MS 150 /* visibility only: power_mgr forces LEDs off + holds before sleep */

bool status_led_is_available(void)
{
#if BOARD_HAS_STATUS_LED
    return (BOARD_LED_PIN >= 0 || BOARD_LED_RED_PIN >= 0 ||
            BOARD_LED_GREEN_PIN >= 0 || BOARD_LED_BLUE_PIN >= 0);
#else
    return false;
#endif
}

bool status_led_is_enabled(void)
{
    return status_led_is_available() && s_enabled;
}

esp_err_t status_led_set_enabled(bool enabled)
{
    if (!status_led_is_available())
    {
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_enabled = enabled;
    return ESP_OK;
}

#if BOARD_HAS_STATUS_LED
/* First pin that exists; the single-LED pin is the last fallback (-1 if absent). */
static int pick_pin(int first, int second, int third)
{
    if (first >= 0)
        return first;
    if (second >= 0)
        return second;
    if (third >= 0)
        return third;
    return BOARD_LED_PIN;
}

static void set_led(int pin, bool on)
{
    if (pin >= 0)
    {
        int level = on ? BOARD_LED_ACTIVE_LEVEL : !BOARD_LED_ACTIVE_LEVEL;
        gpio_set_level((gpio_num_t)pin, level ? 1 : 0);
    }
}

static void delay_ms(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);
    vTaskDelay(ticks > 0 ? ticks : 1);
}

static void blink(int pin, int count, uint32_t on_ms, uint32_t off_ms)
{
    for (int i = 0; i < count; i++)
    {
        set_led(pin, true);
        delay_ms(on_ms);
        set_led(pin, false);
        if (i + 1 < count)
        {
            delay_ms(off_ms);
        }
    }
}

static void led_wake_task(void *param)
{
    blink((int)(intptr_t)param, WAKE_BLINKS, WAKE_ON_MS, WAKE_OFF_MS);
    vTaskDelete(NULL);
}
#endif

/* Pins are configured (output, off, hold released) by power_mgr_early_init(). */
void status_led_init(bool enabled)
{
    s_enabled = status_led_is_available() && enabled;
    ESP_LOGD(TAG, "status LED %s", s_enabled ? "enabled" : "disabled");
}

void status_led_trigger(status_led_event_t event)
{
    if (!s_enabled)
    {
        return;
    }

#if BOARD_HAS_STATUS_LED
    switch (event)
    {
    case STATUS_LED_EVT_WAKEUP:
    {
        int pin = pick_pin(BOARD_LED_BLUE_PIN, BOARD_LED_GREEN_PIN, -1);
        if (pin < 0)
        {
            break;
        }
        if (xTaskCreate(led_wake_task, "led_wake", 2048, (void *)(intptr_t)pin, 1, NULL) != pdPASS)
        {
            ESP_LOGE(TAG, "led_wake task create failed, blinking inline");
            blink(pin, WAKE_BLINKS, WAKE_ON_MS, WAKE_OFF_MS);
        }
        break;
    }

    case STATUS_LED_EVT_SLEEP:
        if (BOARD_LED_RED_PIN >= 0 && BOARD_LED_GREEN_PIN >= 0)
        {
            set_led(BOARD_LED_RED_PIN, true);
            set_led(BOARD_LED_GREEN_PIN, true);
            delay_ms(SLEEP_PULSE_MS);
            set_led(BOARD_LED_RED_PIN, false);
            set_led(BOARD_LED_GREEN_PIN, false);
        }
        else
        {
            blink(pick_pin(BOARD_LED_GREEN_PIN, BOARD_LED_RED_PIN, BOARD_LED_BLUE_PIN),
                  1, SLEEP_PULSE_MS, 0);
        }
        break;

    case STATUS_LED_EVT_REBOOT:
        if (BOARD_LED_RED_PIN >= 0)
        {
            set_led(BOARD_LED_RED_PIN, true); /* stays on until reset */
        }
        else
        {
            blink(pick_pin(BOARD_LED_GREEN_PIN, BOARD_LED_BLUE_PIN, -1),
                  REBOOT_BLINKS, REBOOT_ON_MS, REBOOT_OFF_MS);
        }
        break;

    default:
        break;
    }
#else
    (void)event;
#endif
}