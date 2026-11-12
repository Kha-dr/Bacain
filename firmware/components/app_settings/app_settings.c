#include "app_settings.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "board_config.h" /* BOARD_DISPLAY_ROTATION */
#include <stdatomic.h>

static const char *TAG = "APP_SETTINGS";

#define NVS_NS "settings"
#define KEY_ROTATION "rotation"
#define KEY_BTN_INVERT "btn_invert"
#define KEY_FONT_SIZE "font_size"
#define KEY_NAV_HINTS "nav_hints"
#define KEY_FULL_REFR "full_refresh_n"
#define KEY_SLEEP_TO "sleep_timeout"
#define KEY_STATUS_LED "status_led"

static RTC_DATA_ATTR bool s_cached = false;
static RTC_DATA_ATTR app_settings_t s_settings;
static atomic_bool s_apply_pending = false;

static void mark_changed(void)
{
  atomic_store(&s_apply_pending, true);
}

bool app_settings_take_pending_apply(void)
{
  return atomic_exchange(&s_apply_pending, false);
}
static void set_defaults(app_settings_t *s)
{
  s->rotation = BOARD_DISPLAY_ROTATION % 4;
  s->btn_invert = false;
  s->font_size = APP_FONT_SMALL;
  s->show_nav_hints = true;
  s->full_refresh_interval = APP_FULL_REFRESH_INTERVAL;
  s->sleep_timeout_sec = APP_INACTIVITY_SLEEP_SEC;
  s->status_led_enabled = true;
}

static uint8_t clamp_interval(uint8_t v)
{
  return (v > APP_SETTINGS_REFRESH_MAX) ? APP_SETTINGS_REFRESH_MAX : v;
}

static void read_u8(nvs_handle_t h, const char *key, uint8_t *out, uint8_t def)
{
  if (nvs_get_u8(h, key, out) != ESP_OK)
  {
    *out = def;
  }
}

static esp_err_t write_u8(const char *key, uint8_t value)
{
  nvs_handle_t h;
  esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "nvs_open for '%s' failed: %s", key, esp_err_to_name(err));
    return err;
  }
  err = nvs_set_u8(h, key, value);
  if (err == ESP_OK)
  {
    err = nvs_commit(h);
  }
  nvs_close(h);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "write '%s' failed: %s", key, esp_err_to_name(err));
  }
  return err;
}
static esp_err_t write_u16(const char *key, uint16_t value)
{
  nvs_handle_t h;
  esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "nvs_open for '%s' failed: %s", key, esp_err_to_name(err));
    return err;
  }
  err = nvs_set_u16(h, key, value);
  if (err == ESP_OK)
  {
    err = nvs_commit(h);
  }
  nvs_close(h);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "write '%s' failed: %s", key, esp_err_to_name(err));
  }
  return err;
}

void app_settings_read(bool force_nvs_reload)
{
  if (s_cached && !force_nvs_reload)
  {
    return;
  }
  set_defaults(&s_settings);

  nvs_handle_t h;
  esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
  if (err == ESP_OK)
  {
    uint8_t v;
    read_u8(h, KEY_ROTATION, &v, s_settings.rotation);
    s_settings.rotation = v % 4;
    read_u8(h, KEY_BTN_INVERT, &v, 0);
    s_settings.btn_invert = (v != 0);
    read_u8(h, KEY_FONT_SIZE, &v, APP_FONT_SMALL);
    s_settings.font_size = v ? APP_FONT_LARGE : APP_FONT_SMALL;
    read_u8(h, KEY_NAV_HINTS, &v, 1);
    s_settings.show_nav_hints = (v != 0);
    read_u8(h, KEY_FULL_REFR, &v, s_settings.full_refresh_interval);
    s_settings.full_refresh_interval = clamp_interval(v);
    uint16_t to = s_settings.sleep_timeout_sec;
    if (nvs_get_u16(h, KEY_SLEEP_TO, &to) == ESP_OK)
    {
      s_settings.sleep_timeout_sec = to;
    }
    read_u8(h, KEY_STATUS_LED, &v, 1);
    s_settings.status_led_enabled = (v != 0);
    nvs_close(h);
  }
  else if (err != ESP_ERR_NVS_NOT_FOUND)
  {
    /* NOT_FOUND = namespace never written (fresh device): defaults are correct. */
    ESP_LOGW(TAG, "nvs_open failed (%s), using defaults", esp_err_to_name(err));
  }
  s_cached = true;
}

const app_settings_t *app_settings_get(void)
{
  app_settings_read(false);
  return &s_settings;
}
esp_err_t app_settings_set_rotation(uint8_t rotation)
{
  app_settings_read(false);
  rotation %= 4;
  if (s_settings.rotation == rotation)
    return ESP_OK;
  s_settings.rotation = rotation;
  mark_changed();
  return write_u8(KEY_ROTATION, rotation);
}

esp_err_t app_settings_set_btn_invert(bool invert)
{
  app_settings_read(false);
  if (s_settings.btn_invert == invert)
    return ESP_OK;
  s_settings.btn_invert = invert;
  mark_changed();
  return write_u8(KEY_BTN_INVERT, invert ? 1 : 0);
}

esp_err_t app_settings_set_font_size(app_font_size_t size)
{
  app_settings_read(false);
  size = (size == APP_FONT_LARGE) ? APP_FONT_LARGE : APP_FONT_SMALL;
  if (s_settings.font_size == size)
    return ESP_OK;
  s_settings.font_size = size;
  mark_changed();
  return write_u8(KEY_FONT_SIZE, (uint8_t)size);
}

esp_err_t app_settings_set_nav_hints(bool show)
{
  app_settings_read(false);
  if (s_settings.show_nav_hints == show)
    return ESP_OK;
  s_settings.show_nav_hints = show;
  mark_changed();
  return write_u8(KEY_NAV_HINTS, show ? 1 : 0);
}

esp_err_t app_settings_set_full_refresh_interval(uint8_t interval)
{
  app_settings_read(false);
  interval = clamp_interval(interval);
  if (s_settings.full_refresh_interval == interval)
    return ESP_OK;
  s_settings.full_refresh_interval = interval;
  mark_changed();
  return write_u8(KEY_FULL_REFR, interval);
}
esp_err_t app_settings_set_sleep_timeout(uint16_t seconds)
{
  app_settings_read(false);
  if (s_settings.sleep_timeout_sec == seconds)
    return ESP_OK;
  s_settings.sleep_timeout_sec = seconds; /* read directly by main.c each loop: no apply needed */
  return write_u16(KEY_SLEEP_TO, seconds);
}
esp_err_t app_settings_set_status_led(bool enabled)
{
  app_settings_read(false);
  if (s_settings.status_led_enabled == enabled)
    return ESP_OK;
  s_settings.status_led_enabled = enabled;
  mark_changed();
  return write_u8(KEY_STATUS_LED, enabled ? 1 : 0);
}