#ifndef EPAPER_PORT_H
#define EPAPER_PORT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "board_config.h"

#define EXAMPLE_LCD_WIDTH  BOARD_DISPLAY_WIDTH
#define EXAMPLE_LCD_HEIGHT BOARD_DISPLAY_HEIGHT

/**********************************
 Color Definitions (1-bit monochrome)
**********************************/
#define EPD_COLOR_BLACK    0x00
#define EPD_COLOR_WHITE    0xFF

// Backward-compatible color aliases
#define EPD_1IN54G_BLACK   EPD_COLOR_BLACK
#define EPD_1IN54G_WHITE   EPD_COLOR_WHITE

#ifdef __cplusplus
extern "C" {
#endif

const uint8_t *board_epd_get_lut_full(size_t *out_len);
const uint8_t *board_epd_get_lut_partial(size_t *out_len);

void epaper_port_init(void);
void epaper_port_init_partial(void);
void epaper_port_clear(uint8_t color);
void epaper_port_display(const uint8_t *Image);
void epaper_port_display_part(const uint8_t *Image);
void epaper_port_display_part_diff(const uint8_t *prev_image, const uint8_t *new_image);
void epaper_port_display_full(const uint8_t *Image);
void epaper_port_sleep(void);
void epaper_power_on(void);
void epaper_power_off(void);

#ifdef __cplusplus
}
#endif

#endif // !EPAPER_PORT_H
