/* NetSurfX - request rewriting (Google Search -> DuckDuckGo HTML) */
#ifndef NSX_XBOX_REWRITE_H
#define NSX_XBOX_REWRITE_H

/*
 * host: "www.google.com"; path: "/search"; query: "q=x&hl=en" (an optional
 * leading '?' is ignored, may be NULL). Returns a malloc'd absolute URL to
 * fetch instead, or NULL to leave the request alone.
 */
char *nsx_rewrite_url(const char *host, const char *path, const char *query);

#endif
