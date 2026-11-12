#ifndef TEXT_LAYOUT_H
#define TEXT_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "text_source.h"
#include "board_config.h" /* BOARD_DISPLAY_WIDTH / HEIGHT */

#ifdef __cplusplus
extern "C"
{
#endif

#include "board_config.h" /* BOARD_DISPLAY_WIDTH / HEIGHT */

/* Limits follow the board: larger dimension (either rotation), narrowest body font
 * (Font12, 7 px) and smallest line pitch (14 px). Override per board if needed. */
#define TEXT_LAYOUT_DIM_MAX \
  (BOARD_DISPLAY_WIDTH > BOARD_DISPLAY_HEIGHT ? BOARD_DISPLAY_WIDTH : BOARD_DISPLAY_HEIGHT)

#ifndef TEXT_LAYOUT_MAX_COLS
#define TEXT_LAYOUT_MAX_COLS (TEXT_LAYOUT_DIM_MAX / 7)
#endif
#ifndef TEXT_LAYOUT_MAX_LINES
#define TEXT_LAYOUT_MAX_LINES (TEXT_LAYOUT_DIM_MAX / 14)
#endif
/* One read must hold a full page even with 2-byte UTF-8 characters. */
#define TEXT_LAYOUT_READ_CHUNK \
  ((TEXT_LAYOUT_MAX_COLS * TEXT_LAYOUT_MAX_LINES * 2) > 2048 ? (TEXT_LAYOUT_MAX_COLS * TEXT_LAYOUT_MAX_LINES * 2) : 2048)
/* ▲ ▼ survive layout as single control bytes; ui_text draws them as arrow glyphs.
 * Raw 0x1E/0x1F in source text are dropped (controls), so only real arrows map here. */
#define TEXT_GLYPH_ARROW_UP 0x1E
#define TEXT_GLYPH_ARROW_DOWN 0x1F

  /* Pure geometry in characters/lines. Built by ui_pager_layout_cfg() so the
   * layout and the drawn area come from the same numbers. */
  typedef struct
  {
    uint16_t cols;
    uint16_t lines;
    bool keep_blank_lines; /* paragraph gaps (nav hints, tafsir); collapsed to one */
  } text_layout_cfg_t;

  typedef struct
  {
    char lines[TEXT_LAYOUT_MAX_LINES][TEXT_LAYOUT_MAX_COLS + 1];
    uint16_t line_count;
    size_t start; /* source offset of first byte on this page */
    size_t end;   /* source offset where the next page starts */
    bool is_bof;
    bool is_eof;
    uint8_t progress_pct;
  } text_page_t; /* ~800 bytes: keep static or heap, not on small task stacks */

  /* Unicode -> printable ASCII for the 32..126 bitmap fonts.
   * Returns -1 for zero-width code points (BOM, combining marks, ZW spaces). */
  int text_layout_fold_codepoint(uint32_t cp);

  /* Lays out one page starting at offset. */
  esp_err_t text_layout_page(const text_source_t *src, size_t offset,
                             const text_layout_cfg_t *cfg, text_page_t *out);

  /* Start offset of the page before `current`, re-laid from the nearest line start. */
  size_t text_layout_prev_offset(const text_source_t *src, size_t current,
                                 const text_layout_cfg_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* TEXT_LAYOUT_H */