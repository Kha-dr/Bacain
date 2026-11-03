#include "button.h"
#include "multi_button.h"
#include "board_config.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"

#define TAG "BUTTON"

#define UP_BUTTON_GPIO    BOARD_BTN_UP_PIN
#define DOWN_BUTTON_GPIO  BOARD_BTN_DOWN_PIN
#define BOOT_BUTTON_GPIO  UP_BUTTON_GPIO
#define PWR_BUTTON_GPIO   DOWN_BUTTON_GPIO
#define CHORD_REBOOT_TICKS (2000 / 5) // 2000ms / 5ms per tick = 400 ticks

static Button s_btn_boot;
static Button s_btn_pwr;
static esp_timer_handle_t s_timer = NULL;
static QueueHandle_t s_event_queue = NULL;
static bool s_btn_inverted = false;

static uint8_t read_button_gpio(uint8_t button_id) {
    if (button_id == BTN_ID_UP || button_id == BTN_ID_BOOT) {
        return gpio_get_level(BOOT_BUTTON_GPIO);
    } else if (button_id == BTN_ID_DOWN || button_id == BTN_ID_PWR) {
        return gpio_get_level(PWR_BUTTON_GPIO);
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
static void on_boot_click(Button *b) { send_event(BTN_ID_BOOT, BTN_CLICK); }
static void on_boot_double_click(Button *b) { send_event(BTN_ID_BOOT, BTN_DOUBLE_CLICK); }
static void on_boot_long_press(Button *b) { send_event(BTN_ID_BOOT, BTN_LONG_PRESS); }

// PWR callbacks
static void on_pwr_click(Button *b) { send_event(BTN_ID_PWR, BTN_CLICK); }
static void on_pwr_double_click(Button *b) { send_event(BTN_ID_PWR, BTN_DOUBLE_CLICK); }
static void on_pwr_long_press(Button *b) { send_event(BTN_ID_PWR, BTN_LONG_PRESS); }

static void button_timer_cb(void *arg) {
    static uint16_t s_chord_ticks = 0;

    if (gpio_get_level(BOOT_BUTTON_GPIO) == 0 && gpio_get_level(PWR_BUTTON_GPIO) == 0) {
        s_chord_ticks++;
        // Reset both button state machines so neither fires single clicks or long press
        button_reset(&s_btn_boot);
        button_reset(&s_btn_pwr);
        if (s_chord_ticks >= CHORD_REBOOT_TICKS) {
            esp_restart();
        }
    } else {
        s_chord_ticks = 0;
    }

    button_ticks();
}

esp_err_t button_init(QueueHandle_t *out_queue) {
    s_event_queue = xQueueCreate(16, sizeof(button_event_t));
    if (!s_event_queue) return ESP_ERR_NO_MEM;
    if (out_queue) *out_queue = s_event_queue;

    gpio_config_t conf = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO) | (1ULL << PWR_BUTTON_GPIO),
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

    esp_timer_create_args_t timer_args = {
        .callback = button_timer_cb,
        .name = "btn_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_timer, 5 * 1000)); // 5ms tick matches TICKS_INTERVAL

    ESP_LOGI(TAG, "Buttons initialized (UP/BOOT: GPIO %d, DOWN/PWR: GPIO %d)", UP_BUTTON_GPIO, DOWN_BUTTON_GPIO);
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
}

void button_set_inverted(bool inverted) {
    s_btn_inverted = inverted;
}

bool button_get_inverted(void) {
    return s_btn_inverted;
}
