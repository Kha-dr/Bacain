#ifndef UI_PAGER_H
#define UI_PAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fonts.h"
#include "text_layout.h"

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct
  {
    sFONT *font;
    uint8_t line_pitch;
    uint8_t pad_x;
  } ui_pager_style_t;

  extern const ui_pager_style_t UI_PAGER_STYLE_SMALL; /* Font12, pitch 14, pad 8  */
  extern const ui_pager_style_t UI_PAGER_STYLE_LARGE; /* Font16, pitch 20, pad 10 */
  /* Style matching the current body font (nav hints and other long text outside the reader). */
  const ui_pager_style_t *ui_pager_body_style(void);
  typedef enum
  {
    UI_PAGER_FOOTER_NONE = 0,
    UI_PAGER_FOOTER_STATUS, /* left/right text: "[BM]" / "42%", "2:255" / "3/7" */
    UI_PAGER_FOOTER_HINT,   /* centered hint, follows nav-hint visibility */
  } ui_pager_footer_t;

  typedef struct
  {
    const char *title;
    bool header_inverted;
    bool show_battery;
    ui_pager_footer_t footer;
    const char *footer_left;
    const char *footer_right;
    const char *footer_hint;
    bool force_hint;
    const char *empty_text; /* NULL -> "No text." */
  } ui_pager_chrome_t;

  /* Call after ui_display_init()/rotation: geometry reads Paint.Width/Height.
   * Footer text does not affect geometry, only chrome.footer/force_hint do. */
  text_layout_cfg_t ui_pager_layout_cfg(const ui_pager_style_t *style,
                                        const ui_pager_chrome_t *chrome,
                                        bool keep_blank_lines);
  /* Layout geometry for an explicit body rectangle [top, bottom). */
  text_layout_cfg_t ui_pager_layout_cfg_rect(const ui_pager_style_t *style,
                                             int top, int bottom, bool keep_blank_lines);
  /* Body rectangle for this chrome: rows top .. bottom-1 (same numbers as layout and draw). */
  void ui_pager_body_rect(const ui_pager_chrome_t *chrome, int *top, int *bottom);
  /* Draws only the body (page lines or empty_text) into [top, bottom). No header/footer,
   * no clearing. empty_text NULL -> nothing drawn when the page is empty. */
  void ui_pager_draw_body(const text_page_t *page, const ui_pager_style_t *style,
                          int top, int bottom, const char *empty_text);
  /* Draws header + body + footer into the selected canvas. No flush. */
  void ui_pager_draw(const text_page_t *page, const ui_pager_style_t *style,
                     const ui_pager_chrome_t *chrome);

  void ui_pager_format_progress(const text_page_t *page, char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* UI_PAGER_H */