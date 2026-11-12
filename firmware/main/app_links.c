#include "app_links.h"
#include "text_pager.h"
#include "book_library.h"

static void pager_on_open(const char *path, size_t offset, void *ctx)
{
  (void)ctx;
  book_library_set_active_book(path, false); /* same calls the old open_book made */
  book_library_update_offset(offset);
}

static void pager_on_offset(size_t offset, void *ctx)
{
  (void)ctx;
  book_library_update_offset(offset);
}

void app_links_init(void)
{
  static const text_pager_listener_t k_pager_listener = {
      .on_open = pager_on_open,
      .on_offset = pager_on_offset,
  };
  text_pager_set_listener(&k_pager_listener);
}