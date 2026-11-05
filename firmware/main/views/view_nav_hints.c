#include "view_nav_hints.h"
#include "view_menu.h"
#include "epaper_port.h"
#include "GUI_Paint.h"
#include "fonts.h"
#include "app_config.h"

void view_nav_hints_render(void) {
    uint8_t *canvas = reader_ui_get_canvas();
    if (!canvas) return;

    epaper_power_on();

    Paint_SelectImage(canvas);
    Paint_Clear(WHITE);

    // Title Bar - Solid black box with white text
    Paint_DrawRectangle(0, 0, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(8, 3, "NAVIGATION HINTS", &Font12, WHITE, BLACK);
    Paint_DrawLine(0, UI_HEADER_HEIGHT + 1, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT + 1, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    // State toggle box
    Paint_DrawRectangle(12, 23, DISPLAY_WIDTH - 13, 39, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
    ui_draw_centered_string(25, reader_ui_get_show_nav_hints() ? "Hints: [ON]" : "Hints: [OFF]", &Font12);

    // Context controls list
    Paint_DrawString_EN(8, 44, "Reading:", &Font12, BLACK, WHITE);
    ui_draw_string(14, 58, "▲/▼ Page | 2x Mark", &Font12, BLACK, WHITE);
    ui_draw_string(14, 72, "Hold ▲/▼ Menu", &Font12, BLACK, WHITE);

    Paint_DrawString_EN(8, 88, "Menu:", &Font12, BLACK, WHITE);
    ui_draw_string(14, 102, "▲/▼ Move | 2x▲/▼ Select", &Font12, BLACK, WHITE);

    Paint_DrawString_EN(8, 118, "Library:", &Font12, BLACK, WHITE);
    ui_draw_string(14, 132, "2x▲/▼ Open | Hold Menu", &Font12, BLACK, WHITE);

    Paint_DrawString_EN(8, 148, "Global:", &Font12, BLACK, WHITE);
    ui_draw_string(14, 162, "Hold ▲+▼ (2s) Reboot", &Font12, BLACK, WHITE);

    // Footer
    Paint_DrawLine(0, UI_FOOTER_Y - 2, DISPLAY_WIDTH - 1, UI_FOOTER_Y - 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    ui_draw_centered_string(UI_FOOTER_Y + 1, "2x▲/▼ Toggle | Hold ▲/▼ Back", &Font12);

    reader_ui_flush_canvas(false);
}

void reader_ui_render_nav_hints(void) {
    view_nav_hints_render();
}

reader_view_t view_nav_hints_on_wake(wakeup_reason_t wakeup) {
    (void)wakeup;
    view_nav_hints_render();
    return VIEW_NAV_HINTS;
}

reader_view_t view_nav_hints_handle_event(button_event_t evt) {
    if (evt.type == BTN_EVENT_CUSTOM_REFRESH) {
        view_nav_hints_render();
        return VIEW_NAV_HINTS;
    }

    if (evt.type == BTN_DOUBLE_CLICK) {
        app_settings_save_nav_hints(!reader_ui_get_show_nav_hints());
        view_nav_hints_render();
    } else if (evt.type == BTN_LONG_PRESS) {
        view_menu_render(false);
        return VIEW_MENU;
    }

    return VIEW_NAV_HINTS;
}
