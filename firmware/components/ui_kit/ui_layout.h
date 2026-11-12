#ifndef UI_LAYOUT_H
#define UI_LAYOUT_H

#include "GUI_Paint.h"
#include "fonts.h"
#include "board_config.h" /* BOARD_DISPLAY_WIDTH / BOARD_DISPLAY_HEIGHT */

/* Single source of layout geometry. app_config.h must not define these. */
#define UI_HEADER_HEIGHT 19
#define UI_FOOTER_HEIGHT 16

#define UI_HEADER_TEXT_Y 3
#define UI_BODY_TOP (UI_HEADER_HEIGHT + 3) /* 22 */
#define UI_MARGIN_X 8

static inline int ui_screen_w(void) { return Paint.Width ? (int)Paint.Width : BOARD_DISPLAY_WIDTH; }
static inline int ui_screen_h(void) { return Paint.Height ? (int)Paint.Height : BOARD_DISPLAY_HEIGHT; }
static inline int ui_footer_y(void)
{
  int h = ui_screen_h();
  return (h > UI_FOOTER_HEIGHT) ? (h - UI_FOOTER_HEIGHT) : 0;
}

#endif /* UI_LAYOUT_H */