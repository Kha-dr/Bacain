#include "ui_text.h"
#include <string.h>
#include "ui_layout.h"
#include "text_layout.h"

typedef enum
{
  GLYPH_CHAR,
  GLYPH_UP,
  GLYPH_DOWN
} glyph_kind_t;

/* Decodes one glyph and advances *pp. Shared by draw and width so they never disagree. */
static glyph_kind_t next_glyph(const unsigned char **pp, char *out_ch)
{
  const unsigned char *p = *pp;

  if (p[0] == 0xE2 && p[1] == 0x96 && (p[2] == 0xB2 || p[2] == 0xBC))
  {
    *pp = p + 3;
    return (p[2] == 0xB2) ? GLYPH_UP : GLYPH_DOWN;
  }
  if (p[0] == TEXT_GLYPH_ARROW_UP)
  {
    *pp = p + 1;
    return GLYPH_UP;
  }
  if (p[0] == TEXT_GLYPH_ARROW_DOWN)
  {
    *pp = p + 1;
    return GLYPH_DOWN;
  }
  if (p[0] < 0x80)
  {
    *out_ch = (p[0] >= 32 && p[0] <= 126) ? (char)p[0] : ' ';
    *pp = p + 1;
    return GLYPH_CHAR;
  }
  /* Other UTF-8 sequence or stray byte: consume lead + continuation bytes. */
  p++;
  while ((*p & 0xC0) == 0x80)
  {
    p++;
  }
  *out_ch = '?';
  *pp = p;
  return GLYPH_CHAR;
}

static void draw_triangle(int cx, int top_y, int h, bool up, UWORD color)
{
  int sw = ui_screen_w(), sh = ui_screen_h();
  for (int i = 0; i < h; i++)
  {
    int span = up ? (i / 2) : ((h - 1 - i) / 2);
    int x0 = cx - span, x1 = cx + span, y = top_y + i;
    if (x0 < 0)
      x0 = 0;
    if (x1 >= sw)
      x1 = sw - 1;
    if (y >= 0 && y < sh && x0 <= x1)
    {
      Paint_DrawLine((UWORD)x0, (UWORD)y, (UWORD)x1, (UWORD)y, color,
                     DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    }
  }
}

static void draw_arrow_cell(int x, int y, sFONT *font, bool up, UWORD fg, UWORD bg)
{
  int h = (font->Height > 4) ? font->Height - 4 : font->Height;
  for (int row = 0; row < font->Height; row++)
  { /* clear cell, inclusive bounds */
    Paint_DrawLine((UWORD)x, (UWORD)(y + row), (UWORD)(x + font->Width - 1), (UWORD)(y + row),
                   bg, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  }
  draw_triangle(x + font->Width / 2, y + (font->Height - h) / 2, h, up, fg);
}

int ui_draw_string_n(int x, int y, const char *str, sFONT *font, UWORD fg, UWORD bg, int max_w)
{
  if (!str || !font || font->Width == 0 || x < 0 || y < 0)
  {
    return 0;
  }
  int limit = x + max_w;
  if (limit > ui_screen_w())
  {
    limit = ui_screen_w();
  }

  const unsigned char *p = (const unsigned char *)str;
  int cx = x;
  while (*p != '\0' && cx + font->Width <= limit)
  {
    char ch = ' ';
    glyph_kind_t kind = next_glyph(&p, &ch);
    if (kind == GLYPH_CHAR)
    {
      Paint_DrawChar((UWORD)cx, (UWORD)y, ch, font, fg, bg);
    }
    else
    {
      draw_arrow_cell(cx, y, font, kind == GLYPH_UP, fg, bg);
    }
    cx += font->Width;
  }
  return cx - x;
}

int ui_draw_string(int x, int y, const char *str, sFONT *font, UWORD fg, UWORD bg)
{
  return ui_draw_string_n(x, y, str, font, fg, bg, ui_screen_w());
}

int ui_text_width(const char *str, sFONT *font)
{
  if (!str || !font)
  {
    return 0;
  }
  int glyphs = 0;
  const unsigned char *p = (const unsigned char *)str;
  while (*p != '\0')
  {
    char ch;
    next_glyph(&p, &ch);
    glyphs++;
  }
  return glyphs * font->Width;
}

static void copy_trimmed(char *dst, const char *src, size_t len)
{
  if (len > UI_WRAP_COLS_MAX)
  {
    len = UI_WRAP_COLS_MAX;
  }
  memcpy(dst, src, len);
  while (len > 0 && dst[len - 1] == ' ')
  {
    len--;
  }
  dst[len] = '\0';
}

int ui_text_wrap(const char *text, int cols, int max_lines, char out[][UI_WRAP_COLS_MAX + 1])
{
  if (!text || !out || cols <= 0 || max_lines <= 0)
  {
    return 0;
  }
  if (cols > UI_WRAP_COLS_MAX)
  {
    cols = UI_WRAP_COLS_MAX;
  }

  const char *p = text;
  int n = 0;
  while (n < max_lines)
  {
    while (*p == ' ')
    {
      p++;
    }
    if (*p == '\0')
    {
      break;
    }

    size_t rem = strlen(p);
    bool last = (n == max_lines - 1);

    if (rem <= (size_t)cols)
    { /* remainder fits */
      copy_trimmed(out[n++], p, rem);
      break;
    }
    if (last)
    { /* overflow on last line -> ellipsis */
      size_t take = (cols > 3) ? (size_t)(cols - 3) : 0;
      copy_trimmed(out[n], p, take);
      strcat(out[n], "..."); /* take + 3 + 1 <= UI_WRAP_COLS_MAX + 1 */
      n++;
      break;
    }

    size_t brk = 0; /* last space within cols (p[cols] is valid: rem > cols) */
    for (size_t i = (size_t)cols; i > 0; i--)
    {
      if (p[i] == ' ')
      {
        brk = i;
        break;
      }
    }
    if (brk > 0)
    {
      copy_trimmed(out[n++], p, brk);
      p += brk + 1;
    }
    else
    { /* no space: hard break */
      copy_trimmed(out[n++], p, (size_t)cols);
      p += cols;
    }
  }
  return n;
}

int ui_text_line_count(const char *text, sFONT *font, int max_w, int max_lines)
{
  if (!text || !font || font->Width == 0 || max_w <= 0 || max_lines <= 0)
  {
    return 1;
  }
  char lines[UI_WRAP_LINES_MAX][UI_WRAP_COLS_MAX + 1];
  if (max_lines > UI_WRAP_LINES_MAX)
  {
    max_lines = UI_WRAP_LINES_MAX;
  }
  int n = ui_text_wrap(text, max_w / font->Width, max_lines, lines);
  return (n > 0) ? n : 1;
}

int ui_text_draw_wrapped(int x, int y, const char *text, sFONT *font,
                         int max_w, int max_lines, UWORD fg, UWORD bg)
{
  if (!text || !font || font->Width == 0 || font->Height == 0 || max_w <= 0 || max_lines <= 0)
  {
    return 0;
  }
  char lines[UI_WRAP_LINES_MAX][UI_WRAP_COLS_MAX + 1];
  if (max_lines > UI_WRAP_LINES_MAX)
  {
    max_lines = UI_WRAP_LINES_MAX;
  }
  int n = ui_text_wrap(text, max_w / font->Width, max_lines, lines);
  for (int i = 0; i < n; i++)
  {
    ui_draw_string_n(x, y + i * font->Height, lines[i], font, fg, bg, max_w);
  }
  return n;
}