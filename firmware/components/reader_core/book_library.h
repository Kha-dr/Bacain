#ifndef BOOK_LIBRARY_H
#define BOOK_LIBRARY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef STORAGE_TARGET_T_DEFINED
#define STORAGE_TARGET_T_DEFINED
typedef enum {
    STORAGE_TARGET_SDCARD = 0,
    STORAGE_TARGET_FLASH  = 1,
} storage_target_t;
#endif

#define BOOK_LIBRARY_SDCARD_BOOKS_DIR     "/sdcard/books"
#define BOOK_LIBRARY_SDCARD_STATE_DIR     "/sdcard/.reader"
#define BOOK_LIBRARY_FLASH_BOOKS_DIR      "/spiflash/books"
#define BOOK_LIBRARY_FLASH_STATE_DIR      "/spiflash/.reader"

/* Legacy definitions for backward compatibility */
#define BOOK_LIBRARY_BOOKS_DIR     BOOK_LIBRARY_SDCARD_BOOKS_DIR
#define BOOK_LIBRARY_STATE_DIR     BOOK_LIBRARY_SDCARD_STATE_DIR
#define BOOK_LIBRARY_STATE_FILE    "/sdcard/.reader/state.json"

#define BOOK_FILENAME_MAX_LEN      128
#define BOOK_PATH_MAX_LEN          256
#define BOOK_MAX_COUNT             64
#define BOOK_MAX_BOOKMARKS         32

typedef enum {
    LIBRARY_ENTRY_FILE = 0,
    LIBRARY_ENTRY_DIR  = 1,
    LIBRARY_ENTRY_UP   = 2, // Item ".." untuk kembali ke folder induk
} library_entry_type_t;

typedef struct {
    char filename[BOOK_FILENAME_MAX_LEN];
    char path[BOOK_PATH_MAX_LEN];
    size_t size; // file size in bytes
    library_entry_type_t type;
} book_info_t;

typedef struct {
    char active_book_path[BOOK_PATH_MAX_LEN];
    size_t byte_offset;
    size_t total_bytes;
    size_t bookmarks[BOOK_MAX_BOOKMARKS];
    size_t bookmark_count;
} reading_state_t;

/**
 * @brief Initialize the book library and load reading state.
 * @return ESP_OK on success, or an esp_err_t error code.
 */
esp_err_t book_library_init(void);

/**
 * @brief Deinitialize book library and release resources.
 */
void book_library_deinit(void);

/**
 * @brief Set the storage target for the book library (SD card or SPI flash).
 *
 * @param target Storage target (STORAGE_TARGET_SDCARD or STORAGE_TARGET_FLASH).
 * @return ESP_OK on success, or ESP_ERR_INVALID_STATE if target media is not mounted.
 */
esp_err_t book_library_set_storage_target(storage_target_t target);

/**
 * @brief Get current storage target used by the book library.
 *
 * @return Current storage target.
 */
storage_target_t book_library_get_storage_target(void);

/**
 * @brief Scan active books directory for .txt files.
 * @return ESP_OK on success, or an esp_err_t error code.
 */
esp_err_t book_library_scan(void);

/**
 * @brief Invalidate scanned book library cache.
 */
void book_library_invalidate_cache(void);

/**
 * @brief Enter a subdirectory within current directory and rescan.
 *
 * @param subdir_name Name of subdirectory or subfolder path.
 * @return ESP_OK on success, or an error code.
 */
esp_err_t book_library_enter_dir(const char *subdir_name);

/**
 * @brief Navigate up to parent directory and rescan (cannot go above root books dir).
 *
 * @return ESP_OK on success, or ESP_ERR_INVALID_STATE if already at root dir.
 */
esp_err_t book_library_navigate_up(void);

/**
 * @brief Get current relative path against base books directory.
 *
 * @return Relative path string (e.g. "" for root, or "/Sci-Fi").
 */
const char *book_library_get_current_rel_path(void);

/**
 * @brief Check if current directory is root books directory.
 *
 * @return true if at root books directory, false if inside a subdirectory.
 */
bool book_library_is_root_dir(void);

/**
 * @brief Get the count of available books.
 * @return Number of books found.
 */
size_t book_library_get_count(void);

/**
 * @brief Get book info by index.
 * @param index Index in [0, book_count - 1].
 * @return Pointer to book_info_t, or NULL if index out of bounds.
 */
const book_info_t *book_library_get_book(size_t index);

/**
 * @brief Find book by filename or path.
 * @param filename_or_path Filename (e.g. "alice.txt") or full path.
 * @return Pointer to book_info_t, or NULL if not found.
 */
const book_info_t *book_library_find_book(const char *filename_or_path);

/**
 * @brief Load reading progress state from state file in active storage.
 * @param state Pointer to reading_state_t structure to fill.
 * @return ESP_OK on success, or an esp_err_t error code.
 */
esp_err_t book_library_load_state(reading_state_t *state);

/**
 * @brief Save reading progress state to state file in active storage.
 * @param state Pointer to reading_state_t structure to save.
 * @return ESP_OK on success, or an esp_err_t error code.
 */
esp_err_t book_library_save_state(const reading_state_t *state);

/**
 * @brief Flush in-memory reading state to storage if dirty.
 * @return ESP_OK on success, or an esp_err_t error code.
 */
esp_err_t book_library_flush_state(void);

/**
 * @brief Get copy of current in-memory reading state.
 * @param state Pointer to reading_state_t structure.
 * @return ESP_OK on success.
 */
esp_err_t book_library_get_current_state(reading_state_t *state);

/**
 * @brief Set current active book path and update total bytes.
 * @param book_path Full path to active book.
 * @param reset_offset If true, reset byte_offset to 0; if false, retain offset if same book.
 * @return ESP_OK on success, or an error code.
 */
esp_err_t book_library_set_active_book(const char *book_path, bool reset_offset);

/**
 * @brief Update the byte offset of the active book in memory and mark dirty.
 * @param offset New byte offset.
 * @return ESP_OK on success.
 */
esp_err_t book_library_update_offset(size_t offset);

/**
 * @brief Add a bookmark at the specified offset.
 * @param offset Byte offset to bookmark.
 * @return ESP_OK on success, ESP_ERR_NO_MEM if max reached, ESP_OK if already present.
 */
esp_err_t book_library_add_bookmark(size_t offset);

/**
 * @brief Remove a bookmark at the specified offset.
 * @param offset Byte offset to remove.
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not present.
 */
esp_err_t book_library_remove_bookmark(size_t offset);

/**
 * @brief Check if an offset is bookmarked.
 * @param offset Byte offset to check.
 * @return true if bookmarked, false otherwise.
 */
bool book_library_is_bookmarked(size_t offset);

#ifdef __cplusplus
}
#endif

#endif // BOOK_LIBRARY_H
