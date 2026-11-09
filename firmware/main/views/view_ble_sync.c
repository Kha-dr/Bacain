#define view_ble_sync_on_wake __legacy_view_ble_sync_on_wake
#include "view_ble_sync.h"
#undef view_ble_sync_on_wake
#include "views.h"
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "epaper_port.h"
#include "GUI_Paint.h"
#include "fonts.h"
#include "app_config.h"
#include "board_config.h"
#include "sync_qrcode.h"
#include "ble_sync.h"
#include "book_library.h"
#include "view_menu.h"

#define TAG "VIEW_BLE_SYNC"

#define BLE_SYNC_UNCONNECTED_TIMEOUT_SEC 60
#define BLE_SYNC_HEADSUP_REMAINING_SEC   20
#define BLE_SYNC_HEADSUP_AT_SEC          (BLE_SYNC_UNCONNECTED_TIMEOUT_SEC - BLE_SYNC_HEADSUP_REMAINING_SEC)

static int64_t s_ble_unconnected_since_us = 0;
static bool s_ble_headsup_shown = false;

void view_ble_sync_render(bool is_connected, uint32_t bytes_received, const char *status_override) {
    uint8_t *canvas = reader_ui_get_canvas();
    if (!canvas) return;

    epaper_power_on();

    Paint_SelectImage(canvas);
    Paint_Clear(WHITE);

    // Title Bar - Solid black box with white text
    Paint_DrawRectangle(0, 0, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(8, 3, "SYNC BOOKS", &Font12, WHITE, BLACK);
    Paint_DrawLine(0, UI_HEADER_HEIGHT + 1, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT + 1, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    // Render 25x25 QR code centered with 4x scaling (25 * 4 = 100x100 pixels)
    int scale = 4;
    int qx = (BOARD_DISPLAY_WIDTH - (SYNC_QRCODE_SIZE * scale)) / 2;
    int qy = (BOARD_DISPLAY_HEIGHT - (SYNC_QRCODE_SIZE * scale)) / 2;
    // Draw white quiet zone backing
    Paint_DrawRectangle(qx - 4, qy - 4, qx + (SYNC_QRCODE_SIZE * scale) + 3, qy + (SYNC_QRCODE_SIZE * scale) + 3, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    for (int y = 0; y < SYNC_QRCODE_SIZE; y++) {
        for (int x = 0; x < SYNC_QRCODE_SIZE; x++) {
            int bit_idx = y * SYNC_QRCODE_SIZE + x;
            uint8_t bit = (s_sync_qrcode_bitmap[bit_idx / 8] >> (7 - (bit_idx % 8))) & 1;
            if (bit) {
                Paint_DrawRectangle(qx + x * scale, qy + y * scale,
                                    qx + (x + 1) * scale - 1, qy + (y + 1) * scale - 1,
                                    BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
            }
        }
    }

    // Connection status below QR code
    if (status_override && status_override[0] != '\0') {
        ui_draw_centered_string(162, status_override, &Font12);
    } else if (is_connected) {
        char rx_buf[32];
        snprintf(rx_buf, sizeof(rx_buf), "Rx: %u bytes", (unsigned int)bytes_received);
        ui_draw_centered_string(162, rx_buf, &Font12);
    } else {
        ui_draw_centered_string(162, "Waiting to connect...", &Font12);
    }

    // Footer
    Paint_DrawLine(0, UI_FOOTER_Y - 2, DISPLAY_WIDTH - 1, UI_FOOTER_Y - 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    ui_draw_string(12, UI_FOOTER_Y + 1, "Hold ▲/▼ Exit", &Font12, BLACK, WHITE);

    reader_ui_flush_canvas(false);
}

void reader_ui_render_ble_sync(bool is_connected, uint32_t bytes_received, const char *status_override) {
    view_ble_sync_render(is_connected, bytes_received, status_override);
}

void view_ble_sync_enter(void) {
    s_ble_unconnected_since_us = esp_timer_get_time();
    s_ble_headsup_shown = false;
    ble_sync_start();
    view_ble_sync_render(ble_sync_is_connected(), ble_sync_get_bytes_received(), NULL);
}

reader_view_t view_ble_sync_on_wake(void) {
    view_ble_sync_enter();
    return VIEW_BLE_SYNC;
}

reader_view_t view_ble_sync_handle_event(button_event_t evt) {
    if (evt.type == BTN_EVENT_CUSTOM_REFRESH) {
        s_ble_unconnected_since_us = esp_timer_get_time();
        s_ble_headsup_shown = false;
        view_ble_sync_render(ble_sync_is_connected(), ble_sync_get_bytes_received(), NULL);
        return VIEW_BLE_SYNC;
    }

    if (evt.type == BTN_LONG_PRESS) {
        ble_sync_stop();
        book_library_invalidate_cache();
        book_library_scan();
        app_settings_load(true);
        view_menu_render(false);
        return VIEW_MENU;
    }

    s_ble_unconnected_since_us = esp_timer_get_time();
    if (s_ble_headsup_shown) {
        s_ble_headsup_shown = false;
        view_ble_sync_render(ble_sync_is_connected(), ble_sync_get_bytes_received(), NULL);
    }
    return VIEW_BLE_SYNC;
}

TickType_t view_ble_sync_get_wait_ticks(void) {
    TickType_t wait_ticks = pdMS_TO_TICKS(5000);
    if (ble_sync_is_connected()) {
        s_ble_unconnected_since_us = esp_timer_get_time();
        s_ble_headsup_shown = false;
    } else {
        int64_t elapsed_us = esp_timer_get_time() - s_ble_unconnected_since_us;
        int64_t headsup_us = (int64_t)BLE_SYNC_HEADSUP_AT_SEC * 1000000LL;
        int64_t timeout_us = (int64_t)BLE_SYNC_UNCONNECTED_TIMEOUT_SEC * 1000000LL;
        int64_t next_target_us = s_ble_headsup_shown ? timeout_us : headsup_us;
        int64_t remaining_ms = (next_target_us - elapsed_us) / 1000LL;
        if (remaining_ms <= 0) {
            wait_ticks = 0;
        } else if (remaining_ms < 5000) {
            wait_ticks = pdMS_TO_TICKS(remaining_ms);
        }
    }
    return wait_ticks;
}

reader_view_t view_ble_sync_check_timeout(int64_t *last_activity_us) {
    *last_activity_us = esp_timer_get_time();
    if (ble_sync_is_connected()) {
        s_ble_unconnected_since_us = *last_activity_us;
        s_ble_headsup_shown = false;
        return VIEW_BLE_SYNC;
    }

    int64_t unconnected_sec = (*last_activity_us - s_ble_unconnected_since_us) / 1000000LL;
    if (unconnected_sec >= BLE_SYNC_UNCONNECTED_TIMEOUT_SEC) {
        ESP_LOGI(TAG, "BLE Sync unconnected for %llds. Exiting to menu...", unconnected_sec);
        ble_sync_stop();
        book_library_invalidate_cache();
        book_library_scan();
        app_settings_load(true);
        *last_activity_us = esp_timer_get_time();
        view_menu_render(false);
        return VIEW_MENU;
    } else if (!s_ble_headsup_shown && unconnected_sec >= BLE_SYNC_HEADSUP_AT_SEC) {
        s_ble_headsup_shown = true;
        ESP_LOGI(TAG, "BLE Sync unconnected for %llds. Showing 20s heads-up note.", unconnected_sec);
        view_ble_sync_render(false, ble_sync_get_bytes_received(), "No link: Exit in 20s");
    }

    return VIEW_BLE_SYNC;
}
