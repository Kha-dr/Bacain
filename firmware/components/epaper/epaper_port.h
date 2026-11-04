#ifndef EPAPER_PORT_H
#define EPAPER_PORT_H

#include <stdint.h>
#include <stdbool.h>

#define EXAMPLE_LCD_WIDTH  200
#define EXAMPLE_LCD_HEIGHT 200

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

/* Waveshare Official V2 Look-Up Tables (159 bytes each) */
extern const uint8_t WF_Full_1IN54[159];
extern const uint8_t WF_PARTIAL_1IN54_0[159];

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
