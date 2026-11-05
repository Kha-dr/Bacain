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
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdcard.h"
#include "text_pager.h"

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

#define BOOK_STATE_MAGIC  0x424D4B31U
#define ACTIVE_BOOK_MAGIC 0x41435431U

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t byte_offset;
    uint32_t bookmark_count;
    uint32_t bookmarks[BOOK_MAX_BOOKMARKS];
} book_sidecar_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    char active_book_path[BOOK_PATH_MAX_LEN];
} active_book_record_t;

static const char *TAG = "book_library";

static book_info_t       s_books[BOOK_MAX_COUNT];
static size_t            s_book_count = 0;
static reading_state_t   s_reading_state = {0};
static SemaphoreHandle_t s_mutex = NULL;
static bool              s_initialized = false;
static storage_target_t  s_storage_target = STORAGE_TARGET_FLASH;
static char              s_books_dir[64] = "/spiflash/books";
static char              s_state_dir[64] = "/spiflash/.reader";
static char              s_active_file[80] = "/spiflash/.reader/active.bin";
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
    snprintf(s_active_file, sizeof(s_active_file), "%.32s/.reader/active.bin", mount_pt);
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

static void resolve_book_full_path(const char *path_or_filename, char *full_path, size_t full_size) {
    if (!path_or_filename || !full_path || full_size == 0) {
        if (full_path && full_size > 0) full_path[0] = '\0';
        return;
    }

    if (strncmp(path_or_filename, "/spiflash/", 10) == 0 ||
        strncmp(path_or_filename, "/sdcard/", 8) == 0) {
        strncpy(full_path, path_or_filename, full_size - 1);
        full_path[full_size - 1] = '\0';
        return;
    }

    const char *rel = path_or_filename;
    while (*rel == '/') {
        rel++;
    }
    snprintf(full_path, full_size, "%s/%s", s_books_dir, rel);
}

static void get_book_sidecar_path(const char *full_book_path, char *out_sidecar_path, size_t out_size) {
    if (!full_book_path || !out_sidecar_path || out_size == 0) {
        if (out_sidecar_path && out_size > 0) out_sidecar_path[0] = '\0';
        return;
    }

    const char *rel = full_book_path;
    size_t base_len = strlen(s_books_dir);
    if (strncmp(full_book_path, s_books_dir, base_len) == 0) {
        rel = full_book_path + base_len;
    } else if (strncmp(full_book_path, "/spiflash/books/", 16) == 0) {
        rel = full_book_path + 16;
    } else if (strncmp(full_book_path, "/sdcard/books/", 14) == 0) {
        rel = full_book_path + 14;
    } else if (strncmp(full_book_path, "/spiflash/", 10) == 0) {
        rel = full_book_path + 10;
    } else if (strncmp(full_book_path, "/sdcard/", 8) == 0) {
        rel = full_book_path + 8;
    }

    while (*rel == '/') {
        rel++;
    }

    char sanitized[112];
    size_t len = strlen(rel);
    if (len >= 4 && strcasecmp(rel + len - 4, ".txt") == 0) {
        len -= 4;
    }
    if (len >= sizeof(sanitized)) {
        len = sizeof(sanitized) - 1;
    }

    for (size_t i = 0; i < len; i++) {
        char c = rel[i];
        if (c == '/' || c == '\\' || c == ':') {
            sanitized[i] = '_';
        } else {
            sanitized[i] = c;
        }
    }
    sanitized[len] = '\0';

    snprintf(out_sidecar_path, out_size, "%s/%s.bmk", s_state_dir, sanitized);
}

static esp_err_t write_atomic_file(const char *target_path, const void *data, size_t size) {
    char tmp_path[BOOK_PATH_MAX_LEN + 16];
    int ret = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", target_path);
    if (ret < 0 || ret >= (int)sizeof(tmp_path)) {
        return ESP_FAIL;
    }

    FILE *fp = fopen(tmp_path, "wb");
    if (!fp) {
        ESP_LOGE(TAG, "Failed to open %s for writing: errno %d", tmp_path, errno);
        return ESP_FAIL;
    }

    size_t written = fwrite(data, 1, size, fp);
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);

    if (written != size) {
        ESP_LOGE(TAG, "Partial write to %s (%u / %u bytes)", tmp_path, (unsigned int)written, (unsigned int)size);
        unlink(tmp_path);
        return ESP_FAIL;
    }

    if (rename(tmp_path, target_path) != 0) {
        unlink(target_path);
        if (rename(tmp_path, target_path) != 0) {
            ESP_LOGE(TAG, "Failed to rename %s to %s: errno %d", tmp_path, target_path, errno);
            unlink(tmp_path);
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

static esp_err_t load_book_sidecar(const char *full_book_path, size_t *out_offset, size_t *out_bookmarks, size_t *out_bm_count) {
    if (out_offset) *out_offset = 0;
    if (out_bm_count) *out_bm_count = 0;

    if (!full_book_path || full_book_path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    char sidecar_path[BOOK_PATH_MAX_LEN];
    get_book_sidecar_path(full_book_path, sidecar_path, sizeof(sidecar_path));

    FILE *fp = fopen(sidecar_path, "rb");
    if (!fp) {
        return ESP_ERR_NOT_FOUND;
    }

    book_sidecar_t sc = {0};
    size_t r = fread(&sc, 1, sizeof(sc), fp);
    fclose(fp);

    if (r != sizeof(sc) || sc.magic != BOOK_STATE_MAGIC) {
        return ESP_FAIL;
    }

    size_t max_bytes = SIZE_MAX;
    struct stat st;
    if (stat(full_book_path, &st) == 0) {
        max_bytes = (size_t)st.st_size;
    }

    size_t offset = (size_t)sc.byte_offset;
    if (offset > max_bytes) {
        offset = max_bytes;
    }
    if (out_offset) {
        *out_offset = offset;
    }

    size_t count = (sc.bookmark_count > BOOK_MAX_BOOKMARKS) ? BOOK_MAX_BOOKMARKS : (size_t)sc.bookmark_count;
    size_t valid_count = 0;
    for (size_t i = 0; i < count; i++) {
        size_t bm = (size_t)sc.bookmarks[i];
        if (bm <= max_bytes) {
            if (out_bookmarks) {
                out_bookmarks[valid_count] = bm;
            }
            valid_count++;
        }
    }
    if (out_bm_count) {
        *out_bm_count = valid_count;
    }

    return ESP_OK;
}

static esp_err_t save_book_sidecar(const char *full_book_path, size_t offset, const size_t *bookmarks, size_t bm_count) {
    if (!full_book_path || full_book_path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    ensure_dir_exists(s_state_dir);

    char sidecar_path[BOOK_PATH_MAX_LEN];
    get_book_sidecar_path(full_book_path, sidecar_path, sizeof(sidecar_path));

    book_sidecar_t sc = {0};
    sc.magic = BOOK_STATE_MAGIC;
    sc.byte_offset = (uint32_t)offset;
    if (bm_count > BOOK_MAX_BOOKMARKS) {
        bm_count = BOOK_MAX_BOOKMARKS;
    }
    sc.bookmark_count = (uint32_t)bm_count;
    for (size_t i = 0; i < bm_count && bookmarks != NULL; i++) {
        sc.bookmarks[i] = (uint32_t)bookmarks[i];
    }

    return write_atomic_file(sidecar_path, &sc, sizeof(sc));
}

static esp_err_t save_active_book_record(const char *active_book_path) {
    ensure_dir_exists(s_state_dir);

    active_book_record_t rec = {0};
    rec.magic = ACTIVE_BOOK_MAGIC;
    if (active_book_path) {
        strncpy(rec.active_book_path, active_book_path, sizeof(rec.active_book_path) - 1);
    }

    return write_atomic_file(s_active_file, &rec, sizeof(rec));
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

    memset(&s_reading_state, 0, sizeof(s_reading_state));

    FILE *fp = fopen(s_active_file, "rb");
    if (fp) {
        active_book_record_t rec = {0};
        size_t r = fread(&rec, 1, sizeof(rec), fp);
        fclose(fp);
        if (r == sizeof(rec) && rec.magic == ACTIVE_BOOK_MAGIC && rec.active_book_path[0] != '\0') {
            rec.active_book_path[sizeof(rec.active_book_path) - 1] = '\0';
            struct stat st;
            if (stat(rec.active_book_path, &st) == 0) {
                strncpy(s_reading_state.active_book_path, rec.active_book_path, sizeof(s_reading_state.active_book_path) - 1);
                s_reading_state.total_bytes = (size_t)st.st_size;
                load_book_sidecar(s_reading_state.active_book_path,
                                  &s_reading_state.byte_offset,
                                  s_reading_state.bookmarks,
                                  &s_reading_state.bookmark_count);
            } else {
                ESP_LOGW(TAG, "Saved active book '%s' not found on storage, resetting", rec.active_book_path);
            }
        }
    }

    if (s_reading_state.active_book_path[0] == '\0' && s_book_count > 0) {
        for (size_t i = 0; i < s_book_count; i++) {
            if (s_books[i].type == LIBRARY_ENTRY_FILE) {
                strncpy(s_reading_state.active_book_path, s_books[i].path, sizeof(s_reading_state.active_book_path) - 1);
                s_reading_state.total_bytes = s_books[i].size;
                load_book_sidecar(s_reading_state.active_book_path,
                                  &s_reading_state.byte_offset,
                                  s_reading_state.bookmarks,
                                  &s_reading_state.bookmark_count);
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

    esp_err_t ret = save_active_book_record(state->active_book_path);
    if (ret == ESP_OK && state->active_book_path[0] != '\0') {
        ret = save_book_sidecar(state->active_book_path, state->byte_offset, state->bookmarks, state->bookmark_count);
    }

    if (ret == ESP_OK) {
        s_reading_state = *state;
        s_state_dirty = false;
        s_state_loaded = true;
    }

    xSemaphoreGive(s_mutex);
    return ret;
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
    reading_state_t copy = s_reading_state;
    xSemaphoreGive(s_mutex);

    return book_library_save_state(&copy);
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

    book_library_flush_state();

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    char resolved_path[BOOK_PATH_MAX_LEN];
    resolve_book_full_path(book_path, resolved_path, sizeof(resolved_path));

    if (s_reading_state.active_book_path[0] != '\0' &&
        strcmp(s_reading_state.active_book_path, resolved_path) != 0) {
        save_book_sidecar(s_reading_state.active_book_path,
                          s_reading_state.byte_offset,
                          s_reading_state.bookmarks,
                          s_reading_state.bookmark_count);
    }

    strncpy(s_reading_state.active_book_path, resolved_path, sizeof(s_reading_state.active_book_path) - 1);
    s_reading_state.active_book_path[sizeof(s_reading_state.active_book_path) - 1] = '\0';

    struct stat st;
    if (stat(resolved_path, &st) == 0) {
        s_reading_state.total_bytes = (size_t)st.st_size;
    } else {
        s_reading_state.total_bytes = 0;
    }

    load_book_sidecar(resolved_path,
                      &s_reading_state.byte_offset,
                      s_reading_state.bookmarks,
                      &s_reading_state.bookmark_count);

    if (reset_offset) {
        s_reading_state.byte_offset = 0;
    } else if (s_reading_state.byte_offset > s_reading_state.total_bytes) {
        s_reading_state.byte_offset = s_reading_state.total_bytes;
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

esp_err_t book_library_get_book_state(const char *filename, size_t *out_offset, size_t *out_total_bytes, size_t *out_bookmarks, size_t *out_bm_count) {
    if (!filename || s_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    char full_path[BOOK_PATH_MAX_LEN];
    resolve_book_full_path(filename, full_path, sizeof(full_path));

    struct stat st;
    if (stat(full_path, &st) != 0) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    if (out_total_bytes) {
        *out_total_bytes = (size_t)st.st_size;
    }

    if (strcmp(s_reading_state.active_book_path, full_path) == 0) {
        if (out_offset) {
            *out_offset = s_reading_state.byte_offset;
        }
        if (out_bm_count) {
            *out_bm_count = s_reading_state.bookmark_count;
        }
        if (out_bookmarks && s_reading_state.bookmark_count > 0) {
            memcpy(out_bookmarks, s_reading_state.bookmarks, s_reading_state.bookmark_count * sizeof(size_t));
        }
    } else {
        size_t off = 0;
        size_t bms[BOOK_MAX_BOOKMARKS] = {0};
        size_t bmc = 0;
        load_book_sidecar(full_path, &off, bms, &bmc);
        if (out_offset) {
            *out_offset = off;
        }
        if (out_bm_count) {
            *out_bm_count = bmc;
        }
        if (out_bookmarks && bmc > 0) {
            memcpy(out_bookmarks, bms, bmc * sizeof(size_t));
        }
    }

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t book_library_add_book_bookmark(const char *filename, size_t offset) {
    if (!filename || s_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    char full_path[BOOK_PATH_MAX_LEN];
    resolve_book_full_path(filename, full_path, sizeof(full_path));

    struct stat st;
    if (stat(full_path, &st) != 0) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    if (offset > (size_t)st.st_size) {
        offset = (size_t)st.st_size;
    }

    if (strcmp(s_reading_state.active_book_path, full_path) == 0) {
        xSemaphoreGive(s_mutex);
        return book_library_add_bookmark(offset);
    }

    size_t off = 0;
    size_t bms[BOOK_MAX_BOOKMARKS] = {0};
    size_t bmc = 0;
    load_book_sidecar(full_path, &off, bms, &bmc);

    for (size_t i = 0; i < bmc; i++) {
        if (bms[i] == offset) {
            xSemaphoreGive(s_mutex);
            return ESP_OK;
        }
    }

    if (bmc >= BOOK_MAX_BOOKMARKS) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }

    size_t insert_idx = bmc;
    for (size_t i = 0; i < bmc; i++) {
        if (offset < bms[i]) {
            insert_idx = i;
            break;
        }
    }
    for (size_t i = bmc; i > insert_idx; i--) {
        bms[i] = bms[i - 1];
    }
    bms[insert_idx] = offset;
    bmc++;

    esp_err_t ret = save_book_sidecar(full_path, off, bms, bmc);
    xSemaphoreGive(s_mutex);
    return ret;
}

esp_err_t book_library_remove_book_bookmark(const char *filename, size_t offset) {
    if (!filename || s_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    char full_path[BOOK_PATH_MAX_LEN];
    resolve_book_full_path(filename, full_path, sizeof(full_path));

    if (strcmp(s_reading_state.active_book_path, full_path) == 0) {
        xSemaphoreGive(s_mutex);
        return book_library_remove_bookmark(offset);
    }

    size_t off = 0;
    size_t bms[BOOK_MAX_BOOKMARKS] = {0};
    size_t bmc = 0;
    load_book_sidecar(full_path, &off, bms, &bmc);

    int found_idx = -1;
    for (size_t i = 0; i < bmc; i++) {
        if (bms[i] == offset) {
            found_idx = (int)i;
            break;
        }
    }

    if (found_idx < 0) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    for (size_t i = (size_t)found_idx; i + 1 < bmc; i++) {
        bms[i] = bms[i + 1];
    }
    bmc--;

    esp_err_t ret = save_book_sidecar(full_path, off, bms, bmc);
    xSemaphoreGive(s_mutex);
    return ret;
}

esp_err_t book_library_set_book_offset(const char *filename, size_t offset) {
    if (!filename || s_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    char full_path[BOOK_PATH_MAX_LEN];
    resolve_book_full_path(filename, full_path, sizeof(full_path));

    struct stat st;
    if (stat(full_path, &st) != 0) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    if (offset > (size_t)st.st_size) {
        offset = (size_t)st.st_size;
    }

    if (strcmp(s_reading_state.active_book_path, full_path) == 0) {
        s_reading_state.byte_offset = offset;
        s_reading_state.total_bytes = (size_t)st.st_size;
        reading_state_t copy = s_reading_state;
        xSemaphoreGive(s_mutex);

        text_pager_seek(offset);
        return book_library_save_state(&copy);
    }

    size_t off = 0;
    size_t bms[BOOK_MAX_BOOKMARKS] = {0};
    size_t bmc = 0;
    load_book_sidecar(full_path, &off, bms, &bmc);
    esp_err_t ret = save_book_sidecar(full_path, offset, bms, bmc);
    xSemaphoreGive(s_mutex);
    return ret;
}

esp_err_t book_library_delete_book_state(const char *filename) {
    if (!filename || s_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    char full_path[BOOK_PATH_MAX_LEN];
    resolve_book_full_path(filename, full_path, sizeof(full_path));

    char sidecar_path[BOOK_PATH_MAX_LEN];
    get_book_sidecar_path(full_path, sidecar_path, sizeof(sidecar_path));
    unlink(sidecar_path);

    if (strcmp(s_reading_state.active_book_path, full_path) == 0) {
        s_reading_state.active_book_path[0] = '\0';
        s_reading_state.byte_offset = 0;
        s_reading_state.total_bytes = 0;
        s_reading_state.bookmark_count = 0;
        s_state_dirty = false;
        save_active_book_record("");
    }

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}
