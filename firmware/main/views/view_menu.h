#ifndef VIEW_MENU_H
#define VIEW_MENU_H

#include "views.h"

#ifdef __cplusplus
extern "C"
{
#endif

  extern const view_def_t VIEW_DEF_MENU;

  int view_menu_get_index(void);
  void view_menu_set_index(int index);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_MENU_H */