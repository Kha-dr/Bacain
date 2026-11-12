#include "ui_pager.h"
#include <stdio.h>
#include "ui_layout.h"
#include "ui_text.h"
#include "ui_widgets.h"

const ui_pager_style_t UI_PAGER_STYLE_SMALL = {.font = &Font12, .line_pitch = 14, .pad_x = 8};
const ui_pager_style_t UI_PAGER_STYLE_LARGE = {.font = &Font16, .line_pitch = 20, .pad_x = 10};
const ui_pager_style_t *ui_pager_body_style(void)
{
  return (ui_body_font() == &Font16) ? &UI_PAGER_STYLE_LARGE : &UI_PAGER_STYLE_SMALL;
}
static const ui_pager_chrome_t k_default_chrome = {0};

static const ui_pager_style_t *style_or_default(const ui_pager_style_t *s)
{
  return (s && s->font && s->font->Width > 0 && s->line_pitch > 0) ? s : &UI_PAGER_STYLE_SMALL;
}

static const ui_pager_chrome_t *chrome_or_default(const ui_pager_chrome_t *c)
{
  return c ? c : &k_default_chrome;
}

static int body_top(void)
{
  return UI_BODY_TOP;
}

static int body_bottom(const ui_pager_chrome_t *c)
{
  switch (c->footer)
  {
  case UI_PAGER_FOOTER_STATUS:
    return ui_footer_y() - 2; /* == ui_draw_footer_lr() */
  case UI_PAGER_FOOTER_HINT:
    return ui_body_bottom(c->force_hint);
  default:
    return ui_screen_h() - 2;
  }
}

static uint16_t calc_cols(const ui_pager_style_t *s)
{
  int usable = ui_screen_w() - 2 * (int)s->pad_x;
  int cols = (usable > 0) ? usable / s->font->Width : 1;
  if (cols < 1)
    cols = 1;
  if (cols > TEXT_LAYOUT_MAX_COLS)
    cols = TEXT_LAYOUT_MAX_COLS;
  return (uint16_t)cols;
}

static uint16_t calc_lines(const ui_pager_style_t *s, int top, int bottom)
{
  int area = bottom - top;
  if (area < s->font->Height)
  {
    return 1;
  }
  int lines = (area - s->font->Height) / s->line_pitch + 1; /* last line needs only font height */
  if (lines > TEXT_LAYOUT_MAX_LINES)
    lines = TEXT_LAYOUT_MAX_LINES;
  return (uint16_t)lines;
}

text_layout_cfg_t ui_pager_layout_cfg_rect(const ui_pager_style_t *style,
                                           int top, int bottom, bool keep_blank_lines)
{
  const ui_pager_style_t *st = style_or_default(style);
  text_layout_cfg_t cfg = {
      .cols = calc_cols(st),
      .lines = calc_lines(st, top, bottom),
      .keep_blank_lines = keep_blank_lines,
  };
  return cfg;
}

text_layout_cfg_t ui_pager_layout_cfg(const ui_pager_style_t *style,
                                      const ui_pager_chrome_t *chrome,
                                      bool keep_blank_lines)
{
  return ui_pager_layout_cfg_rect(style, body_top(), body_bottom(chrome_or_default(chrome)),
                                  keep_blank_lines);
}
void ui_pager_body_rect(const ui_pager_chrome_t *chrome, int *top, int *bottom)
{
  const ui_pager_chrome_t *c = chrome_or_default(chrome);
  if (top)
  {
    *top = body_top();
  }
  if (bottom)
  {
    *bottom = body_bottom(c);
  }
}
void ui_pager_draw_body(const text_page_t *page, const ui_pager_style_t *style,
                        int top, int bottom, const char *empty_text)
{
  const ui_pager_style_t *st = style_or_default(style);
  const int w = ui_screen_w();

  if (!page || page->line_count == 0)
  {
    if (empty_text)
    {
      /* Same font as the page (follows the font setting); wrapped; "" draws nothing. */
      const char *const msg[1] = {empty_text};
      ui_draw_message_in(msg, 1, st->font, top, bottom);
    }
    return;
  }

  const int cols = calc_cols(st);
  int margin = (w - cols * st->font->Width) / 2;
  if (margin < 0)
  {
    margin = 0;
  }
  for (uint16_t i = 0; i < page->line_count; i++)
  {
    int y = top + (int)i * st->line_pitch;
    if (y + st->font->Height > bottom)
    {
      break;
    }
    ui_draw_string_n(margin, y, page->lines[i], st->font, BLACK, WHITE, w - margin);
  }
}
void ui_pager_draw(const text_page_t *page, const ui_pager_style_t *style,
                   const ui_pager_chrome_t *chrome)
{
  const ui_pager_chrome_t *c = chrome_or_default(chrome);
  const int w = ui_screen_w();

  if (c->header_inverted)
  {
    ui_draw_header(c->title, c->show_battery);
  }
  else
  {
    ui_draw_header_plain(c->title, c->show_battery);
  }

  const int top = body_top();
  const int bottom = body_bottom(c);
  ui_fill_rect(0, top, w - 1, bottom, WHITE);
  ui_pager_draw_body(page, style, top, bottom, c->empty_text ? c->empty_text : "No text.");

  switch (c->footer)
  {
  case UI_PAGER_FOOTER_STATUS:
    ui_draw_footer_lr(c->footer_left, c->footer_right);
    break;
  case UI_PAGER_FOOTER_HINT:
    ui_draw_footer(c->footer_hint, c->force_hint);
    break;
  default:
    break;
  }
}

void ui_pager_format_progress(const text_page_t *page, char *buf, size_t len)
{
  if (!buf || len == 0)
  {
    return;
  }
  snprintf(buf, len, "%u%%", page ? (unsigned)page->progress_pct : 0u);
}