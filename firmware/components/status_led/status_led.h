#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        STATUS_LED_EVT_WAKEUP,
        STATUS_LED_EVT_SLEEP,
        STATUS_LED_EVT_REBOOT,
    } status_led_event_t;

    void status_led_init(bool enabled); /* enabled: from app_settings_get()->status_led_enabled */
    void status_led_trigger(status_led_event_t event);
    bool status_led_is_available(void);
    bool status_led_is_enabled(void);
    esp_err_t status_led_set_enabled(bool enabled);

#ifdef __cplusplus
}
#endif

#endif // STATUS_LED_H
