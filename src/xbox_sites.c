/*
 * NetSurfX - per-site settings (JavaScript on or off for one site).
 * See xbox_sites.h. Used from the main thread only.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <libwapcaplet/libwapcaplet.h>

#include "utils/nsurl.h"
#include "utils/log.h"

#include "xbox_sites.h"

#define SITES_FILE "D:\\data\\js-sites.txt"
#define SITES_MAX 256
#define HOST_MAX 128

struct site {
	char host[HOST_MAX];
	char mode; /* '+' or '-' */
};

static struct site *g_sites;
static int g_count;
static bool g_loaded;

static void sites_load(void)
{
	FILE *f;
	char line[256];

	if (g_loaded)
		return;
	g_loaded = true;
	g_sites = calloc(SITES_MAX, sizeof *g_sites);
	if (g_sites == NULL)
		return;
	f = fopen(SITES_FILE, "r");
	if (f == NULL)
		return;
	while (g_count < SITES_MAX && fgets(line, sizeof line, f) != NULL) {
		char *p = line, *e;
		char mode;

		while (*p == ' ' || *p == '\t') p++;
		if (*p != '+' && *p != '-')
			continue; /* comments and junk */
		mode = *p++;
		while (*p == ' ' || *p == '\t') p++;
		e = p;
		while (*e && !isspace((unsigned char)*e)) {
			*e = (char)tolower((unsigned char)*e);
			e++;
		}
		*e = '\0';
		if (p[0] == '\0' || (size_t)(e - p) >= HOST_MAX)
			continue;
		memcpy(g_sites[g_count].host, p, (size_t)(e - p) + 1);
		g_sites[g_count].mode = mode;
		g_count++;
	}
	fclose(f);
	NSLOG(netsurf, INFO, "Per-site JavaScript rules: %d", g_count);
}

static bool sites_save(void)
{
	FILE *f = fopen(SITES_FILE, "w");

	if (f == NULL)
		return false;
	fputs("# NetSurfX per-site JavaScript: +site = on, -site = off\n", f);
	for (int i = 0; i < g_count; i++)
		fprintf(f, "%c%s\n", g_sites[i].mode, g_sites[i].host);
	return fclose(f) == 0;
}

/* host of url, lower case, in buf; false for URLs without a host */
static bool url_host(struct nsurl *url, char *buf, size_t len)
{
	lwc_string *h;
	size_t n;

	if (url == NULL)
		return false;
	h = nsurl_get_component(url, NSURL_HOST);
	if (h == NULL)
		return false;
	n = lwc_string_length(h);
	if (n == 0 || n >= len) {
		lwc_string_unref(h);
		return false;
	}
	for (size_t i = 0; i < n; i++)
		buf[i] = (char)tolower((unsigned char)lwc_string_data(h)[i]);
	buf[n] = '\0';
	lwc_string_unref(h);
	return true;
}

/* the best (longest) rule for host, or -1 */
static int sites_find(const char *host)
{
	size_t hl = strlen(host);
	int best = -1;
	size_t best_len = 0;

	for (int i = 0; i < g_count; i++) {
		size_t sl = strlen(g_sites[i].host);
		bool match = false;

		if (sl == hl && strcmp(host, g_sites[i].host) == 0)
			match = true;
		else if (sl < hl && host[hl - sl - 1] == '.' &&
			 strcmp(host + hl - sl, g_sites[i].host) == 0)
			match = true;
		if (match && sl > best_len) {
			best = i;
			best_len = sl;
		}
	}
	return best;
}

const char *nsx_site_name(struct nsurl *url)
{
	static char name[HOST_MAX];

	if (!url_host(url, name, sizeof name))
		return NULL;
	if (strncmp(name, "www.", 4) == 0 && name[4] != '\0')
		memmove(name, name + 4, strlen(name + 4) + 1);
	return name;
}

enum nsx_site_js nsx_site_js_get(struct nsurl *url)
{
	char host[HOST_MAX];
	int i;

	sites_load();
	if (g_sites == NULL || !url_host(url, host, sizeof host))
		return NSX_SITE_JS_DEFAULT;
	i = sites_find(host);
	if (i < 0)
		return NSX_SITE_JS_DEFAULT;
	return g_sites[i].mode == '+' ? NSX_SITE_JS_ON : NSX_SITE_JS_OFF;
}

bool nsx_site_js_allowed(struct nsurl *url, bool def)
{
	switch (nsx_site_js_get(url)) {
	case NSX_SITE_JS_ON: return true;
	case NSX_SITE_JS_OFF: return false;
	default: return def;
	}
}

bool nsx_site_js_set(struct nsurl *url, enum nsx_site_js mode)
{
	const char *name;
	int i;

	sites_load();
	name = nsx_site_name(url);
	if (g_sites == NULL || name == NULL)
		return false;

	/* the rule is kept for the site name itself ("example.com") */
	for (i = 0; i < g_count; i++)
		if (strcmp(g_sites[i].host, name) == 0)
			break;
	if (mode == NSX_SITE_JS_DEFAULT) {
		if (i < g_count) {
			memmove(&g_sites[i], &g_sites[i + 1],
				(size_t)(g_count - i - 1) * sizeof *g_sites);
			g_count--;
		}
	} else {
		if (i == g_count) {
			if (g_count >= SITES_MAX)
				return false;
			snprintf(g_sites[i].host, HOST_MAX, "%s", name);
			g_count++;
		}
		g_sites[i].mode = (mode == NSX_SITE_JS_ON) ? '+' : '-';
	}
	return sites_save();
}

void nsx_sites_fini(void)
{
	free(g_sites);
	g_sites = NULL;
	g_count = 0;
	g_loaded = false;
}
