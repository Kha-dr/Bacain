#include "button.h"
#include "multi_button.h"
#include "board_config.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"

#define TAG "BUTTON"

#define BUTTON_SAMPLE_INTERVAL_MS 10
#define BUTTON_SAMPLE_INTERVAL_US (BUTTON_SAMPLE_INTERVAL_MS * 1000)
#define CHORD_REBOOT_TICKS        (2000 / BUTTON_SAMPLE_INTERVAL_MS)
#define WAKEUP_BOOT_CREDIT_MS     120
#define WAKEUP_BOOT_CREDIT_TICKS  (WAKEUP_BOOT_CREDIT_MS / BUTTON_SAMPLE_INTERVAL_MS)
#define WAKEUP_MBTN_CREDIT_TICKS  (WAKEUP_BOOT_CREDIT_MS / TICKS_INTERVAL)

static Button s_btn_boot;
static Button s_btn_pwr;
static esp_timer_handle_t s_timer = NULL;
static QueueHandle_t s_event_queue = NULL;
static bool s_btn_inverted = false;
static bool s_has_wake_seed = false;
static bool s_wake_fast_dispatch = false;
static btn_id_t s_wake_seed_btn = BTN_ID_BOOT;
static uint16_t s_chord_ticks = 0;

static uint8_t read_button_gpio(uint8_t button_id) {
    if (button_id == BTN_ID_UP || button_id == BTN_ID_BOOT) {
        return gpio_get_level(BOARD_BTN_UP_PIN);
    } else if (button_id == BTN_ID_DOWN || button_id == BTN_ID_PWR) {
        return gpio_get_level(BOARD_BTN_DOWN_PIN);
    }
    return 1;
}

static void send_event(btn_id_t btn, btn_press_type_t type) {
    if (!s_event_queue) return;
    btn_id_t effective_btn = btn;
    if (s_btn_inverted) {
        if (btn == BTN_ID_UP) effective_btn = BTN_ID_DOWN;
        else if (btn == BTN_ID_DOWN) effective_btn = BTN_ID_UP;
    }
    button_event_t evt = { .button = effective_btn, .type = type };
    xQueueSend(s_event_queue, &evt, 0);
}

// BOOT callbacks
static void on_boot_click(Button *b) {
    s_wake_fast_dispatch = false;
    send_event(BTN_ID_BOOT, BTN_CLICK);
}
static void on_boot_double_click(Button *b) {
    s_wake_fast_dispatch = false;
    send_event(BTN_ID_BOOT, BTN_DOUBLE_CLICK);
}
static void on_boot_long_press(Button *b) {
    s_wake_fast_dispatch = false;
    send_event(BTN_ID_BOOT, BTN_LONG_PRESS);
}

// PWR callbacks
static void on_pwr_click(Button *b) {
    s_wake_fast_dispatch = false;
    send_event(BTN_ID_PWR, BTN_CLICK);
}
static void on_pwr_double_click(Button *b) {
    s_wake_fast_dispatch = false;
    send_event(BTN_ID_PWR, BTN_DOUBLE_CLICK);
}
static void on_pwr_long_press(Button *b) {
    s_wake_fast_dispatch = false;
    send_event(BTN_ID_PWR, BTN_LONG_PRESS);
}

static void apply_wake_seed(void) {
    if (!s_has_wake_seed) return;
    s_has_wake_seed = false;

    Button *target_btn = (s_wake_seed_btn == BTN_ID_BOOT || s_wake_seed_btn == BTN_ID_UP) ? &s_btn_boot : &s_btn_pwr;
    int target_gpio = (s_wake_seed_btn == BTN_ID_BOOT || s_wake_seed_btn == BTN_ID_UP) ? BOARD_BTN_UP_PIN : BOARD_BTN_DOWN_PIN;
    Button *other_btn = (s_wake_seed_btn == BTN_ID_BOOT || s_wake_seed_btn == BTN_ID_UP) ? &s_btn_pwr : &s_btn_boot;
    int other_gpio = (s_wake_seed_btn == BTN_ID_BOOT || s_wake_seed_btn == BTN_ID_UP) ? BOARD_BTN_DOWN_PIN : BOARD_BTN_UP_PIN;

    int64_t boot_ms = esp_timer_get_time() / 1000;
    if (boot_ms < WAKEUP_BOOT_CREDIT_MS) {
        boot_ms = WAKEUP_BOOT_CREDIT_MS;
    }
    uint16_t elapsed_ticks = (uint16_t)(boot_ms / TICKS_INTERVAL);
    if (elapsed_ticks >= LONG_TICKS) {
        elapsed_ticks = LONG_TICKS - 1;
    }

    if (gpio_get_level(target_gpio) == 0) {
        target_btn->button_level = 0;
        target_btn->debounce_cnt = 0;
        target_btn->ticks = elapsed_ticks;
        target_btn->repeat = 1;
        target_btn->event = (uint8_t)MBTN_PRESS_DOWN;
        target_btn->state = BTN_STATE_PRESS;

        if (gpio_get_level(other_gpio) == 0) {
            s_wake_fast_dispatch = false;
            other_btn->button_level = 0;
            other_btn->debounce_cnt = 0;
            other_btn->ticks = elapsed_ticks;
            other_btn->repeat = 1;
            other_btn->event = (uint8_t)MBTN_PRESS_DOWN;
            other_btn->state = BTN_STATE_PRESS;
            uint16_t chord_elapsed = (uint16_t)(boot_ms / BUTTON_SAMPLE_INTERVAL_MS);
            if (chord_elapsed < WAKEUP_BOOT_CREDIT_TICKS) {
                chord_elapsed = WAKEUP_BOOT_CREDIT_TICKS;
            }
            if (chord_elapsed < CHORD_REBOOT_TICKS) {
                s_chord_ticks = chord_elapsed;
            }
        }
    } else {
        target_btn->button_level = 1;
        target_btn->debounce_cnt = 0;
        target_btn->ticks = 0;
        target_btn->repeat = 1;
        target_btn->event = (uint8_t)MBTN_PRESS_UP;
        target_btn->state = BTN_STATE_RELEASE;
        if (s_wake_fast_dispatch && s_event_queue != NULL) {
            send_event(s_wake_seed_btn, BTN_CLICK);
            button_reset(target_btn);
            s_wake_fast_dispatch = false;
        }
    }
}

void button_seed_wakeup(btn_id_t wake_btn) {
    s_has_wake_seed = true;
    s_wake_seed_btn = wake_btn;
    s_wake_fast_dispatch = true;
    if (s_timer != NULL) {
        apply_wake_seed();
    }
}

static void button_timer_cb(void *arg) {
    if (gpio_get_level(BOARD_BTN_UP_PIN) == 0 && gpio_get_level(BOARD_BTN_DOWN_PIN) == 0) {
        s_chord_ticks++;
        s_wake_fast_dispatch = false;
        // Reset both button state machines so neither fires single clicks or long press
        button_reset(&s_btn_boot);
        button_reset(&s_btn_pwr);
        if (s_chord_ticks >= CHORD_REBOOT_TICKS) {
            esp_restart();
        }
    } else {
        s_chord_ticks = 0;
    }

#if (BUTTON_SAMPLE_INTERVAL_MS > TICKS_INTERVAL)
    // Scale tick accumulation to match TICKS_INTERVAL timing specifications
    if (s_btn_boot.state > BTN_STATE_IDLE) {
        s_btn_boot.ticks += (BUTTON_SAMPLE_INTERVAL_MS / TICKS_INTERVAL - 1);
    }
    if (s_btn_pwr.state > BTN_STATE_IDLE) {
        s_btn_pwr.ticks += (BUTTON_SAMPLE_INTERVAL_MS / TICKS_INTERVAL - 1);
    }
#endif

    button_ticks();

    if (s_wake_fast_dispatch) {
        Button *wake_btn = (s_wake_seed_btn == BTN_ID_BOOT || s_wake_seed_btn == BTN_ID_UP) ? &s_btn_boot : &s_btn_pwr;
        if (wake_btn->state == BTN_STATE_RELEASE) {
            send_event(s_wake_seed_btn, BTN_CLICK);
            button_reset(wake_btn);
            s_wake_fast_dispatch = false;
        } else if (wake_btn->state == BTN_STATE_LONG_HOLD || wake_btn->state == BTN_STATE_REPEAT) {
            s_wake_fast_dispatch = false;
        }
    }
}

esp_err_t button_init(QueueHandle_t *out_queue) {
    s_event_queue = xQueueCreate(16, sizeof(button_event_t));
    if (!s_event_queue) return ESP_ERR_NO_MEM;
    if (out_queue) *out_queue = s_event_queue;

    gpio_config_t conf = {
        .pin_bit_mask = (1ULL << BOARD_BTN_UP_PIN) | (1ULL << BOARD_BTN_DOWN_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&conf));

    button_init_struct(&s_btn_boot, read_button_gpio, 0, BTN_ID_BOOT);
    button_attach(&s_btn_boot, MBTN_SINGLE_CLICK, on_boot_click);
    button_attach(&s_btn_boot, MBTN_DOUBLE_CLICK, on_boot_double_click);
    button_attach(&s_btn_boot, MBTN_LONG_PRESS_START, on_boot_long_press);
    button_start(&s_btn_boot);

    button_init_struct(&s_btn_pwr, read_button_gpio, 0, BTN_ID_PWR);
    button_attach(&s_btn_pwr, MBTN_SINGLE_CLICK, on_pwr_click);
    button_attach(&s_btn_pwr, MBTN_DOUBLE_CLICK, on_pwr_double_click);
    button_attach(&s_btn_pwr, MBTN_LONG_PRESS_START, on_pwr_long_press);
    button_start(&s_btn_pwr);

    s_chord_ticks = 0;
    apply_wake_seed();

    esp_timer_create_args_t timer_args = {
        .callback = button_timer_cb,
        .name = "btn_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_timer, BUTTON_SAMPLE_INTERVAL_US));

    ESP_LOGI(TAG, "Buttons initialized (UP: GPIO %d, DOWN: GPIO %d)", BOARD_BTN_UP_PIN, BOARD_BTN_DOWN_PIN);
    return ESP_OK;
}

void button_deinit(void) {
    if (s_timer) {
        esp_timer_stop(s_timer);
        esp_timer_delete(s_timer);
        s_timer = NULL;
    }
    button_stop(&s_btn_boot);
    button_stop(&s_btn_pwr);
    if (s_event_queue) {
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
    }
    s_has_wake_seed = false;
    s_wake_fast_dispatch = false;
}

void button_set_inverted(bool inverted) {
    s_btn_inverted = inverted;
}

bool button_get_inverted(void) {
    return s_btn_inverted;
}
