#include "reader_ui.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "epaper_port.h"
#include "GUI_Paint.h"
#include "fonts.h"
#include "text_pager.h"
#include "book_library.h"
#include "power_mgr.h"
#include "app_config.h"

#define TAG "READER_UI"

static uint8_t *s_canvas = NULL;
static int s_reading_page_turns = 0;

esp_err_t reader_ui_init(void) {
    if (!s_canvas) {
        s_canvas = (uint8_t *)heap_caps_malloc(DISPLAY_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
        if (!s_canvas) {
            s_canvas = (uint8_t *)malloc(DISPLAY_BUFFER_SIZE);
        }
        if (!s_canvas) {
            ESP_LOGE(TAG, "Failed to allocate display canvas buffer");
            return ESP_ERR_NO_MEM;
        }
    }

    Paint_NewImage(s_canvas, DISPLAY_WIDTH, DISPLAY_HEIGHT, ROTATE_0, WHITE);
    Paint_SetScale(2);
    Paint_SelectImage(s_canvas);
    Paint_Clear(WHITE);

    epaper_power_on();
    epaper_port_init();
    epaper_port_clear(WHITE);
    epaper_port_init_partial();
    vTaskDelay(pdMS_TO_TICKS(500));

    return ESP_OK;
}

void reader_ui_render_reading(void) {
    if (!s_canvas) return;

    epaper_power_on();

    Paint_SelectImage(s_canvas);
    text_pager_render_to_buffer(s_canvas);

    // Add Battery & Status to top header right corner
    char bat_str[16];
    snprintf(bat_str, sizeof(bat_str), "%u%%", power_mgr_get_battery_percentage());
    Paint_DrawString_EN(165, 2, bat_str, &Font12, BLACK, WHITE);

    s_reading_page_turns++;
    if (s_reading_page_turns >= 10) {
        epaper_port_init();
        epaper_port_display(s_canvas);
        epaper_port_init_partial();
        s_reading_page_turns = 0;
    } else {
        epaper_port_display_part(s_canvas);
    }
}

void reader_ui_render_reading_force_full(void) {
    if (!s_canvas) return;

    epaper_power_on();

    Paint_SelectImage(s_canvas);
    text_pager_render_to_buffer(s_canvas);

    // Add Battery & Status to top header right corner
    char bat_str[16];
    snprintf(bat_str, sizeof(bat_str), "%u%%", power_mgr_get_battery_percentage());
    Paint_DrawString_EN(165, 2, bat_str, &Font12, BLACK, WHITE);

    s_reading_page_turns = 0;
    epaper_port_init();
    epaper_port_display(s_canvas);
    epaper_port_init_partial();
}

void reader_ui_render_menu(int selected_index) {
    if (!s_canvas) return;

    epaper_power_on();

    Paint_SelectImage(s_canvas);
    Paint_Clear(WHITE);

    // Title Bar - Solid black box with white text
    Paint_DrawRectangle(0, 0, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(8, 3, "READER MENU", &Font12, WHITE, BLACK);

    char bat_str[16];
    snprintf(bat_str, sizeof(bat_str), "%u%%", power_mgr_get_battery_percentage());
    Paint_DrawString_EN(165, 3, bat_str, &Font12, WHITE, BLACK);

    Paint_DrawLine(0, UI_HEADER_HEIGHT + 1, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT + 1, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    // Menu Options
    const char *items[] = {
        "Continue Reading",
        "Book Library",
        "Font Size: [16px]",
        "Sync Books (BLE)",
        "Power Off / Sleep"
    };
    int count = 5;

    char font_str[32];
    if (text_pager_get_font_size() == TEXT_PAGER_FONT_12) {
        snprintf(font_str, sizeof(font_str), "Font Size: [12px]");
    } else {
        snprintf(font_str, sizeof(font_str), "Font Size: [16px]");
    }
    items[2] = font_str;

    int y = 30;
    for (int i = 0; i < count; i++) {
        if (i == selected_index) {
            Paint_DrawRectangle(4, y - 2, DISPLAY_WIDTH - 4, y + 16, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
            Paint_DrawString_EN(8, y, ">", &Font12, BLACK, WHITE);
        }
        Paint_DrawString_EN(22, y, items[i], &Font12, BLACK, WHITE);
        y += 24;
    }

    // Footer
    Paint_DrawLine(0, UI_FOOTER_Y - 2, DISPLAY_WIDTH - 1, UI_FOOTER_Y - 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawString_EN(6, UI_FOOTER_Y + 1, "BOOT:Dn PWR:Up", &Font12, BLACK, WHITE);
    Paint_DrawString_EN(112, UI_FOOTER_Y + 1, "2xTap:Select", &Font12, BLACK, WHITE);

    epaper_port_display_part(s_canvas);
}

void reader_ui_render_book_list(int selected_index) {
    if (!s_canvas) return;

    epaper_power_on();

    Paint_SelectImage(s_canvas);
    Paint_Clear(WHITE);

    // Title Bar - Solid black box with white text
    Paint_DrawRectangle(0, 0, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(8, 3, "SELECT BOOK", &Font12, WHITE, BLACK);
    Paint_DrawLine(0, UI_HEADER_HEIGHT + 1, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT + 1, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    size_t count = book_library_get_count();
    if (count == 0) {
        Paint_DrawString_EN(20, 80, "No books in /books!", &Font12, BLACK, WHITE);
        Paint_DrawString_EN(20, 100, "Use BLE Sync to add.", &Font12, BLACK, WHITE);
    } else {
        int y = 28;
        for (size_t i = 0; i < count && i < 6; i++) {
            const book_info_t *book = book_library_get_book(i);
            if (!book) continue;

            if ((int)i == selected_index) {
                Paint_DrawRectangle(4, y - 2, DISPLAY_WIDTH - 4, y + 16, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
                Paint_DrawString_EN(8, y, ">", &Font12, BLACK, WHITE);
            }

            char display_name[24];
            strncpy(display_name, book->filename, sizeof(display_name) - 1);
            display_name[sizeof(display_name) - 1] = '\0';
            Paint_DrawString_EN(22, y, display_name, &Font12, BLACK, WHITE);
            y += 24;
        }
    }

    // Footer
    Paint_DrawLine(0, UI_FOOTER_Y - 2, DISPLAY_WIDTH - 1, UI_FOOTER_Y - 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawString_EN(6, UI_FOOTER_Y + 1, "BOOT:Dn PWR:Up", &Font12, BLACK, WHITE);
    Paint_DrawString_EN(112, UI_FOOTER_Y + 1, "2xTap:Open", &Font12, BLACK, WHITE);

    epaper_port_display_part(s_canvas);
}

void reader_ui_render_ble_sync(bool is_connected, uint32_t bytes_received) {
    if (!s_canvas) return;

    epaper_power_on();

    Paint_SelectImage(s_canvas);
    Paint_Clear(WHITE);

    // Title Bar - Solid black box with white text
    Paint_DrawRectangle(0, 0, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(8, 3, "BLE BOOK SYNC", &Font12, WHITE, BLACK);
    Paint_DrawLine(0, UI_HEADER_HEIGHT + 1, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT + 1, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    Paint_DrawString_EN(12, 35, "Open Web PWA in", &Font12, BLACK, WHITE);
    Paint_DrawString_EN(12, 50, "Chrome / Edge:", &Font12, BLACK, WHITE);

    Paint_DrawRectangle(10, 70, 190, 95, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
    Paint_DrawString_EN(16, 76, "Mini-E-Reader", &Font16, BLACK, WHITE);

    if (is_connected) {
        Paint_DrawString_EN(12, 110, "Status: CONNECTED", &Font12, BLACK, WHITE);
        char rx_buf[32];
        snprintf(rx_buf, sizeof(rx_buf), "Rx: %lu bytes", (unsigned long)bytes_received);
        Paint_DrawString_EN(12, 130, rx_buf, &Font12, BLACK, WHITE);
    } else {
        Paint_DrawString_EN(12, 110, "Status: ADVERTISING", &Font12, BLACK, WHITE);
        Paint_DrawString_EN(12, 130, "Ready for connection...", &Font12, BLACK, WHITE);
    }

    // Footer
    Paint_DrawLine(0, UI_FOOTER_Y - 2, DISPLAY_WIDTH - 1, UI_FOOTER_Y - 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawString_EN(12, UI_FOOTER_Y + 1, "[BOOT Long] Exit Sync", &Font12, BLACK, WHITE);

    epaper_port_display_part(s_canvas);
}

void reader_ui_render_splash(const char *title, const char *subtitle) {
    if (!s_canvas) return;

    epaper_power_on();

    Paint_SelectImage(s_canvas);
    Paint_Clear(WHITE);

    Paint_DrawRectangle(10, 40, 190, 160, BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
    Paint_DrawString_EN(20, 65, title, &Font16, BLACK, WHITE);
    if (subtitle) {
        Paint_DrawString_EN(20, 110, subtitle, &Font12, BLACK, WHITE);
    }

    epaper_port_display(s_canvas);
}

void reader_ui_render_sleeping(void) {
    if (!s_canvas) return;

    epaper_power_on();

    Paint_SelectImage(s_canvas);
    Paint_Clear(WHITE);

    Paint_DrawRectangle(10, 40, 190, 160, BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
    Paint_DrawString_EN(20, 60, "Mini-E-Reader", &Font16, BLACK, WHITE);
    Paint_DrawString_EN(30, 95, "SLEEPING...", &Font16, BLACK, WHITE);
    Paint_DrawString_EN(18, 130, "Press [PWR] to Wake", &Font12, BLACK, WHITE);

    epaper_port_display_part(s_canvas);
}
