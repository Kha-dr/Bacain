#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "board_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* 1 bpp canvas (Paint scale 2). main.c can check: DISPLAY_BUFFER_SIZE == UI_CANVAS_BYTES */
#define UI_CANVAS_BYTES (((BOARD_DISPLAY_WIDTH + 7) / 8) * BOARD_DISPLAY_HEIGHT)

  typedef enum
  {
    UI_REFRESH_PARTIAL = 0,
    UI_REFRESH_FULL,
    UI_REFRESH_PAGE_TURN, /* partial, upgraded to full every N turns */
  } ui_refresh_t;

  esp_err_t ui_display_init(bool is_cold_boot);
  uint8_t *ui_display_canvas(void);

  void ui_display_set_rotation(uint8_t rotation);
  uint8_t ui_display_get_rotation(void);
  void ui_display_set_full_refresh_interval(uint8_t n);

  uint8_t *ui_begin(bool clear_white);
  void ui_end(ui_refresh_t mode);
  void ui_display_flush(bool full_refresh);

  bool ui_display_page_turn_wants_full(void);
  void ui_display_note_full_refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_DISPLAY_H */