#include "board_config.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "board_waveshare_154";

void board_power_pre_sleep(void) {
    ESP_LOGI(TAG, "Waveshare 1.54 pre-sleep hook");
#if defined(BOARD_AUDIO_PWR_PIN) && (BOARD_AUDIO_PWR_PIN >= 0)
    if (GPIO_IS_VALID_OUTPUT_GPIO(BOARD_AUDIO_PWR_PIN)) {
        gpio_set_level((gpio_num_t)BOARD_AUDIO_PWR_PIN, !BOARD_AUDIO_PWR_ACTIVE_LVL);
    }
#endif
#if defined(BOARD_EPD_PWR_PIN) && (BOARD_EPD_PWR_PIN >= 0)
    if (GPIO_IS_VALID_OUTPUT_GPIO(BOARD_EPD_PWR_PIN)) {
        gpio_set_level((gpio_num_t)BOARD_EPD_PWR_PIN, !BOARD_EPD_PWR_ACTIVE_LVL);
    }
#endif
}
