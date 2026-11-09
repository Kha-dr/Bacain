#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif

#if __has_include("driver/gpio.h")
#include "driver/gpio.h"
#endif

#if __has_include("esp_adc/adc_oneshot.h")
#include "esp_adc/adc_oneshot.h"
#endif

#if __has_include("esp_err.h")
#include "esp_err.h"
#endif

// Map explicit compiler/CMake overrides (-DCONFIG_BOARD_*) before loading board_def.h
// when CONFIG_BOARD_PROFILE_CUSTOM is active or when overridden via CLI
#if defined(BOARD_DEF_HEADER)
#include BOARD_DEF_HEADER
#elif defined(BOARD_PROFILE_HAS_DEF_H)
#include "board_def.h"
#elif defined(CONFIG_BOARD_PROFILE_CUSTOM) || defined(BOARD_PROFILE_CUSTOM)
// Custom board: pin definitions supplied via compiler/Kconfig flags
#else
#error "Missing mandatory board profile: specify BOARD_PROFILE, BOARD_DEF_HEADER, or BOARD_PROFILE_CUSTOM"
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Battery OCV discharge curve point (permille: 0..1000 of span, pct: 0..100)
typedef struct {
    uint16_t permille;
    uint8_t  pct;
} battery_ocv_point_t;

// Board Power & Hardware Hook Functions
// Any board can optionally override these in boards/<board_id>/board.c
// Default weak implementations are provided in power_mgr.c
esp_err_t board_power_early_init(void) __attribute__((weak));
esp_err_t board_power_init(void)       __attribute__((weak));
bool      board_power_is_charging(void) __attribute__((weak));
uint32_t  board_power_get_battery_mv(void) __attribute__((weak));
void      board_power_pre_sleep(void)  __attribute__((weak));
void      board_power_post_wake(void)  __attribute__((weak));

// Mandatory Category A Definitions

#ifndef BOARD_NAME
#ifdef CONFIG_BOARD_NAME
#define BOARD_NAME                  CONFIG_BOARD_NAME
#else
#error "Missing mandatory board definition: BOARD_NAME"
#endif
#endif

#ifndef BOARD_DISPLAY_WIDTH
#ifdef CONFIG_BOARD_DISPLAY_WIDTH
#define BOARD_DISPLAY_WIDTH         CONFIG_BOARD_DISPLAY_WIDTH
#else
#error "Missing mandatory board definition: BOARD_DISPLAY_WIDTH"
#endif
#endif

#ifndef BOARD_DISPLAY_HEIGHT
#ifdef CONFIG_BOARD_DISPLAY_HEIGHT
#define BOARD_DISPLAY_HEIGHT        CONFIG_BOARD_DISPLAY_HEIGHT
#else
#error "Missing mandatory board definition: BOARD_DISPLAY_HEIGHT"
#endif
#endif

#ifndef BOARD_DISPLAY_ROTATION
#ifdef CONFIG_BOARD_DISPLAY_ROTATION
#define BOARD_DISPLAY_ROTATION      CONFIG_BOARD_DISPLAY_ROTATION
#else
#error "Missing mandatory board definition: BOARD_DISPLAY_ROTATION"
#endif
#endif

#ifndef BOARD_DISPLAY_BUFFER_SIZE
#define BOARD_DISPLAY_BUFFER_SIZE   (BOARD_DISPLAY_WIDTH * BOARD_DISPLAY_HEIGHT / 8)
#endif

#ifndef BOARD_EPD_DC_PIN
#ifdef CONFIG_BOARD_EPD_DC_PIN
#define BOARD_EPD_DC_PIN            CONFIG_BOARD_EPD_DC_PIN
#else
#error "Missing mandatory board definition: BOARD_EPD_DC_PIN"
#endif
#endif

#ifndef BOARD_EPD_CS_PIN
#ifdef CONFIG_BOARD_EPD_CS_PIN
#define BOARD_EPD_CS_PIN            CONFIG_BOARD_EPD_CS_PIN
#else
#error "Missing mandatory board definition: BOARD_EPD_CS_PIN"
#endif
#endif

#ifndef BOARD_EPD_SCK_PIN
#ifdef CONFIG_BOARD_EPD_SCK_PIN
#define BOARD_EPD_SCK_PIN           CONFIG_BOARD_EPD_SCK_PIN
#else
#error "Missing mandatory board definition: BOARD_EPD_SCK_PIN"
#endif
#endif

#ifndef BOARD_EPD_MOSI_PIN
#ifdef CONFIG_BOARD_EPD_MOSI_PIN
#define BOARD_EPD_MOSI_PIN          CONFIG_BOARD_EPD_MOSI_PIN
#else
#error "Missing mandatory board definition: BOARD_EPD_MOSI_PIN"
#endif
#endif

#ifndef BOARD_EPD_RST_PIN
#ifdef CONFIG_BOARD_EPD_RST_PIN
#define BOARD_EPD_RST_PIN           CONFIG_BOARD_EPD_RST_PIN
#else
#error "Missing mandatory board definition: BOARD_EPD_RST_PIN"
#endif
#endif

#ifndef BOARD_EPD_BUSY_PIN
#ifdef CONFIG_BOARD_EPD_BUSY_PIN
#define BOARD_EPD_BUSY_PIN          CONFIG_BOARD_EPD_BUSY_PIN
#else
#error "Missing mandatory board definition: BOARD_EPD_BUSY_PIN"
#endif
#endif

#ifndef BOARD_EPD_PWR_PIN
#ifdef CONFIG_BOARD_EPD_PWR_PIN
#define BOARD_EPD_PWR_PIN           CONFIG_BOARD_EPD_PWR_PIN
#else
#error "Missing mandatory board definition: BOARD_EPD_PWR_PIN"
#endif
#endif

#ifndef BOARD_EPD_PWR_ACTIVE_LVL
#ifdef CONFIG_BOARD_EPD_PWR_ACTIVE_LVL
#define BOARD_EPD_PWR_ACTIVE_LVL    CONFIG_BOARD_EPD_PWR_ACTIVE_LVL
#else
#define BOARD_EPD_PWR_ACTIVE_LVL    0
#endif
#endif

#ifndef BOARD_EPD_BUSY_ACTIVE_LVL
#ifdef CONFIG_BOARD_EPD_BUSY_ACTIVE_LVL
#define BOARD_EPD_BUSY_ACTIVE_LVL   CONFIG_BOARD_EPD_BUSY_ACTIVE_LVL
#else
#define BOARD_EPD_BUSY_ACTIVE_LVL   1
#endif
#endif

#ifndef BOARD_BTN_UP_PIN
#ifdef CONFIG_BOARD_BTN_UP_PIN
#define BOARD_BTN_UP_PIN            CONFIG_BOARD_BTN_UP_PIN
#else
#error "Missing mandatory board definition: BOARD_BTN_UP_PIN"
#endif
#endif

#ifndef BOARD_BTN_DOWN_PIN
#ifdef CONFIG_BOARD_BTN_DOWN_PIN
#define BOARD_BTN_DOWN_PIN          CONFIG_BOARD_BTN_DOWN_PIN
#else
#error "Missing mandatory board definition: BOARD_BTN_DOWN_PIN"
#endif
#endif

#ifndef BOARD_BTN_BOOT_PIN
#define BOARD_BTN_BOOT_PIN          BOARD_BTN_UP_PIN
#endif

#ifndef BOARD_BTN_PWR_PIN
#define BOARD_BTN_PWR_PIN           BOARD_BTN_DOWN_PIN
#endif

// Category B Feature Guards

// MicroSD SDMMC Pinout
#ifndef BOARD_HAS_SDCARD
#ifdef CONFIG_BOARD_HAS_SDCARD
#define BOARD_HAS_SDCARD            CONFIG_BOARD_HAS_SDCARD
#else
#define BOARD_HAS_SDCARD            0
#endif
#endif

#if BOARD_HAS_SDCARD
#ifndef BOARD_SDMMC_CLK_PIN
#ifdef CONFIG_BOARD_SDMMC_CLK_PIN
#define BOARD_SDMMC_CLK_PIN         CONFIG_BOARD_SDMMC_CLK_PIN
#else
#error "Missing mandatory board definition: BOARD_SDMMC_CLK_PIN required when BOARD_HAS_SDCARD is enabled"
#endif
#endif

#ifndef BOARD_SDMMC_CMD_PIN
#ifdef CONFIG_BOARD_SDMMC_CMD_PIN
#define BOARD_SDMMC_CMD_PIN         CONFIG_BOARD_SDMMC_CMD_PIN
#else
#error "Missing mandatory board definition: BOARD_SDMMC_CMD_PIN required when BOARD_HAS_SDCARD is enabled"
#endif
#endif

#ifndef BOARD_SDMMC_D0_PIN
#ifdef CONFIG_BOARD_SDMMC_D0_PIN
#define BOARD_SDMMC_D0_PIN          CONFIG_BOARD_SDMMC_D0_PIN
#else
#error "Missing mandatory board definition: BOARD_SDMMC_D0_PIN required when BOARD_HAS_SDCARD is enabled"
#endif
#endif

#ifndef BOARD_SD_CLK_PIN
#ifdef CONFIG_BOARD_SD_CLK_PIN
#define BOARD_SD_CLK_PIN            CONFIG_BOARD_SD_CLK_PIN
#else
#define BOARD_SD_CLK_PIN            BOARD_SDMMC_CLK_PIN
#endif
#endif

#ifndef BOARD_SD_CMD_PIN
#ifdef CONFIG_BOARD_SD_CMD_PIN
#define BOARD_SD_CMD_PIN            CONFIG_BOARD_SD_CMD_PIN
#else
#define BOARD_SD_CMD_PIN            BOARD_SDMMC_CMD_PIN
#endif
#endif

#ifndef BOARD_SD_D0_PIN
#ifdef CONFIG_BOARD_SD_D0_PIN
#define BOARD_SD_D0_PIN             CONFIG_BOARD_SD_D0_PIN
#else
#define BOARD_SD_D0_PIN             BOARD_SDMMC_D0_PIN
#endif
#endif

#else // !BOARD_HAS_SDCARD

#ifndef BOARD_SDMMC_CLK_PIN
#define BOARD_SDMMC_CLK_PIN         -1
#endif
#ifndef BOARD_SDMMC_CMD_PIN
#define BOARD_SDMMC_CMD_PIN         -1
#endif
#ifndef BOARD_SDMMC_D0_PIN
#define BOARD_SDMMC_D0_PIN          -1
#endif
#ifndef BOARD_SD_CLK_PIN
#define BOARD_SD_CLK_PIN            -1
#endif
#ifndef BOARD_SD_CMD_PIN
#define BOARD_SD_CMD_PIN            -1
#endif
#ifndef BOARD_SD_D0_PIN
#define BOARD_SD_D0_PIN             -1
#endif

#endif // BOARD_HAS_SDCARD

// Power Latch
#ifndef BOARD_HAS_PWR_LATCH
#ifdef CONFIG_BOARD_HAS_PWR_LATCH
#define BOARD_HAS_PWR_LATCH         CONFIG_BOARD_HAS_PWR_LATCH
#else
#define BOARD_HAS_PWR_LATCH         0
#endif
#endif

#if BOARD_HAS_PWR_LATCH
#ifndef BOARD_PWR_LATCH_PIN
#ifdef CONFIG_BOARD_PWR_LATCH_PIN
#define BOARD_PWR_LATCH_PIN         CONFIG_BOARD_PWR_LATCH_PIN
#else
#error "Missing mandatory board definition: BOARD_PWR_LATCH_PIN required when BOARD_HAS_PWR_LATCH is enabled"
#endif
#endif

#ifndef BOARD_PWR_LATCH_ACTIVE_LVL
#ifdef CONFIG_BOARD_PWR_LATCH_ACTIVE_LVL
#define BOARD_PWR_LATCH_ACTIVE_LVL  CONFIG_BOARD_PWR_LATCH_ACTIVE_LVL
#else
#error "Missing mandatory board definition: BOARD_PWR_LATCH_ACTIVE_LVL required when BOARD_HAS_PWR_LATCH is enabled"
#endif
#endif

#ifndef BOARD_VBAT_PWR_HOLD_PIN
#define BOARD_VBAT_PWR_HOLD_PIN     BOARD_PWR_LATCH_PIN
#endif

#else // !BOARD_HAS_PWR_LATCH

#ifndef BOARD_PWR_LATCH_PIN
#define BOARD_PWR_LATCH_PIN         -1
#endif
#ifndef BOARD_PWR_LATCH_ACTIVE_LVL
#define BOARD_PWR_LATCH_ACTIVE_LVL  0
#endif
#ifndef BOARD_VBAT_PWR_HOLD_PIN
#define BOARD_VBAT_PWR_HOLD_PIN     -1
#endif

#endif // BOARD_HAS_PWR_LATCH

// Battery ADC
#ifndef BOARD_HAS_BATTERY_ADC
#ifdef CONFIG_BOARD_HAS_BATTERY_ADC
#define BOARD_HAS_BATTERY_ADC       CONFIG_BOARD_HAS_BATTERY_ADC
#else
#define BOARD_HAS_BATTERY_ADC       0
#endif
#endif

#if BOARD_HAS_BATTERY_ADC
#ifndef BOARD_BATTERY_ADC_UNIT
#ifdef CONFIG_BOARD_BATTERY_ADC_UNIT
#define BOARD_BATTERY_ADC_UNIT      CONFIG_BOARD_BATTERY_ADC_UNIT
#else
#error "Missing mandatory board definition: BOARD_BATTERY_ADC_UNIT required when BOARD_HAS_BATTERY_ADC is enabled"
#endif
#endif

#ifndef BOARD_BATTERY_ADC_CHANNEL
#ifdef CONFIG_BOARD_BATTERY_ADC_CHANNEL
#define BOARD_BATTERY_ADC_CHANNEL   CONFIG_BOARD_BATTERY_ADC_CHANNEL
#else
#error "Missing mandatory board definition: BOARD_BATTERY_ADC_CHANNEL required when BOARD_HAS_BATTERY_ADC is enabled"
#endif
#endif

#ifndef BOARD_BATTERY_ADC_ATTEN
#ifdef CONFIG_BOARD_BATTERY_ADC_ATTEN
#define BOARD_BATTERY_ADC_ATTEN     CONFIG_BOARD_BATTERY_ADC_ATTEN
#else
#error "Missing mandatory board definition: BOARD_BATTERY_ADC_ATTEN required when BOARD_HAS_BATTERY_ADC is enabled"
#endif
#endif

#ifndef BOARD_VOLTAGE_DIVIDER_RATIO
#ifdef CONFIG_BOARD_VOLTAGE_DIVIDER_RATIO
#define BOARD_VOLTAGE_DIVIDER_RATIO CONFIG_BOARD_VOLTAGE_DIVIDER_RATIO
#else
#error "Missing mandatory board definition: BOARD_VOLTAGE_DIVIDER_RATIO required when BOARD_HAS_BATTERY_ADC is enabled"
#endif
#endif

#ifndef BOARD_BATTERY_MIN_MV
#ifdef CONFIG_BOARD_BATTERY_MIN_MV
#define BOARD_BATTERY_MIN_MV        CONFIG_BOARD_BATTERY_MIN_MV
#else
#error "Missing mandatory board definition: BOARD_BATTERY_MIN_MV required when BOARD_HAS_BATTERY_ADC is enabled"
#endif
#endif

#ifndef BOARD_BATTERY_MAX_MV
#ifdef CONFIG_BOARD_BATTERY_MAX_MV
#define BOARD_BATTERY_MAX_MV        CONFIG_BOARD_BATTERY_MAX_MV
#else
#error "Missing mandatory board definition: BOARD_BATTERY_MAX_MV required when BOARD_HAS_BATTERY_ADC is enabled"
#endif
#endif

#ifndef BOARD_BATTERY_OCV_TABLE
#error "Missing mandatory board definition: BOARD_BATTERY_OCV_TABLE required when BOARD_HAS_BATTERY_ADC is enabled"
#endif

#else // !BOARD_HAS_BATTERY_ADC

#ifndef BOARD_BATTERY_ADC_UNIT
#define BOARD_BATTERY_ADC_UNIT      0
#endif
#ifndef BOARD_BATTERY_ADC_CHANNEL
#define BOARD_BATTERY_ADC_CHANNEL   -1
#endif
#ifndef BOARD_BATTERY_ADC_ATTEN
#define BOARD_BATTERY_ADC_ATTEN     0
#endif
#ifndef BOARD_VOLTAGE_DIVIDER_RATIO
#define BOARD_VOLTAGE_DIVIDER_RATIO 0.0f
#endif
#ifndef BOARD_BATTERY_MIN_MV
#define BOARD_BATTERY_MIN_MV        0
#endif
#ifndef BOARD_BATTERY_MAX_MV
#define BOARD_BATTERY_MAX_MV        0
#endif
#ifndef BOARD_BATTERY_OCV_TABLE
#define BOARD_BATTERY_OCV_TABLE     { { 0, 0 } }
#endif

#endif // BOARD_HAS_BATTERY_ADC

// Charging & USB Detection
#ifndef BOARD_HAS_CHARGE_DETECT
#ifdef CONFIG_BOARD_HAS_CHARGE_DETECT
#define BOARD_HAS_CHARGE_DETECT     CONFIG_BOARD_HAS_CHARGE_DETECT
#else
#define BOARD_HAS_CHARGE_DETECT     0
#endif
#endif

#if BOARD_HAS_CHARGE_DETECT
#ifndef BOARD_CHARGE_DETECT_PIN
#ifdef CONFIG_BOARD_CHARGE_DETECT_PIN
#define BOARD_CHARGE_DETECT_PIN     CONFIG_BOARD_CHARGE_DETECT_PIN
#else
#error "Missing mandatory board definition: BOARD_CHARGE_DETECT_PIN required when BOARD_HAS_CHARGE_DETECT is enabled"
#endif
#endif

#ifndef BOARD_CHARGE_ACTIVE_LEVEL
#ifdef CONFIG_BOARD_CHARGE_ACTIVE_LEVEL
#define BOARD_CHARGE_ACTIVE_LEVEL   CONFIG_BOARD_CHARGE_ACTIVE_LEVEL
#else
#error "Missing mandatory board definition: BOARD_CHARGE_ACTIVE_LEVEL required when BOARD_HAS_CHARGE_DETECT is enabled"
#endif
#endif

#ifndef BOARD_VBUS_SENSE_PIN
#ifdef CONFIG_BOARD_VBUS_SENSE_PIN
#define BOARD_VBUS_SENSE_PIN        CONFIG_BOARD_VBUS_SENSE_PIN
#else
#define BOARD_VBUS_SENSE_PIN        -1
#endif
#endif

#else // !BOARD_HAS_CHARGE_DETECT

#ifndef BOARD_CHARGE_DETECT_PIN
#define BOARD_CHARGE_DETECT_PIN     -1
#endif
#ifndef BOARD_CHARGE_ACTIVE_LEVEL
#define BOARD_CHARGE_ACTIVE_LEVEL   0
#endif
#ifndef BOARD_VBUS_SENSE_PIN
#define BOARD_VBUS_SENSE_PIN        -1
#endif

#endif // BOARD_HAS_CHARGE_DETECT

// Status LEDs
#ifndef BOARD_HAS_STATUS_LED
#ifdef CONFIG_BOARD_HAS_STATUS_LED
#define BOARD_HAS_STATUS_LED        CONFIG_BOARD_HAS_STATUS_LED
#else
#define BOARD_HAS_STATUS_LED        0
#endif
#endif

#if BOARD_HAS_STATUS_LED
#ifndef BOARD_LED_ACTIVE_LEVEL
#ifdef CONFIG_BOARD_LED_ACTIVE_LEVEL
#define BOARD_LED_ACTIVE_LEVEL      CONFIG_BOARD_LED_ACTIVE_LEVEL
#else
#error "Missing mandatory board definition: BOARD_LED_ACTIVE_LEVEL required when BOARD_HAS_STATUS_LED is enabled"
#endif
#endif

#ifndef BOARD_LED_PIN
#ifdef CONFIG_BOARD_LED_PIN
#define BOARD_LED_PIN               CONFIG_BOARD_LED_PIN
#elif defined(BOARD_LED_GREEN_PIN) && (BOARD_LED_GREEN_PIN >= 0)
#define BOARD_LED_PIN               BOARD_LED_GREEN_PIN
#elif defined(BOARD_LED_RED_PIN) && (BOARD_LED_RED_PIN >= 0)
#define BOARD_LED_PIN               BOARD_LED_RED_PIN
#elif defined(BOARD_LED_BLUE_PIN) && (BOARD_LED_BLUE_PIN >= 0)
#define BOARD_LED_PIN               BOARD_LED_BLUE_PIN
#else
#error "Missing mandatory board definition: BOARD_LED_PIN required when BOARD_HAS_STATUS_LED is enabled"
#endif
#endif

#ifndef BOARD_LED_RED_PIN
#ifdef CONFIG_BOARD_LED_RED_PIN
#define BOARD_LED_RED_PIN           CONFIG_BOARD_LED_RED_PIN
#else
#define BOARD_LED_RED_PIN           -1
#endif
#endif

#ifndef BOARD_LED_GREEN_PIN
#ifdef CONFIG_BOARD_LED_GREEN_PIN
#define BOARD_LED_GREEN_PIN         CONFIG_BOARD_LED_GREEN_PIN
#else
#define BOARD_LED_GREEN_PIN         BOARD_LED_PIN
#endif
#endif

#ifndef BOARD_LED_BLUE_PIN
#ifdef CONFIG_BOARD_LED_BLUE_PIN
#define BOARD_LED_BLUE_PIN          CONFIG_BOARD_LED_BLUE_PIN
#else
#define BOARD_LED_BLUE_PIN          -1
#endif
#endif

#else // !BOARD_HAS_STATUS_LED

#ifndef BOARD_LED_ACTIVE_LEVEL
#define BOARD_LED_ACTIVE_LEVEL      0
#endif
#ifndef BOARD_LED_PIN
#define BOARD_LED_PIN               -1
#endif
#ifndef BOARD_LED_RED_PIN
#define BOARD_LED_RED_PIN           -1
#endif
#ifndef BOARD_LED_GREEN_PIN
#define BOARD_LED_GREEN_PIN         -1
#endif
#ifndef BOARD_LED_BLUE_PIN
#define BOARD_LED_BLUE_PIN          -1
#endif

#endif // BOARD_HAS_STATUS_LED

// Peripheral Power Rails & Audio
#ifndef BOARD_HAS_AUDIO
#ifdef CONFIG_BOARD_HAS_AUDIO
#define BOARD_HAS_AUDIO             CONFIG_BOARD_HAS_AUDIO
#else
#define BOARD_HAS_AUDIO             0
#endif
#endif

#if BOARD_HAS_AUDIO
#ifndef BOARD_AUDIO_PWR_PIN
#ifdef CONFIG_BOARD_AUDIO_PWR_PIN
#define BOARD_AUDIO_PWR_PIN         CONFIG_BOARD_AUDIO_PWR_PIN
#else
#error "Missing mandatory board definition: BOARD_AUDIO_PWR_PIN required when BOARD_HAS_AUDIO is enabled"
#endif
#endif

#ifndef BOARD_AUDIO_PWR_ACTIVE_LVL
#ifdef CONFIG_BOARD_AUDIO_PWR_ACTIVE_LVL
#define BOARD_AUDIO_PWR_ACTIVE_LVL  CONFIG_BOARD_AUDIO_PWR_ACTIVE_LVL
#else
#error "Missing mandatory board definition: BOARD_AUDIO_PWR_ACTIVE_LVL required when BOARD_HAS_AUDIO is enabled"
#endif
#endif

#ifndef BOARD_AUDIO_PA_PIN
#ifdef CONFIG_BOARD_AUDIO_PA_PIN
#define BOARD_AUDIO_PA_PIN          CONFIG_BOARD_AUDIO_PA_PIN
#else
#error "Missing mandatory board definition: BOARD_AUDIO_PA_PIN required when BOARD_HAS_AUDIO is enabled"
#endif
#endif

#ifndef BOARD_I2S_BCLK_PIN
#ifdef CONFIG_BOARD_I2S_BCLK_PIN
#define BOARD_I2S_BCLK_PIN          CONFIG_BOARD_I2S_BCLK_PIN
#else
#error "Missing mandatory board definition: BOARD_I2S_BCLK_PIN required when BOARD_HAS_AUDIO is enabled"
#endif
#endif

#ifndef BOARD_I2S_LRCK_PIN
#ifdef CONFIG_BOARD_I2S_LRCK_PIN
#define BOARD_I2S_LRCK_PIN          CONFIG_BOARD_I2S_LRCK_PIN
#else
#error "Missing mandatory board definition: BOARD_I2S_LRCK_PIN required when BOARD_HAS_AUDIO is enabled"
#endif
#endif

#ifndef BOARD_I2S_DOUT_PIN
#ifdef CONFIG_BOARD_I2S_DOUT_PIN
#define BOARD_I2S_DOUT_PIN          CONFIG_BOARD_I2S_DOUT_PIN
#else
#error "Missing mandatory board definition: BOARD_I2S_DOUT_PIN required when BOARD_HAS_AUDIO is enabled"
#endif
#endif

#else // !BOARD_HAS_AUDIO

#ifndef BOARD_AUDIO_PWR_PIN
#define BOARD_AUDIO_PWR_PIN         -1
#endif
#ifndef BOARD_AUDIO_PWR_ACTIVE_LVL
#define BOARD_AUDIO_PWR_ACTIVE_LVL  0
#endif
#ifndef BOARD_AUDIO_PA_PIN
#define BOARD_AUDIO_PA_PIN          -1
#endif
#ifndef BOARD_I2S_BCLK_PIN
#define BOARD_I2S_BCLK_PIN          -1
#endif
#ifndef BOARD_I2S_LRCK_PIN
#define BOARD_I2S_LRCK_PIN          -1
#endif
#ifndef BOARD_I2S_DOUT_PIN
#define BOARD_I2S_DOUT_PIN          -1
#endif

#endif // BOARD_HAS_AUDIO

#ifndef BOARD_MUSHAF_ENABLE
#ifdef CONFIG_BOARD_MUSHAF_ENABLE
#define BOARD_MUSHAF_ENABLE         CONFIG_BOARD_MUSHAF_ENABLE
#else
#define BOARD_MUSHAF_ENABLE         0
#endif
#endif
#if BOARD_MUSHAF_ENABLE && !defined(CONFIG_BOARD_MUSHAF_ENABLE)
#define CONFIG_BOARD_MUSHAF_ENABLE  1
#elif !defined(CONFIG_BOARD_MUSHAF_ENABLE)
#define CONFIG_BOARD_MUSHAF_ENABLE  0
#endif

#ifdef __cplusplus
}
#endif

#endif // BOARD_CONFIG_H
