#ifndef UI_SCREENS_H
#define UI_SCREENS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  /* Draws the 48x48 BacaIn logo at an integer scale, centered on center_x. */
  void ui_draw_logo(int center_x, int top_y, int scale);

  /* Full-screen system screens (fixed Font16/Font12). Each does begin + draw + full refresh. */
  void ui_screen_splash(const char *title, const char *subtitle);
  void ui_screen_power_off(bool buttons_inverted);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREENS_H */