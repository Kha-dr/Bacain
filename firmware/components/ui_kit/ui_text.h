#ifndef UI_TEXT_H
#define UI_TEXT_H

#include <stdbool.h>
#include "GUI_Paint.h"
#include "fonts.h"
#include "board_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Widest wrapped line: larger panel dimension / narrowest body font (Font12, 7 px). */
#define UI_WRAP_COLS_MAX \
  ((BOARD_DISPLAY_WIDTH > BOARD_DISPLAY_HEIGHT ? BOARD_DISPLAY_WIDTH : BOARD_DISPLAY_HEIGHT) / 7)
#define UI_WRAP_LINES_MAX 4

  /* Draws a UTF-8 string. Supports ▲ (E2 96 B2) and ▼ (E2 96 BC) as vector glyphs.
   * Non-printable ASCII -> ' ', other multi-byte UTF-8 -> '?'. Never indexes the font
   * table outside 32..126. Clips at x + max_w and at screen width. Returns pixels drawn. */
  int ui_draw_string_n(int x, int y, const char *str, sFONT *font, UWORD fg, UWORD bg, int max_w);
  int ui_draw_string(int x, int y, const char *str, sFONT *font, UWORD fg, UWORD bg);
  int ui_text_width(const char *str, sFONT *font);

  /* Word wrap into at most max_lines; last line gets "..." if text remains.
   * Note: columns are counted in bytes (same as the old break_text_lines). */
  int ui_text_wrap(const char *text, int cols, int max_lines,
                   char out[][UI_WRAP_COLS_MAX + 1]);
  int ui_text_line_count(const char *text, sFONT *font, int max_w, int max_lines);
  int ui_text_draw_wrapped(int x, int y, const char *text, sFONT *font,
                           int max_w, int max_lines, UWORD fg, UWORD bg);

#ifdef __cplusplus
}
#endif

#endif /* UI_TEXT_H */