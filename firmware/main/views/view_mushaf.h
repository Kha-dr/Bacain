#ifndef VIEW_MUSHAF_H
#define VIEW_MUSHAF_H

#include <stdbool.h>
#include "views.h" /* VIEWS_HAS_MUSHAF, view_def_t */

#if VIEWS_HAS_MUSHAF

#ifdef __cplusplus
extern "C"
{
#endif

  /* Owns VIEW_MUSHAF_MENU .. VIEW_MUSHAF_BOOKMARKS. */
  extern const view_def_t VIEW_DEF_MUSHAF;

  void view_mushaf_init(bool is_cold_boot);
  void view_mushaf_save_before_sleep(void);

#ifdef __cplusplus
}
#endif

#endif /* VIEWS_HAS_MUSHAF */

#endif /* VIEW_MUSHAF_H */