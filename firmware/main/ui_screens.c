#include "ui_screens.h"
#include <stdio.h>
#include "GUI_Paint.h"
#include "fonts.h"
#include "ui_layout.h"
#include "ui_display.h"
#include "ui_text.h"
#include "ui_widgets.h"
#include "bacain_logo.h" /* BACA_ICON_WIDTH, BACA_ICON_HEIGHT, s_bacain_icon_48x48 */

#define FRAME_INSET 8
#define GAP_LOGO 12
#define GAP_TITLE 10
#define GAP_DIVIDER 12

static int min_dim(void)
{
  int w = ui_screen_w(), h = ui_screen_h();
  return (w < h) ? w : h;
}

/* 1x up to 399 px, 2x from 400 px, ... (logo is 48 px, designed for ~200 px panels). */
static int logo_scale(void)
{
  int s = min_dim() / 200;
  return (s < 1) ? 1 : s;
}

void ui_draw_logo(int center_x, int top_y, int scale)
{
  if (scale < 1)
  {
    scale = 1;
  }
  const int start_x = center_x - (BACA_ICON_WIDTH * scale) / 2;
  const int stride = BACA_ICON_WIDTH / 8;

  for (int y = 0; y < BACA_ICON_HEIGHT; y++)
  {
    for (int x = 0; x < BACA_ICON_WIDTH; x++)
    {
      if (s_bacain_icon_48x48[y * stride + (x / 8)] & (0x80 >> (x % 8)))
      {
        int px = start_x + x * scale;
        int py = top_y + y * scale;
        ui_fill_rect(px, py, px + scale - 1, py + scale - 1, BLACK); /* clips */
      }
    }
  }
}

/* Frame + logo + title + divider + subtitle, centered vertically on any panel. */
static void draw_brand_screen(const char *title, const char *subtitle)
{
  const int w = ui_screen_w(), h = ui_screen_h();
  const int s = logo_scale();
  const int logo_h = BACA_ICON_HEIGHT * s;
  const int block = logo_h + GAP_LOGO + Font16.Height + GAP_TITLE + 1 + GAP_DIVIDER + Font12.Height;

  Paint_DrawRectangle(FRAME_INSET, FRAME_INSET, (UWORD)(w - 1 - FRAME_INSET),
                      (UWORD)(h - 1 - FRAME_INSET), BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);

  int y = (h - block) / 2;
  ui_draw_logo(w / 2, y, s);
  y += logo_h + GAP_LOGO;

  ui_draw_text_centered(y, title, &Font16);
  y += Font16.Height + GAP_TITLE;

  Paint_DrawLine((UWORD)(w / 2 - w / 6), (UWORD)y, (UWORD)(w / 2 + w / 6), (UWORD)y,
                 BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  y += 1 + GAP_DIVIDER;

  ui_draw_text_centered(y, subtitle, &Font12);
}

void ui_screen_splash(const char *title, const char *subtitle)
{
  if (!ui_begin(true))
  {
    return;
  }
  draw_brand_screen((title && title[0]) ? title : "BacaIn",
                    (subtitle && subtitle[0]) ? subtitle : "Press \xE2\x96\xB2/\xE2\x96\xBC");
  ui_end(UI_REFRESH_FULL);
}

void ui_screen_power_off(bool buttons_inverted)
{
  if (!ui_begin(true))
  {
    return;
  }
  char wake[32];
  snprintf(wake, sizeof(wake), "Hold %s to turn on",
           buttons_inverted ? "\xE2\x96\xB2" : "\xE2\x96\xBC"); /* ▲ / ▼ */
  draw_brand_screen("BacaIn", wake);
  ui_end(UI_REFRESH_FULL);
}