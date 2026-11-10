#include "button.h"
#include "multi_button.h"
#include "board_config.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "sdkconfig.h"
#include "esp_sleep.h"
#include "esp_attr.h"
#include "esp_rom_sys.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"
#include "soc/timer_group_reg.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "button wake stub register map is ESP32-S3 only"
#endif

#define WAKE_GESTURE_NONE 0
#define WAKE_GESTURE_CLICK 1
#define WAKE_GESTURE_DOUBLE 2
#define WAKE_GESTURE_HOLD 3

#define WDT_WRITE_KEY 0x50D83AA1

#ifndef RTC_CNTL_WDT_FEED
#define RTC_CNTL_WDT_FEED (1UL << 31)
#endif

static RTC_DATA_ATTR uint8_t s_rtc_wake_gesture;
static RTC_DATA_ATTR uint8_t s_rtc_wake_gpio;

#define WAKE_FEED_WDT() do { \
    REG_WRITE(RTC_CNTL_WDTWPROTECT_REG, WDT_WRITE_KEY); \
    REG_WRITE(RTC_CNTL_WDTFEED_REG, RTC_CNTL_WDT_FEED); \
    REG_WRITE(RTC_CNTL_WDTWPROTECT_REG, 0); \
    REG_WRITE(TIMG_WDTWPROTECT_REG(0), WDT_WRITE_KEY); \
    REG_WRITE(TIMG_WDTFEED_REG(0), 1); \
    REG_WRITE(TIMG_WDTWPROTECT_REG(0), 0); \
} while (0)

void RTC_IRAM_ATTR esp_wake_deep_sleep(void) {
    esp_default_wake_deep_sleep();
    s_rtc_wake_gesture = WAKE_GESTURE_NONE;

    uint32_t status = REG_GET_FIELD(RTC_CNTL_EXT_WAKEUP1_STATUS_REG, RTC_CNTL_EXT_WAKEUP1_STATUS);
    uint8_t gpio;
    uint8_t other_gpio;

    if (status & (1UL << BOARD_BTN_UP_PIN)) {
        gpio = BOARD_BTN_UP_PIN;
        other_gpio = BOARD_BTN_DOWN_PIN;
    } else if (status & (1UL << BOARD_BTN_DOWN_PIN)) {
        gpio = BOARD_BTN_DOWN_PIN;
        other_gpio = BOARD_BTN_UP_PIN;
    } else {
        return;
    }

    if (((REG_READ(RTC_GPIO_IN_REG) >> (RTC_GPIO_IN_NEXT_S + other_gpio)) & 1U) == 0) {
        return;
    }

    uint8_t gesture = WAKE_GESTURE_NONE;
    uint8_t presses = 1;
    bool pressed = true;
    uint8_t current_level = 0;
    uint8_t debounce_cnt = 0;
    uint16_t press_ms = 0;
    uint16_t gap_ms = 0;
    uint16_t total_ms = 0;

    while (total_ms < 1500) {
        esp_rom_delay_us(10000);
        total_ms += 10;
        WAKE_FEED_WDT();

        uint8_t raw = (REG_READ(RTC_GPIO_IN_REG) >> (RTC_GPIO_IN_NEXT_S + gpio)) & 1U;
        bool level_changed = false;
        if (raw != current_level) {
            if (++debounce_cnt >= 3) {
                current_level = raw;
                debounce_cnt = 0;
                level_changed = true;
            }
        } else {
            debounce_cnt = 0;
        }

        if (pressed) {
            if (level_changed && current_level == 1) {
                pressed = false;
                gap_ms = 0;
                if (presses == 2) {
                    gesture = WAKE_GESTURE_DOUBLE;
                    break;
                }
            } else {
                press_ms += 10;
                if (presses == 1) {
                    if (press_ms >= 600) {
                        gesture = WAKE_GESTURE_HOLD;
                        break;
                    }
                } else if (presses == 2) {
                    if (press_ms >= 600) {
                        gesture = WAKE_GESTURE_DOUBLE;
                        break;
                    }
                }
            }
        } else {
            if (level_changed && current_level == 0) {
                pressed = true;
                presses = 2;
                press_ms = 0;
            } else {
                gap_ms += 10;
                if (gap_ms >= 350) {
                    if (presses == 1) {
                        gesture = WAKE_GESTURE_CLICK;
                    }
                    break;
                }
            }
        }
    }

    if (total_ms >= 1500) {
        gesture = WAKE_GESTURE_NONE;
    }

    s_rtc_wake_gpio = gpio;
    s_rtc_wake_gesture = gesture;
}

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
static btn_id_t s_wake_seed_btn = BTN_ID_BOOT;
static uint16_t s_chord_ticks = 0;
static TaskHandle_t s_btn_task = NULL;
static bool s_timer_running = false;
static portMUX_TYPE s_btn_lock = portMUX_INITIALIZER_UNLOCKED;

static void button_task(void *arg) {
    (void)arg;
    uint32_t notify_val = 0;
    while (1) {
        if (xTaskNotifyWait(0, 0xFFFFFFFFUL, &notify_val, portMAX_DELAY) == pdTRUE) {
            bool restart = false;
            portENTER_CRITICAL(&s_btn_lock);
            if (s_timer != NULL && !s_timer_running) {
                s_timer_running = true;
                restart = true;
            }
            portEXIT_CRITICAL(&s_btn_lock);
            if (restart) {
                esp_timer_start_periodic(s_timer, BUTTON_SAMPLE_INTERVAL_US);
            }
        }
    }
}

static void IRAM_ATTR button_gpio_isr(void *arg) {
    (void)arg;
    gpio_intr_disable((gpio_num_t)BOARD_BTN_UP_PIN);
    gpio_intr_disable((gpio_num_t)BOARD_BTN_DOWN_PIN);
    BaseType_t high_task_wakeup = pdFALSE;
    if (s_btn_task != NULL) {
        xTaskNotifyFromISR(s_btn_task, 1, eSetValueWithOverwrite, &high_task_wakeup);
        portYIELD_FROM_ISR(high_task_wakeup);
    }
}

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
    send_event(BTN_ID_BOOT, BTN_CLICK);
}
static void on_boot_double_click(Button *b) {
    send_event(BTN_ID_BOOT, BTN_DOUBLE_CLICK);
}
static void on_boot_long_press(Button *b) {
    send_event(BTN_ID_BOOT, BTN_LONG_PRESS);
}

// PWR callbacks
static void on_pwr_click(Button *b) {
    send_event(BTN_ID_PWR, BTN_CLICK);
}
static void on_pwr_double_click(Button *b) {
    send_event(BTN_ID_PWR, BTN_DOUBLE_CLICK);
}
static void on_pwr_long_press(Button *b) {
    send_event(BTN_ID_PWR, BTN_LONG_PRESS);
}

static void apply_wake_seed(void) {
    if (!s_has_wake_seed) return;
    s_has_wake_seed = false;

    if (s_rtc_wake_gesture != WAKE_GESTURE_NONE) {
        uint8_t gesture = s_rtc_wake_gesture;
        s_rtc_wake_gesture = WAKE_GESTURE_NONE;

        btn_id_t btn = (s_rtc_wake_gpio == BOARD_BTN_UP_PIN) ? BTN_ID_BOOT : BTN_ID_PWR;
        Button *target = (s_rtc_wake_gpio == BOARD_BTN_UP_PIN) ? &s_btn_boot : &s_btn_pwr;

        if (gesture == WAKE_GESTURE_CLICK) {
            send_event(btn, BTN_CLICK);
            button_reset(target);
        } else if (gesture == WAKE_GESTURE_DOUBLE) {
            send_event(btn, BTN_DOUBLE_CLICK);
            button_reset(target);
        } else if (gesture == WAKE_GESTURE_HOLD) {
            send_event(btn, BTN_LONG_PRESS);
            target->button_level = 0;
            target->debounce_cnt = 0;
            target->state = BTN_STATE_LONG_HOLD;
        }
        return;
    }

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
    }
}

void button_seed_wakeup(btn_id_t wake_btn) {
    s_has_wake_seed = true;
    s_wake_seed_btn = wake_btn;
    if (s_timer != NULL) {
        apply_wake_seed();
        bool restart = false;
        portENTER_CRITICAL(&s_btn_lock);
        if (!s_timer_running) {
            s_timer_running = true;
            restart = true;
        }
        portEXIT_CRITICAL(&s_btn_lock);
        if (restart) {
            esp_timer_start_periodic(s_timer, BUTTON_SAMPLE_INTERVAL_US);
        }
    }
}

static void button_timer_cb(void *arg) {
    if (gpio_get_level(BOARD_BTN_UP_PIN) == 0 && gpio_get_level(BOARD_BTN_DOWN_PIN) == 0) {
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

    if (s_btn_boot.state == BTN_STATE_IDLE &&
        s_btn_pwr.state == BTN_STATE_IDLE &&
        s_chord_ticks == 0 &&
        !s_has_wake_seed &&
        gpio_get_level((gpio_num_t)BOARD_BTN_UP_PIN) != 0 &&
        gpio_get_level((gpio_num_t)BOARD_BTN_DOWN_PIN) != 0) {
        // Race-free order:
        // 1) enable both pin interrupts
        gpio_intr_enable((gpio_num_t)BOARD_BTN_UP_PIN);
        gpio_intr_enable((gpio_num_t)BOARD_BTN_DOWN_PIN);

        // 2) esp_timer_stop()
        esp_timer_stop(s_timer);
        portENTER_CRITICAL(&s_btn_lock);
        s_timer_running = false;
        portEXIT_CRITICAL(&s_btn_lock);

        // 3) re-read both pins; if either is pressed, disable the interrupts
        //    and restart the timer immediately.
        if (gpio_get_level((gpio_num_t)BOARD_BTN_UP_PIN) == 0 ||
            gpio_get_level((gpio_num_t)BOARD_BTN_DOWN_PIN) == 0) {
            gpio_intr_disable((gpio_num_t)BOARD_BTN_UP_PIN);
            gpio_intr_disable((gpio_num_t)BOARD_BTN_DOWN_PIN);
            bool restart = false;
            portENTER_CRITICAL(&s_btn_lock);
            if (!s_timer_running) {
                s_timer_running = true;
                restart = true;
            }
            portEXIT_CRITICAL(&s_btn_lock);
            if (restart) {
                esp_timer_start_periodic(s_timer, BUTTON_SAMPLE_INTERVAL_US);
            }
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

    ESP_ERROR_CHECK(gpio_wakeup_enable((gpio_num_t)BOARD_BTN_UP_PIN, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(gpio_wakeup_enable((gpio_num_t)BOARD_BTN_DOWN_PIN, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());

    BaseType_t task_ret = xTaskCreate(button_task, "btn_task", 2048, NULL, 10, &s_btn_task);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button task");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t isr_err = gpio_install_isr_service(0);
    if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install gpio isr service (0x%x)", isr_err);
        return isr_err;
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add((gpio_num_t)BOARD_BTN_UP_PIN, button_gpio_isr, NULL));
    ESP_ERROR_CHECK(gpio_isr_handler_add((gpio_num_t)BOARD_BTN_DOWN_PIN, button_gpio_isr, NULL));

    gpio_intr_disable((gpio_num_t)BOARD_BTN_UP_PIN);
    gpio_intr_disable((gpio_num_t)BOARD_BTN_DOWN_PIN);

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
    s_timer_running = true;
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_timer, BUTTON_SAMPLE_INTERVAL_US));

    ESP_LOGI(TAG, "Buttons initialized (UP: GPIO %d, DOWN: GPIO %d)", BOARD_BTN_UP_PIN, BOARD_BTN_DOWN_PIN);
    return ESP_OK;
}

void button_deinit(void) {
    if (s_timer) {
        if (s_timer_running) {
            esp_timer_stop(s_timer);
        }
        esp_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_timer_running = false;
    if (s_btn_task) {
        vTaskDelete(s_btn_task);
        s_btn_task = NULL;
    }
    gpio_intr_disable((gpio_num_t)BOARD_BTN_UP_PIN);
    gpio_intr_disable((gpio_num_t)BOARD_BTN_DOWN_PIN);
    gpio_isr_handler_remove((gpio_num_t)BOARD_BTN_UP_PIN);
    gpio_isr_handler_remove((gpio_num_t)BOARD_BTN_DOWN_PIN);
    gpio_wakeup_disable((gpio_num_t)BOARD_BTN_UP_PIN);
    gpio_wakeup_disable((gpio_num_t)BOARD_BTN_DOWN_PIN);
    button_stop(&s_btn_boot);
    button_stop(&s_btn_pwr);
    if (s_event_queue) {
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
    }
    s_has_wake_seed = false;
}

void button_set_inverted(bool inverted) {
    s_btn_inverted = inverted;
}

bool button_get_inverted(void) {
    return s_btn_inverted;
}
