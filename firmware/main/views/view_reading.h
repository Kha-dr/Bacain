#ifndef VIEW_READING_H
#define VIEW_READING_H

#include "esp_err.h"
#include "views.h"

#ifdef __cplusplus
extern "C"
{
#endif

  extern const view_def_t VIEW_DEF_READING;

  /* Opens the active book (or the first file in the library) if none is open. */
  esp_err_t view_reading_ensure_book_open(void);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_READING_H */