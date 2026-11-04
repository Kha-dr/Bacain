#include "book_library.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

#include "esp_log.h"
#include "esp_err.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdcard.h"

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

static const char *TAG = "book_library";

static book_info_t       s_books[BOOK_MAX_COUNT];
static size_t            s_book_count = 0;
static reading_state_t   s_reading_state = {0};
static SemaphoreHandle_t s_mutex = NULL;
static bool              s_initialized = false;
static storage_target_t  s_storage_target = STORAGE_TARGET_FLASH;
static char              s_books_dir[64] = "/spiflash/books";
static char              s_state_dir[64] = "/spiflash/.reader";
static char              s_state_file[80] = "/spiflash/.reader/state.json";
static char              s_current_dir[BOOK_PATH_MAX_LEN] = "/spiflash/books";
static char              s_rel_path[BOOK_PATH_MAX_LEN] = "";
static bool              s_state_dirty = false;
static bool              s_scanned = false;
static bool              s_state_loaded = false;

static bool is_root_dir_locked(void) {
    return (strcmp(s_current_dir, s_books_dir) == 0);
}

static void update_current_rel_path(void) {
    size_t base_len = strlen(s_books_dir);
    if (strncmp(s_current_dir, s_books_dir, base_len) == 0) {
        const char *rel = s_current_dir + base_len;
        if (*rel == '/') {
            snprintf(s_rel_path, sizeof(s_rel_path), "%s", rel);
        } else if (*rel == '\0') {
            s_rel_path[0] = '\0';
        } else {
            snprintf(s_rel_path, sizeof(s_rel_path), "/%s", rel);
        }
    } else {
        s_rel_path[0] = '\0';
    }
}

static int compare_books(const void *a, const void *b) {
    const book_info_t *book_a = (const book_info_t *)a;
    const book_info_t *book_b = (const book_info_t *)b;

    if (book_a->type == LIBRARY_ENTRY_UP) {
        return (book_b->type == LIBRARY_ENTRY_UP) ? 0 : -1;
    }
    if (book_b->type == LIBRARY_ENTRY_UP) {
        return 1;
    }

    if (book_a->type != book_b->type) {
        // LIBRARY_ENTRY_DIR should come before LIBRARY_ENTRY_FILE
        return (book_a->type == LIBRARY_ENTRY_DIR) ? -1 : 1;
    }

    return strcasecmp(book_a->filename, book_b->filename);
}

static void ensure_dir_exists(const char *path) {
    if (!path) return;
    struct stat st;
    if (stat(path, &st) != 0) {
        mkdir(path, 0777);
    }
}

static void update_target_paths(storage_target_t target) {
    const char *mount_pt = (target == STORAGE_TARGET_FLASH) ? FLASH_MOUNT_POINT : SDCARD_MOUNT_POINT;
    snprintf(s_books_dir, sizeof(s_books_dir), "%.32s/books", mount_pt);
    snprintf(s_state_dir, sizeof(s_state_dir), "%.32s/.reader", mount_pt);
    snprintf(s_state_file, sizeof(s_state_file), "%.32s/.reader/state.json", mount_pt);
    strncpy(s_current_dir, s_books_dir, sizeof(s_current_dir) - 1);
    s_current_dir[sizeof(s_current_dir) - 1] = '\0';
    update_current_rel_path();
}

static bool check_storage_mounted(storage_target_t target) {
    if (target == STORAGE_TARGET_FLASH) {
        if (!flash_storage_is_mounted()) {
            esp_err_t ret = flash_storage_init();
            if (ret != ESP_OK || !flash_storage_is_mounted()) {
                ESP_LOGE(TAG, "SPI Flash storage is not mounted (err 0x%x)", ret);
                return false;
            }
        }
        return true;
    } else { // STORAGE_TARGET_SDCARD
        if (!sdcard_is_mounted()) {
            ESP_LOGE(TAG, "SD card is not mounted");
            return false;
        }
        return true;
    }
}

esp_err_t book_library_init(void) {
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    s_storage_target = STORAGE_TARGET_FLASH;
    update_target_paths(STORAGE_TARGET_FLASH);
    s_initialized = true;

    if (check_storage_mounted(s_storage_target)) {
        ensure_dir_exists("/spiflash/books");
        ensure_dir_exists("/spiflash/.reader");
        ensure_dir_exists(s_books_dir);
        ensure_dir_exists(s_state_dir);
        if (!s_scanned || s_book_count == 0) {
            book_library_scan();
        }
        if (!s_state_loaded) {
            book_library_load_state(&s_reading_state);
        }
    } else {
        ESP_LOGW(TAG, "Initial storage target (%s) not mounted at init",
                 s_storage_target == STORAGE_TARGET_FLASH ? "FLASH" : "SDCARD");
    }

    ESP_LOGI(TAG, "Book library initialized with %u books", (unsigned int)s_book_count);
    return ESP_OK;
}

void book_library_deinit(void) {
    if (s_mutex) {
        book_library_flush_state();

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_book_count = 0;
        s_scanned = false;
        s_state_loaded = false;
        memset(&s_reading_state, 0, sizeof(s_reading_state));
        s_state_dirty = false;
        s_initialized = false;
        s_storage_target = STORAGE_TARGET_FLASH;
        update_target_paths(STORAGE_TARGET_FLASH);
        xSemaphoreGive(s_mutex);

        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
}

esp_err_t book_library_set_storage_target(storage_target_t target) {
    if (target != STORAGE_TARGET_SDCARD && target != STORAGE_TARGET_FLASH) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!check_storage_mounted(target)) {
        ESP_LOGE(TAG, "Cannot set storage target: media %s not mounted",
                 target == STORAGE_TARGET_FLASH ? "FLASH" : "SDCARD");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_storage_target == target && s_scanned && s_book_count > 0) {
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    s_storage_target = target;
    update_target_paths(target);
    s_scanned = false;
    s_state_loaded = false;

    ensure_dir_exists(s_books_dir);
    ensure_dir_exists(s_state_dir);

    xSemaphoreGive(s_mutex);

    // Rescan library and load state for the newly selected storage
    book_library_scan();
    book_library_load_state(&s_reading_state);

    ESP_LOGI(TAG, "Book library storage target set to %s (books_dir=%s)",
             target == STORAGE_TARGET_FLASH ? "FLASH" : "SDCARD", s_books_dir);
    return ESP_OK;
}

storage_target_t book_library_get_storage_target(void) {
    storage_target_t target = STORAGE_TARGET_FLASH;
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        target = s_storage_target;
        xSemaphoreGive(s_mutex);
    } else {
        target = s_storage_target;
    }
    return target;
}

esp_err_t book_library_scan(void) {
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_scanned && s_book_count > 0) {
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    if (!check_storage_mounted(s_storage_target)) {
        ESP_LOGE(TAG, "Storage target %s not mounted, cannot scan books",
                 s_storage_target == STORAGE_TARGET_FLASH ? "FLASH" : "SDCARD");
        s_book_count = 0;
        s_scanned = false;
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    ensure_dir_exists(s_books_dir);
    ensure_dir_exists(s_current_dir);

    s_book_count = 0;

    // Jika bukan root books dir, tambahkan entri pertama ".." untuk kembali ke folder induk
    if (!is_root_dir_locked()) {
        char parent_path[BOOK_PATH_MAX_LEN];
        strncpy(parent_path, s_current_dir, sizeof(parent_path) - 1);
        parent_path[sizeof(parent_path) - 1] = '\0';

        char *last_slash = strrchr(parent_path, '/');
        if (last_slash && last_slash != parent_path) {
            *last_slash = '\0';
        }

        size_t base_len = strlen(s_books_dir);
        if (strlen(parent_path) < base_len ||
            strncmp(parent_path, s_books_dir, base_len) != 0) {
            strncpy(parent_path, s_books_dir, sizeof(parent_path) - 1);
            parent_path[sizeof(parent_path) - 1] = '\0';
        }

        book_info_t *up = &s_books[s_book_count++];
        strncpy(up->filename, "..", sizeof(up->filename) - 1);
        up->filename[sizeof(up->filename) - 1] = '\0';
        strncpy(up->path, parent_path, sizeof(up->path) - 1);
        up->path[sizeof(up->path) - 1] = '\0';
        up->size = 0;
        up->type = LIBRARY_ENTRY_UP;
    }

    DIR *dir = opendir(s_current_dir);
    if (!dir) {
        ESP_LOGW(TAG, "Cannot open books directory: %s", s_current_dir);
        s_scanned = false;
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    struct dirent *ent = NULL;

    while ((ent = readdir(dir)) != NULL) {
        if (s_book_count >= BOOK_MAX_COUNT) {
            ESP_LOGW(TAG, "Max book limit (%d) reached", BOOK_MAX_COUNT);
            break;
        }

        // Skip hidden files and directories (names starting with '.')
        if (ent->d_name[0] == '.') {
            continue;
        }

        char full_path[BOOK_PATH_MAX_LEN];
        int ret = snprintf(full_path, sizeof(full_path), "%s/%s", s_current_dir, ent->d_name);
        if (ret < 0 || ret >= (int)sizeof(full_path)) {
            continue;
        }

        struct stat st;
        if (stat(full_path, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            book_info_t *book = &s_books[s_book_count];
            strncpy(book->filename, ent->d_name, sizeof(book->filename) - 1);
            book->filename[sizeof(book->filename) - 1] = '\0';
            strncpy(book->path, full_path, sizeof(book->path) - 1);
            book->path[sizeof(book->path) - 1] = '\0';
            book->size = 0;
            book->type = LIBRARY_ENTRY_DIR;
            s_book_count++;
        } else if (S_ISREG(st.st_mode)) {
            size_t name_len = strlen(ent->d_name);
            if (name_len < 4) {
                continue;
            }

            // Must end in .txt
            if (strcasecmp(ent->d_name + name_len - 4, ".txt") != 0) {
                continue;
            }

            book_info_t *book = &s_books[s_book_count];
            strncpy(book->filename, ent->d_name, sizeof(book->filename) - 1);
            book->filename[sizeof(book->filename) - 1] = '\0';
            strncpy(book->path, full_path, sizeof(book->path) - 1);
            book->path[sizeof(book->path) - 1] = '\0';
            book->size = (size_t)st.st_size;
            book->type = LIBRARY_ENTRY_FILE;
            s_book_count++;
        }
    }

    closedir(dir);

    if (s_book_count > 1) {
        qsort(s_books, s_book_count, sizeof(book_info_t), compare_books);
    }

    s_scanned = true;

    ESP_LOGI(TAG, "Scanned %u entries in %s", (unsigned int)s_book_count, s_current_dir);

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

void book_library_invalidate_cache(void) {
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_scanned = false;
        xSemaphoreGive(s_mutex);
    } else {
        s_scanned = false;
    }
}

esp_err_t book_library_enter_dir(const char *subdir_name) {
    if (!subdir_name || subdir_name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (strcmp(subdir_name, "..") == 0) {
        return book_library_navigate_up();
    }
    if (strcmp(subdir_name, ".") == 0) {
        return ESP_OK;
    }

    char target_path[BOOK_PATH_MAX_LEN];

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    size_t base_len = strlen(s_books_dir);
    if (strncmp(subdir_name, s_books_dir, base_len) == 0 &&
        (subdir_name[base_len] == '/' || subdir_name[base_len] == '\0')) {
        int r = snprintf(target_path, sizeof(target_path), "%s", subdir_name);
        if (r < 0 || r >= (int)sizeof(target_path)) {
            xSemaphoreGive(s_mutex);
            return ESP_ERR_INVALID_SIZE;
        }
    } else {
        const char *sub = subdir_name;
        while (*sub == '/') {
            sub++;
        }
        if (*sub == '\0') {
            xSemaphoreGive(s_mutex);
            return ESP_OK;
        }
        int r = snprintf(target_path, sizeof(target_path), "%s/%s", s_current_dir, sub);
        if (r < 0 || r >= (int)sizeof(target_path)) {
            xSemaphoreGive(s_mutex);
            return ESP_ERR_INVALID_SIZE;
        }
    }
    xSemaphoreGive(s_mutex);

    size_t tlen = strlen(target_path);
    if (tlen > 1 && target_path[tlen - 1] == '/') {
        target_path[tlen - 1] = '\0';
    }

    struct stat st;
    if (stat(target_path, &st) != 0) {
        ESP_LOGW(TAG, "Subdirectory does not exist: %s", target_path);
        return ESP_ERR_NOT_FOUND;
    }
    if (!S_ISDIR(st.st_mode)) {
        ESP_LOGW(TAG, "Target is not a directory: %s", target_path);
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    strncpy(s_current_dir, target_path, sizeof(s_current_dir) - 1);
    s_current_dir[sizeof(s_current_dir) - 1] = '\0';
    update_current_rel_path();
    s_scanned = false;
    xSemaphoreGive(s_mutex);

    return book_library_scan();
}

esp_err_t book_library_navigate_up(void) {
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (is_root_dir_locked()) {
        xSemaphoreGive(s_mutex);
        ESP_LOGD(TAG, "Already at root books directory");
        return ESP_ERR_INVALID_STATE;
    }

    char parent_path[BOOK_PATH_MAX_LEN];
    strncpy(parent_path, s_current_dir, sizeof(parent_path) - 1);
    parent_path[sizeof(parent_path) - 1] = '\0';

    char *last_slash = strrchr(parent_path, '/');
    if (last_slash && last_slash != parent_path) {
        *last_slash = '\0';
    }

    size_t base_len = strlen(s_books_dir);
    if (strlen(parent_path) < base_len ||
        strncmp(parent_path, s_books_dir, base_len) != 0) {
        strncpy(parent_path, s_books_dir, sizeof(parent_path) - 1);
        parent_path[sizeof(parent_path) - 1] = '\0';
    }

    strncpy(s_current_dir, parent_path, sizeof(s_current_dir) - 1);
    s_current_dir[sizeof(s_current_dir) - 1] = '\0';
    update_current_rel_path();
    s_scanned = false;

    xSemaphoreGive(s_mutex);

    return book_library_scan();
}

const char *book_library_get_current_rel_path(void) {
    return s_rel_path;
}

bool book_library_is_root_dir(void) {
    bool is_root = true;
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        is_root = is_root_dir_locked();
        xSemaphoreGive(s_mutex);
    } else {
        is_root = is_root_dir_locked();
    }
    return is_root;
}

size_t book_library_get_count(void) {
    size_t count = 0;
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        count = s_book_count;
        xSemaphoreGive(s_mutex);
    }
    return count;
}

const book_info_t *book_library_get_book(size_t index) {
    const book_info_t *res = NULL;
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        if (index < s_book_count) {
            res = &s_books[index];
        }
        xSemaphoreGive(s_mutex);
    }
    return res;
}

const book_info_t *book_library_find_book(const char *filename_or_path) {
    if (!filename_or_path || !s_mutex) {
        return NULL;
    }

    const book_info_t *found = NULL;
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    for (size_t i = 0; i < s_book_count; i++) {
        if (strcasecmp(s_books[i].filename, filename_or_path) == 0 ||
            strcmp(s_books[i].path, filename_or_path) == 0) {
            found = &s_books[i];
            break;
        }
    }

    xSemaphoreGive(s_mutex);
    return found;
}

esp_err_t book_library_load_state(reading_state_t *state) {
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_state_loaded) {
        if (state) {
            *state = s_reading_state;
        }
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    if (!check_storage_mounted(s_storage_target)) {
        ESP_LOGE(TAG, "Storage target %s not mounted, cannot load state",
                 s_storage_target == STORAGE_TARGET_FLASH ? "FLASH" : "SDCARD");
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    FILE *fp = fopen(s_state_file, "r");
    if (!fp) {
        ESP_LOGI(TAG, "State file not found (%s), initializing defaults", s_state_file);
        memset(&s_reading_state, 0, sizeof(s_reading_state));
        for (size_t i = 0; i < s_book_count; i++) {
            if (s_books[i].type == LIBRARY_ENTRY_FILE) {
                strncpy(s_reading_state.active_book_path, s_books[i].path, sizeof(s_reading_state.active_book_path) - 1);
                s_reading_state.total_bytes = s_books[i].size;
                break;
            }
        }
        if (state) {
            *state = s_reading_state;
        }
        s_state_dirty = false;
        s_state_loaded = true;
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 16384) {
        fclose(fp);
        ESP_LOGW(TAG, "State file empty or abnormally large (%ld bytes)", fsize);
        if (state) {
            *state = s_reading_state;
        }
        xSemaphoreGive(s_mutex);
        return ESP_FAIL;
    }

    char *json_buf = (char *)malloc(fsize + 1);
    if (!json_buf) {
        fclose(fp);
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }

    size_t read_bytes = fread(json_buf, 1, fsize, fp);
    fclose(fp);
    json_buf[read_bytes] = '\0';

    cJSON *root = cJSON_Parse(json_buf);
    free(json_buf);

    if (!root) {
        ESP_LOGE(TAG, "Failed to parse state JSON");
        if (state) {
            *state = s_reading_state;
        }
        xSemaphoreGive(s_mutex);
        return ESP_FAIL;
    }

    memset(&s_reading_state, 0, sizeof(s_reading_state));

    cJSON *item = cJSON_GetObjectItem(root, "active_book_path");
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(s_reading_state.active_book_path, item->valuestring, sizeof(s_reading_state.active_book_path) - 1);
    }

    item = cJSON_GetObjectItem(root, "byte_offset");
    if (cJSON_IsNumber(item)) {
        s_reading_state.byte_offset = (size_t)item->valuedouble;
    }

    item = cJSON_GetObjectItem(root, "total_bytes");
    if (cJSON_IsNumber(item)) {
        s_reading_state.total_bytes = (size_t)item->valuedouble;
    }

    cJSON *bms = cJSON_GetObjectItem(root, "bookmarks");
    if (cJSON_IsArray(bms)) {
        int bcount = cJSON_GetArraySize(bms);
        for (int i = 0; i < bcount && i < BOOK_MAX_BOOKMARKS; i++) {
            cJSON *b = cJSON_GetArrayItem(bms, i);
            if (cJSON_IsNumber(b)) {
                s_reading_state.bookmarks[s_reading_state.bookmark_count++] = (size_t)b->valuedouble;
            }
        }
    }

    cJSON_Delete(root);

    // Validate that active_book_path exists and refresh total_bytes
    if (s_reading_state.active_book_path[0] != '\0') {
        struct stat st;
        if (stat(s_reading_state.active_book_path, &st) != 0) {
            ESP_LOGW(TAG, "Saved active book '%s' not found on storage, resetting", s_reading_state.active_book_path);
            s_reading_state.active_book_path[0] = '\0';
            s_reading_state.byte_offset = 0;
            s_reading_state.total_bytes = 0;
            s_reading_state.bookmark_count = 0;
        } else if (s_reading_state.total_bytes == 0) {
            s_reading_state.total_bytes = (size_t)st.st_size;
        }
    }

    if (s_reading_state.active_book_path[0] == '\0' && s_book_count > 0) {
        for (size_t i = 0; i < s_book_count; i++) {
            if (s_books[i].type == LIBRARY_ENTRY_FILE) {
                strncpy(s_reading_state.active_book_path, s_books[i].path, sizeof(s_reading_state.active_book_path) - 1);
                s_reading_state.byte_offset = 0;
                s_reading_state.total_bytes = s_books[i].size;
                s_reading_state.bookmark_count = 0;
                break;
            }
        }
    }
    if (state) {
        *state = s_reading_state;
    }

    s_state_dirty = false;
    s_state_loaded = true;

    ESP_LOGI(TAG, "Loaded state: active=%s, offset=%u, total=%u, bookmarks=%u",
             s_reading_state.active_book_path,
             (unsigned int)s_reading_state.byte_offset,
             (unsigned int)s_reading_state.total_bytes,
             (unsigned int)s_reading_state.bookmark_count);

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t book_library_save_state(const reading_state_t *state) {
    if (!state || s_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (!check_storage_mounted(s_storage_target)) {
        ESP_LOGE(TAG, "Storage target %s not mounted, cannot save state",
                 s_storage_target == STORAGE_TARGET_FLASH ? "FLASH" : "SDCARD");
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    ensure_dir_exists(s_state_dir);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "active_book_path", state->active_book_path);
    cJSON_AddNumberToObject(root, "byte_offset", (double)state->byte_offset);
    cJSON_AddNumberToObject(root, "total_bytes", (double)state->total_bytes);

    cJSON *bm_arr = cJSON_CreateArray();
    if (bm_arr) {
        for (size_t i = 0; i < state->bookmark_count; i++) {
            cJSON_AddItemToArray(bm_arr, cJSON_CreateNumber((double)state->bookmarks[i]));
        }
        cJSON_AddItemToObject(root, "bookmarks", bm_arr);
    }

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json_str) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }

    char tmp_file[96];
    int ret = snprintf(tmp_file, sizeof(tmp_file), "%.80s.tmp", s_state_file);
    if (ret < 0 || ret >= (int)sizeof(tmp_file)) {
        free(json_str);
        xSemaphoreGive(s_mutex);
        return ESP_FAIL;
    }

    FILE *fp = fopen(tmp_file, "w");
    if (!fp) {
        ESP_LOGE(TAG, "Failed to open %s for writing: errno %d", tmp_file, errno);
        free(json_str);
        xSemaphoreGive(s_mutex);
        return ESP_FAIL;
    }

    size_t len = strlen(json_str);
    size_t written = fwrite(json_str, 1, len, fp);
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);
    free(json_str);

    if (written != len) {
        ESP_LOGE(TAG, "Partial write to %s (%u / %u bytes)", tmp_file, (unsigned int)written, (unsigned int)len);
        unlink(tmp_file);
        xSemaphoreGive(s_mutex);
        return ESP_FAIL;
    }

    if (rename(tmp_file, s_state_file) != 0) {
        if (errno == EEXIST) {
            unlink(s_state_file);
            if (rename(tmp_file, s_state_file) != 0) {
                ESP_LOGE(TAG, "Failed to rename %s to %s after unlink: errno %d", tmp_file, s_state_file, errno);
                unlink(tmp_file);
                xSemaphoreGive(s_mutex);
                return ESP_FAIL;
            }
        } else {
            ESP_LOGE(TAG, "Failed to rename %s to %s: errno %d", tmp_file, s_state_file, errno);
            unlink(tmp_file);
            xSemaphoreGive(s_mutex);
            return ESP_FAIL;
        }
    }

    s_reading_state = *state;
    s_state_dirty = false;
    s_state_loaded = true;

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t book_library_flush_state(void) {
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (!s_state_dirty) {
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }
    xSemaphoreGive(s_mutex);

    esp_err_t ret = book_library_save_state(&s_reading_state);
    if (ret == ESP_OK) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_state_dirty = false;
        xSemaphoreGive(s_mutex);
    }
    return ret;
}

esp_err_t book_library_get_current_state(reading_state_t *state) {
    if (!state || s_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *state = s_reading_state;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t book_library_set_active_book(const char *book_path, bool reset_offset) {
    if (!book_path || s_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Pastikan state buku lama di-flush sebelum berganti ke buku baru */
    book_library_flush_state();

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    bool same_book = (strcmp(s_reading_state.active_book_path, book_path) == 0);

    strncpy(s_reading_state.active_book_path, book_path, sizeof(s_reading_state.active_book_path) - 1);
    s_reading_state.active_book_path[sizeof(s_reading_state.active_book_path) - 1] = '\0';

    if (reset_offset || !same_book) {
        s_reading_state.byte_offset = 0;
        s_reading_state.bookmark_count = 0;
    }

    struct stat st;
    if (stat(book_path, &st) == 0) {
        s_reading_state.total_bytes = (size_t)st.st_size;
    } else {
        s_reading_state.total_bytes = 0;
    }

    reading_state_t copy = s_reading_state;
    xSemaphoreGive(s_mutex);

    return book_library_save_state(&copy);
}

esp_err_t book_library_update_offset(size_t offset) {
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_reading_state.byte_offset = offset;
    s_state_dirty = true;
    s_state_loaded = true;
    xSemaphoreGive(s_mutex);

    return ESP_OK;
}

esp_err_t book_library_add_bookmark(size_t offset) {
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    // Check if already exists
    for (size_t i = 0; i < s_reading_state.bookmark_count; i++) {
        if (s_reading_state.bookmarks[i] == offset) {
            xSemaphoreGive(s_mutex);
            return ESP_OK;
        }
    }

    if (s_reading_state.bookmark_count >= BOOK_MAX_BOOKMARKS) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }

    // Insert sorted
    size_t insert_idx = s_reading_state.bookmark_count;
    for (size_t i = 0; i < s_reading_state.bookmark_count; i++) {
        if (offset < s_reading_state.bookmarks[i]) {
            insert_idx = i;
            break;
        }
    }

    for (size_t i = s_reading_state.bookmark_count; i > insert_idx; i--) {
        s_reading_state.bookmarks[i] = s_reading_state.bookmarks[i - 1];
    }
    s_reading_state.bookmarks[insert_idx] = offset;
    s_reading_state.bookmark_count++;

    reading_state_t copy = s_reading_state;
    xSemaphoreGive(s_mutex);

    return book_library_save_state(&copy);
}

esp_err_t book_library_remove_bookmark(size_t offset) {
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    int found_idx = -1;
    for (size_t i = 0; i < s_reading_state.bookmark_count; i++) {
        if (s_reading_state.bookmarks[i] == offset) {
            found_idx = (int)i;
            break;
        }
    }

    if (found_idx < 0) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    for (size_t i = (size_t)found_idx; i + 1 < s_reading_state.bookmark_count; i++) {
        s_reading_state.bookmarks[i] = s_reading_state.bookmarks[i + 1];
    }
    s_reading_state.bookmark_count--;

    reading_state_t copy = s_reading_state;
    xSemaphoreGive(s_mutex);

    return book_library_save_state(&copy);
}

bool book_library_is_bookmarked(size_t offset) {
    if (s_mutex == NULL) {
        return false;
    }

    bool found = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    for (size_t i = 0; i < s_reading_state.bookmark_count; i++) {
        if (s_reading_state.bookmarks[i] == offset) {
            found = true;
            break;
        }
    }

    xSemaphoreGive(s_mutex);
    return found;
}
