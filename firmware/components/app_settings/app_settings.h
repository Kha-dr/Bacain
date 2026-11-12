#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C"
{
#endif
/* Inactivity before deep sleep, seconds (0 = never). Single default source. */
#ifdef CONFIG_APP_INACTIVITY_SLEEP_SEC
#define APP_INACTIVITY_SLEEP_SEC CONFIG_APP_INACTIVITY_SLEEP_SEC
#else
#define APP_INACTIVITY_SLEEP_SEC 5
#endif
#define APP_SETTINGS_REFRESH_MAX 100
/* Default full-refresh interval (page turns). Single source; removed from app_config.h. */
#ifndef APP_FULL_REFRESH_INTERVAL
#define APP_FULL_REFRESH_INTERVAL 10
#endif

  typedef enum
  {
    APP_FONT_SMALL = 0, /* Font12, matches stored NVS value 0 */
    APP_FONT_LARGE = 1, /* Font16 */
  } app_font_size_t;

  typedef struct
  {
    uint8_t rotation; /* 0..3 */
    bool btn_invert;
    app_font_size_t font_size;
    bool show_nav_hints;
    uint8_t full_refresh_interval; /* 0 = never, 1..100 page turns */
    uint16_t sleep_timeout_sec;    /* 0 = never sleep on inactivity */
    bool status_led_enabled;       /* used only if the board has a status LED */
  } app_settings_t;
  void app_settings_load(bool force_nvs_reload);
  const app_settings_t *app_settings_get(void);

  /* Setters update RAM/RTC immediately and persist to NVS only when the value changed.
   * On NVS failure the new value stays active for this session and the error is returned. */
  esp_err_t app_settings_set_rotation(uint8_t rotation);
  esp_err_t app_settings_set_btn_invert(bool invert);
  esp_err_t app_settings_set_font_size(app_font_size_t size);
  esp_err_t app_settings_set_nav_hints(bool show);
  esp_err_t app_settings_set_full_refresh_interval(uint8_t interval);
  esp_err_t app_settings_set_sleep_timeout(uint16_t seconds);
  esp_err_t app_settings_set_status_led(bool enabled);
  /* True once after any setter changed a value since the last call. Setters may run on
   * the BLE tasks; the main task calls this and then app_settings_apply(). */
  bool app_settings_take_pending_apply(void);
  /* Pushes current values into display, buttons, pager. Defined in app_settings_apply.c */
  void app_settings_apply(void);
  /* Pure load: NVS -> RTC cache, no side effects. */
  void app_settings_read(bool force_nvs_reload);

  /* Defined in main/app_settings_apply.c (needs ui_kit, button, text_pager).
   * Call only from the main task. Other components use the setters/getters above. */
  void app_settings_apply(void);
  void app_settings_load(bool force_nvs_reload); /* read + apply */

#ifdef __cplusplus
}
#endif

#endif /* APP_SETTINGS_H */