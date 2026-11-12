#include "text_source.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "text_source";

esp_err_t text_source_open_file_range(text_source_t *src, const char *path, size_t base, size_t len)
{
  if (!src || !path)
  {
    return ESP_ERR_INVALID_ARG;
  }
  memset(src, 0, sizeof(*src));

  FILE *fp = fopen(path, "rb");
  if (!fp)
  {
    ESP_LOGE(TAG, "open failed: %s", path);
    return ESP_ERR_NOT_FOUND;
  }
  if (fseek(fp, 0, SEEK_END) != 0)
  {
    fclose(fp);
    return ESP_FAIL;
  }
  long fsize = ftell(fp);
  if (fsize < 0)
  {
    fclose(fp);
    return ESP_FAIL;
  }

  size_t total = (size_t)fsize;
  if (base > total)
  {
    base = total;
  }
  if (len == TEXT_SOURCE_TO_END || len > total - base)
  {
    len = total - base;
  }

  src->kind = TEXT_SRC_FILE;
  src->fp = fp;
  src->base = base;
  src->size = len;
  return ESP_OK;
}

esp_err_t text_source_open_file(text_source_t *src, const char *path)
{
  return text_source_open_file_range(src, path, 0, TEXT_SOURCE_TO_END);
}

void text_source_from_mem(text_source_t *src, const void *data, size_t len)
{
  if (!src)
  {
    return;
  }
  memset(src, 0, sizeof(*src));
  if (!data)
  {
    return;
  }
  src->kind = TEXT_SRC_MEM;
  src->data = (const uint8_t *)data;
  src->size = len;
}

void text_source_from_str(text_source_t *src, const char *str)
{
  text_source_from_mem(src, str, str ? strlen(str) : 0);
}

size_t text_source_read(const text_source_t *src, size_t offset, void *buf, size_t len)
{
  if (!src || !buf || len == 0 || offset >= src->size)
  {
    return 0;
  }
  if (len > src->size - offset)
  {
    len = src->size - offset;
  }

  switch (src->kind)
  {
  case TEXT_SRC_MEM:
    memcpy(buf, src->data + offset, len);
    return len;
  case TEXT_SRC_FILE:
    if (fseek(src->fp, (long)(src->base + offset), SEEK_SET) != 0)
    {
      ESP_LOGE(TAG, "fseek failed at %u", (unsigned)(src->base + offset));
      return 0;
    }
    return fread(buf, 1, len, src->fp);
  default:
    return 0;
  }
}

size_t text_source_size(const text_source_t *src)
{
  return src ? src->size : 0;
}

bool text_source_is_open(const text_source_t *src)
{
  return src && src->kind != TEXT_SRC_NONE;
}

void text_source_close(text_source_t *src)
{
  if (!src)
  {
    return;
  }
  if (src->kind == TEXT_SRC_FILE && src->fp)
  {
    fclose(src->fp);
  }
  memset(src, 0, sizeof(*src));
}