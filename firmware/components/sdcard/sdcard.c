#include "sdcard.h"
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "esp_partition.h"
#include "wear_levelling.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "board_config.h"

#define TAG "SDCARD"

#define SDMMC_D0_PIN    BOARD_SDMMC_D0_PIN
#define SDMMC_CLK_PIN   BOARD_SDMMC_CLK_PIN
#define SDMMC_CMD_PIN   BOARD_SDMMC_CMD_PIN

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

#define BMIG_MAGIC        0x47494D42U /* "BMIG" */
#define BMIG_PART_OFFSET  0x20000U    /* 128 KB into storage partition */

static void unpack_flasher_migration_bundle(void) {
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "storage");
    if (!part || part->size <= BMIG_PART_OFFSET + 8) {
        return;
    }

    uint32_t header[2] = {0, 0};
    if (esp_partition_read(part, BMIG_PART_OFFSET, header, sizeof(header)) != ESP_OK) {
        return;
    }
    if (header[0] != BMIG_MAGIC || header[1] == 0 || header[1] > 256) {
        return;
    }

    uint32_t file_count = header[1];
    ESP_LOGI(TAG, "Found flasher content migration bundle (%u files) at +0x%x",
             (unsigned)file_count, (unsigned)BMIG_PART_OFFSET);

    size_t cursor = BMIG_PART_OFFSET + sizeof(header);
    uint8_t chunk[512];

    for (uint32_t i = 0; i < file_count; i++) {
        uint16_t path_len = 0;
        if (cursor + 2 > part->size ||
            esp_partition_read(part, cursor, &path_len, 2) != ESP_OK ||
            path_len == 0 || path_len >= 192) {
            break;
        }
        cursor += 2;

        char rel_path[192];
        if (cursor + path_len + 4 > part->size ||
            esp_partition_read(part, cursor, rel_path, path_len) != ESP_OK) {
            break;
        }
        rel_path[path_len] = '\0';
        cursor += path_len;

        uint32_t data_len = 0;
        if (esp_partition_read(part, cursor, &data_len, 4) != ESP_OK ||
            cursor + 4 + data_len > part->size) {
            break;
        }
        cursor += 4;

        char full_path[256];
        if (rel_path[0] == '/') {
            snprintf(full_path, sizeof(full_path), "%s%s", FLASH_MOUNT_POINT, rel_path);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", FLASH_MOUNT_POINT, rel_path);
        }

        /* Ensure parent subdirectory exists (e.g. /spiflash/books/Subdir/file.txt) */
        for (char *p = full_path + strlen(FLASH_MOUNT_POINT) + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                mkdir(full_path, 0777);
                *p = '/';
            }
        }

        FILE *fp = fopen(full_path, "wb");
        uint32_t remaining = data_len;
        while (remaining > 0) {
            uint32_t to_read = remaining > sizeof(chunk) ? sizeof(chunk) : remaining;
            if (esp_partition_read(part, cursor, chunk, to_read) != ESP_OK) {
                break;
            }
            if (fp) {
                fwrite(chunk, 1, to_read, fp);
            }
            cursor += to_read;
            remaining -= to_read;
        }
        if (fp) {
            fclose(fp);
            ESP_LOGI(TAG, "Restored migrated file: %s (%u bytes)", full_path, (unsigned)data_len);
        }
    }

    /* Erase the BMIG header sector so migration runs only once */
    esp_partition_erase_range(part, BMIG_PART_OFFSET, 4096);
}

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

    unpack_flasher_migration_bundle();

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
