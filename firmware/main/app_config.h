#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "sdkconfig.h"
#include "board_config.h"

#ifdef CONFIG_APP_SYNC_URL
#define APP_SYNC_URL CONFIG_APP_SYNC_URL
#else
#define APP_SYNC_URL "https://bacain.khadr.dev"
#endif

#define DISPLAY_WIDTH BOARD_DISPLAY_WIDTH
#define DISPLAY_HEIGHT BOARD_DISPLAY_HEIGHT
#define DISPLAY_BUFFER_SIZE BOARD_DISPLAY_BUFFER_SIZE

#endif // APP_CONFIG_H