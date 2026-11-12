#ifndef VIEW_LIBRARY_H
#define VIEW_LIBRARY_H

#include "views.h"

#ifdef __cplusplus
extern "C"
{
#endif

  extern const view_def_t VIEW_DEF_LIBRARY;

  int view_library_get_index(void);
  void view_library_set_index(int index);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_LIBRARY_H */