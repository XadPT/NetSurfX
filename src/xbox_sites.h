/*
 * NetSurfX - per-site settings (JavaScript on or off for one site).
 *
 * The list lives in D:\data\js-sites.txt, one site per line:
 *   +example.com    JavaScript always on for this site
 *   -example.com    JavaScript always off for this site
 * A site also covers its subdomains (example.com covers www.example.com).
 * Sites not in the list follow the global JavaScript setting.
 */
#ifndef NSX_XBOX_SITES_H
#define NSX_XBOX_SITES_H

#include <stdbool.h>

struct nsurl;

enum nsx_site_js { NSX_SITE_JS_DEFAULT = 0, NSX_SITE_JS_ON, NSX_SITE_JS_OFF };

/* JavaScript decision for a page: the site's rule, or def. */
bool nsx_site_js_allowed(struct nsurl *url, bool def);

/* The rule stored for the site of url (DEFAULT when none). */
enum nsx_site_js nsx_site_js_get(struct nsurl *url);

/* Stores (or, with DEFAULT, removes) the rule and saves the file. */
bool nsx_site_js_set(struct nsurl *url, enum nsx_site_js mode);

/* The site name used for url ("example.com"), or NULL. Static buffer. */
const char *nsx_site_name(struct nsurl *url);

/* Frees the list (at exit). */
void nsx_sites_fini(void);

#endif
