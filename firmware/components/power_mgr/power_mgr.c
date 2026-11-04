#include "power_mgr.h"
#include "board_config.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "POWER_MGR"

#define VBAT_PWR_HOLD_PIN  BOARD_VBAT_PWR_HOLD_PIN
#define BOOT_BUTTON_PIN    BOARD_BTN_BOOT_PIN
#define PWR_BUTTON_PIN     BOARD_BTN_PWR_PIN
#define AUDIO_PWR_PIN      BOARD_AUDIO_PWR_PIN
#define AUDIO_PA_PIN       BOARD_AUDIO_PA_PIN
#define LED_GREEN_PIN      BOARD_LED_GREEN_PIN

#define BATTERY_ADC_CHANNEL BOARD_BATTERY_ADC_CHANNEL
#define BATTERY_ADC_SAMPLE_COUNT 16

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t         s_cali_handle = NULL;
static bool                      s_cali_enabled = false;

void power_mgr_early_init(void) {
    // 1. Configure and assert GPIO 17 HIGH immediately before disabling sleep hold
    gpio_config_t pwr_conf = {
        .pin_bit_mask = (1ULL << VBAT_PWR_HOLD_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pwr_conf);
    gpio_set_level(VBAT_PWR_HOLD_PIN, 1);

    // 2. Disable audio amplifier rails (active-low PMOS: 1 = OFF) and LED to prevent power waste
    gpio_reset_pin(LED_GREEN_PIN);
    uint64_t audio_pin_mask = (1ULL << AUDIO_PWR_PIN) | (1ULL << LED_GREEN_PIN);
    if (GPIO_IS_VALID_OUTPUT_GPIO(AUDIO_PA_PIN)) {
        audio_pin_mask |= (1ULL << AUDIO_PA_PIN);
    }
    gpio_config_t audio_conf = {
        .pin_bit_mask = audio_pin_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&audio_conf);
    gpio_set_level(AUDIO_PWR_PIN, 1); // 1 = OFF
    if (GPIO_IS_VALID_OUTPUT_GPIO(AUDIO_PA_PIN)) {
        gpio_set_level(AUDIO_PA_PIN, 0);  // 0 = Mute
    }
    gpio_set_level(LED_GREEN_PIN, 1); // 1 = OFF (Active-Low)

    // 3. Release any hold from prior deep sleep now that pins are driven HIGH
    gpio_hold_dis(VBAT_PWR_HOLD_PIN);
    gpio_hold_dis(AUDIO_PWR_PIN);
    gpio_hold_dis(LED_GREEN_PIN);
    gpio_deep_sleep_hold_dis();

    // 4. Initialize ADC for battery monitoring
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    if (adc_oneshot_new_unit(&init_config, &s_adc_handle) == ESP_OK) {
        adc_oneshot_chan_cfg_t chan_config = {
            .bitwidth = ADC_BITWIDTH_DEFAULT,
            .atten = ADC_ATTEN_DB_12,
        };
        adc_oneshot_config_channel(s_adc_handle, BATTERY_ADC_CHANNEL, &chan_config);

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .chan = BATTERY_ADC_CHANNEL,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        if (adc_cali_create_scheme_curve_fitting(&cali_config, &s_cali_handle) == ESP_OK) {
            s_cali_enabled = true;
        }
#endif
    }

    ESP_LOGI(TAG, "Power manager initialized. Board power latched.");
}

wakeup_reason_t power_mgr_get_wakeup_cause(void) {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        uint64_t status = esp_sleep_get_ext1_wakeup_status();
        if (status & (1ULL << BOOT_BUTTON_PIN)) {
            return WAKEUP_REASON_BOOT_BUTTON;
        } else if (status & (1ULL << PWR_BUTTON_PIN)) {
            return WAKEUP_REASON_PWR_BUTTON;
        }
        return WAKEUP_REASON_OTHER;
    } else if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        return WAKEUP_REASON_TIMER;
    }
    return WAKEUP_REASON_COLD_BOOT;
}

uint32_t power_mgr_get_battery_voltage_mv(void) {
    if (!s_adc_handle) {
        ESP_LOGE(TAG, "ADC unit uninitialized");
        return 0;
    }

    uint32_t sum_raw = 0;
    uint32_t valid_samples = 0;
    for (int i = 0; i < BATTERY_ADC_SAMPLE_COUNT; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc_handle, BATTERY_ADC_CHANNEL, &raw) == ESP_OK) {
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

    return (uint32_t)((float)mv * BOARD_VOLTAGE_DIVIDER_RATIO);
}

typedef struct {
    uint32_t mv;
    uint8_t  pct;
} battery_ocv_point_t;

static const battery_ocv_point_t s_battery_ocv_table[] = {
    { 4180, 100 },
    { 4060,  90 },
    { 3980,  80 },
    { 3900,  70 },
    { 3840,  60 },
    { 3800,  50 },
    { 3760,  40 },
    { 3730,  30 },
    { 3680,  20 },
    { 3600,  10 },
    { 3450,   5 },
    { 3300,   0 },
};

uint8_t power_mgr_get_battery_percentage(void) {
    uint32_t mv = power_mgr_get_battery_voltage_mv();
    const size_t table_size = sizeof(s_battery_ocv_table) / sizeof(s_battery_ocv_table[0]);

    if (mv >= s_battery_ocv_table[0].mv) {
        return 100;
    }
    if (mv <= s_battery_ocv_table[table_size - 1].mv) {
        return 0;
    }

    for (size_t i = 0; i < table_size - 1; i++) {
        if (s_battery_ocv_table[i].mv > mv && mv >= s_battery_ocv_table[i + 1].mv) {
            uint32_t mv_high = s_battery_ocv_table[i].mv;
            uint32_t mv_low  = s_battery_ocv_table[i + 1].mv;
            uint32_t pct_high = s_battery_ocv_table[i].pct;
            uint32_t pct_low  = s_battery_ocv_table[i + 1].pct;

            uint32_t pct = pct_low + ((mv - mv_low) * (pct_high - pct_low)) / (mv_high - mv_low);
            if (pct > 100) {
                pct = 100;
            }
            return (uint8_t)pct;
        }
    }

    return 0;
}

void power_mgr_enter_deep_sleep(uint32_t sleep_seconds) {
    ESP_LOGI(TAG, "Preparing deep sleep (sleep_seconds=%lu)...", sleep_seconds);

    // Disable all wakeup sources first
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    // Setup Ext1 wakeup on BOOT (GPIO 0) and PWR (GPIO 18) pins
    const uint64_t ext1_pins = (1ULL << BOOT_BUTTON_PIN) | (1ULL << PWR_BUTTON_PIN);
    ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(ext1_pins, ESP_EXT1_WAKEUP_ANY_LOW));

    // Configure RTC pull-ups to avoid floating pins
    rtc_gpio_pulldown_dis(BOOT_BUTTON_PIN);
    rtc_gpio_pullup_en(BOOT_BUTTON_PIN);
    rtc_gpio_pulldown_dis(PWR_BUTTON_PIN);
    rtc_gpio_pullup_en(PWR_BUTTON_PIN);

    esp_sleep_pd_config(ESP_PD_DOMAIN_MAX, ESP_PD_OPTION_AUTO);

    if (sleep_seconds > 0) {
        esp_sleep_enable_timer_wakeup(((uint64_t)sleep_seconds) * 1000000ULL);
    }

    // Keep GPIO 17 latched high during sleep so battery circuit stays alive
    // Latch pin audio PMOS & LED agar gate tidak mengambang (floating) dan amplifier tidak menyedot 15-30mA saat sleep
    gpio_set_level(VBAT_PWR_HOLD_PIN, 1);
    gpio_set_level(AUDIO_PWR_PIN, 1);
    gpio_set_level(LED_GREEN_PIN, 1); // 1 = OFF during sleep (Active-Low)
    gpio_hold_en(VBAT_PWR_HOLD_PIN);
    gpio_hold_en(AUDIO_PWR_PIN);
    gpio_hold_en(LED_GREEN_PIN);
    gpio_deep_sleep_hold_en();

    // Wait for buttons to be released so Ext1 ANY_LOW does not trigger immediately
    // Pastikan loop menunggu sampai kedua tombol (GPIO 0 & GPIO 18) bernilai 1 (HIGH) sebelum esp_deep_sleep_start()
    while (gpio_get_level(BOOT_BUTTON_PIN) == 0 || gpio_get_level(PWR_BUTTON_PIN) == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_deep_sleep_start();
}

void power_mgr_power_off(void) {
    ESP_LOGI(TAG, "Hard power off requested. Dropping GPIO 17...");
    gpio_hold_dis(VBAT_PWR_HOLD_PIN);
    gpio_set_level(VBAT_PWR_HOLD_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
}
