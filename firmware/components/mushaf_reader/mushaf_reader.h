#ifndef MUSHAF_READER_H
#define MUSHAF_READER_H

#include "sdkconfig.h"
#include <stdbool.h>
#include <stdint.h>
#include "board_config.h"
#include "esp_err.h"
#include "text_layout.h" /* text_page_t (finding 2) */

#ifdef __cplusplus
extern "C"
{
#endif

#define BQM_MAGIC (0x314D5142U)
#define BQM_VERSION 2 /* v2: word images placed for the reader frame (body 22 .. H-18) */
#define MUSHAF_MAX_BOOKMARKS 8
#define MUSHAF_TOTAL_SURAHS 114
#define MUSHAF_TOTAL_AYAHS 6236

    typedef struct __attribute__((packed))
    {
        char magic[4];
        uint16_t version;
        uint8_t layer_mask;
        uint8_t mushaf_style_id;
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
        char edition_tag[20];
    } bqm_header_t;

    typedef struct __attribute__((packed))
    {
        uint8_t surah_number;
        uint16_t ayah_count;
        uint16_t first_global_ayah;
        uint8_t start_juz;
        char name_latin[18];
    } bqm_surah_entry_t;

    typedef struct __attribute__((packed))
    {
        uint8_t juz;
        uint8_t hizb_quarter;
        uint8_t mushaf_subpages;
        uint8_t latin_subpages;
        uint8_t trans_subpages;
        uint8_t tafsir_subpages;
        uint16_t reserved;
        uint32_t mushaf_off;
        uint32_t latin_off;
        uint32_t trans_off;
        uint32_t tafsir_off;
    } bqm_ayah_entry_t;

    typedef struct __attribute__((packed))
    {
        uint8_t width;
        uint8_t height;
        int8_t y_offset;
        uint8_t stride_bytes;
        uint32_t bitmap_off;
    } bqm_word_sprite_t;

    typedef struct __attribute__((packed))
    {
        uint16_t word_id;
        uint16_t x;
        uint16_t y;
    } bqm_placed_word_t;

    typedef enum
    {
        MUSHAF_LAYER_MUSHAF = 0,
        MUSHAF_LAYER_LATIN = 1,
        MUSHAF_LAYER_TRANSLATE = 2,
        MUSHAF_LAYER_TAFSIR = 3,
        MUSHAF_LAYER_COUNT = 4,
    } mushaf_layer_t;

    typedef struct __attribute__((packed))
    {
        uint8_t surah;
        uint16_t ayah;
        uint8_t juz;
    } mushaf_bookmark_t;

    typedef struct
    {
        bool started;
        uint8_t surah;
        uint16_t ayah;
        uint8_t layer;
        uint8_t subpage;
        uint8_t bookmark_count;
        mushaf_bookmark_t bookmarks[MUSHAF_MAX_BOOKMARKS];
    } mushaf_state_t;

    esp_err_t mushaf_reader_init(const mushaf_state_t *rtc_state);
    bool mushaf_reader_is_available(void);
    const bqm_header_t *mushaf_reader_get_header(void);
    const mushaf_state_t *mushaf_reader_get_state(void);
    esp_err_t mushaf_reader_save_state(void);
    esp_err_t mushaf_reader_get_surah(uint8_t surah, bqm_surah_entry_t *out);
    esp_err_t mushaf_reader_get_ayah(uint8_t surah, uint16_t ayah, bqm_ayah_entry_t *out, uint16_t *out_global_ayah);
    uint8_t mushaf_reader_get_subpage_count(void);
    esp_err_t mushaf_reader_set_cursor(uint8_t surah, uint16_t ayah, uint8_t layer);
    esp_err_t mushaf_reader_set_bookmarks(const mushaf_bookmark_t *list, uint8_t count);
    bool mushaf_reader_is_bookmarked(uint8_t surah, uint16_t ayah);
    bool mushaf_reader_toggle_bookmark(void);
    esp_err_t mushaf_reader_add_bookmark(uint8_t surah, uint16_t ayah);
    esp_err_t mushaf_reader_delete_bookmark(uint8_t index);
    esp_err_t mushaf_reader_step_page(int8_t dir);
    esp_err_t mushaf_reader_cycle_layer(void);

    /* Text layers (Latin / Translate / Tafsir): current subpage, laid out exactly like
     * the book reader page. ESP_ERR_NOT_SUPPORTED on the mushaf layer. */
    esp_err_t mushaf_reader_get_text_page(text_page_t *out);

    /* Mushaf layer: draws the current subpage's word sprites into the selected canvas,
     * clipped to rows clip_top .. clip_bottom-1. Caller clears the body first.
     * ESP_ERR_NOT_SUPPORTED on text layers. */
    esp_err_t mushaf_reader_draw_mushaf_page(int clip_top, int clip_bottom);

#ifdef __cplusplus
}
#endif

#endif /* MUSHAF_READER_H */