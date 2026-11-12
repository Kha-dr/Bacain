#ifndef APP_LINKS_H
#define APP_LINKS_H
/* Connects modules that must not include each other.
 * Call once at boot, after text_pager_init() and before any book is opened. */
void app_links_init(void);
#endif