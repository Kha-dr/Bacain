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

#if defined(CONFIG_BOARD_PROFILE_WAVESHARE_154) || !defined(CONFIG_BOARD_PROFILE_CUSTOM)
#if __has_include("../../boards/waveshare_154/board_def.h")
#include "../../boards/waveshare_154/board_def.h"
#elif __has_include("../../../boards/waveshare_154/board_def.h")
#include "../../../boards/waveshare_154/board_def.h"
#else
#include "../../boards/waveshare_154/board_def.h"
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Display & E-Paper Parameters (Default: Waveshare ESP32-S3-ePaper-1.54)
#ifndef BOARD_NAME
#ifdef CONFIG_BOARD_NAME
#define BOARD_NAME                  CONFIG_BOARD_NAME
#else
#define BOARD_NAME                  "Waveshare ESP32-S3-ePaper-1.54"
#endif
#endif

#ifndef BOARD_DISPLAY_WIDTH
#ifdef CONFIG_BOARD_DISPLAY_WIDTH
#define BOARD_DISPLAY_WIDTH         CONFIG_BOARD_DISPLAY_WIDTH
#else
#define BOARD_DISPLAY_WIDTH         200
#endif
#endif

#ifndef BOARD_DISPLAY_HEIGHT
#ifdef CONFIG_BOARD_DISPLAY_HEIGHT
#define BOARD_DISPLAY_HEIGHT        CONFIG_BOARD_DISPLAY_HEIGHT
#else
#define BOARD_DISPLAY_HEIGHT        200
#endif
#endif

#ifndef BOARD_DISPLAY_ROTATION
#ifdef CONFIG_BOARD_DISPLAY_ROTATION
#define BOARD_DISPLAY_ROTATION      CONFIG_BOARD_DISPLAY_ROTATION
#else
#define BOARD_DISPLAY_ROTATION      0
#endif
#endif

#ifndef BOARD_DISPLAY_BUFFER_SIZE
#define BOARD_DISPLAY_BUFFER_SIZE   (BOARD_DISPLAY_WIDTH * BOARD_DISPLAY_HEIGHT / 8) // 5000 bytes default
#endif

// E-Paper SPI Pinout (Default: Waveshare ESP32-S3-ePaper-1.54)
#ifndef BOARD_EPD_DC_PIN
#ifdef CONFIG_BOARD_EPD_DC_PIN
#define BOARD_EPD_DC_PIN            CONFIG_BOARD_EPD_DC_PIN
#else
#define BOARD_EPD_DC_PIN            10
#endif
#endif

#ifndef BOARD_EPD_CS_PIN
#ifdef CONFIG_BOARD_EPD_CS_PIN
#define BOARD_EPD_CS_PIN            CONFIG_BOARD_EPD_CS_PIN
#else
#define BOARD_EPD_CS_PIN            11
#endif
#endif

#ifndef BOARD_EPD_SCK_PIN
#ifdef CONFIG_BOARD_EPD_SCK_PIN
#define BOARD_EPD_SCK_PIN           CONFIG_BOARD_EPD_SCK_PIN
#else
#define BOARD_EPD_SCK_PIN           12
#endif
#endif

#ifndef BOARD_EPD_MOSI_PIN
#ifdef CONFIG_BOARD_EPD_MOSI_PIN
#define BOARD_EPD_MOSI_PIN          CONFIG_BOARD_EPD_MOSI_PIN
#else
#define BOARD_EPD_MOSI_PIN          13
#endif
#endif

#ifndef BOARD_EPD_RST_PIN
#ifdef CONFIG_BOARD_EPD_RST_PIN
#define BOARD_EPD_RST_PIN           CONFIG_BOARD_EPD_RST_PIN
#else
#define BOARD_EPD_RST_PIN           9
#endif
#endif

#ifndef BOARD_EPD_BUSY_PIN
#ifdef CONFIG_BOARD_EPD_BUSY_PIN
#define BOARD_EPD_BUSY_PIN          CONFIG_BOARD_EPD_BUSY_PIN
#else
#define BOARD_EPD_BUSY_PIN          8
#endif
#endif

#ifndef BOARD_EPD_PWR_PIN
#ifdef CONFIG_BOARD_EPD_PWR_PIN
#define BOARD_EPD_PWR_PIN           CONFIG_BOARD_EPD_PWR_PIN
#else
#define BOARD_EPD_PWR_PIN           6
#endif
#endif

// Navigation Buttons (Default: Waveshare ESP32-S3-ePaper-1.54: BOOT = UP, PWR = DOWN)
#ifndef BOARD_BTN_UP_PIN
#ifdef CONFIG_BOARD_BTN_UP_PIN
#define BOARD_BTN_UP_PIN            CONFIG_BOARD_BTN_UP_PIN
#else
#define BOARD_BTN_UP_PIN            0
#endif
#endif

#ifndef BOARD_BTN_DOWN_PIN
#ifdef CONFIG_BOARD_BTN_DOWN_PIN
#define BOARD_BTN_DOWN_PIN          CONFIG_BOARD_BTN_DOWN_PIN
#else
#define BOARD_BTN_DOWN_PIN          18
#endif
#endif

// Aliases
#ifndef BOARD_BTN_BOOT_PIN
#define BOARD_BTN_BOOT_PIN          BOARD_BTN_UP_PIN
#endif

#ifndef BOARD_BTN_PWR_PIN
#define BOARD_BTN_PWR_PIN           BOARD_BTN_DOWN_PIN
#endif

// MicroSD SDMMC Pinout (Default: Waveshare ESP32-S3-ePaper-1.54)
#ifndef BOARD_SDMMC_D0_PIN
#ifdef CONFIG_BOARD_SDMMC_D0_PIN
#define BOARD_SDMMC_D0_PIN          CONFIG_BOARD_SDMMC_D0_PIN
#else
#define BOARD_SDMMC_D0_PIN          40
#endif
#endif

#ifndef BOARD_SDMMC_CLK_PIN
#ifdef CONFIG_BOARD_SDMMC_CLK_PIN
#define BOARD_SDMMC_CLK_PIN         CONFIG_BOARD_SDMMC_CLK_PIN
#else
#define BOARD_SDMMC_CLK_PIN         39
#endif
#endif

#ifndef BOARD_SDMMC_CMD_PIN
#ifdef CONFIG_BOARD_SDMMC_CMD_PIN
#define BOARD_SDMMC_CMD_PIN         CONFIG_BOARD_SDMMC_CMD_PIN
#else
#define BOARD_SDMMC_CMD_PIN         41
#endif
#endif

// MicroSD SD Pinout
#ifndef BOARD_SD_CLK_PIN
#ifdef CONFIG_BOARD_SD_CLK_PIN
#define BOARD_SD_CLK_PIN            CONFIG_BOARD_SD_CLK_PIN
#else
#define BOARD_SD_CLK_PIN            14
#endif
#endif

#ifndef BOARD_SD_CMD_PIN
#ifdef CONFIG_BOARD_SD_CMD_PIN
#define BOARD_SD_CMD_PIN            CONFIG_BOARD_SD_CMD_PIN
#else
#define BOARD_SD_CMD_PIN            15
#endif
#endif

#ifndef BOARD_SD_D0_PIN
#ifdef CONFIG_BOARD_SD_D0_PIN
#define BOARD_SD_D0_PIN             CONFIG_BOARD_SD_D0_PIN
#else
#define BOARD_SD_D0_PIN             16
#endif
#endif

// Power Management & Battery ADC (Default: Waveshare ESP32-S3-ePaper-1.54)
#ifndef BOARD_VBAT_PWR_HOLD_PIN
#ifdef CONFIG_BOARD_VBAT_PWR_HOLD_PIN
#define BOARD_VBAT_PWR_HOLD_PIN     CONFIG_BOARD_VBAT_PWR_HOLD_PIN
#else
#define BOARD_VBAT_PWR_HOLD_PIN     17
#endif
#endif

#ifndef BOARD_PWR_LATCH_PIN
#ifdef CONFIG_BOARD_PWR_LATCH_PIN
#define BOARD_PWR_LATCH_PIN         CONFIG_BOARD_PWR_LATCH_PIN
#else
#define BOARD_PWR_LATCH_PIN         BOARD_VBAT_PWR_HOLD_PIN
#endif
#endif

#ifndef BOARD_AUDIO_PWR_PIN
#ifdef CONFIG_BOARD_AUDIO_PWR_PIN
#define BOARD_AUDIO_PWR_PIN         CONFIG_BOARD_AUDIO_PWR_PIN
#else
#define BOARD_AUDIO_PWR_PIN         42
#endif
#endif

#ifndef BOARD_AUDIO_PA_PIN
#ifdef CONFIG_BOARD_AUDIO_PA_PIN
#define BOARD_AUDIO_PA_PIN          CONFIG_BOARD_AUDIO_PA_PIN
#else
#define BOARD_AUDIO_PA_PIN          46
#endif
#endif

#ifndef BOARD_LED_GREEN_PIN
#ifdef CONFIG_BOARD_LED_GREEN_PIN
#define BOARD_LED_GREEN_PIN         CONFIG_BOARD_LED_GREEN_PIN
#else
#define BOARD_LED_GREEN_PIN         3
#endif
#endif

#ifndef BOARD_BATTERY_ADC_CHANNEL
#ifdef CONFIG_BOARD_BATTERY_ADC_CHANNEL
#define BOARD_BATTERY_ADC_CHANNEL   CONFIG_BOARD_BATTERY_ADC_CHANNEL
#else
#define BOARD_BATTERY_ADC_CHANNEL   3 // GPIO 4 (ADC1_CH3 on ESP32-S3)
#endif
#endif

#ifndef BOARD_BATTERY_ADC_ATTEN
#ifdef CONFIG_BOARD_BATTERY_ADC_ATTEN
#define BOARD_BATTERY_ADC_ATTEN     CONFIG_BOARD_BATTERY_ADC_ATTEN
#else
#define BOARD_BATTERY_ADC_ATTEN     ADC_ATTEN_DB_12
#endif
#endif

#ifndef BOARD_BATTERY_MAX_MV
#define BOARD_BATTERY_MAX_MV        4200
#endif

#ifndef BOARD_BATTERY_MIN_MV
#define BOARD_BATTERY_MIN_MV        3300
#endif

#ifndef BOARD_VOLTAGE_DIVIDER_RATIO
#define BOARD_VOLTAGE_DIVIDER_RATIO 2.0f
#endif

// Audio I2S Pinout
#ifndef BOARD_I2S_BCLK_PIN
#ifdef CONFIG_BOARD_I2S_BCLK_PIN
#define BOARD_I2S_BCLK_PIN          CONFIG_BOARD_I2S_BCLK_PIN
#else
#define BOARD_I2S_BCLK_PIN          4
#endif
#endif

#ifndef BOARD_I2S_LRCK_PIN
#ifdef CONFIG_BOARD_I2S_LRCK_PIN
#define BOARD_I2S_LRCK_PIN          CONFIG_BOARD_I2S_LRCK_PIN
#else
#define BOARD_I2S_LRCK_PIN          5
#endif
#endif

#ifndef BOARD_I2S_DOUT_PIN
#ifdef CONFIG_BOARD_I2S_DOUT_PIN
#define BOARD_I2S_DOUT_PIN          CONFIG_BOARD_I2S_DOUT_PIN
#else
#define BOARD_I2S_DOUT_PIN          7
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif // BOARD_CONFIG_H
