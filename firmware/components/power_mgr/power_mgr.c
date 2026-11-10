#include "power_mgr.h"
#include "board_config.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "POWER_MGR"

#ifndef BOARD_PWR_LATCH_PIN
#define BOARD_PWR_LATCH_PIN -1
#endif

#ifndef BOARD_PWR_LATCH_ACTIVE_LVL
#define BOARD_PWR_LATCH_ACTIVE_LVL 1
#endif

#ifndef BOARD_AUDIO_PWR_PIN
#define BOARD_AUDIO_PWR_PIN -1
#endif

#ifndef BOARD_AUDIO_PWR_ACTIVE_LVL
#define BOARD_AUDIO_PWR_ACTIVE_LVL 0
#endif

#ifndef BOARD_AUDIO_PA_PIN
#define BOARD_AUDIO_PA_PIN -1
#endif

#ifndef BOARD_LED_PIN
#define BOARD_LED_PIN -1
#endif

#ifndef BOARD_LED_RED_PIN
#define BOARD_LED_RED_PIN -1
#endif

#ifndef BOARD_LED_GREEN_PIN
#define BOARD_LED_GREEN_PIN -1
#endif

#ifndef BOARD_LED_BLUE_PIN
#define BOARD_LED_BLUE_PIN -1
#endif

#ifndef BOARD_LED_ACTIVE_LEVEL
#define BOARD_LED_ACTIVE_LEVEL 0
#endif

#ifndef BOARD_CHARGE_DETECT_PIN
#define BOARD_CHARGE_DETECT_PIN -1
#endif

#ifndef BOARD_CHARGE_ACTIVE_LEVEL
#define BOARD_CHARGE_ACTIVE_LEVEL 0
#endif

#ifndef BOARD_VBUS_SENSE_PIN
#define BOARD_VBUS_SENSE_PIN -1
#endif

#ifndef BOARD_HAS_BATTERY_ADC
#define BOARD_HAS_BATTERY_ADC 0
#endif

#ifndef BOARD_BATTERY_ADC_CHANNEL
#define BOARD_BATTERY_ADC_CHANNEL -1
#endif

#ifndef BOARD_BATTERY_ADC_UNIT
#define BOARD_BATTERY_ADC_UNIT 1
#endif

#ifndef BOARD_BATTERY_ADC_ATTEN
#define BOARD_BATTERY_ADC_ATTEN ADC_ATTEN_DB_12
#endif

#ifndef BOARD_BATTERY_MIN_MV
#define BOARD_BATTERY_MIN_MV 3300
#endif

#ifndef BOARD_BATTERY_MAX_MV
#define BOARD_BATTERY_MAX_MV 4180
#endif

#ifndef BOARD_VOLTAGE_DIVIDER_RATIO
#define BOARD_VOLTAGE_DIVIDER_RATIO 2.0f
#endif

#define BATTERY_ADC_SAMPLE_COUNT 16

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t         s_cali_handle = NULL;
static bool                      s_cali_enabled = false;
static bool                      s_battery_available = false;

RTC_DATA_ATTR static uint32_t s_rtc_cached_battery_mv = 0;
RTC_DATA_ATTR static bool s_rtc_battery_valid = false;
static bool s_fresh_conversion_done = false;

// Default weak board power hook implementations
esp_err_t __attribute__((weak)) board_power_early_init(void) {
    return ESP_OK;
}

esp_err_t __attribute__((weak)) board_power_init(void) {
    return ESP_OK;
}

bool __attribute__((weak)) board_power_is_charging(void) {
#if defined(BOARD_CHARGE_DETECT_PIN) && (BOARD_CHARGE_DETECT_PIN >= 0)
    if (GPIO_IS_VALID_GPIO(BOARD_CHARGE_DETECT_PIN)) {
        return gpio_get_level((gpio_num_t)BOARD_CHARGE_DETECT_PIN) == BOARD_CHARGE_ACTIVE_LEVEL;
    }
#endif
#if defined(BOARD_VBUS_SENSE_PIN) && (BOARD_VBUS_SENSE_PIN >= 0)
    if (GPIO_IS_VALID_GPIO(BOARD_VBUS_SENSE_PIN)) {
        return gpio_get_level((gpio_num_t)BOARD_VBUS_SENSE_PIN) == 1;
    }
#endif
    return false;
}

uint32_t __attribute__((weak)) board_power_get_battery_mv(void) {
    return 0;
}

void __attribute__((weak)) board_power_pre_sleep(void) {
}

void __attribute__((weak)) board_power_post_wake(void) {
}

static const battery_ocv_point_t s_battery_ocv_table[] = BOARD_BATTERY_OCV_TABLE;

bool power_mgr_is_charging(void) {
    return board_power_is_charging();
}

bool power_mgr_is_battery_available(void) {
    return s_battery_available;
}

esp_err_t power_mgr_init(void) {
    board_power_init();

    if (!BOARD_HAS_BATTERY_ADC || BOARD_BATTERY_ADC_CHANNEL < 0) {
        s_battery_available = false;
        return ESP_OK;
    }

    if (s_adc_handle != NULL) {
        s_battery_available = true;
        return ESP_OK;
    }

    adc_unit_t unit = (BOARD_BATTERY_ADC_UNIT == 2) ? ADC_UNIT_2 : ADC_UNIT_1;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = unit,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_config, &s_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC unit: %s", esp_err_to_name(ret));
        s_battery_available = false;
        return ret;
    }

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = (adc_atten_t)BOARD_BATTERY_ADC_ATTEN,
    };
    ret = adc_oneshot_config_channel(s_adc_handle, (adc_channel_t)BOARD_BATTERY_ADC_CHANNEL, &chan_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC channel: %s", esp_err_to_name(ret));
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        s_battery_available = false;
        return ret;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .chan = (adc_channel_t)BOARD_BATTERY_ADC_CHANNEL,
        .atten = (adc_atten_t)BOARD_BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_config, &s_cali_handle) == ESP_OK) {
        s_cali_enabled = true;
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = (adc_atten_t)BOARD_BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_line_fitting(&cali_config, &s_cali_handle) == ESP_OK) {
        s_cali_enabled = true;
    }
#endif

    s_battery_available = true;
    return ESP_OK;
}

void power_mgr_early_init(void) {
    board_power_early_init();

#if defined(BOARD_PWR_LATCH_PIN) && (BOARD_PWR_LATCH_PIN >= 0)
    if (GPIO_IS_VALID_OUTPUT_GPIO(BOARD_PWR_LATCH_PIN)) {
        gpio_config_t pwr_conf = {
            .pin_bit_mask = (1ULL << BOARD_PWR_LATCH_PIN),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&pwr_conf);
        gpio_set_level((gpio_num_t)BOARD_PWR_LATCH_PIN, BOARD_PWR_LATCH_ACTIVE_LVL);
        gpio_sleep_sel_dis((gpio_num_t)BOARD_PWR_LATCH_PIN);
    }
#endif

#if defined(BOARD_AUDIO_PWR_PIN) && (BOARD_AUDIO_PWR_PIN >= 0)
    if (GPIO_IS_VALID_OUTPUT_GPIO(BOARD_AUDIO_PWR_PIN)) {
        gpio_config_t audio_conf = {
            .pin_bit_mask = (1ULL << BOARD_AUDIO_PWR_PIN),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&audio_conf);
        gpio_set_level((gpio_num_t)BOARD_AUDIO_PWR_PIN, !BOARD_AUDIO_PWR_ACTIVE_LVL);
        gpio_sleep_sel_dis((gpio_num_t)BOARD_AUDIO_PWR_PIN);
    }
#endif

#if defined(BOARD_AUDIO_PA_PIN) && (BOARD_AUDIO_PA_PIN >= 0)
    if (GPIO_IS_VALID_OUTPUT_GPIO(BOARD_AUDIO_PA_PIN)) {
        gpio_config_t pa_conf = {
            .pin_bit_mask = (1ULL << BOARD_AUDIO_PA_PIN),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&pa_conf);
        gpio_set_level((gpio_num_t)BOARD_AUDIO_PA_PIN, 0);
    }
#endif

    const int led_pins[] = {
        BOARD_LED_PIN,
        BOARD_LED_RED_PIN,
        BOARD_LED_GREEN_PIN,
        BOARD_LED_BLUE_PIN
    };
    const int inactive_led_level = (BOARD_LED_ACTIVE_LEVEL == 0) ? 1 : 0;
    for (size_t i = 0; i < sizeof(led_pins) / sizeof(led_pins[0]); i++) {
        int pin = led_pins[i];
        if (pin >= 0 && GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
            gpio_reset_pin((gpio_num_t)pin);
            gpio_config_t led_conf = {
                .pin_bit_mask = (1ULL << pin),
                .mode = GPIO_MODE_OUTPUT,
                .pull_up_en = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            gpio_config(&led_conf);
            gpio_set_level((gpio_num_t)pin, inactive_led_level);
        }
    }

#if defined(BOARD_CHARGE_DETECT_PIN) && (BOARD_CHARGE_DETECT_PIN >= 0)
    if (GPIO_IS_VALID_GPIO(BOARD_CHARGE_DETECT_PIN)) {
        gpio_config_t chg_conf = {
            .pin_bit_mask = (1ULL << BOARD_CHARGE_DETECT_PIN),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&chg_conf);
    }
#endif

#if defined(BOARD_VBUS_SENSE_PIN) && (BOARD_VBUS_SENSE_PIN >= 0)
    if (GPIO_IS_VALID_GPIO(BOARD_VBUS_SENSE_PIN)) {
        gpio_config_t vbus_conf = {
            .pin_bit_mask = (1ULL << BOARD_VBUS_SENSE_PIN),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&vbus_conf);
    }
#endif

    if (BOARD_PWR_LATCH_PIN >= 0 && GPIO_IS_VALID_OUTPUT_GPIO(BOARD_PWR_LATCH_PIN)) {
        gpio_hold_dis((gpio_num_t)BOARD_PWR_LATCH_PIN);
    }
    if (BOARD_AUDIO_PWR_PIN >= 0 && GPIO_IS_VALID_OUTPUT_GPIO(BOARD_AUDIO_PWR_PIN)) {
        gpio_hold_dis((gpio_num_t)BOARD_AUDIO_PWR_PIN);
    }
    for (size_t i = 0; i < sizeof(led_pins) / sizeof(led_pins[0]); i++) {
        int pin = led_pins[i];
        if (pin >= 0 && GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
            gpio_hold_dis((gpio_num_t)pin);
        }
    }
    gpio_deep_sleep_hold_dis();

    power_mgr_init();
    board_power_post_wake();

    ESP_LOGI(TAG, "Power manager initialized");
}

wakeup_reason_t power_mgr_get_wakeup_cause(void) {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        uint64_t status = esp_sleep_get_ext1_wakeup_status();
        if (BOARD_BTN_UP_PIN >= 0 && (status & (1ULL << BOARD_BTN_UP_PIN))) {
            return WAKEUP_REASON_BOOT_BUTTON;
        } else if (BOARD_BTN_DOWN_PIN >= 0 && (status & (1ULL << BOARD_BTN_DOWN_PIN))) {
            return WAKEUP_REASON_PWR_BUTTON;
        }
        return WAKEUP_REASON_OTHER;
    } else if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        return WAKEUP_REASON_TIMER;
    }
    return WAKEUP_REASON_COLD_BOOT;
}

void power_mgr_cache_battery_mv(uint32_t mv) {
    s_rtc_cached_battery_mv = mv;
    s_rtc_battery_valid = (mv > 0);
}

uint32_t power_mgr_read_battery_mv(void) {
    uint32_t board_mv = board_power_get_battery_mv();
    if (board_mv > 0) {
        s_rtc_cached_battery_mv = board_mv;
        s_rtc_battery_valid = true;
        s_fresh_conversion_done = true;
        return board_mv;
    }

    if (!s_battery_available) {
        return 0;
    }
    if (!s_adc_handle) {
        ESP_LOGE(TAG, "ADC unit uninitialized");
        return 0;
    }

    uint32_t sum_raw = 0;
    uint32_t valid_samples = 0;
    for (int i = 0; i < BATTERY_ADC_SAMPLE_COUNT; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc_handle, (adc_channel_t)BOARD_BATTERY_ADC_CHANNEL, &raw) == ESP_OK) {
            sum_raw += (uint32_t)raw;
            valid_samples++;
        }
    }

    if (valid_samples == 0) {
        ESP_LOGE(TAG, "Battery ADC read failed");
        return 0;
    }

    int avg_raw = (int)(sum_raw / valid_samples);
    int mv = 0;
    if (s_cali_enabled && s_cali_handle) {
        if (adc_cali_raw_to_voltage(s_cali_handle, avg_raw, &mv) != ESP_OK) {
            mv = (avg_raw * 3300) / 4095;
        }
    } else {
        mv = (avg_raw * 3300) / 4095;
    }

    uint32_t calculated_mv = (uint32_t)((float)mv * BOARD_VOLTAGE_DIVIDER_RATIO);
    s_rtc_cached_battery_mv = calculated_mv;
    s_rtc_battery_valid = (calculated_mv > 0);
    s_fresh_conversion_done = true;
    return calculated_mv;
}

uint32_t power_mgr_get_battery_mv(void) {
    if (s_rtc_battery_valid && !s_fresh_conversion_done && esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED) {
        return s_rtc_cached_battery_mv;
    }

    return power_mgr_read_battery_mv();
}

uint32_t power_mgr_get_battery_voltage_mv(void) {
    return power_mgr_get_battery_mv();
}

uint8_t power_mgr_get_battery_pct(void) {
    uint32_t board_mv = board_power_get_battery_mv();
    if (!s_battery_available && board_mv == 0) {
        return 100;
    }

    uint32_t mv = power_mgr_get_battery_mv();
    if (mv < 2500) {
        return 100;
    }

    uint32_t v_max = BOARD_BATTERY_MAX_MV;
    uint32_t v_min = BOARD_BATTERY_MIN_MV;
    if (v_max <= v_min) {
        return (mv >= v_max) ? 100 : 0;
    }

    if (mv >= v_max) {
        return 100;
    }
    if (mv <= v_min) {
        return 0;
    }

    uint32_t permille = (uint32_t)(((uint64_t)(mv - v_min) * 1000ULL) / (v_max - v_min));
    const size_t table_size = sizeof(s_battery_ocv_table) / sizeof(s_battery_ocv_table[0]);

    if (permille >= s_battery_ocv_table[0].permille) {
        return s_battery_ocv_table[0].pct;
    }
    if (permille <= s_battery_ocv_table[table_size - 1].permille) {
        return s_battery_ocv_table[table_size - 1].pct;
    }

    for (size_t i = 0; i < table_size - 1; i++) {
        if (s_battery_ocv_table[i].permille >= permille && permille >= s_battery_ocv_table[i + 1].permille) {
            uint32_t p_high = s_battery_ocv_table[i].permille;
            uint32_t p_low  = s_battery_ocv_table[i + 1].permille;
            uint32_t pct_high = s_battery_ocv_table[i].pct;
            uint32_t pct_low  = s_battery_ocv_table[i + 1].pct;

            if (p_high == p_low) {
                return (uint8_t)pct_high;
            }

            uint32_t pct = pct_low + ((permille - p_low) * (pct_high - pct_low)) / (p_high - p_low);
            if (pct > 100) {
                pct = 100;
            }
            return (uint8_t)pct;
        }
    }

    return 0;
}

uint8_t power_mgr_get_battery_percentage(void) {
    return power_mgr_get_battery_pct();
}

static void wait_for_buttons_release(void) {
    if (BOARD_BTN_UP_PIN >= 0 && GPIO_IS_VALID_GPIO(BOARD_BTN_UP_PIN)) {
        gpio_set_direction((gpio_num_t)BOARD_BTN_UP_PIN, GPIO_MODE_INPUT);
    }
    if (BOARD_BTN_DOWN_PIN >= 0 && GPIO_IS_VALID_GPIO(BOARD_BTN_DOWN_PIN) && BOARD_BTN_DOWN_PIN != BOARD_BTN_UP_PIN) {
        gpio_set_direction((gpio_num_t)BOARD_BTN_DOWN_PIN, GPIO_MODE_INPUT);
    }

    int wait_cycles = 0;
    while (wait_cycles < 300) {
        bool button_pressed = false;
        if (BOARD_BTN_UP_PIN >= 0 && GPIO_IS_VALID_GPIO(BOARD_BTN_UP_PIN)) {
            if (gpio_get_level((gpio_num_t)BOARD_BTN_UP_PIN) == 0) {
                button_pressed = true;
            }
        }
        if (BOARD_BTN_DOWN_PIN >= 0 && GPIO_IS_VALID_GPIO(BOARD_BTN_DOWN_PIN) && BOARD_BTN_DOWN_PIN != BOARD_BTN_UP_PIN) {
            if (gpio_get_level((gpio_num_t)BOARD_BTN_DOWN_PIN) == 0) {
                button_pressed = true;
            }
        }
        if (!button_pressed) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        wait_cycles++;
    }
}

void power_mgr_enter_deep_sleep(uint32_t sleep_seconds) {
    ESP_LOGI(TAG, "Entering deep sleep (%lu s)", (unsigned long)sleep_seconds);

    // Refresh battery voltage cache before deep sleep
    power_mgr_read_battery_mv();

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    uint64_t ext1_pins = 0;
    if (BOARD_BTN_UP_PIN >= 0 && rtc_gpio_is_valid_gpio((gpio_num_t)BOARD_BTN_UP_PIN)) {
        ext1_pins |= (1ULL << BOARD_BTN_UP_PIN);
        rtc_gpio_pullup_en((gpio_num_t)BOARD_BTN_UP_PIN);
        rtc_gpio_pulldown_dis((gpio_num_t)BOARD_BTN_UP_PIN);
    }
    if (BOARD_BTN_DOWN_PIN >= 0 && rtc_gpio_is_valid_gpio((gpio_num_t)BOARD_BTN_DOWN_PIN) && BOARD_BTN_DOWN_PIN != BOARD_BTN_UP_PIN) {
        ext1_pins |= (1ULL << BOARD_BTN_DOWN_PIN);
        rtc_gpio_pullup_en((gpio_num_t)BOARD_BTN_DOWN_PIN);
        rtc_gpio_pulldown_dis((gpio_num_t)BOARD_BTN_DOWN_PIN);
    }
    if (ext1_pins != 0) {
        esp_sleep_enable_ext1_wakeup_io(ext1_pins, ESP_EXT1_WAKEUP_ANY_LOW);
    }

    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    if (sleep_seconds > 0) {
        esp_sleep_enable_timer_wakeup(((uint64_t)sleep_seconds) * 1000000ULL);
    }

    if (BOARD_PWR_LATCH_PIN >= 0 && GPIO_IS_VALID_OUTPUT_GPIO(BOARD_PWR_LATCH_PIN)) {
        gpio_set_level((gpio_num_t)BOARD_PWR_LATCH_PIN, BOARD_PWR_LATCH_ACTIVE_LVL);
        gpio_hold_en((gpio_num_t)BOARD_PWR_LATCH_PIN);
    }
    if (BOARD_AUDIO_PWR_PIN >= 0 && GPIO_IS_VALID_OUTPUT_GPIO(BOARD_AUDIO_PWR_PIN)) {
        gpio_set_level((gpio_num_t)BOARD_AUDIO_PWR_PIN, !BOARD_AUDIO_PWR_ACTIVE_LVL);
        gpio_hold_en((gpio_num_t)BOARD_AUDIO_PWR_PIN);
    }

    const int led_pins[] = {
        BOARD_LED_PIN,
        BOARD_LED_RED_PIN,
        BOARD_LED_GREEN_PIN,
        BOARD_LED_BLUE_PIN
    };
    const int inactive_led_level = (BOARD_LED_ACTIVE_LEVEL == 0) ? 1 : 0;
    for (size_t i = 0; i < sizeof(led_pins) / sizeof(led_pins[0]); i++) {
        int pin = led_pins[i];
        if (pin >= 0 && GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
            gpio_set_level((gpio_num_t)pin, inactive_led_level);
            gpio_hold_en((gpio_num_t)pin);
        }
    }

    wait_for_buttons_release();

    board_power_pre_sleep();
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_deep_sleep_hold_en();
    esp_deep_sleep_start();
}

void power_mgr_power_off(void) {
    ESP_LOGI(TAG, "Power off requested");
    wait_for_buttons_release();
    if (BOARD_PWR_LATCH_PIN >= 0 && GPIO_IS_VALID_OUTPUT_GPIO(BOARD_PWR_LATCH_PIN)) {
        gpio_hold_dis((gpio_num_t)BOARD_PWR_LATCH_PIN);
        gpio_set_level((gpio_num_t)BOARD_PWR_LATCH_PIN, !BOARD_PWR_LATCH_ACTIVE_LVL);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    power_mgr_enter_deep_sleep(0);
}
