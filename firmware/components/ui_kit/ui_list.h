#ifndef UI_LIST_H
#define UI_LIST_H

#include <stdbool.h>
#include <stddef.h>
#include "fonts.h"
#include "button.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define UI_ROW_TEXT_X 22
#define UI_ROW_PAD_Y 3

  typedef struct
  {
    sFONT *font;   /* NULL -> body font (ui_body_font()) */
    int max_lines; /* 0 -> 2; 1 = single line with "...", up to UI_WRAP_LINES_MAX */
  } ui_row_style_t;

  extern const ui_row_style_t UI_ROW_DEFAULT; /* body font, 2 lines */

  /* Row = one list item. Returns its pixel height. */
  int ui_row_height(const char *text, const ui_row_style_t *style);
  int ui_draw_row(int y, const char *text, const ui_row_style_t *style, bool selected);

  /* List: items supplied by callback so views keep their own data. */
  typedef const char *(*ui_list_text_fn)(void *ctx, size_t index);

  typedef struct
  {
    size_t count;
    int selected;
    ui_list_text_fn get_text;
    void *ctx;
    const ui_row_style_t *style; /* NULL -> UI_ROW_DEFAULT */
    int top;                     /* usually return of ui_draw_header() */
    int bottom;                  /* usually ui_body_bottom(force) */
  } ui_list_t;

  /* Window chosen by pixel height: selected item plus one item of look-ahead stay visible. */
  size_t ui_list_first_visible(const ui_list_t *list);
  size_t ui_draw_list(const ui_list_t *list); /* returns rows drawn */

  int ui_list_clamp(int index, int count);
  bool ui_list_step(int *index, int count, button_event_t evt); /* BOOT = up, PWR = down, wraps */

#ifdef __cplusplus
}
#endif

#endif /* UI_LIST_H */