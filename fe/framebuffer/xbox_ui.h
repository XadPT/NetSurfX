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
void nsx_toast(const char *msg);
void nsx_check_igr(void);
void nsx_return_to_dashboard(void);

int nsfb_get_width_safe(void);
int nsfb_get_height_safe(void);

#endif
