#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include <stdbool.h>
#include <stddef.h>
#include "GUI_Paint.h"
#include "fonts.h"

#ifdef __cplusplus
extern "C"
{
#endif
  /* Body font: lists, messages, nav hints. Set by app_settings_apply(); default Font12.
   * Headers and footers always use Font12. */
  void ui_set_body_font(sFONT *font);
  sFONT *ui_body_font(void);

  void ui_set_nav_hints_visible(bool visible);
  bool ui_nav_hints_visible(void);

  /* Inclusive-bounds rectangle fill (avoids Paint_ClearWindows / filled-rect edge ambiguity). */
  void ui_fill_rect(int x0, int y0, int x1, int y1, UWORD color);

  /* Header: inverted bar (menus) or plain with underline (reading). Returns body top y. */
  int ui_draw_header(const char *title, bool show_battery);
  int ui_draw_header_plain(const char *title, bool show_battery);

  /* Footer hint: shown if force_show or nav hints enabled. Returns body bottom y. */
  int ui_body_bottom(bool force_footer);
  int ui_draw_footer(const char *hint, bool force_show);
  /* Status footer (always shown): left + right aligned text, e.g. "[BM]" / "42%". */
  int ui_draw_footer_lr(const char *left, const char *right);

  /* Centered text */
  void ui_draw_text_centered(int y, const char *text, sFONT *font);

  /* Centered block of lines inside [top, bottom). Each line is word-wrapped to the
   * screen width. font NULL -> body font. */
  void ui_draw_message_in(const char *const lines[], size_t count, sFONT *font, int top, int bottom);

  /* Same, inside the standard body area. font NULL -> body font. */
  void ui_draw_message(const char *const lines[], size_t count, sFONT *font, bool force_footer);

#ifdef __cplusplus
}
#endif

#endif /* UI_WIDGETS_H */