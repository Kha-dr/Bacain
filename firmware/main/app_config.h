#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "board_config.h"

#define DISPLAY_WIDTH          BOARD_DISPLAY_WIDTH
#define DISPLAY_HEIGHT         BOARD_DISPLAY_HEIGHT
#define DISPLAY_BUFFER_SIZE    BOARD_DISPLAY_BUFFER_SIZE

// UI Layout Coordinates
#define UI_HEADER_HEIGHT       19
#define UI_FOOTER_Y            184
#define UI_FOOTER_HEIGHT       16
#define UI_BODY_Y_START        22
#define UI_BODY_MAX_HEIGHT     160

// Timeouts
#define UI_INACTIVITY_SLEEP_SEC 60

#endif // APP_CONFIG_H
