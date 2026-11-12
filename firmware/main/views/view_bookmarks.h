#ifndef VIEW_BOOKMARKS_H
#define VIEW_BOOKMARKS_H

#include "views.h"

#ifdef __cplusplus
extern "C"
{
#endif

  extern const view_def_t VIEW_DEF_BOOKMARKS;

  int view_bookmarks_get_index(void);
  void view_bookmarks_set_index(int index);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_BOOKMARKS_H */