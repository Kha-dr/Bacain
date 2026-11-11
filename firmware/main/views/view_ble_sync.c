#define view_ble_sync_on_wake __legacy_view_ble_sync_on_wake
#include "view_ble_sync.h"
#undef view_ble_sync_on_wake
#include "views.h"
#include <stdio.h>
#include <string.h>
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
#define BLE_SYNC_HEADSUP_REMAINING_SEC 20
#define BLE_SYNC_HEADSUP_AT_SEC (BLE_SYNC_UNCONNECTED_TIMEOUT_SEC - BLE_SYNC_HEADSUP_REMAINING_SEC)

/* Poll interval while a status screen is showing (state transitions show up within this time) */
#define BLE_SYNC_STATUS_POLL_MS 1000
/* Redraw progress only every N percent (e-paper: limit refresh count) */
#define BLE_SYNC_PROGRESS_STEP_PCT 10

/* Status screen layout (tuned for 200x200; adjust if the panel differs) */
#define SYNC_Y_LABEL (UI_HEADER_HEIGHT + 28)
#define SYNC_Y_NAME (SYNC_Y_LABEL + 30)
#define SYNC_Y_BAR (SYNC_Y_NAME + 24)
#define SYNC_BAR_H 14
#define SYNC_BAR_MARGIN 16
#define SYNC_Y_PCT (SYNC_Y_BAR + SYNC_BAR_H + 8)
#define SYNC_Y_BYTES (SYNC_Y_PCT + 18)

typedef enum
{
    SYNC_SCREEN_QR = 0,
    SYNC_SCREEN_CONNECTED,
    SYNC_SCREEN_TRANSFERRING,
    SYNC_SCREEN_COMPLETE,
    SYNC_SCREEN_FAILED,
    SYNC_SCREEN_CANCELLED,
} sync_screen_t;

static int64_t s_ble_unconnected_since_us = 0;
static bool s_ble_headsup_shown = false;

static bool s_has_rendered = false;
static sync_screen_t s_last_screen = SYNC_SCREEN_QR;
static int s_last_pct_bucket = -1;

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static sync_screen_t resolve_screen(bool is_connected, const ble_sync_xfer_info_t *x)
{
    if (!is_connected)
        return SYNC_SCREEN_QR;

    switch (x->state)
    {
    case BLE_SYNC_XFER_TRANSFERRING:
        return SYNC_SCREEN_TRANSFERRING;
    case BLE_SYNC_XFER_COMPLETE:
        return SYNC_SCREEN_COMPLETE;
    case BLE_SYNC_XFER_FAILED:
        return SYNC_SCREEN_FAILED;
    case BLE_SYNC_XFER_ABORTED:
        return SYNC_SCREEN_CANCELLED;
    case BLE_SYNC_XFER_IDLE:
    default:
        return SYNC_SCREEN_CONNECTED;
    }
}

static uint8_t xfer_pct(const ble_sync_xfer_info_t *x)
{
    if (x->total == 0)
        return 0;
    uint64_t p = ((uint64_t)x->bytes * 100ULL) / (uint64_t)x->total;
    return (uint8_t)(p > 100 ? 100 : p);
}

static int pct_bucket_for(sync_screen_t screen, const ble_sync_xfer_info_t *x)
{
    if (screen != SYNC_SCREEN_TRANSFERRING)
        return -1;
    return xfer_pct(x) / BLE_SYNC_PROGRESS_STEP_PCT;
}

static void fmt_size(char *out, size_t n, uint32_t b)
{
    if (b < 1024U)
        snprintf(out, n, "%u B", (unsigned)b);
    else if (b < 1024U * 1024U)
        snprintf(out, n, "%u.%u KB", (unsigned)(b / 1024U),
                 (unsigned)(((b % 1024U) * 10U) / 1024U));
    else
        snprintf(out, n, "%u.%u MB", (unsigned)(b / (1024U * 1024U)),
                 (unsigned)(((b % (1024U * 1024U)) * 10U) / (1024U * 1024U)));
}

/* Truncate to what fits on one line, ending with "..." */
static void fit_text(const char *in, char *out, size_t out_sz, const sFONT *font)
{
    int max_chars = (DISPLAY_WIDTH - 8) / font->Width;
    if (max_chars < 4)
        max_chars = 4;
    if (max_chars > (int)out_sz - 1)
        max_chars = (int)out_sz - 1;

    size_t len = strlen(in);
    if ((int)len <= max_chars)
    {
        memcpy(out, in, len + 1);
        return;
    }
    memcpy(out, in, (size_t)max_chars - 3);
    memcpy(out + max_chars - 3, "...", 4);
}

static void draw_header(void)
{
    Paint_DrawRectangle(0, 0, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(8, 3, "SYNC BOOKS", &Font12, WHITE, BLACK);
    Paint_DrawLine(0, UI_HEADER_HEIGHT + 1, DISPLAY_WIDTH - 1, UI_HEADER_HEIGHT + 1, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
}

static void draw_footer(sync_screen_t screen)
{
    Paint_DrawLine(0, UI_FOOTER_Y - 2, DISPLAY_WIDTH - 1, UI_FOOTER_Y - 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    /* Long press during transfer calls ble_sync_stop(), which deletes the partial file */
    ui_draw_centered_string(UI_FOOTER_Y + 1,
                            screen == SYNC_SCREEN_TRANSFERRING ? "Hold ▲/▼ Cancel" : "Hold ▲/▼ Back",
                            &Font12);
}

static void draw_qr_body(const char *status_override)
{
    const int scale = 4; /* 25 * 4 = 100x100 px */
    int qx = (BOARD_DISPLAY_WIDTH - (SYNC_QRCODE_SIZE * scale)) / 2;
    int qy = (BOARD_DISPLAY_HEIGHT - (SYNC_QRCODE_SIZE * scale)) / 2;

    Paint_DrawRectangle(qx - 4, qy - 4,
                        qx + (SYNC_QRCODE_SIZE * scale) + 3, qy + (SYNC_QRCODE_SIZE * scale) + 3,
                        WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);

    for (int y = 0; y < SYNC_QRCODE_SIZE; y++)
    {
        for (int x = 0; x < SYNC_QRCODE_SIZE; x++)
        {
            int bit_idx = y * SYNC_QRCODE_SIZE + x;
            uint8_t bit = (s_sync_qrcode_bitmap[bit_idx / 8] >> (7 - (bit_idx % 8))) & 1;
            if (bit)
            {
                Paint_DrawRectangle(qx + x * scale, qy + y * scale,
                                    qx + (x + 1) * scale - 1, qy + (y + 1) * scale - 1,
                                    BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
            }
        }
    }

    ui_draw_centered_string(162,
                            (status_override && status_override[0] != '\0') ? status_override
                                                                            : "Waiting to connect...",
                            &Font12);
}

static void draw_progress_bar(uint8_t pct)
{
    int x0 = SYNC_BAR_MARGIN;
    int x1 = DISPLAY_WIDTH - 1 - SYNC_BAR_MARGIN;
    int y0 = SYNC_Y_BAR;
    int y1 = SYNC_Y_BAR + SYNC_BAR_H;

    Paint_DrawRectangle(x0, y0, x1, y1, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);

    int inner_w = (x1 - 2) - (x0 + 2);
    int fill_w = (inner_w * pct) / 100;
    if (fill_w > 0)
    {
        Paint_DrawRectangle(x0 + 2, y0 + 2, x0 + 2 + fill_w, y1 - 2, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    }
}

static void draw_status_body(sync_screen_t screen, const ble_sync_xfer_info_t *x)
{
    char line[48];
    char a[16], b[16];

    const char *label = "Connected";
    switch (screen)
    {
    case SYNC_SCREEN_TRANSFERRING:
        label = "Transferring";
        break;
    case SYNC_SCREEN_COMPLETE:
        label = "Complete";
        break;
    case SYNC_SCREEN_FAILED:
        label = "Failed";
        break;
    case SYNC_SCREEN_CANCELLED:
        label = "Cancelled";
        break;
    default:
        label = "Connected";
        break;
    }
    ui_draw_centered_string(SYNC_Y_LABEL, label, &Font16);

    if (screen == SYNC_SCREEN_CONNECTED)
    {
        ui_draw_centered_string(SYNC_Y_NAME, "Ready to receive", &Font12);
        ui_draw_centered_string(SYNC_Y_NAME + 18, "Upload from the app", &Font12);
        return;
    }

    if (x->filename[0] != '\0')
    {
        fit_text(x->filename, line, sizeof(line), &Font12);
        ui_draw_centered_string(SYNC_Y_NAME, line, &Font12);
    }

    if (screen == SYNC_SCREEN_TRANSFERRING)
    {
        uint8_t pct = xfer_pct(x);
        draw_progress_bar(pct);
        snprintf(line, sizeof(line), "%u%%", (unsigned)pct);
        ui_draw_centered_string(SYNC_Y_PCT, line, &Font12);
        fmt_size(a, sizeof(a), x->bytes);
        fmt_size(b, sizeof(b), x->total);
        snprintf(line, sizeof(line), "%s / %s", a, b);
        ui_draw_centered_string(SYNC_Y_BYTES, line, &Font12);
    }
    else if (screen == SYNC_SCREEN_COMPLETE)
    {
        draw_progress_bar(100);
        fmt_size(a, sizeof(a), x->bytes);
        snprintf(line, sizeof(line), "%s saved", a);
        ui_draw_centered_string(SYNC_Y_PCT, line, &Font12);
    }
    else if (screen == SYNC_SCREEN_FAILED)
    {
        snprintf(line, sizeof(line), "Error 0x%02X", (unsigned)x->status);
        ui_draw_centered_string(SYNC_Y_BAR, line, &Font12);
    }
    else if (screen == SYNC_SCREEN_CANCELLED)
    {
        ui_draw_centered_string(SYNC_Y_BAR, "Partial file removed", &Font12);
    }
}

static void render_screen(sync_screen_t screen, const ble_sync_xfer_info_t *x, const char *status_override)
{
    uint8_t *canvas = reader_ui_get_canvas();
    if (!canvas)
        return;

    epaper_power_on();
    Paint_SelectImage(canvas);
    Paint_Clear(WHITE);

    draw_header();
    if (screen == SYNC_SCREEN_QR)
        draw_qr_body(status_override);
    else
        draw_status_body(screen, x);
    draw_footer(screen);

    reader_ui_flush_canvas(false);

    s_has_rendered = true;
    s_last_screen = screen;
    s_last_pct_bucket = pct_bucket_for(screen, x);
}

/* Redraw only if the screen or the progress bucket changed */
static void refresh_if_changed(void)
{
    ble_sync_xfer_info_t x;
    ble_sync_get_xfer_info(&x);
    sync_screen_t screen = resolve_screen(ble_sync_is_connected(), &x);
    int bucket = pct_bucket_for(screen, &x);

    if (!s_has_rendered || screen != s_last_screen || bucket != s_last_pct_bucket)
    {
        ESP_LOGI(TAG, "Sync screen %d -> %d (bucket %d)", (int)s_last_screen, (int)screen, bucket);
        render_screen(screen, &x, NULL);
    }
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

void view_ble_sync_render(bool is_connected, uint32_t bytes_received, const char *status_override)
{
    (void)bytes_received; /* now taken from ble_sync_get_xfer_info() */
    ble_sync_xfer_info_t x;
    ble_sync_get_xfer_info(&x);
    render_screen(resolve_screen(is_connected, &x), &x, status_override);
}

void reader_ui_render_ble_sync(bool is_connected, uint32_t bytes_received, const char *status_override)
{
    view_ble_sync_render(is_connected, bytes_received, status_override);
}

void view_ble_sync_enter(void)
{
    s_ble_unconnected_since_us = esp_timer_get_time();
    s_ble_headsup_shown = false;
    s_has_rendered = false;
    s_last_pct_bucket = -1;
    ble_sync_start();
    view_ble_sync_render(ble_sync_is_connected(), ble_sync_get_bytes_received(), NULL);
}

reader_view_t view_ble_sync_on_wake(void)
{
    view_ble_sync_enter();
    return VIEW_BLE_SYNC;
}

reader_view_t view_ble_sync_handle_event(button_event_t evt)
{
    if (evt.type == BTN_EVENT_CUSTOM_REFRESH)
    {
        s_ble_unconnected_since_us = esp_timer_get_time();
        s_ble_headsup_shown = false;
        view_ble_sync_render(ble_sync_is_connected(), ble_sync_get_bytes_received(), NULL);
        return VIEW_BLE_SYNC;
    }

    if (evt.type == BTN_LONG_PRESS)
    {
        ble_sync_stop();
        book_library_invalidate_cache();
        book_library_scan();
        app_settings_load(true);
        view_menu_render(false);
        return VIEW_MENU;
    }

    s_ble_unconnected_since_us = esp_timer_get_time();
    if (s_ble_headsup_shown)
    {
        s_ble_headsup_shown = false;
        view_ble_sync_render(ble_sync_is_connected(), ble_sync_get_bytes_received(), NULL);
    }
    return VIEW_BLE_SYNC;
}

TickType_t view_ble_sync_get_wait_ticks(void)
{
    TickType_t wait_ticks = pdMS_TO_TICKS(5000);

    if (ble_sync_is_connected())
    {
        s_ble_unconnected_since_us = esp_timer_get_time();
        s_ble_headsup_shown = false;
        /* Poll so Connected -> Transferring -> Complete transitions appear promptly */
        wait_ticks = pdMS_TO_TICKS(BLE_SYNC_STATUS_POLL_MS);
    }
    else
    {
        /* Just disconnected while a status screen is up: switch back to QR quickly */
        if (s_last_screen != SYNC_SCREEN_QR)
        {
            return pdMS_TO_TICKS(BLE_SYNC_STATUS_POLL_MS);
        }

        int64_t elapsed_us = esp_timer_get_time() - s_ble_unconnected_since_us;
        int64_t headsup_us = (int64_t)BLE_SYNC_HEADSUP_AT_SEC * 1000000LL;
        int64_t timeout_us = (int64_t)BLE_SYNC_UNCONNECTED_TIMEOUT_SEC * 1000000LL;
        int64_t next_target_us = s_ble_headsup_shown ? timeout_us : headsup_us;
        int64_t remaining_ms = (next_target_us - elapsed_us) / 1000LL;
        if (remaining_ms <= 0)
        {
            wait_ticks = 0;
        }
        else if (remaining_ms < 5000)
        {
            wait_ticks = pdMS_TO_TICKS(remaining_ms);
        }
    }
    return wait_ticks;
}

reader_view_t view_ble_sync_check_timeout(int64_t *last_activity_us)
{
    *last_activity_us = esp_timer_get_time();

    if (ble_sync_is_connected())
    {
        s_ble_unconnected_since_us = *last_activity_us;
        s_ble_headsup_shown = false;
        refresh_if_changed(); /* Connected / Transferring / Complete / Failed / Cancelled */
        return VIEW_BLE_SYNC;
    }

    /* Disconnected: if a status screen is still showing, go back to QR first */
    if (s_last_screen != SYNC_SCREEN_QR)
    {
        refresh_if_changed();
    }

    int64_t unconnected_sec = (*last_activity_us - s_ble_unconnected_since_us) / 1000000LL;
    if (unconnected_sec >= BLE_SYNC_UNCONNECTED_TIMEOUT_SEC)
    {
        ESP_LOGI(TAG, "BLE Sync unconnected for %llds. Exiting to menu...", unconnected_sec);
        ble_sync_stop();
        book_library_invalidate_cache();
        book_library_scan();
        app_settings_load(true);
        *last_activity_us = esp_timer_get_time();
        view_menu_render(false);
        return VIEW_MENU;
    }
    else if (!s_ble_headsup_shown && unconnected_sec >= BLE_SYNC_HEADSUP_AT_SEC)
    {
        s_ble_headsup_shown = true;
        ESP_LOGI(TAG, "BLE Sync unconnected for %llds. Showing 20s heads-up note.", unconnected_sec);
        view_ble_sync_render(false, ble_sync_get_bytes_received(), "No link: Exit in 20s");
    }

    return VIEW_BLE_SYNC;
}