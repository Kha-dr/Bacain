#ifndef TEXT_SOURCE_H
#define TEXT_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define TEXT_SOURCE_TO_END SIZE_MAX

  typedef enum
  {
    TEXT_SRC_NONE = 0,
    TEXT_SRC_FILE,
    TEXT_SRC_MEM,
  } text_source_kind_t;

  /* Byte source with local offsets 0..size-1.
   * FILE: [base, base+size) of an open file (whole book, or one ayah/tafsir record).
   * MEM : caller-owned buffer; must outlive the source (static strings, ROM tables).
   * Not thread-safe: caller serializes access (text_pager holds its mutex). */
  typedef struct
  {
    text_source_kind_t kind;
    size_t size;
    FILE *fp;
    size_t base;
    const uint8_t *data;
  } text_source_t;

  esp_err_t text_source_open_file(text_source_t *src, const char *path);
  esp_err_t text_source_open_file_range(text_source_t *src, const char *path, size_t base, size_t len);
  void text_source_from_mem(text_source_t *src, const void *data, size_t len);
  void text_source_from_str(text_source_t *src, const char *str);

  size_t text_source_read(const text_source_t *src, size_t offset, void *buf, size_t len);
  size_t text_source_size(const text_source_t *src);
  bool text_source_is_open(const text_source_t *src);
  void text_source_close(text_source_t *src);

#ifdef __cplusplus
}
#endif

#endif /* TEXT_SOURCE_H */