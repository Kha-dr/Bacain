#ifndef QURAN_READER_H
#define QURAN_READER_H

#include "sdkconfig.h"
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BQM_MAGIC 0x314D5142U
#define BQM_VERSION 1
#define QURAN_MAX_BOOKMARKS 8
#define QURAN_TOTAL_SURAHS 114
#define QURAN_TOTAL_AYAHS 6236

typedef struct __attribute__((packed)) {
    char     magic[4];
    uint16_t version;
    uint8_t  layer_mask;
    uint8_t  mushaf_style_id;
    uint16_t display_width;
    uint16_t display_height;
    uint16_t total_surahs;
    uint16_t total_ayahs;
    uint32_t word_dict_count;
    uint32_t surah_table_off;
    uint32_t ayah_table_off;
    uint32_t word_dict_off;
    uint32_t sprite_blob_off;
    uint32_t mushaf_pages_off;
    uint32_t text_blob_off;
    char     edition_tag[20];
} bqm_header_t;

typedef struct __attribute__((packed)) {
    uint8_t  surah_number;
    uint16_t ayah_count;
    uint16_t first_global_ayah;
    uint8_t  start_juz;
    char     name_latin[18];
} bqm_surah_entry_t;

typedef struct __attribute__((packed)) {
    uint8_t  juz;
    uint8_t  hizb_quarter;
    uint8_t  mushaf_subpages;
    uint8_t  latin_subpages;
    uint8_t  trans_subpages;
    uint8_t  tafsir_subpages;
    uint16_t reserved;
    uint32_t mushaf_off;
    uint32_t latin_off;
    uint32_t trans_off;
    uint32_t tafsir_off;
} bqm_ayah_entry_t;

typedef struct __attribute__((packed)) {
    uint8_t  width;
    uint8_t  height;
    int8_t   y_offset;
    uint8_t  stride_bytes;
    uint32_t bitmap_off;
} bqm_word_sprite_t;

typedef struct __attribute__((packed)) {
    uint16_t word_id;
    uint16_t x;
    uint16_t y;
} bqm_placed_word_t;

typedef enum {
    QURAN_LAYER_MUSHAF    = 0,
    QURAN_LAYER_LATIN     = 1,
    QURAN_LAYER_TRANSLATE = 2,
    QURAN_LAYER_TAFSIR    = 3,
    QURAN_LAYER_COUNT     = 4,
} quran_layer_t;

typedef struct __attribute__((packed)) {
    uint8_t  surah;
    uint16_t ayah;
    uint8_t  juz;
} quran_bookmark_t;

typedef struct {
    bool             started;
    uint8_t          surah;
    uint16_t         ayah;
    uint8_t          layer;
    uint8_t          subpage;
    uint8_t          bookmark_count;
    quran_bookmark_t bookmarks[QURAN_MAX_BOOKMARKS];
} quran_state_t;

esp_err_t           quran_reader_init(const quran_state_t *rtc_state);
bool                quran_reader_has_bqm(void);
const bqm_header_t *quran_reader_get_header(void);
const quran_state_t *quran_reader_get_state(void);
esp_err_t           quran_reader_save_state(void);
esp_err_t           quran_reader_get_surah_info(uint8_t surah, bqm_surah_entry_t *out);
esp_err_t           quran_reader_get_ayah_info(uint8_t surah, uint16_t ayah, bqm_ayah_entry_t *out, uint16_t *out_global_ayah);
uint8_t             quran_reader_get_active_subpage_count(void);
esp_err_t           quran_reader_set_cursor(uint8_t surah, uint16_t ayah, uint8_t layer);
esp_err_t           quran_reader_set_bookmarks(const quran_bookmark_t *list, uint8_t count);
bool                quran_reader_is_bookmarked(uint8_t surah, uint16_t ayah);
bool                quran_reader_toggle_bookmark(void);
esp_err_t           quran_reader_delete_bookmark(uint8_t index);
esp_err_t           quran_reader_step_page(int8_t dir);
esp_err_t           quran_reader_cycle_layer(void);
esp_err_t           quran_reader_render_subpage(uint8_t *canvas_buf);

#ifdef __cplusplus
}
#endif

#endif // QURAN_READER_H
