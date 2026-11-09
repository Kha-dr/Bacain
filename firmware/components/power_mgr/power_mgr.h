#ifndef POWER_MGR_H
#define POWER_MGR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_sleep.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WAKEUP_REASON_COLD_BOOT = 0,
    WAKEUP_REASON_BOOT_BUTTON,
    WAKEUP_REASON_PWR_BUTTON,
    WAKEUP_REASON_TIMER,
    WAKEUP_REASON_OTHER,
} wakeup_reason_t;

// Configures and latches power rails, audio pins, and LEDs
void power_mgr_early_init(void);

// Initializes power management and battery ADC monitoring
esp_err_t power_mgr_init(void);

// Returns true if device is connected to external charge or power
bool power_mgr_is_charging(void);

// Returns true if hardware battery ADC is available
bool power_mgr_is_battery_available(void);

// Detects cause of wake up
wakeup_reason_t power_mgr_get_wakeup_cause(void);

// Battery ADC readings
uint32_t power_mgr_get_battery_voltage_mv(void);
uint8_t  power_mgr_get_battery_percentage(void);
uint32_t power_mgr_get_battery_mv(void);
uint8_t  power_mgr_get_battery_pct(void);

// Enters low-power deep sleep (sleep_seconds = 0 for indefinite until button)
void power_mgr_enter_deep_sleep(uint32_t sleep_seconds);

// Hard power off (drops power latch or falls back to deep sleep)
void power_mgr_power_off(void);

#ifdef __cplusplus
}
#endif

#endif // POWER_MGR_H
