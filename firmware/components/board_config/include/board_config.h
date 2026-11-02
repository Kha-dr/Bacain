#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

// Display & E-Paper Parameters
#define BOARD_DISPLAY_WIDTH         200
#define BOARD_DISPLAY_HEIGHT        200
#define BOARD_DISPLAY_ROTATION      0 // 0: upright portrait, 1: 90 deg, 2: 180 deg, 3: 270 deg
#define BOARD_DISPLAY_BUFFER_SIZE   (BOARD_DISPLAY_WIDTH * BOARD_DISPLAY_HEIGHT / 8) // 5000 bytes

// E-Paper SPI Pinout
#define BOARD_EPD_DC_PIN            10
#define BOARD_EPD_CS_PIN            11
#define BOARD_EPD_SCK_PIN           12
#define BOARD_EPD_MOSI_PIN          13
#define BOARD_EPD_RST_PIN           9
#define BOARD_EPD_BUSY_PIN          8
#define BOARD_EPD_PWR_PIN           GPIO_NUM_6

// Navigation Buttons
#define BOARD_BTN_DOWN_PIN          GPIO_NUM_0   // Tombol navigasi turun / Lanjut halaman (dulu BOOT)
#define BOARD_BTN_UP_PIN            GPIO_NUM_18  // Tombol navigasi naik / Balik halaman (dulu PWR)
// Aliases
#define BOARD_BTN_BOOT_PIN          BOARD_BTN_DOWN_PIN
#define BOARD_BTN_PWR_PIN           BOARD_BTN_UP_PIN

// MicroSD SDMMC Pinout
#define BOARD_SDMMC_D0_PIN          GPIO_NUM_40
#define BOARD_SDMMC_CLK_PIN         GPIO_NUM_39
#define BOARD_SDMMC_CMD_PIN         GPIO_NUM_41

// Power Management & Battery ADC
#define BOARD_VBAT_PWR_HOLD_PIN     GPIO_NUM_17
#define BOARD_AUDIO_PWR_PIN         GPIO_NUM_42
#define BOARD_AUDIO_PA_PIN          GPIO_NUM_46
#define BOARD_LED_GREEN_PIN         GPIO_NUM_3
#define BOARD_BATTERY_ADC_CHANNEL   ADC_CHANNEL_3 // GPIO 4 (ADC1_CH3 on ESP32-S3)
#define BOARD_BATTERY_MAX_MV        4200
#define BOARD_BATTERY_MIN_MV        3300
#define BOARD_VOLTAGE_DIVIDER_RATIO 2.0f

#ifdef __cplusplus
}
#endif

#endif // BOARD_CONFIG_H
