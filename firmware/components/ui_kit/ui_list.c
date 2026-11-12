#include "ui_list.h"
#include "ui_layout.h"
#include "ui_text.h"
#include "ui_widgets.h"

const ui_row_style_t UI_ROW_DEFAULT = {.font = NULL, .max_lines = 2};

/* Resolves NULL font / 0 lines against the current body font. */
static ui_row_style_t resolve(const ui_row_style_t *s)
{
  ui_row_style_t r = {.font = ui_body_font(), .max_lines = 2};
  if (s)
  {
    if (s->font)
      r.font = s->font;
    if (s->max_lines > 0)
      r.max_lines = s->max_lines;
  }
  return r;
}

static int text_max_w(void)
{
  return ui_screen_w() - UI_ROW_TEXT_X - 6;
}

int ui_row_height(const char *text, const ui_row_style_t *style)
{
  ui_row_style_t st = resolve(style);
  int lines = ui_text_line_count(text ? text : "", st.font, text_max_w(), st.max_lines);
  return lines * st.font->Height + 2 * UI_ROW_PAD_Y;
}

int ui_draw_row(int y, const char *text, const ui_row_style_t *style, bool selected)
{
  ui_row_style_t st = resolve(style);
  int h = ui_row_height(text, &st);
  int ty = y + UI_ROW_PAD_Y;

  if (selected)
  {
    Paint_DrawRectangle(4, (UWORD)y, (UWORD)(ui_screen_w() - 5), (UWORD)(y + h - 1),
                        BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
    ui_draw_string(8, ty, ">", st.font, BLACK, WHITE);
  }
  ui_text_draw_wrapped(UI_ROW_TEXT_X, ty, text ? text : "", st.font,
                       text_max_w(), st.max_lines, BLACK, WHITE);
  return h;
}

static int item_h(const ui_list_t *l, const ui_row_style_t *st, size_t i)
{
  return ui_row_height(l->get_text(l->ctx, i), st);
}

size_t ui_list_first_visible(const ui_list_t *l)
{
  if (!l || !l->get_text || l->count == 0)
  {
    return 0;
  }
  ui_row_style_t st = resolve(l->style);
  int area = l->bottom - l->top;
  size_t sel = (size_t)ui_list_clamp(l->selected, (int)l->count);

  int acc = item_h(l, &st, sel);
  if (sel + 1 < l->count)
  {
    int h_next = item_h(l, &st, sel + 1);
    if (acc + h_next <= area)
    {
      acc += h_next;
    }
  }

  size_t start = sel;
  while (start > 0)
  {
    int h = item_h(l, &st, start - 1);
    if (acc + h > area)
    {
      break;
    }
    acc += h;
    start--;
  }
  return start;
}

size_t ui_draw_list(const ui_list_t *l)
{
  if (!l || !l->get_text || l->count == 0)
  {
    return 0;
  }
  ui_row_style_t st = resolve(l->style);
  int sel = ui_list_clamp(l->selected, (int)l->count);
  int y = l->top;
  size_t drawn = 0;

  for (size_t i = ui_list_first_visible(l); i < l->count; i++)
  {
    const char *t = l->get_text(l->ctx, i);
    int h = ui_row_height(t, &st);
    if (y + h > l->bottom && drawn > 0)
    {
      break;
    }
    ui_draw_row(y, t, &st, (int)i == sel);
    y += h;
    drawn++;
  }
  return drawn;
}

int ui_list_clamp(int index, int count)
{
  if (count <= 0 || index < 0)
    return 0;
  if (index >= count)
    return count - 1;
  return index;
}

bool ui_list_step(int *index, int count, button_event_t evt)
{
  if (!index || count <= 0 || evt.type != BTN_CLICK)
  {
    return false;
  }
  if (evt.button == BTN_ID_BOOT)
  {
    *index = (*index - 1 + count) % count;
    return true;
  }
  if (evt.button == BTN_ID_PWR)
  {
    *index = (*index + 1) % count;
    return true;
  }
  return false;
}