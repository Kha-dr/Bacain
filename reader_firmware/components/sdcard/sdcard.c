#include "sdcard.h"
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"

#define TAG "SDCARD"

#define SDMMC_D0_PIN    GPIO_NUM_40
#define SDMMC_CLK_PIN   GPIO_NUM_39
#define SDMMC_CMD_PIN   GPIO_NUM_41

static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;

static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
static bool s_flash_mounted = false;

esp_err_t sdcard_init(void) {
    if (s_mounted) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing SD card (1-line SDMMC)...");

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = SDMMC_CLK_PIN;
    slot_config.cmd = SDMMC_CMD_PIN;
    slot_config.d0  = SDMMC_D0_PIN;

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(SDCARD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card VFS (err 0x%x)", ret);
        s_mounted = false;
        s_card = NULL;
        return ret;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "SD card mounted successfully. Size: %llu MB",
             ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size / (1024 * 1024));

    // Ensure standard directories exist
    mkdir(SDCARD_MOUNT_POINT "/books", 0777);
    mkdir(SDCARD_MOUNT_POINT "/.reader", 0777);

    return ESP_OK;
}

void sdcard_deinit(void) {
    if (!s_mounted) return;
    esp_vfs_fat_sdcard_unmount(SDCARD_MOUNT_POINT, s_card);
    s_mounted = false;
    s_card = NULL;
    ESP_LOGI(TAG, "SD card unmounted.");
}

bool sdcard_is_mounted(void) {
    return s_mounted;
}

uint64_t sdcard_get_total_bytes(void) {
    if (!s_mounted || !s_card) return 0;
    FATFS *fs;
    DWORD fre_clust;
    if (f_getfree("0:", &fre_clust, &fs) != FR_OK) {
        return ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size;
    }
    return ((uint64_t)(fs->n_fatent - 2)) * fs->csize * 512;
}

uint64_t sdcard_get_free_bytes(void) {
    if (!s_mounted || !s_card) return 0;
    FATFS *fs;
    DWORD fre_clust;
    if (f_getfree("0:", &fre_clust, &fs) != FR_OK) return 0;
    return ((uint64_t)fre_clust) * fs->csize * 512;
}

/* ========================================================================= */
/* SPI Flash FAT Storage Implementation                                      */
/* ========================================================================= */

esp_err_t flash_storage_init(void) {
    if (s_flash_mounted) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing SPI flash FAT storage...");

    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 8,
        .allocation_unit_size = 4096
    };

    esp_err_t ret = esp_vfs_fat_spiflash_mount_rw_wl(FLASH_MOUNT_POINT, "storage", &mount_config, &s_wl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPI flash FAT storage (err 0x%x)", ret);
        s_flash_mounted = false;
        s_wl_handle = WL_INVALID_HANDLE;
        return ret;
    }

    s_flash_mounted = true;
    ESP_LOGI(TAG, "SPI flash FAT storage mounted successfully at %s", FLASH_MOUNT_POINT);

    // Ensure standard directories exist
    mkdir(FLASH_MOUNT_POINT "/books", 0777);
    mkdir(FLASH_MOUNT_POINT "/.reader", 0777);

    return ESP_OK;
}

void flash_storage_deinit(void) {
    if (!s_flash_mounted) {
        return;
    }

    esp_err_t ret = esp_vfs_fat_spiflash_unmount_rw_wl(FLASH_MOUNT_POINT, s_wl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unmount SPI flash FAT storage (err 0x%x)", ret);
    }
    s_flash_mounted = false;
    s_wl_handle = WL_INVALID_HANDLE;
    ESP_LOGI(TAG, "SPI flash FAT storage unmounted.");
}

bool flash_storage_is_mounted(void) {
    return s_flash_mounted;
}

uint64_t flash_storage_get_total_bytes(void) {
    if (!s_flash_mounted) {
        return 0;
    }
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    esp_err_t ret = esp_vfs_fat_info(FLASH_MOUNT_POINT, &total_bytes, &free_bytes);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get flash storage info (0x%x)", ret);
        return 0;
    }
    return total_bytes;
}

uint64_t flash_storage_get_free_bytes(void) {
    if (!s_flash_mounted) {
        return 0;
    }
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    esp_err_t ret = esp_vfs_fat_info(FLASH_MOUNT_POINT, &total_bytes, &free_bytes);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get flash storage info (0x%x)", ret);
        return 0;
    }
    return free_bytes;
}
