#include "ui_widgets.h"
#include <stdio.h>
#include <string.h>
#include "ui_layout.h"
#include "ui_text.h"
#include "power_mgr.h"

#define MSG_MAX_LINES 8
#define MSG_LINE_GAP 4

static bool s_nav_hints_visible = true;
static sFONT *s_body_font = &Font12;

void ui_set_nav_hints_visible(bool visible) { s_nav_hints_visible = visible; }
bool ui_nav_hints_visible(void) { return s_nav_hints_visible; }

void ui_set_body_font(sFONT *font) { s_body_font = font ? font : &Font12; }
sFONT *ui_body_font(void) { return s_body_font; }

void ui_fill_rect(int x0, int y0, int x1, int y1, UWORD color)
{
  int sw = ui_screen_w(), sh = ui_screen_h();
  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (x1 >= sw)
    x1 = sw - 1;
  if (y1 >= sh)
    y1 = sh - 1;
  for (int y = y0; y <= y1 && x0 <= x1; y++)
  {
    Paint_DrawLine((UWORD)x0, (UWORD)y, (UWORD)x1, (UWORD)y, color, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  }
}

static int draw_header_common(const char *title, bool show_battery, UWORD fg, UWORD bg)
{
  int w = ui_screen_w();
  int bat_w = 0;

  if (show_battery)
  {
    char bat[8];
    snprintf(bat, sizeof(bat), "%u%%", (unsigned)power_mgr_get_battery_percentage());
    bat_w = ui_text_width(bat, &Font12);
    ui_draw_string(w - bat_w - UI_MARGIN_X, UI_HEADER_TEXT_Y, bat, &Font12, fg, bg);
  }
  if (title)
  {
    int right_reserve = bat_w ? (bat_w + UI_MARGIN_X + 4) : UI_MARGIN_X;
    ui_draw_string_n(UI_MARGIN_X, UI_HEADER_TEXT_Y, title, &Font12, fg, bg,
                     w - UI_MARGIN_X - right_reserve);
  }
  return UI_BODY_TOP;
}

int ui_draw_header(const char *title, bool show_battery)
{
  ui_fill_rect(0, 0, ui_screen_w() - 1, UI_HEADER_HEIGHT, BLACK);
  return draw_header_common(title, show_battery, WHITE, BLACK);
}

int ui_draw_header_plain(const char *title, bool show_battery)
{
  int w = ui_screen_w();
  ui_fill_rect(0, 0, w - 1, UI_HEADER_HEIGHT - 1, WHITE);
  draw_header_common(title, show_battery, BLACK, WHITE);
  Paint_DrawLine(0, UI_HEADER_HEIGHT, (UWORD)(w - 1), UI_HEADER_HEIGHT, BLACK,
                 DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  return UI_BODY_TOP;
}

int ui_body_bottom(bool force_footer)
{
  return (force_footer || s_nav_hints_visible) ? (ui_footer_y() - 2) : (ui_screen_h() - 2);
}

int ui_draw_footer(const char *hint, bool force_show)
{
  if (hint && (force_show || s_nav_hints_visible))
  {
    int w = ui_screen_w(), fy = ui_footer_y();
    Paint_DrawLine(0, (UWORD)(fy - 2), (UWORD)(w - 1), (UWORD)(fy - 2), BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    ui_draw_text_centered(fy + 1, hint, &Font12);
  }
  return ui_body_bottom(force_show);
}

int ui_draw_footer_lr(const char *left, const char *right)
{
  int w = ui_screen_w(), fy = ui_footer_y();
  ui_fill_rect(0, fy, w - 1, ui_screen_h() - 1, WHITE);
  Paint_DrawLine(0, (UWORD)(fy - 1), (UWORD)(w - 1), (UWORD)(fy - 1), BLACK,
                 DOT_PIXEL_1X1, LINE_STYLE_SOLID);

  int right_w = right ? ui_text_width(right, &Font12) : 0;
  if (left)
  {
    ui_draw_string_n(UI_MARGIN_X, fy + 2, left, &Font12, BLACK, WHITE,
                     w - 2 * UI_MARGIN_X - right_w - 4);
  }
  if (right)
  {
    int x = w - right_w - UI_MARGIN_X;
    ui_draw_string(x < 0 ? 0 : x, fy + 2, right, &Font12, BLACK, WHITE);
  }
  return fy - 2;
}

void ui_draw_text_centered(int y, const char *text, sFONT *font)
{
  if (!text || !font)
  {
    return;
  }
  int w = ui_screen_w();
  int x = (w - ui_text_width(text, font)) / 2;
  if (x < 4)
  {
    x = 4;
  }
  ui_draw_string_n(x, y, text, font, BLACK, WHITE, w - x - 4);
}

void ui_draw_message_in(const char *const lines[], size_t count, sFONT *font, int top, int bottom)
{
  if (!lines || count == 0)
  {
    return;
  }
  if (!font)
  {
    font = s_body_font;
  }

  /* Wrap every input line into one flat list (static: UI runs on one task). */
  static char s_out[MSG_MAX_LINES][UI_WRAP_COLS_MAX + 1];
  char tmp[UI_WRAP_LINES_MAX][UI_WRAP_COLS_MAX + 1];
  int cols = (ui_screen_w() - 8) / font->Width;
  int n = 0;

  for (size_t i = 0; i < count && n < MSG_MAX_LINES; i++)
  {
    if (!lines[i])
    {
      continue;
    }
    int k = ui_text_wrap(lines[i], cols, UI_WRAP_LINES_MAX, tmp);
    for (int j = 0; j < k && n < MSG_MAX_LINES; j++)
    {
      memcpy(s_out[n++], tmp[j], sizeof(tmp[j]));
    }
  }
  if (n == 0)
  {
    return;
  }

  int pitch = font->Height + MSG_LINE_GAP;
  int total = n * pitch - MSG_LINE_GAP;
  int y = top + ((bottom - top) - total) / 2;
  if (y < top)
  {
    y = top;
  }
  for (int i = 0; i < n && y + font->Height <= bottom; i++, y += pitch)
  {
    ui_draw_text_centered(y, s_out[i], font);
  }
}

void ui_draw_message(const char *const lines[], size_t count, sFONT *font, bool force_footer)
{
  ui_draw_message_in(lines, count, font, UI_BODY_TOP, ui_body_bottom(force_footer));
}