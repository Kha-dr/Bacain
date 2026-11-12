#include "app_settings.h"
#include "ui_display.h"
#include "ui_widgets.h"
#include "button.h"
#include "status_led.h"
#include "text_pager.h"

void app_settings_apply(void)
{
  const app_settings_t *s = app_settings_get();
  bool large = (s->font_size == APP_FONT_LARGE);

  ui_display_set_rotation(s->rotation);
  ui_display_set_full_refresh_interval(s->full_refresh_interval);
  ui_set_nav_hints_visible(s->show_nav_hints);
  ui_set_body_font(large ? &Font16 : &Font12); /* lists, messages, hints */
  button_set_inverted(s->btn_invert);
  text_pager_set_font_size(large ? TEXT_PAGER_FONT_16 : TEXT_PAGER_FONT_12); /* reader, mushaf text */
  status_led_set_enabled(s->status_led_enabled);                             /* ESP_ERR_NOT_SUPPORTED without an LED: ignored by design */
}

void app_settings_load(bool force_nvs_reload)
{
  app_settings_read(force_nvs_reload);
  app_settings_apply();
}