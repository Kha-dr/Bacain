#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BTN_ID_UP   = 0,    // Navigation Up (BOARD_BTN_UP_PIN)
    BTN_ID_DOWN = 1,    // Navigation Down (BOARD_BTN_DOWN_PIN)
    // Aliases backward-compatible
    BTN_ID_BOOT = BTN_ID_UP,
    BTN_ID_PWR  = BTN_ID_DOWN,
} btn_id_t;

typedef btn_id_t button_id_t;

typedef enum {
    BTN_CLICK = 0,
    BTN_DOUBLE_CLICK,
    BTN_LONG_PRESS,
    BTN_EVENT_CUSTOM_REFRESH = 99,
} btn_press_type_t;

typedef struct {
    btn_id_t         button;
    btn_press_type_t type;
} button_event_t;

// Initializes buttons with polling timer & event queue
esp_err_t button_init(QueueHandle_t *out_queue);

// Deinitializes timer and button resources
void button_deinit(void);

void button_set_inverted(bool inverted);
bool button_get_inverted(void);
void button_seed_wakeup(btn_id_t wake_btn);

#ifdef __cplusplus
}
#endif

#endif // BUTTON_H
