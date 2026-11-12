#include "ui_display.h"
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "epaper_port.h"
#include "GUI_Paint.h"

#ifdef BOARD_DISPLAY_BUFFER_SIZE
_Static_assert(UI_CANVAS_BYTES == BOARD_DISPLAY_BUFFER_SIZE,
               "ui_kit canvas size differs from board buffer size");
#endif

static const char *TAG = "UI_DISPLAY";

static uint8_t *s_canvas = NULL;
static uint8_t s_rotation = BOARD_DISPLAY_ROTATION % 4;
static uint8_t s_full_refresh_interval = 0; /* set by app_settings_apply() at boot */
static bool s_is_wake_frame = false;

/* RTC memory (~8 KB, shared with other RTC_DATA_ATTR state): keep the last frame
 * across deep sleep only when it fits. Larger panels skip the wake-up diff. */
#ifndef UI_RTC_CANVAS_MAX
#define UI_RTC_CANVAS_MAX 6144
#endif
#if UI_CANVAS_BYTES <= UI_RTC_CANVAS_MAX
#define UI_KEEP_RTC_CANVAS 1
static RTC_DATA_ATTR uint8_t s_rtc_canvas[UI_CANVAS_BYTES];
static RTC_DATA_ATTR bool s_rtc_canvas_valid = false;
#else
#define UI_KEEP_RTC_CANVAS 0
#endif
static RTC_DATA_ATTR uint8_t s_rtc_turns_since_full = 0;

static UWORD map_rotation(uint8_t r)
{
  switch (r & 3)
  {
  case 1:
    return ROTATE_90;
  case 2:
    return ROTATE_180;
  case 3:
    return ROTATE_270;
  default:
    return ROTATE_0;
  }
}

static uint8_t *alloc_canvas(void)
{
  uint8_t *p = heap_caps_malloc(UI_CANVAS_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (!p)
    p = malloc(UI_CANVAS_BYTES);
  if (!p)
    p = heap_caps_malloc(UI_CANVAS_BYTES, MALLOC_CAP_SPIRAM);
  return p;
}

static void new_image(void)
{
  /* Paint_NewImage recomputes Paint.Width/Height for the rotation; buffer content is kept. */
  Paint_NewImage(s_canvas, BOARD_DISPLAY_WIDTH, BOARD_DISPLAY_HEIGHT, map_rotation(s_rotation), WHITE);
  Paint_SetScale(2);
}

esp_err_t ui_display_init(bool is_cold_boot)
{
  s_is_wake_frame = !is_cold_boot;

  if (!s_canvas)
  {
    s_canvas = alloc_canvas();
    if (!s_canvas)
    {
      ESP_LOGE(TAG, "Failed to allocate display canvas");
      return ESP_ERR_NO_MEM;
    }
  }

  new_image();
  Paint_SelectImage(s_canvas);
  Paint_Clear(WHITE);

  epaper_power_on();
  if (is_cold_boot)
  {
    epaper_port_init();
    epaper_port_clear(WHITE);
    epaper_port_init_partial();
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  else
  {
    epaper_port_init_partial();
  }
  return ESP_OK;
}

uint8_t *ui_display_canvas(void)
{
  return s_canvas;
}

void ui_display_set_rotation(uint8_t rotation)
{
  s_rotation = rotation % 4;
  if (s_canvas)
  {
    new_image();
  }
  else
  {
    Paint_SetRotate(map_rotation(s_rotation));
  }
}

uint8_t ui_display_get_rotation(void)
{
  return s_rotation;
}

void ui_display_set_full_refresh_interval(uint8_t n)
{
  s_full_refresh_interval = n;
}

uint8_t *ui_begin(bool clear_white)
{
  if (!s_canvas)
  {
    return NULL;
  }
  epaper_power_on();
  Paint_SelectImage(s_canvas);
  if (clear_white)
  {
    Paint_Clear(WHITE);
  }
  return s_canvas;
}

void ui_display_flush(bool full_refresh)
{
  if (!s_canvas)
  {
    return;
  }
  if (full_refresh)
  {
    s_rtc_turns_since_full = 0;
    epaper_port_display_full(s_canvas);
  }
#if UI_KEEP_RTC_CANVAS
  else if (s_is_wake_frame && s_rtc_canvas_valid)
  {
    epaper_port_display_part_diff(s_rtc_canvas, s_canvas);
  }
#endif
  else
  {
    epaper_port_display_part(s_canvas);
  }
  s_is_wake_frame = false;
#if UI_KEEP_RTC_CANVAS
  memcpy(s_rtc_canvas, s_canvas, UI_CANVAS_BYTES);
  s_rtc_canvas_valid = true;
#endif
}

bool ui_display_page_turn_wants_full(void)
{
  if (s_full_refresh_interval == 0)
  {
    return false;
  }
  if (s_rtc_turns_since_full < UINT8_MAX)
  {
    s_rtc_turns_since_full++;
  }
  return s_rtc_turns_since_full >= s_full_refresh_interval; /* reset in flush(full) */
}

void ui_display_note_full_refresh(void)
{
  s_rtc_turns_since_full = 0;
}

void ui_end(ui_refresh_t mode)
{
  bool full = (mode == UI_REFRESH_FULL) ||
              (mode == UI_REFRESH_PAGE_TURN && ui_display_page_turn_wants_full());
  ui_display_flush(full);
}