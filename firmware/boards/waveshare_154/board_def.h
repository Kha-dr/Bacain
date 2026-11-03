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

#ifndef BOARD_PWR_LATCH_PIN
#define BOARD_PWR_LATCH_PIN         17
#endif

#ifndef BOARD_BATTERY_ADC_CHANNEL
#define BOARD_BATTERY_ADC_CHANNEL   3
#endif

#ifndef BOARD_BATTERY_ADC_ATTEN
#define BOARD_BATTERY_ADC_ATTEN     ADC_ATTEN_DB_12
#endif

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
