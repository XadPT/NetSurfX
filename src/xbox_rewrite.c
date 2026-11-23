/*
 * NetSurfX - request rewriting.
 *
 * Google Search has required JavaScript since January 2025: without it,
 * /search answers with an "enable JavaScript" page whose refresh and
 * consent redirects never reach any results, and on a 64 MB machine the
 * heavy pages around it are a known trigger for running out of memory.
 * Searches (and the Google home page, whose form leads there) are sent to
 * DuckDuckGo's HTML version instead, keeping the query.
 *
 * The function works on plain strings so it can be unit-tested on a PC.
 */
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xbox_rewrite.h"

#define DDG_HTML "https://html.duckduckgo.com/html/"

/* "google.<tld>", "www.google.<tld>", "google.co.<cc>", "www.google.com.<cc>" */
static bool is_google_host(const char *host)
{
	size_t n;
	if (strncasecmp(host, "www.", 4) == 0) host += 4;
	if (strncasecmp(host, "google.", 7) != 0) return false;
	host += 7;
	n = strlen(host);
	if (n < 2 || n > 7) return false;
	for (size_t i = 0; i < n; i++)
		if (!isalpha((unsigned char)host[i]) && host[i] != '.') return false;
	/* "com", "pt", "co.uk", "com.br" - at most one dot */
	{
		const char *dot = strchr(host, '.');
		if (dot && strchr(dot + 1, '.')) return false;
		if (dot == host || host[n - 1] == '.') return false;
	}
	return true;
}

/* value of parameter `name` in a query string ("a=1&q=x"), not decoded */
static bool query_param(const char *q, const char *name, const char **val, size_t *len)
{
	size_t nl = strlen(name);
	while (q && *q) {
		const char *amp = strchr(q, '&');
		size_t seg = amp ? (size_t)(amp - q) : strlen(q);
		if (seg > nl && strncmp(q, name, nl) == 0 && q[nl] == '=') {
			*val = q + nl + 1;
			*len = seg - nl - 1;
			return true;
		}
		q = amp ? amp + 1 : NULL;
	}
	return false;
}

char *nsx_rewrite_url(const char *host, const char *path, const char *query)
{
	const char *q = NULL;
	size_t ql = 0;
	char *out;

	if (host == NULL || path == NULL || !is_google_host(host))
		return NULL;
	if (query && query[0] == '?') query++;

	if (strcmp(path, "/search") == 0) {
		if (query == NULL || !query_param(query, "q", &q, &ql) || ql == 0)
			return strdup(DDG_HTML);
	} else if (strcmp(path, "/") == 0 || strcmp(path, "") == 0 || strcmp(path, "/webhp") == 0) {
		/* the home page: a search typed there would end in /search */
		if (query && query_param(query, "q", &q, &ql) && ql > 0) {
			/* /webhp?q=... and /#q=... style searches */
		} else {
			return strdup(DDG_HTML);
		}
	} else {
		return NULL;
	}

	if (ql > 2000) ql = 2000;
	out = malloc(sizeof DDG_HTML + 3 + ql);
	if (out == NULL) return NULL;
	sprintf(out, "%s?q=%.*s", DDG_HTML, (int)ql, q);
	return out;
}
