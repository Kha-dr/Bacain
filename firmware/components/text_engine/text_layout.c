#include "text_layout.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "text_layout";
/* RULES.md 2.3: compile-time buffers. All callers run on the main (UI) task;
 * text_pager additionally holds its mutex. Not reentrant. */
static uint8_t s_read_buf[TEXT_LAYOUT_READ_CHUNK];
static text_page_t s_prev_page;

#define CP_INCOMPLETE 0xFFFFFFFFu
#define CP_REPLACEMENT 0xFFFDu
#define SNAP_WINDOW 256
#define PREV_MAX_STEPS 64

/* ---------- UTF-8 ---------- */

static size_t align_utf8_forward(const uint8_t *b, size_t cur, size_t len)
{
  while (cur < len && (b[cur] & 0xC0) == 0x80)
  {
    cur++;
  }
  return cur;
}

/* CP_INCOMPLETE (len 0) when the sequence is cut by the end of the buffer.
 * Malformed sequence -> U+FFFD, len 1. */
static uint32_t decode_utf8(const uint8_t *b, size_t cur, size_t len, int *out_len)
{
  uint8_t b0 = b[cur];
  int need;
  uint32_t cp;

  if (b0 < 0x80)
  {
    *out_len = 1;
    return b0;
  }
  else if ((b0 & 0xE0) == 0xC0)
  {
    need = 1;
    cp = b0 & 0x1F;
  }
  else if ((b0 & 0xF0) == 0xE0)
  {
    need = 2;
    cp = b0 & 0x0F;
  }
  else if ((b0 & 0xF8) == 0xF0)
  {
    need = 3;
    cp = b0 & 0x07;
  }
  else
  {
    *out_len = 1;
    return CP_REPLACEMENT;
  }

  if (cur + (size_t)need >= len)
  {
    *out_len = 0;
    return CP_INCOMPLETE;
  }
  for (int i = 1; i <= need; i++)
  {
    uint8_t c = b[cur + (size_t)i];
    if ((c & 0xC0) != 0x80)
    {
      *out_len = 1;
      return CP_REPLACEMENT;
    }
    cp = (cp << 6) | (c & 0x3F);
  }
  *out_len = need + 1;
  return cp;
}

/* ---------- Unicode -> ASCII fold ---------- */

typedef struct
{
  uint16_t cp;
  char ch;
} fold_entry_t;

static const fold_entry_t k_fold[] = {
    /* Latin Extended-A/B: macrons and others used in transliteration */
    {0x0100, 'A'},
    {0x0101, 'a'},
    {0x0112, 'E'},
    {0x0113, 'e'},
    {0x0120, 'G'},
    {0x0121, 'g'},
    {0x012A, 'I'},
    {0x012B, 'i'},
    {0x014C, 'O'},
    {0x014D, 'o'},
    {0x0160, 'S'},
    {0x0161, 's'},
    {0x016A, 'U'},
    {0x016B, 'u'},
    {0x017D, 'Z'},
    {0x017E, 'z'},
    {0x01E6, 'G'},
    {0x01E7, 'g'},
    /* ayn / hamza modifier letters */
    {0x02B9, '\''},
    {0x02BB, '\''},
    {0x02BC, '\''},
    {0x02BE, '\''},
    {0x02BF, '\''},
    /* Latin Extended Additional: dot/line below */
    {0x1E0C, 'D'},
    {0x1E0D, 'd'},
    {0x1E0E, 'D'},
    {0x1E0F, 'd'},
    {0x1E24, 'H'},
    {0x1E25, 'h'},
    {0x1E2A, 'H'},
    {0x1E2B, 'h'},
    {0x1E62, 'S'},
    {0x1E63, 's'},
    {0x1E6C, 'T'},
    {0x1E6D, 't'},
    {0x1E6E, 'T'},
    {0x1E6F, 't'},
    {0x1E92, 'Z'},
    {0x1E93, 'z'},
    {0x1E94, 'Z'},
    {0x1E95, 'z'},
    /* punctuation */
    {0x2010, '-'},
    {0x2011, '-'},
    {0x2012, '-'},
    {0x2013, '-'},
    {0x2014, '-'},
    {0x2212, '-'},
    {0x2018, '\''},
    {0x2019, '\''},
    {0x201A, '\''},
    {0x201B, '\''},
    {0x2032, '\''},
    {0x201C, '"'},
    {0x201D, '"'},
    {0x201E, '"'},
    {0x2022, '*'},
    {0x2026, '.'},
};

/* U+00C0..U+00FF, one char each */
static const char k_latin1_upper[] =
    "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";

int text_layout_fold_codepoint(uint32_t cp)
{
  if (cp >= 32 && cp <= 126)
    return (int)cp;
  if (cp == 0x25B2)
    return TEXT_GLYPH_ARROW_UP;
  if (cp == 0x25BC)
    return TEXT_GLYPH_ARROW_DOWN;
  if (cp == '\t')
    return ' ';
  if (cp < 32 || cp == 0x7F)
    return -1; /* other controls */
  if (cp >= 0x0300 && cp <= 0x036F)
    return -1; /* combining marks */
  if (cp >= 0x200B && cp <= 0x200F)
    return -1; /* zero-width, LRM/RLM */
  if (cp == 0xFEFF || cp == 0x00AD)
    return -1; /* BOM, soft hyphen */
  if (cp == 0x00A0 || (cp >= 0x2000 && cp <= 0x200A))
    return ' ';
  if (cp >= 0x00C0 && cp <= 0x00FF)
    return k_latin1_upper[cp - 0x00C0];
  if (cp == 0x00AB || cp == 0x00BB)
    return '"';
  if (cp == 0x00B4)
    return '\'';
  if (cp == 0x00B7)
    return '.';

  for (size_t i = 0; i < sizeof(k_fold) / sizeof(k_fold[0]); i++)
  {
    if (k_fold[i].cp == cp)
    {
      return k_fold[i].ch;
    }
  }
  return '?'; /* visible marker for unsupported glyphs */
}

/* ---------- helpers ---------- */

static uint16_t clamp_u16(uint16_t v, uint16_t lo, uint16_t hi)
{
  return (v < lo) ? lo : (v > hi) ? hi
                                  : v;
}

static bool only_whitespace(const uint8_t *b, size_t from, size_t n)
{
  for (size_t i = from; i < n; i++)
  {
    uint8_t c = b[i];
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '\f')
    {
      return false;
    }
  }
  return true;
}

/* ---------- page layout ---------- */

esp_err_t text_layout_page(const text_source_t *src, size_t offset,
                           const text_layout_cfg_t *cfg, text_page_t *page)
{
  if (!src || !cfg || !page)
  {
    return ESP_ERR_INVALID_ARG;
  }
  memset(page, 0, sizeof(*page));

  const size_t total = text_source_size(src);
  const uint16_t max_lines = clamp_u16(cfg->lines, 1, TEXT_LAYOUT_MAX_LINES);
  const uint16_t max_cols = clamp_u16(cfg->cols, 1, TEXT_LAYOUT_MAX_COLS);

  if (offset >= total)
  {
    page->start = page->end = total;
    page->is_bof = (total == 0);
    page->is_eof = true;
    page->progress_pct = 100;
    return ESP_OK;
  }

  uint8_t *buf = s_read_buf;
  const size_t n = text_source_read(src, offset, buf, TEXT_LAYOUT_READ_CHUNK);
  if (n == 0)
  {
    ESP_LOGE(TAG, "read failed at %u", (unsigned)offset);
    return ESP_FAIL;
  }
  const bool chunk_is_tail = (offset + n >= total);

  size_t cur = align_utf8_forward(buf, 0, n);
  page->start = offset + cur;

  bool prev_blank = true; /* suppress blank lines at the top of a page */
  bool page_break = false;
  bool soft_wrapped = false;

  while (page->line_count < max_lines && cur < n && !page_break)
  {
    char *line = page->lines[page->line_count];
    const size_t line_start = cur;
    uint16_t cols = 0;
    size_t sp_cur = 0;
    uint16_t sp_cols = 0;
    bool have_sp = false;
    bool hard_eol = false;
    bool chunk_cut = false;

    if (soft_wrapped)
    { /* continuation line: drop spaces at the wrap point */
      while (cur < n && (buf[cur] == ' ' || buf[cur] == '\t'))
      {
        cur++;
      }
    }
    soft_wrapped = false;

    while (cur < n)
    {
      const uint8_t b = buf[cur];

      if (b == '\f')
      {
        cur++;
        if (cur < n && buf[cur] == '\r')
          cur++;
        if (cur < n && buf[cur] == '\n')
          cur++;
        if (page->line_count == 0 && cols == 0)
        {
          continue; /* leading form feed: no empty page */
        }
        page_break = true;
        break;
      }
      if (b == '\n' || b == '\r')
      {
        cur++;
        if (b == '\r' && cur < n && buf[cur] == '\n')
          cur++;
        hard_eol = true;
        break;
      }

      int len = 0;
      uint32_t cp = decode_utf8(buf, cur, n, &len);
      if (cp == CP_INCOMPLETE)
      {
        if (!chunk_is_tail)
        {
          chunk_cut = true;
          break;
        }
        cp = CP_REPLACEMENT;
        len = 1;
      }

      int ch = text_layout_fold_codepoint(cp);
      if (ch < 0)
      {
        cur += (size_t)len;
        continue;
      }

      if (cols >= max_cols)
      {
        if (ch != ' ' && have_sp)
        { /* back up to last word boundary */
          cur = sp_cur;
          cols = sp_cols;
        }
        soft_wrapped = true; /* no space: hard break mid-word */
        break;
      }

      if (ch == ' ' && cols > 0)
      {
        sp_cur = cur + (size_t)len;
        sp_cols = cols;
        have_sp = true;
      }
      line[cols++] = (char)ch;
      cur += (size_t)len;
    }

    if (cur >= n && !hard_eol && !page_break && !soft_wrapped && !chunk_is_tail)
    {
      chunk_cut = true;
    }
    if (chunk_cut && page->line_count > 0)
    {
      cur = line_start; /* line crosses the chunk: lay it out on the next page */
      break;
    }

    while (cols > 0 && line[cols - 1] == ' ')
    {
      cols--;
    }
    line[cols] = '\0';

    if (cols > 0)
    {
      page->line_count++;
      prev_blank = false;
    }
    else if (hard_eol && cfg->keep_blank_lines && !prev_blank)
    {
      page->line_count++; /* line[] is already "" */
      prev_blank = true;
    }
  }

  while (page->line_count > 0 && page->lines[page->line_count - 1][0] == '\0')
  {
    page->line_count--; /* no trailing blank line */
  }

  if (cur == 0)
  {
    cur = n; /* always make progress */
  }
  page->end = offset + cur;
  if (page->end >= total || (chunk_is_tail && only_whitespace(buf, cur, n)))
  {
    page->end = total;
    page->is_eof = true;
  }
  page->is_bof = (page->start == 0);
  page->progress_pct = page->is_eof ? 100
                                    : (uint8_t)(((uint64_t)page->start * 100) / total);

  return ESP_OK;
}

/* ---------- backward paging ---------- */

/* Moves pos back to the byte after the nearest '\n' or '\f' within SNAP_WINDOW.
 * Forward layout restarts wrapping at every line break, so starting there
 * reproduces the forward page boundaries in most cases. */
static size_t snap_to_line_start(const text_source_t *src, size_t pos)
{
  if (pos == 0)
  {
    return 0;
  }
  size_t from = (pos > SNAP_WINDOW) ? pos - SNAP_WINDOW : 0;
  uint8_t tmp[SNAP_WINDOW];
  size_t n = text_source_read(src, from, tmp, pos - from);

  for (size_t i = n; i > 0; i--)
  {
    if (tmp[i - 1] == '\n' || tmp[i - 1] == '\f')
    {
      return from + i;
    }
  }
  return (from == 0) ? 0 : pos;
}

size_t text_layout_prev_offset(const text_source_t *src, size_t current,
                               const text_layout_cfg_t *cfg)
{
  if (!src || !cfg || current == 0)
  {
    return 0;
  }

  size_t page_bytes = (size_t)clamp_u16(cfg->lines, 1, TEXT_LAYOUT_MAX_LINES) *
                      clamp_u16(cfg->cols, 1, TEXT_LAYOUT_MAX_COLS);
  size_t span = page_bytes * 3; /* headroom for multi-byte UTF-8 */
  size_t cand = (current > span) ? current - span : 0;
  cand = snap_to_line_start(src, cand);

  text_page_t *pg = &s_prev_page;
  if (!pg)
  {
    ESP_LOGE(TAG, "no memory for backtrack page");
    return cand;
  }

  size_t result = cand;
  size_t pos = cand;
  for (int step = 0; step < PREV_MAX_STEPS && pos < current; step++)
  {
    if (text_layout_page(src, pos, cfg, pg) != ESP_OK)
    {
      break;
    }
    result = pg->start;
    if (pg->end >= current || pg->end <= pos)
    {
      break;
    }
    pos = pg->end;
  }

  return result;
}