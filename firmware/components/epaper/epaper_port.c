#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "epaper_port.h"
#include "board_config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if __has_include("esp_rom_sys.h")
#include "esp_rom_sys.h"
#else
void esp_rom_delay_us(uint32_t us);
#endif

#define EPD_DC_PIN    BOARD_EPD_DC_PIN
#define EPD_CS_PIN    BOARD_EPD_CS_PIN
#define EPD_SCK_PIN   BOARD_EPD_SCK_PIN
#define EPD_MOSI_PIN  BOARD_EPD_MOSI_PIN
#define EPD_RST_PIN   BOARD_EPD_RST_PIN
#define EPD_BUSY_PIN  BOARD_EPD_BUSY_PIN
#define EPD_PWR_PIN   BOARD_EPD_PWR_PIN

#define epaper_rst_1() gpio_set_level(EPD_RST_PIN, 1)
#define epaper_rst_0() gpio_set_level(EPD_RST_PIN, 0)
#define epaper_cs_1   gpio_set_level(EPD_CS_PIN, 1)
#define epaper_cs_0   gpio_set_level(EPD_CS_PIN, 0)
#define epaper_dc_1   gpio_set_level(EPD_DC_PIN, 1)
#define epaper_dc_0   gpio_set_level(EPD_DC_PIN, 0)
#define ReadBusy      gpio_get_level(EPD_BUSY_PIN)

const uint8_t * __attribute__((weak)) board_epd_get_lut_full(size_t *out_len)
{
    if (out_len) *out_len = 0;
    return NULL;
}

const uint8_t * __attribute__((weak)) board_epd_get_lut_partial(size_t *out_len)
{
    if (out_len) *out_len = 0;
    return NULL;
}

static spi_device_handle_t spi = NULL;

static void epaper_gpio_init(void) {
    gpio_reset_pin(EPD_RST_PIN);
    gpio_reset_pin(EPD_DC_PIN);
    gpio_reset_pin(EPD_CS_PIN);
    gpio_reset_pin(EPD_BUSY_PIN);

    gpio_config_t gpio_conf = {};
    gpio_conf.intr_type     = GPIO_INTR_DISABLE;
    gpio_conf.mode          = GPIO_MODE_OUTPUT;
    gpio_conf.pin_bit_mask  = ((uint64_t) 0x01 << EPD_RST_PIN) |
                              ((uint64_t) 0x01 << EPD_DC_PIN) |
                              ((uint64_t) 0x01 << EPD_CS_PIN);
    gpio_conf.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en    = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

    gpio_conf.mode         = GPIO_MODE_INPUT;
    gpio_conf.pin_bit_mask = ((uint64_t) 0x01 << EPD_BUSY_PIN);
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

    epaper_rst_1();
}

static void epaper_reset(void)
{
    epaper_rst_1();
    esp_rom_delay_us(BOARD_EPD_RST_RECOVERY_US);
    epaper_rst_0();
    esp_rom_delay_us(BOARD_EPD_RST_PULSE_US);
    epaper_rst_1();
    esp_rom_delay_us(BOARD_EPD_RST_RECOVERY_US);
}

static void epaper_spi_init(void) {
    static bool s_spi_initialized = false;
    if (s_spi_initialized && spi != NULL) {
        epaper_gpio_init();
        return;
    }

    esp_err_t ret;
    spi_bus_config_t buscfg = {
        .miso_io_num   = -1,
        .mosi_io_num   = EPD_MOSI_PIN,
        .sclk_io_num   = EPD_SCK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 65536,
    };
    spi_device_interface_config_t devcfg = {
        .spics_io_num  = -1,
        .clock_speed_hz = 20 * 1000 * 1000, // 20 MHz
        .mode          = 0,
        .queue_size    = 7,
    };

    ret = spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE("EPD", "spi_bus_initialize failed: %d", ret);
        return;
    }

    if (spi == NULL) {
        ret = spi_bus_add_device(SPI3_HOST, &devcfg, &spi);
        ESP_ERROR_CHECK(ret);
    }
    epaper_gpio_init();
    s_spi_initialized = true;
}

#ifndef BOARD_EPD_BUSY_ACTIVE_LVL
#define BOARD_EPD_BUSY_ACTIVE_LVL 1
#endif

/*
  Waiting for e-Paper idle.
*/
static void epaper_readbusyh(void) {
    ESP_LOGI("EPD", "Waiting for e-Paper idle (pin=%d)...", ReadBusy);
    vTaskDelay(pdMS_TO_TICKS(10));
    int timeout = 0;
    while (ReadBusy == BOARD_EPD_BUSY_ACTIVE_LVL) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout++;
        if (timeout > 400) { // 4 seconds safety timeout
            ESP_LOGW("EPD", "e-Paper busy timeout (pin=%d)", ReadBusy);
            return;
        }
    }
    ESP_LOGI("EPD", "e-Paper idle release (pin=%d, elapsed=%d ms)", ReadBusy, timeout * 10);
}

static void spi_send_byte(uint8_t data) {
    if (spi == NULL) {
        ESP_LOGE("EPD", "SPI device not initialized");
        return;
    }
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length    = 8;
    t.tx_buffer = &data;
    ret = spi_device_polling_transmit(spi, &t);
    assert(ret == ESP_OK);
}

static void epaper_SendCommand(uint8_t Reg) {
    epaper_dc_0;
    epaper_cs_0;
    spi_send_byte(Reg);
    epaper_cs_1;
}

static void epaper_SendData(uint8_t Data) {
    epaper_dc_1;
    epaper_cs_0;
    spi_send_byte(Data);
    epaper_cs_1;
}

static void epaper_write_bytes(const uint8_t *Data, int len) {
    if (spi == NULL || Data == NULL || len <= 0) {
        return;
    }
    epaper_dc_1;
    epaper_cs_0;
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length    = 8 * len;
    t.tx_buffer = Data;
    ret = spi_device_polling_transmit(spi, &t);
    assert(ret == ESP_OK);
    epaper_cs_1;
}

void epaper_Sendbuffera(uint8_t *Data, int len) {
    epaper_write_bytes(Data, len);
}

static void epaper_set_lut(const uint8_t *lut) {
    epaper_SendCommand(0x32);
    epaper_write_bytes(lut, 153);
    epaper_readbusyh();

    epaper_SendCommand(0x3F);
    epaper_SendData(lut[153]);

    epaper_SendCommand(0x03);
    epaper_SendData(lut[154]);

    epaper_SendCommand(0x04);
    epaper_SendData(lut[155]);
    epaper_SendData(lut[156]);
    epaper_SendData(lut[157]);

    epaper_SendCommand(0x2C);
    epaper_SendData(lut[158]);
}

static void epaper_TurnOnDisplay(void) {
    epaper_SendCommand(0x22);
    epaper_SendData(0xC7);
    epaper_SendCommand(0x20);
    epaper_readbusyh();
}

static void epaper_TurnOnDisplayPart(void) {
    epaper_SendCommand(0x22);
    epaper_SendData(0xCF);
    epaper_SendCommand(0x20);
    epaper_readbusyh();
}

void epaper_port_init(void) {
    const uint8_t x_end_byte = (BOARD_DISPLAY_WIDTH / 8) - 1;
    const uint16_t y_end_line = BOARD_DISPLAY_HEIGHT - 1;

    epaper_spi_init();
    epaper_reset();

    epaper_readbusyh();
    epaper_SendCommand(0x12);  // SWRESET
    epaper_readbusyh();

    epaper_SendCommand(0x01);  // Driver output control
    epaper_SendData((uint8_t)(y_end_line & 0xFF));
    epaper_SendData((uint8_t)((y_end_line >> 8) & 0xFF));
    epaper_SendData(0x01);

    epaper_SendCommand(0x11);  // Data entry mode
    epaper_SendData(0x01);

    epaper_SendCommand(0x44);  // Set Windows X
    epaper_SendData(0x00);
    epaper_SendData(x_end_byte);

    epaper_SendCommand(0x45);  // Set Windows Y
    epaper_SendData((uint8_t)(y_end_line & 0xFF));
    epaper_SendData((uint8_t)((y_end_line >> 8) & 0xFF));
    epaper_SendData(0x00);
    epaper_SendData(0x00);

    epaper_SendCommand(0x3C);  // BorderWaveform
    epaper_SendData(0x01);

    epaper_SendCommand(0x18);
    epaper_SendData(0x80);

    epaper_SendCommand(0x22);  // Load Temperature and waveform setting
    epaper_SendData(0xB1);
    epaper_SendCommand(0x20);  // Master Activation
    epaper_readbusyh();

    epaper_SendCommand(0x4E);  // Set Cursor X
    epaper_SendData(0x00);

    epaper_SendCommand(0x4F);  // Set Cursor Y
    epaper_SendData((uint8_t)(y_end_line & 0xFF));
    epaper_SendData((uint8_t)((y_end_line >> 8) & 0xFF));
    epaper_readbusyh();

    const uint8_t *lut_full = board_epd_get_lut_full(NULL);
    if (lut_full != NULL) {
        epaper_set_lut(lut_full);
    }
}

void epaper_port_init_partial(void) {
    const uint8_t x_end_byte = (BOARD_DISPLAY_WIDTH / 8) - 1;
    const uint16_t y_end_line = BOARD_DISPLAY_HEIGHT - 1;

    epaper_spi_init();
    epaper_reset();
    epaper_readbusyh();

    const uint8_t *lut_part = board_epd_get_lut_partial(NULL);
    if (lut_part != NULL) {
        epaper_set_lut(lut_part);
    }

    epaper_SendCommand(0x37);
    epaper_SendData(0x00);
    epaper_SendData(0x00);
    epaper_SendData(0x00);
    epaper_SendData(0x00);
    epaper_SendData(0x00);
    epaper_SendData(0x40);
    epaper_SendData(0x00);
    epaper_SendData(0x00);
    epaper_SendData(0x00);
    epaper_SendData(0x00);

    epaper_SendCommand(0x3C);
    epaper_SendData(0x80);

    epaper_SendCommand(0x22);
    epaper_SendData(0xC0);
    epaper_SendCommand(0x20);
    epaper_readbusyh();

    epaper_SendCommand(0x01);  // Driver output control
    epaper_SendData((uint8_t)(y_end_line & 0xFF));
    epaper_SendData((uint8_t)((y_end_line >> 8) & 0xFF));
    epaper_SendData(0x01);

    epaper_SendCommand(0x11);
    epaper_SendData(0x01);

    epaper_SendCommand(0x44);
    epaper_SendData(0x00);
    epaper_SendData(x_end_byte);

    epaper_SendCommand(0x45);
    epaper_SendData((uint8_t)(y_end_line & 0xFF));
    epaper_SendData((uint8_t)((y_end_line >> 8) & 0xFF));
    epaper_SendData(0x00);
    epaper_SendData(0x00);

    epaper_SendCommand(0x4E);
    epaper_SendData(0x00);
    epaper_SendCommand(0x4F);
    epaper_SendData((uint8_t)(y_end_line & 0xFF));
    epaper_SendData((uint8_t)((y_end_line >> 8) & 0xFF));
}

void epaper_port_clear(uint8_t color) {
    const uint16_t y_end_line = BOARD_DISPLAY_HEIGHT - 1;
    uint8_t fill_val = (color == 0x00) ? 0x00 : 0xFF;
    uint8_t line_buffer[250];
    memset(line_buffer, fill_val, sizeof(line_buffer));

    epaper_SendCommand(0x4E);  // Set Cursor X
    epaper_SendData(0x00);
    epaper_SendCommand(0x4F);  // Set Cursor Y
    epaper_SendData((uint8_t)(y_end_line & 0xFF));
    epaper_SendData((uint8_t)((y_end_line >> 8) & 0xFF));

    epaper_SendCommand(0x24);
    for (int i = 0; i < (BOARD_DISPLAY_BUFFER_SIZE / sizeof(line_buffer)); i++) {
        epaper_write_bytes(line_buffer, sizeof(line_buffer));
    }

    epaper_SendCommand(0x4E);  // Set Cursor X
    epaper_SendData(0x00);
    epaper_SendCommand(0x4F);  // Set Cursor Y
    epaper_SendData((uint8_t)(y_end_line & 0xFF));
    epaper_SendData((uint8_t)((y_end_line >> 8) & 0xFF));

    epaper_SendCommand(0x26);
    for (int i = 0; i < (BOARD_DISPLAY_BUFFER_SIZE / sizeof(line_buffer)); i++) {
        epaper_write_bytes(line_buffer, sizeof(line_buffer));
    }

    epaper_TurnOnDisplay();
}

void epaper_port_display(const uint8_t *Image) {
    if (!Image) return;

    const uint16_t y_end_line = BOARD_DISPLAY_HEIGHT - 1;

    epaper_SendCommand(0x4E);  // Set Cursor X
    epaper_SendData(0x00);
    epaper_SendCommand(0x4F);  // Set Cursor Y
    epaper_SendData((uint8_t)(y_end_line & 0xFF));
    epaper_SendData((uint8_t)((y_end_line >> 8) & 0xFF));

    epaper_SendCommand(0x24);
    epaper_write_bytes(Image, BOARD_DISPLAY_BUFFER_SIZE);

    epaper_SendCommand(0x4E);  // Set Cursor X
    epaper_SendData(0x00);
    epaper_SendCommand(0x4F);  // Set Cursor Y
    epaper_SendData((uint8_t)(y_end_line & 0xFF));
    epaper_SendData((uint8_t)((y_end_line >> 8) & 0xFF));

    epaper_SendCommand(0x26);
    epaper_write_bytes(Image, BOARD_DISPLAY_BUFFER_SIZE);

    epaper_TurnOnDisplay();
}

void epaper_port_display_part(const uint8_t *Image) {
    if (!Image) {
        return;
    }

    const uint16_t y_end_line = BOARD_DISPLAY_HEIGHT - 1;

    epaper_SendCommand(0x4E);  // Set Cursor X
    epaper_SendData(0x00);
    epaper_SendCommand(0x4F);  // Set Cursor Y
    epaper_SendData((uint8_t)(y_end_line & 0xFF));
    epaper_SendData((uint8_t)((y_end_line >> 8) & 0xFF));

    epaper_SendCommand(0x24);
    epaper_write_bytes(Image, BOARD_DISPLAY_BUFFER_SIZE);

    epaper_TurnOnDisplayPart();
}

void epaper_port_display_part_diff(const uint8_t *prev_image, const uint8_t *new_image) {
    if (!new_image) {
        return;
    }
    const uint16_t y_end_line = BOARD_DISPLAY_HEIGHT - 1;
    if (prev_image) {
        epaper_SendCommand(0x4E);  // Set Cursor X
        epaper_SendData(0x00);
        epaper_SendCommand(0x4F);  // Set Cursor Y
        epaper_SendData((uint8_t)(y_end_line & 0xFF));
        epaper_SendData((uint8_t)((y_end_line >> 8) & 0xFF));
        epaper_SendCommand(0x26);  // Old data RAM 0x26
        epaper_write_bytes(prev_image, BOARD_DISPLAY_BUFFER_SIZE);
    }
    epaper_SendCommand(0x4E);      // Set Cursor X
    epaper_SendData(0x00);
    epaper_SendCommand(0x4F);      // Set Cursor Y
    epaper_SendData((uint8_t)(y_end_line & 0xFF));
    epaper_SendData((uint8_t)((y_end_line >> 8) & 0xFF));
    epaper_SendCommand(0x24);      // New data RAM 0x24
    epaper_write_bytes(new_image, BOARD_DISPLAY_BUFFER_SIZE);

    epaper_TurnOnDisplayPart();
}

void epaper_port_display_full(const uint8_t *Image) {
    if (!Image) {
        return;
    }

    epaper_port_init();
    epaper_port_display(Image);

    // Switch LUT to partial refresh for subsequent partial updates without hardware reset
    const uint8_t *lut_part = board_epd_get_lut_partial(NULL);
    if (lut_part != NULL) {
        epaper_set_lut(lut_part);
    }
    epaper_SendCommand(0x37);
    epaper_SendData(0x00);
    epaper_SendData(0x00);
    epaper_SendData(0x00);
    epaper_SendData(0x00);
    epaper_SendData(0x00);
    epaper_SendData(0x40);
    epaper_SendData(0x00);
    epaper_SendData(0x00);
    epaper_SendData(0x00);
    epaper_SendData(0x00);

    epaper_SendCommand(0x3C);
    epaper_SendData(0x80);

    epaper_SendCommand(0x22);
    epaper_SendData(0xC0);
    epaper_SendCommand(0x20);
    epaper_readbusyh();
}

void epaper_port_sleep(void) {
    epaper_SendCommand(0x10);
    epaper_SendData(0x01);
}

#ifndef BOARD_EPD_PWR_ACTIVE_LVL
#define BOARD_EPD_PWR_ACTIVE_LVL 0
#endif

void epaper_power_on(void) {
    static bool s_pwr_gpio_inited = false;
    if (!s_pwr_gpio_inited) {
        gpio_config_t conf = {};
        conf.intr_type = GPIO_INTR_DISABLE;
        conf.mode = GPIO_MODE_INPUT_OUTPUT;
        conf.pin_bit_mask = (1ULL << EPD_PWR_PIN);
        conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        conf.pull_up_en = GPIO_PULLUP_ENABLE;
        ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&conf));
        s_pwr_gpio_inited = true;
    }
    if (gpio_get_level(EPD_PWR_PIN) != BOARD_EPD_PWR_ACTIVE_LVL) {
        gpio_set_level(EPD_PWR_PIN, BOARD_EPD_PWR_ACTIVE_LVL);
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

void epaper_power_off(void) {
    epaper_rst_0();
    epaper_cs_0;
    epaper_dc_0;
    gpio_set_level(EPD_PWR_PIN, !BOARD_EPD_PWR_ACTIVE_LVL);
}
