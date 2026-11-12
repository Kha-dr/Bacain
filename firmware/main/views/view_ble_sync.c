#include "view_ble_sync.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "fonts.h"
#include "GUI_Paint.h"
#include "app_settings.h"
#include "book_library.h"
#include "ble_sync.h"
#include "sync_qrcode.h"
#include "ui_layout.h"
#include "ui_display.h"
#include "ui_text.h"
#include "ui_widgets.h"

#define TAG "VIEW_BLE_SYNC"

#define UNCONNECTED_TIMEOUT_SEC 60
#define HEADSUP_REMAINING_SEC 20
#define HEADSUP_AT_SEC (UNCONNECTED_TIMEOUT_SEC - HEADSUP_REMAINING_SEC)
#define HEADSUP_TEXT "No link: Exit in 20s"

#define STATUS_POLL_MS 1000
#define PROGRESS_STEP_PCT 10

#define QR_QUIET_MODULES 2 /* white border around the code, in modules */
#define QR_CAPTION_GAP 12

#define ICON_STROKE DOT_PIXEL_2X2
#define BAR_H 14
#define BAR_MARGIN 16
#define GAP_ICON 12
#define GAP_NAME 12
#define GAP_BAR 8
#define GAP_PCT 6

typedef enum
{
    SCREEN_QR = 0,
    SCREEN_CONNECTED,
    SCREEN_TRANSFERRING,
    SCREEN_COMPLETE,
    SCREEN_FAILED,
    SCREEN_CANCELLED,
} sync_screen_t;

/* Vertical positions for the status screen, computed from the panel size. */
typedef struct
{
    int icon_r;
    int icon_cy;
    int name_y;
    int bar_y;
    int pct_y;
    int bytes_y;
} sync_layout_t;

static int64_t s_unconnected_since_us = 0;
static bool s_headsup_shown = false;
static bool s_has_rendered = false;
static sync_screen_t s_last_screen = SCREEN_QR;
static int s_last_bucket = -1;

/* ---------- state helpers ---------- */

static sync_screen_t resolve_screen(bool connected, const ble_sync_xfer_info_t *x)
{
    if (!connected)
    {
        return SCREEN_QR;
    }
    switch (x->state)
    {
    case BLE_SYNC_XFER_TRANSFERRING:
        return SCREEN_TRANSFERRING;
    case BLE_SYNC_XFER_COMPLETE:
        return SCREEN_COMPLETE;
    case BLE_SYNC_XFER_FAILED:
        return SCREEN_FAILED;
    case BLE_SYNC_XFER_ABORTED:
        return SCREEN_CANCELLED;
    case BLE_SYNC_XFER_IDLE:
    default:
        return SCREEN_CONNECTED;
    }
}

static const char *screen_label(sync_screen_t s)
{
    switch (s)
    {
    case SCREEN_QR:
        return "QR";
    case SCREEN_TRANSFERRING:
        return "Transferring";
    case SCREEN_COMPLETE:
        return "Complete";
    case SCREEN_FAILED:
        return "Failed";
    case SCREEN_CANCELLED:
        return "Cancelled";
    default:
        return "Connected";
    }
}

static uint8_t xfer_pct(const ble_sync_xfer_info_t *x)
{
    if (x->total == 0)
    {
        return 0;
    }
    uint64_t p = ((uint64_t)x->bytes * 100ULL) / (uint64_t)x->total;
    return (uint8_t)(p > 100 ? 100 : p);
}

static int pct_bucket(sync_screen_t screen, const ble_sync_xfer_info_t *x)
{
    return (screen == SCREEN_TRANSFERRING) ? (xfer_pct(x) / PROGRESS_STEP_PCT) : -1;
}

static void fmt_size(char *out, size_t n, uint32_t b)
{
    if (b < 1024U)
    {
        snprintf(out, n, "%u B", (unsigned)b);
    }
    else if (b < 1024U * 1024U)
    {
        snprintf(out, n, "%u.%u KB", (unsigned)(b / 1024U), (unsigned)(((b % 1024U) * 10U) / 1024U));
    }
    else
    {
        snprintf(out, n, "%u.%u MB", (unsigned)(b / (1024U * 1024U)),
                 (unsigned)(((b % (1024U * 1024U)) * 10U) / (1024U * 1024U)));
    }
}

static void reset_idle_timer(void)
{
    s_unconnected_since_us = esp_timer_get_time();
    s_headsup_shown = false;
}

/* ---------- layout ---------- */

static int body_top(void) { return UI_BODY_TOP; }
static int body_bottom(void) { return ui_body_bottom(true); } /* footer is always shown here */

/* Icon + name + bar + percent + bytes as one block, centered in the body. */
static sync_layout_t sync_layout(void)
{
    const int w = ui_screen_w(), h = ui_screen_h();
    const int fh = Font12.Height;
    sync_layout_t L;

    L.icon_r = ((w < h) ? w : h) / 11; /* 18 on 200 px */
    if (L.icon_r < 12)
    {
        L.icon_r = 12;
    }

    const int block = 2 * L.icon_r + GAP_ICON + fh + GAP_NAME + BAR_H + GAP_BAR + fh + GAP_PCT + fh;
    int y = body_top() + ((body_bottom() - body_top()) - block) / 2;
    if (y < body_top())
    {
        y = body_top();
    }

    L.icon_cy = y + L.icon_r;
    y += 2 * L.icon_r + GAP_ICON;
    L.name_y = y;
    y += fh + GAP_NAME;
    L.bar_y = y;
    y += BAR_H + GAP_BAR;
    L.pct_y = y;
    y += fh + GAP_PCT;
    L.bytes_y = y;
    return L;
}

/* ---------- drawing ---------- */

/* One line, centered, "..." when too long (ui_text_wrap with max_lines = 1). */
static void draw_centered_fit(int y, const char *text, sFONT *font)
{
    char line[1][UI_WRAP_COLS_MAX + 1];
    int cols = (ui_screen_w() - 8) / font->Width;
    if (ui_text_wrap(text, cols, 1, line) > 0)
    {
        ui_draw_text_centered(y, line[0], font);
    }
}

/* Largest integer module size that fits the body with quiet zone and caption. */
static void draw_qr(void)
{
    const int w = ui_screen_w();
    const int avail_h = body_bottom() - body_top() - (Font12.Height + QR_CAPTION_GAP);
    const int span = SYNC_QRCODE_SIZE + 2 * QR_QUIET_MODULES;

    int scale = (w - 8) / span;
    if (avail_h / span < scale)
    {
        scale = avail_h / span;
    }
    if (scale < 1)
    {
        scale = 1;
    }

    const int size = SYNC_QRCODE_SIZE * scale;
    const int quiet = QR_QUIET_MODULES * scale;
    const int block = size + 2 * quiet + QR_CAPTION_GAP + Font12.Height;
    const int qx = (w - size) / 2;
    int top = body_top() + ((body_bottom() - body_top()) - block) / 2;
    if (top < body_top())
    {
        top = body_top();
    }
    const int qy = top + quiet;

    ui_fill_rect(qx - quiet, qy - quiet, qx + size + quiet - 1, qy + size + quiet - 1, WHITE);
    for (int y = 0; y < SYNC_QRCODE_SIZE; y++)
    {
        for (int x = 0; x < SYNC_QRCODE_SIZE; x++)
        {
            int bit = y * SYNC_QRCODE_SIZE + x;
            if ((s_sync_qrcode_bitmap[bit / 8] >> (7 - (bit % 8))) & 1)
            {
                ui_fill_rect(qx + x * scale, qy + y * scale,
                             qx + (x + 1) * scale - 1, qy + (y + 1) * scale - 1, BLACK);
            }
        }
    }
    ui_draw_text_centered(qy + size + quiet + QR_CAPTION_GAP,
                          s_headsup_shown ? HEADSUP_TEXT : "Waiting to connect...", &Font12);
}

static void draw_progress_bar(int y, uint8_t pct)
{
    const int x0 = BAR_MARGIN;
    const int x1 = ui_screen_w() - 1 - BAR_MARGIN;
    Paint_DrawRectangle((UWORD)x0, (UWORD)y, (UWORD)x1, (UWORD)(y + BAR_H), BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);

    int inner_w = (x1 - 2) - (x0 + 2);
    int fill_w = (inner_w * pct) / 100;
    if (fill_w > 0)
    {
        ui_fill_rect(x0 + 2, y + 2, x0 + 2 + fill_w, y + BAR_H - 2, BLACK);
    }
}

static void stroke(int x0, int y0, int x1, int y1)
{
    Paint_DrawLine((UWORD)x0, (UWORD)y0, (UWORD)x1, (UWORD)y1, BLACK, ICON_STROKE, LINE_STYLE_SOLID);
}

/* Ring + glyph. Glyph coordinates are designed for r = 18 and scaled by K(). */
static void draw_state_icon(sync_screen_t s, const sync_layout_t *L)
{
    const int cx = ui_screen_w() / 2;
    const int cy = L->icon_cy;
    const int r = L->icon_r;
#define K(n) ((n) * r / 18)

    Paint_DrawCircle((UWORD)cx, (UWORD)cy, (UWORD)r, BLACK, ICON_STROKE, DRAW_FILL_EMPTY);

    switch (s)
    {
    case SCREEN_TRANSFERRING: /* download: arrow into a tray */
        stroke(cx, cy - K(9), cx, cy + K(3));
        stroke(cx - K(5), cy - K(2), cx, cy + K(3));
        stroke(cx + K(5), cy - K(2), cx, cy + K(3));
        stroke(cx - K(9), cy + K(4), cx - K(9), cy + K(9));
        stroke(cx - K(9), cy + K(9), cx + K(9), cy + K(9));
        stroke(cx + K(9), cy + K(9), cx + K(9), cy + K(4));
        break;
    case SCREEN_COMPLETE: /* check mark */
        stroke(cx - K(9), cy, cx - K(3), cy + K(6));
        stroke(cx - K(3), cy + K(6), cx + K(9), cy - K(7));
        break;
    case SCREEN_FAILED: /* cross */
        stroke(cx - K(7), cy - K(7), cx + K(7), cy + K(7));
        stroke(cx - K(7), cy + K(7), cx + K(7), cy - K(7));
        break;
    case SCREEN_CANCELLED: /* stop square */
        ui_fill_rect(cx - K(6), cy - K(6), cx + K(6), cy + K(6), BLACK);
        break;
    default: /* connected: Bluetooth rune */
        stroke(cx, cy - K(10), cx, cy + K(10));
        stroke(cx, cy - K(10), cx + K(6), cy - K(5));
        stroke(cx + K(6), cy - K(5), cx - K(6), cy + K(5));
        stroke(cx, cy + K(10), cx + K(6), cy + K(5));
        stroke(cx + K(6), cy + K(5), cx - K(6), cy - K(5));
        break;
    }
#undef K
}

static void draw_status(sync_screen_t screen, const ble_sync_xfer_info_t *x)
{
    const sync_layout_t L = sync_layout();
    char line[64], a[24], b[24];

    draw_state_icon(screen, &L);

    if (screen == SCREEN_CONNECTED)
    {
        ui_draw_text_centered(L.name_y, "Ready to receive", &Font12);
        ui_draw_text_centered(L.name_y + Font12.Height + GAP_PCT, "Upload from the app", &Font12);
        return;
    }
    if (x->filename[0] != '\0')
    {
        draw_centered_fit(L.name_y, x->filename, &Font12);
    }

    switch (screen)
    {
    case SCREEN_TRANSFERRING:
    {
        uint8_t pct = xfer_pct(x);
        draw_progress_bar(L.bar_y, pct);
        snprintf(line, sizeof(line), "%u%%", (unsigned)pct);
        ui_draw_text_centered(L.pct_y, line, &Font12);
        fmt_size(a, sizeof(a), x->bytes);
        fmt_size(b, sizeof(b), x->total);
        snprintf(line, sizeof(line), "%s / %s", a, b);
        ui_draw_text_centered(L.bytes_y, line, &Font12);
        break;
    }
    case SCREEN_COMPLETE:
        draw_progress_bar(L.bar_y, 100);
        fmt_size(a, sizeof(a), x->bytes);
        snprintf(line, sizeof(line), "%s saved", a);
        ui_draw_text_centered(L.pct_y, line, &Font12);
        break;
    case SCREEN_FAILED:
        snprintf(line, sizeof(line), "Error 0x%02X", (unsigned)x->status);
        ui_draw_text_centered(L.bar_y, line, &Font12);
        break;
    case SCREEN_CANCELLED:
        ui_draw_text_centered(L.bar_y, "Partial file removed", &Font12);
        break;
    default:
        break;
    }
}

/* Single draw path: dispatcher render, polling refresh and heads-up all come here. */
static void draw_current(bool full_refresh)
{
    ble_sync_xfer_info_t x;
    ble_sync_get_xfer_info(&x);
    sync_screen_t screen = resolve_screen(ble_sync_is_connected(), &x);

    if (!ui_begin(true))
    {
        return;
    }
    ui_draw_header("SYNC BOOKS", false);
    if (screen == SCREEN_QR)
    {
        draw_qr();
    }
    else
    {
        draw_status(screen, &x);
    }
    /* Long press during transfer calls ble_sync_stop(), which deletes the partial file */
    ui_draw_footer(screen == SCREEN_TRANSFERRING ? "Hold ▲/▼ Cancel" : "Hold ▲/▼ Back", true);
    ui_end(full_refresh ? UI_REFRESH_FULL : UI_REFRESH_PARTIAL);

    s_has_rendered = true;
    s_last_screen = screen;
    s_last_bucket = pct_bucket(screen, &x);
}

static void refresh_if_changed(void)
{
    ble_sync_xfer_info_t x;
    ble_sync_get_xfer_info(&x);
    sync_screen_t screen = resolve_screen(ble_sync_is_connected(), &x);
    int bucket = pct_bucket(screen, &x);

    if (!s_has_rendered || screen != s_last_screen || bucket != s_last_bucket)
    {
        ESP_LOGI(TAG, "screen %s -> %s (bucket %d)",
                 screen_label(s_last_screen), screen_label(screen), bucket);
        draw_current(false);
    }
}

/* ---------- view_def_t ---------- */

static void enter(reader_view_t self)
{
    (void)self;
    reset_idle_timer();
    s_has_rendered = false;
    s_last_screen = SCREEN_QR;
    s_last_bucket = -1;
    book_library_scan(); /* was done by the menu before entering */
    ble_sync_start();
}

static void leave(reader_view_t self)
{
    (void)self;
    ble_sync_stop();
    book_library_invalidate_cache();
    book_library_scan();
    app_settings_load(true); /* reload + apply: settings may change during sync */
}

static reader_view_t wake(reader_view_t self)
{
    enter(self); /* BLE stack does not survive deep sleep: restart it, no render */
    return self;
}

static void render(reader_view_t self, bool full_refresh)
{
    (void)self;
    draw_current(full_refresh);
}

static reader_view_t event(reader_view_t self, button_event_t evt)
{
    if (evt.type == BTN_EVENT_CUSTOM_REFRESH)
    {
        reset_idle_timer();
        return self; /* dispatcher renders */
    }
    if (evt.type == BTN_LONG_PRESS)
    {
        return VIEW_MENU; /* leave() stops BLE and rescans */
    }

    bool was_headsup = s_headsup_shown;
    reset_idle_timer();
    if (was_headsup)
    {
        draw_current(false); /* clear the heads-up text */
    }
    return self;
}

const view_def_t VIEW_DEF_BLE_SYNC = {
    .first = VIEW_BLE_SYNC,
    .last = VIEW_BLE_SYNC,
    .full_on_refresh = false,
    .enter = enter,
    .wake = wake,
    .render = render,
    .event = event,
    .leave = leave,
};

/* ---------- main-loop hooks ---------- */

TickType_t view_ble_sync_get_wait_ticks(void)
{
    if (ble_sync_is_connected())
    {
        reset_idle_timer();
        return pdMS_TO_TICKS(STATUS_POLL_MS); /* show state transitions promptly */
    }
    if (s_last_screen != SCREEN_QR)
    {
        return pdMS_TO_TICKS(STATUS_POLL_MS); /* just disconnected: back to QR quickly */
    }

    int64_t elapsed_us = esp_timer_get_time() - s_unconnected_since_us;
    int64_t target_us = (int64_t)(s_headsup_shown ? UNCONNECTED_TIMEOUT_SEC : HEADSUP_AT_SEC) * 1000000LL;
    int64_t remaining_ms = (target_us - elapsed_us) / 1000LL;

    if (remaining_ms <= 0)
    {
        return 0;
    }
    return (remaining_ms < 5000) ? pdMS_TO_TICKS(remaining_ms) : pdMS_TO_TICKS(5000);
}

reader_view_t view_ble_sync_check_timeout(int64_t *last_activity_us)
{
    *last_activity_us = esp_timer_get_time();

    if (ble_sync_is_connected())
    {
        reset_idle_timer();
        refresh_if_changed();
        return VIEW_BLE_SYNC;
    }
    if (s_last_screen != SCREEN_QR)
    {
        refresh_if_changed();
    }

    int64_t idle_sec = (*last_activity_us - s_unconnected_since_us) / 1000000LL;
    if (idle_sec >= UNCONNECTED_TIMEOUT_SEC)
    {
        ESP_LOGI(TAG, "unconnected for %llds, exiting to menu", idle_sec);
        reader_view_t next = views_navigate(VIEW_BLE_SYNC, VIEW_MENU); /* runs leave() */
        *last_activity_us = esp_timer_get_time();
        return next;
    }
    if (!s_headsup_shown && idle_sec >= HEADSUP_AT_SEC)
    {
        ESP_LOGI(TAG, "unconnected for %llds, showing heads-up", idle_sec);
        s_headsup_shown = true;
        draw_current(false);
    }
    return VIEW_BLE_SYNC;
}