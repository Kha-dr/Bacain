#ifndef SDCARD_H
#define SDCARD_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDCARD_MOUNT_POINT "/sdcard"
#define FLASH_MOUNT_POINT  "/spiflash"

esp_err_t sdcard_init(void);
void      sdcard_deinit(void);
bool      sdcard_is_mounted(void);
uint64_t  sdcard_get_total_bytes(void);
uint64_t  sdcard_get_free_bytes(void);

esp_err_t flash_storage_init(void);
void      flash_storage_deinit(void);
bool      flash_storage_is_mounted(void);
uint64_t  flash_storage_get_total_bytes(void);
uint64_t  flash_storage_get_free_bytes(void);

#ifdef __cplusplus
}
#endif

#endif // SDCARD_H
