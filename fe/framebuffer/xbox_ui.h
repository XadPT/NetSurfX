/* NetSurfX - ecras proprios da Xbox */
#ifndef NETSURF_FB_XBOX_UI_H
#define NETSURF_FB_XBOX_UI_H

#include <stdbool.h>
#include <stddef.h>

struct browser_window;

/* teclado no ecra; devolve true se confirmado. submit=true se o utilizador pediu "Ir/Enter" */
bool nsx_osk_run(const char *title, const char *initial, char *out, size_t outsz,
		 bool multiline, bool *submit);
void nsx_help_show(void);
void nsx_bookmarks_init(void);
bool nsx_bookmark_add(struct browser_window *bw);
/* Black button: add the page, or offer to remove it (bookmark list on the bookmarks page) */
void nsx_bookmark_button(struct browser_window *bw);
/* screensaver: black screen until any input (the scheduler keeps running) */
void nsx_screensaver_run(void);
void nsx_toast(const char *msg);
/* keep the screen when a menu returns a choice (menus redrawn in a loop) */
void nsx_menu_keep(bool keep);
int nsx_menu(const char *title, const char *const *labels, int n, int sel,
	     const char *hint, int *dir);
const char *nsx_history_page(void);
void nsx_clear_browsing_data(void);
void nsx_check_igr(void);
void nsx_return_to_dashboard(void);
void nsx_before_exit(void);   /* gui.c: cookies e cache */

int nsfb_get_width_safe(void);
int nsfb_get_height_safe(void);

#endif
