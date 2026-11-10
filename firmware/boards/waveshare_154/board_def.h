#ifndef BOARD_WAVESHARE_154_H
#define BOARD_WAVESHARE_154_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BOARD_NAME
#define BOARD_NAME                  "Waveshare ESP32-S3-ePaper-1.54"
#endif

#ifndef BOARD_DISPLAY_WIDTH
#define BOARD_DISPLAY_WIDTH         200
#endif

#ifndef BOARD_DISPLAY_HEIGHT
#define BOARD_DISPLAY_HEIGHT        200
#endif

#ifndef BOARD_DISPLAY_ROTATION
#define BOARD_DISPLAY_ROTATION      0
#endif

#ifndef BOARD_DISPLAY_CONTROLLER
#define BOARD_DISPLAY_CONTROLLER    "SSD1681"
#endif

#ifndef BOARD_EPD_DC_PIN
#define BOARD_EPD_DC_PIN            10
#endif

#ifndef BOARD_EPD_CS_PIN
#define BOARD_EPD_CS_PIN            11
#endif

#ifndef BOARD_EPD_SCK_PIN
#define BOARD_EPD_SCK_PIN           12
#endif

#ifndef BOARD_EPD_MOSI_PIN
#define BOARD_EPD_MOSI_PIN          13
#endif

#ifndef BOARD_EPD_RST_PIN
#define BOARD_EPD_RST_PIN           9
#endif

#ifndef BOARD_EPD_BUSY_PIN
#define BOARD_EPD_BUSY_PIN          8
#endif

#ifndef BOARD_EPD_PWR_PIN
#define BOARD_EPD_PWR_PIN           6
#endif

#ifndef BOARD_EPD_PWR_ACTIVE_LVL
#define BOARD_EPD_PWR_ACTIVE_LVL    0
#endif

#ifndef BOARD_EPD_BUSY_ACTIVE_LVL
#define BOARD_EPD_BUSY_ACTIVE_LVL   1
#endif

#ifndef BOARD_EPD_RST_PULSE_US
#define BOARD_EPD_RST_PULSE_US      20       /* SSD1681 min reset pulse is 10us; 20us margin */
#endif

#ifndef BOARD_EPD_RST_RECOVERY_US
#define BOARD_EPD_RST_RECOVERY_US   200      /* Post-reset recovery delay */
#endif

#ifndef BOARD_HAS_SDCARD
#define BOARD_HAS_SDCARD            1
#endif

#ifndef BOARD_SDMMC_CLK_PIN
#define BOARD_SDMMC_CLK_PIN         39
#endif

#ifndef BOARD_SDMMC_CMD_PIN
#define BOARD_SDMMC_CMD_PIN         41
#endif

#ifndef BOARD_SDMMC_D0_PIN
#define BOARD_SDMMC_D0_PIN          40
#endif

#ifndef BOARD_SD_CLK_PIN
#define BOARD_SD_CLK_PIN            BOARD_SDMMC_CLK_PIN
#endif

#ifndef BOARD_SD_CMD_PIN
#define BOARD_SD_CMD_PIN            BOARD_SDMMC_CMD_PIN
#endif

#ifndef BOARD_SD_D0_PIN
#define BOARD_SD_D0_PIN             BOARD_SDMMC_D0_PIN
#endif

#ifndef BOARD_BTN_UP_PIN
#define BOARD_BTN_UP_PIN            0
#endif

#ifndef BOARD_BTN_DOWN_PIN
#define BOARD_BTN_DOWN_PIN          18
#endif

#ifndef BOARD_BTN_BOOT_PIN
#define BOARD_BTN_BOOT_PIN          BOARD_BTN_UP_PIN
#endif

#ifndef BOARD_BTN_PWR_PIN
#define BOARD_BTN_PWR_PIN           BOARD_BTN_DOWN_PIN
#endif

// Power Latch
#ifndef BOARD_HAS_PWR_LATCH
#define BOARD_HAS_PWR_LATCH         1
#endif

#ifndef BOARD_PWR_LATCH_PIN
#define BOARD_PWR_LATCH_PIN         17
#endif

#ifndef BOARD_PWR_LATCH_ACTIVE_LVL
#define BOARD_PWR_LATCH_ACTIVE_LVL  1
#endif

// Battery ADC
#ifndef BOARD_HAS_BATTERY_ADC
#define BOARD_HAS_BATTERY_ADC       1
#endif

#ifndef BOARD_BATTERY_ADC_UNIT
#define BOARD_BATTERY_ADC_UNIT      1
#endif

#ifndef BOARD_BATTERY_ADC_CHANNEL
#define BOARD_BATTERY_ADC_CHANNEL   3
#endif

#ifndef BOARD_BATTERY_ADC_ATTEN
#define BOARD_BATTERY_ADC_ATTEN     ADC_ATTEN_DB_12
#endif

#ifndef BOARD_VOLTAGE_DIVIDER_RATIO
#define BOARD_VOLTAGE_DIVIDER_RATIO 2.0f
#endif

#ifndef BOARD_BATTERY_MIN_MV
#define BOARD_BATTERY_MIN_MV        3300
#endif

#ifndef BOARD_BATTERY_MAX_MV
#define BOARD_BATTERY_MAX_MV        4140
#endif

#ifndef BOARD_BATTERY_OCV_TABLE
#define BOARD_BATTERY_OCV_TABLE { \
    { 1000, 100 }, \
    {  864,  90 }, \
    {  773,  80 }, \
    {  682,  70 }, \
    {  614,  60 }, \
    {  568,  50 }, \
    {  523,  40 }, \
    {  489,  30 }, \
    {  432,  20 }, \
    {  341,  10 }, \
    {  170,   5 }, \
    {    0,   0 }  \
}
#endif

// Charging & USB Detection
#ifndef BOARD_HAS_CHARGE_DETECT
#define BOARD_HAS_CHARGE_DETECT     0
#endif

#ifndef BOARD_CHARGE_DETECT_PIN
#define BOARD_CHARGE_DETECT_PIN     -1
#endif

#ifndef BOARD_CHARGE_ACTIVE_LEVEL
#define BOARD_CHARGE_ACTIVE_LEVEL   0
#endif

#ifndef BOARD_VBUS_SENSE_PIN
#define BOARD_VBUS_SENSE_PIN        -1
#endif

// Status LEDs
#ifndef BOARD_HAS_STATUS_LED
#define BOARD_HAS_STATUS_LED        1
#endif

#ifndef BOARD_LED_ACTIVE_LEVEL
#define BOARD_LED_ACTIVE_LEVEL      0
#endif

#ifndef BOARD_LED_PIN
#define BOARD_LED_PIN               3
#endif

#ifndef BOARD_LED_RED_PIN
#define BOARD_LED_RED_PIN           -1
#endif

#ifndef BOARD_LED_GREEN_PIN
#define BOARD_LED_GREEN_PIN         3
#endif

#ifndef BOARD_LED_BLUE_PIN
#define BOARD_LED_BLUE_PIN          -1
#endif

// Peripheral Power Rails
#ifndef BOARD_HAS_AUDIO
#define BOARD_HAS_AUDIO             1
#endif

#ifndef BOARD_AUDIO_PWR_PIN
#define BOARD_AUDIO_PWR_PIN         42
#endif

#ifndef BOARD_AUDIO_PWR_ACTIVE_LVL
#define BOARD_AUDIO_PWR_ACTIVE_LVL  0
#endif

#ifndef BOARD_AUDIO_PA_PIN
#define BOARD_AUDIO_PA_PIN          46
#endif

// Audio I2S Pinout

#ifndef BOARD_I2S_BCLK_PIN
#define BOARD_I2S_BCLK_PIN          4
#endif

#ifndef BOARD_I2S_LRCK_PIN
#define BOARD_I2S_LRCK_PIN          5
#endif

#ifndef BOARD_I2S_DOUT_PIN
#define BOARD_I2S_DOUT_PIN          7
#endif

#ifdef __cplusplus
}
#endif

#endif // BOARD_WAVESHARE_154_H
