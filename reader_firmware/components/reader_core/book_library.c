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
static bool              s_state_dirty = false;

static int compare_books(const void *a, const void *b) {
    const book_info_t *book_a = (const book_info_t *)a;
    const book_info_t *book_b = (const book_info_t *)b;
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
        book_library_scan();
        book_library_load_state(&s_reading_state);
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

    s_storage_target = target;
    update_target_paths(target);

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

    if (!check_storage_mounted(s_storage_target)) {
        ESP_LOGE(TAG, "Storage target %s not mounted, cannot scan books",
                 s_storage_target == STORAGE_TARGET_FLASH ? "FLASH" : "SDCARD");
        s_book_count = 0;
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    ensure_dir_exists(s_books_dir);

    DIR *dir = opendir(s_books_dir);
    if (!dir) {
        ESP_LOGW(TAG, "Cannot open books directory: %s", s_books_dir);
        s_book_count = 0;
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    s_book_count = 0;
    struct dirent *ent = NULL;

    while ((ent = readdir(dir)) != NULL) {
        if (s_book_count >= BOOK_MAX_COUNT) {
            ESP_LOGW(TAG, "Max book limit (%d) reached", BOOK_MAX_COUNT);
            break;
        }

        // Skip hidden files
        if (ent->d_name[0] == '.') {
            continue;
        }

        size_t name_len = strlen(ent->d_name);
        if (name_len < 4) {
            continue;
        }

        // Must end in .txt
        if (strcasecmp(ent->d_name + name_len - 4, ".txt") != 0) {
            continue;
        }

        book_info_t *book = &s_books[s_book_count];
        // Pastikan nama file + path tidak melebihi buffer
        if (name_len >= sizeof(book->filename)) {
            continue;
        }

        int ret = snprintf(book->path, sizeof(book->path), "%.60s/%.190s", s_books_dir, ent->d_name);
        if (ret < 0 || ret >= (int)sizeof(book->path)) {
            continue;
        }

        strncpy(book->filename, ent->d_name, sizeof(book->filename) - 1);
        book->filename[sizeof(book->filename) - 1] = '\0';

        struct stat st;
        if (stat(book->path, &st) == 0) {
            book->size = (size_t)st.st_size;
        } else {
            book->size = 0;
        }

        s_book_count++;
    }

    closedir(dir);

    if (s_book_count > 1) {
        qsort(s_books, s_book_count, sizeof(book_info_t), compare_books);
    }

    ESP_LOGI(TAG, "Scanned %u books in %s", (unsigned int)s_book_count, s_books_dir);

    xSemaphoreGive(s_mutex);
    return ESP_OK;
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
        if (s_book_count > 0) {
            strncpy(s_reading_state.active_book_path, s_books[0].path, sizeof(s_reading_state.active_book_path) - 1);
            s_reading_state.total_bytes = s_books[0].size;
        }
        if (state) {
            *state = s_reading_state;
        }
        s_state_dirty = false;
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
        strncpy(s_reading_state.active_book_path, s_books[0].path, sizeof(s_reading_state.active_book_path) - 1);
        s_reading_state.byte_offset = 0;
        s_reading_state.total_bytes = s_books[0].size;
        s_reading_state.bookmark_count = 0;
    }
    if (state) {
        *state = s_reading_state;
    }

    s_state_dirty = false;

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
