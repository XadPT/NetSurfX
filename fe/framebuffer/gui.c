/*
 * Copyright 2008, 2014 Vincent Sanders <vince@netsurf-browser.org>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <limits.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <nsutils/time.h>

#include <SDL.h>
#include <libnsfb.h>
#include <libnsfb_plot.h>
#include <libnsfb_event.h>

#include "utils/utils.h"
#include "utils/nsoption.h"
#include "utils/filepath.h"
#include "utils/log.h"
#include "utils/messages.h"
#include "utils/utf8.h"
#include "netsurf/browser_window.h"
#include "netsurf/keypress.h"
#include "desktop/browser_history.h"
#include "netsurf/plotters.h"
#include "netsurf/window.h"
#include "netsurf/misc.h"
#include "netsurf/netsurf.h"
#include "netsurf/cookie_db.h"
#include "content/fetch.h"
#include "desktop/searchweb.h"

#include "framebuffer/gui.h"
#include "framebuffer/fbtk.h"
#include "framebuffer/framebuffer.h"
#include "framebuffer/schedule.h"
#include "framebuffer/findfile.h"
#include "framebuffer/image_data.h"
#include "framebuffer/font.h"
#include "framebuffer/clipboard.h"
#include "desktop/search.h"
#include "framebuffer/corewindow.h"
#include "html/webfont.h"
#include "content/backing_store.h"
#include "content/urldb.h"
#include "html/box.h"
#include <dom/dom.h>
#include "html/html_save.h"
#include "html/form_internal.h"
#include "content/content.h"
#include "netsurf/content.h"
#include <sys/stat.h>
#include "desktop/scrollbar.h"

#ifdef FB_USE_FREETYPE
extern struct gui_canvas_table *framebuffer_canvas_table;
#endif
#include "framebuffer/fetch.h"
#include "framebuffer/bitmap.h"
#include "framebuffer/local_history.h"
#include "framebuffer/xbox_ui.h"
#include "xbox_kbd.h"
#include "xbox_download.h"
#include "xbox_perf.h"
#include "content/llcache.h"
#include "xbox_splash.h"

#define NSX_VERSION "0.8.4-test"
#include "netsurf/download.h"
#include "desktop/download.h"
#include "framebuffer/xbox_file.h"

#include "nsfb_xbox.h"
#include "xbox_net.h"
#include "xbox_platform.h"


#define NSFB_TOOLBAR_DEFAULT_LAYOUT "blfsrut"

fbtk_widget_t *fbtk;

static bool fb_complete = false;

struct gui_window *input_window = NULL;
struct gui_window *search_current_window;
struct gui_window *window_list = NULL;

/* private data for browser user widget */
struct browser_widget_s {
	struct browser_window *bw; /**< The browser window connected to this gui window */
	int scrollx, scrolly; /**< scroll offsets. */

	/* Pending window redraw state. */
	bool redraw_required; /**< flag indicating the foreground loop
			       * needs to redraw the browser widget.
			       */
	bbox_t redraw_box; /**< Area requiring redraw. */
	bool pan_required; /**< flag indicating the foreground loop
			    * needs to pan the window.
			    */
	int panx, pany; /**< Panning required. */
};

static struct gui_drag {
	enum state {
		GUI_DRAG_NONE,
		GUI_DRAG_PRESSED,
		GUI_DRAG_DRAG
	} state;
	int button;
	int x;
	int y;
	bool grabbed_pointer;
} gui_drag;


/**
 * Cause an abnormal program termination.
 *
 * \note This never returns and is intended to terminate without any cleanup.
 *
 * \param error The message to display to the user.
 */
static void die(const char *error)
{
	NSLOG(netsurf, CRITICAL, "%s", error);
	nsx_fatal(error);
}


/**
 * Warn the user of an event.
 *
 * \param[in] warning A warning looked up in the message translation table
 * \param[in] detail Additional text to be displayed or NULL.
 * \return NSERROR_OK on success or error code if there was a
 *           faliure displaying the message to the user.
 */
static nserror fb_warn_user(const char *warning, const char *detail)
{
	NSLOG(netsurf, INFO, "%s %s", warning, detail);
	return NSERROR_OK;
}

/* queue a redraw operation, co-ordinates are relative to the window */
static void
fb_queue_redraw(struct fbtk_widget_s *widget, int x0, int y0, int x1, int y1)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(widget);

	bwidget->redraw_box.x0 = min(bwidget->redraw_box.x0, x0);
	bwidget->redraw_box.y0 = min(bwidget->redraw_box.y0, y0);
	bwidget->redraw_box.x1 = max(bwidget->redraw_box.x1, x1);
	bwidget->redraw_box.y1 = max(bwidget->redraw_box.y1, y1);

	if (fbtk_clip_to_widget(widget, &bwidget->redraw_box)) {
		bwidget->redraw_required = true;
		fbtk_request_redraw(widget);
	} else {
		bwidget->redraw_box.y0 = bwidget->redraw_box.x0 = INT_MAX;
		bwidget->redraw_box.y1 = bwidget->redraw_box.x1 = -(INT_MAX);
		bwidget->redraw_required = false;
	}
}

/* queue a window scroll */
static void
widget_scroll_y(struct gui_window *gw, int y, bool abs)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(gw->browser);
	int content_width, content_height;
	int height;

	NSLOG(netsurf, DEEPDEBUG, "window scroll");
	if (abs) {
		bwidget->pany = y - bwidget->scrolly;
	} else {
		bwidget->pany += y;
	}

	browser_window_get_extents(gw->bw, true,
			&content_width, &content_height);

	height = fbtk_get_height(gw->browser);

	/* dont pan off the top */
	if ((bwidget->scrolly + bwidget->pany) < 0)
		bwidget->pany = -bwidget->scrolly;

	/* do not pan off the bottom of the content */
	if ((bwidget->scrolly + bwidget->pany) > (content_height - height))
		bwidget->pany = (content_height - height) - bwidget->scrolly;

	if (bwidget->pany == 0)
		return;

	bwidget->pan_required = true;

	fbtk_request_redraw(gw->browser);

	fbtk_set_scroll_position(gw->vscroll, bwidget->scrolly + bwidget->pany);
}

/* queue a window scroll */
static void
widget_scroll_x(struct gui_window *gw, int x, bool abs)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(gw->browser);
	int content_width, content_height;
	int width;

	if (abs) {
		bwidget->panx = x - bwidget->scrollx;
	} else {
		bwidget->panx += x;
	}

	browser_window_get_extents(gw->bw, true,
			&content_width, &content_height);

	width = fbtk_get_width(gw->browser);

	/* dont pan off the left */
	if ((bwidget->scrollx + bwidget->panx) < 0)
		bwidget->panx = - bwidget->scrollx;

	/* do not pan off the right of the content */
	if ((bwidget->scrollx + bwidget->panx) > (content_width - width))
		bwidget->panx = (content_width - width) - bwidget->scrollx;

	if (bwidget->panx == 0)
		return;

	bwidget->pan_required = true;

	fbtk_request_redraw(gw->browser);

	fbtk_set_scroll_position(gw->hscroll, bwidget->scrollx + bwidget->panx);
}

static void
fb_pan(fbtk_widget_t *widget,
       struct browser_widget_s *bwidget,
       struct browser_window *bw)
{
	int x;
	int y;
	int width;
	int height;
	nsfb_bbox_t srcbox;
	nsfb_bbox_t dstbox;

	nsfb_t *nsfb = fbtk_get_nsfb(widget);

	height = fbtk_get_height(widget);
	width = fbtk_get_width(widget);

	NSLOG(netsurf, DEEPDEBUG, "panning %d, %d",
			bwidget->panx, bwidget->pany);

	x = fbtk_get_absx(widget);
	y = fbtk_get_absy(widget);

	/* if the pan exceeds the viewport size just redraw the whole area */
	if (bwidget->pany >= height || bwidget->pany <= -height ||
	    bwidget->panx >= width || bwidget->panx <= -width) {

		bwidget->scrolly += bwidget->pany;
		bwidget->scrollx += bwidget->panx;
		fb_queue_redraw(widget, 0, 0, width, height);

		/* ensure we don't try to scroll again */
		bwidget->panx = 0;
		bwidget->pany = 0;
		bwidget->pan_required = false;
		return;
	}

	if (bwidget->pany < 0) {
		/* pan up by less then viewport height */
		srcbox.x0 = x;
		srcbox.y0 = y;
		srcbox.x1 = srcbox.x0 + width;
		srcbox.y1 = srcbox.y0 + height + bwidget->pany;

		dstbox.x0 = x;
		dstbox.y0 = y - bwidget->pany;
		dstbox.x1 = dstbox.x0 + width;
		dstbox.y1 = dstbox.y0 + height + bwidget->pany;

		/* move part that remains visible up */
		nsfb_plot_copy(nsfb, &srcbox, nsfb, &dstbox);

		/* redraw newly exposed area */
		bwidget->scrolly += bwidget->pany;
		fb_queue_redraw(widget, 0, 0, width, - bwidget->pany);

	} else if (bwidget->pany > 0) {
		/* pan down by less then viewport height */
		srcbox.x0 = x;
		srcbox.y0 = y + bwidget->pany;
		srcbox.x1 = srcbox.x0 + width;
		srcbox.y1 = srcbox.y0 + height - bwidget->pany;

		dstbox.x0 = x;
		dstbox.y0 = y;
		dstbox.x1 = dstbox.x0 + width;
		dstbox.y1 = dstbox.y0 + height - bwidget->pany;

		/* move part that remains visible down */
		nsfb_plot_copy(nsfb, &srcbox, nsfb, &dstbox);

		/* redraw newly exposed area */
		bwidget->scrolly += bwidget->pany;
		fb_queue_redraw(widget, 0, height - bwidget->pany,
				width, height);
	}

	if (bwidget->panx < 0) {
		/* pan left by less then viewport width */
		srcbox.x0 = x;
		srcbox.y0 = y;
		srcbox.x1 = srcbox.x0 + width + bwidget->panx;
		srcbox.y1 = srcbox.y0 + height;

		dstbox.x0 = x - bwidget->panx;
		dstbox.y0 = y;
		dstbox.x1 = dstbox.x0 + width + bwidget->panx;
		dstbox.y1 = dstbox.y0 + height;

		/* move part that remains visible left */
		nsfb_plot_copy(nsfb, &srcbox, nsfb, &dstbox);

		/* redraw newly exposed area */
		bwidget->scrollx += bwidget->panx;
		fb_queue_redraw(widget, 0, 0, -bwidget->panx, height);

	} else if (bwidget->panx > 0) {
		/* pan right by less then viewport width */
		srcbox.x0 = x + bwidget->panx;
		srcbox.y0 = y;
		srcbox.x1 = srcbox.x0 + width - bwidget->panx;
		srcbox.y1 = srcbox.y0 + height;

		dstbox.x0 = x;
		dstbox.y0 = y;
		dstbox.x1 = dstbox.x0 + width - bwidget->panx;
		dstbox.y1 = dstbox.y0 + height;

		/* move part that remains visible right */
		nsfb_plot_copy(nsfb, &srcbox, nsfb, &dstbox);

		/* redraw newly exposed area */
		bwidget->scrollx += bwidget->panx;
		fb_queue_redraw(widget, width - bwidget->panx, 0,
				width, height);
	}

	bwidget->pan_required = false;
	bwidget->panx = 0;
	bwidget->pany = 0;
}

static void
fb_redraw(fbtk_widget_t *widget,
	  struct browser_widget_s *bwidget,
	  struct browser_window *bw)
{
	int x;
	int y;
	int caret_x, caret_y, caret_h;
	struct rect clip;
	struct redraw_context ctx = {
		.interactive = true,
		.background_images = true,
		.plot = &fb_plotters
	};
	nsfb_t *nsfb = fbtk_get_nsfb(widget);

	x = fbtk_get_absx(widget);
	y = fbtk_get_absy(widget);

	/* adjust clipping co-ordinates according to window location */
	bwidget->redraw_box.y0 += y;
	bwidget->redraw_box.y1 += y;
	bwidget->redraw_box.x0 += x;
	bwidget->redraw_box.x1 += x;

	nsfb_claim(nsfb, &bwidget->redraw_box);

	/* redraw bounding box is relative to window */
	clip.x0 = bwidget->redraw_box.x0;
	clip.y0 = bwidget->redraw_box.y0;
	clip.x1 = bwidget->redraw_box.x1;
	clip.y1 = bwidget->redraw_box.y1;

	browser_window_redraw(bw,
			x - bwidget->scrollx,
			y - bwidget->scrolly,
			&clip, &ctx);

	if (fbtk_get_caret(widget, &caret_x, &caret_y, &caret_h)) {
		/* This widget has caret, so render it */
		nsfb_bbox_t line;
		nsfb_plot_pen_t pen;

		line.x0 = x - bwidget->scrollx + caret_x;
		line.y0 = y - bwidget->scrolly + caret_y;
		line.x1 = x - bwidget->scrollx + caret_x;
		line.y1 = y - bwidget->scrolly + caret_y + caret_h;

		pen.stroke_type = NFSB_PLOT_OPTYPE_SOLID;
		pen.stroke_width = 1;
		pen.stroke_colour = 0xFF0000FF;

		nsfb_plot_line(nsfb, &line, &pen);
	}

	nsfb_update(fbtk_get_nsfb(widget), &bwidget->redraw_box);

	bwidget->redraw_box.y0 = bwidget->redraw_box.x0 = INT_MAX;
	bwidget->redraw_box.y1 = bwidget->redraw_box.x1 = INT_MIN;
	bwidget->redraw_required = false;
}

static int
fb_browser_window_redraw(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;
	struct browser_widget_s *bwidget;

	bwidget = fbtk_get_userpw(widget);
	if (bwidget == NULL) {
		NSLOG(netsurf, INFO,
		      "browser widget from widget %p was null", widget);
		return -1;
	}

	/* local history window on top: draw the page once it is closed */
	if (fb_local_history_visible())
		return 0;

	if (bwidget->pan_required) {
		fb_pan(widget, bwidget, gw->bw);
	}

	if (bwidget->redraw_required) {
		fb_redraw(widget, bwidget, gw->bw);
	} else {
		bwidget->redraw_box.x0 = 0;
		bwidget->redraw_box.y0 = 0;
		bwidget->redraw_box.x1 = fbtk_get_width(widget);
		bwidget->redraw_box.y1 = fbtk_get_height(widget);
		fb_redraw(widget, bwidget, gw->bw);
	}
	return 0;
}

static int fb_browser_window_destroy(fbtk_widget_t *widget,
		fbtk_callback_info *cbi)
{
	struct browser_widget_s *browser_widget;

	if (widget == NULL) {
		return 0;
	}

	/* Free private data */
	browser_widget = fbtk_get_userpw(widget);
	free(browser_widget);

	return 0;
}

static void
framebuffer_surface_iterator(void *ctx, const char *name, enum nsfb_type_e type)
{
	const char *arg0 = ctx;

	fprintf(stderr, "%s: %s\n", arg0, name);
}

static enum nsfb_type_e fetype = NSFB_SURFACE_COUNT;
static const char *fename;
static int febpp;
static int fewidth;
static int feheight;
static const char *feurl;

static void
framebuffer_pick_default_fename(void *ctx, const char *name, enum nsfb_type_e type)
{
	if (type < fetype) {
		fename = name;
	}
}

static bool
process_cmdline(int argc, char** argv)
{
	int vw, vh, ox, oy;
	(void)argc; (void)argv;

	fename = "xbox";
	febpp = 32;

	/* 640x480 ja esta activo; 720p so se pedido nas opcoes */
	vw = 640; vh = 480;
	if (nsoption_bool(xbox_hd))
		nsx_video_init(true, &vw, &vh);
	ox = nsoption_int(xbox_overscan_x);
	oy = nsoption_int(xbox_overscan_y);
	if (ox < 0) ox = 0;
	if (oy < 0) oy = 0;
	if (ox > vw / 8) ox = vw / 8;
	if (oy > vh / 8) oy = vh / 8;
	fewidth = vw - 2 * ox;
	feheight = vh - 2 * oy;

	if ((nsoption_charp(homepage_url) != NULL) &&
	    (nsoption_charp(homepage_url)[0] != '\0')) {
		feurl = nsoption_charp(homepage_url);
	} else {
		feurl = NETSURF_HOMEPAGE;
	}
	return true;
}

/**
 * Set option defaults for framebuffer frontend
 *
 * @param defaults The option table to update.
 * @return error status.
 */
/* memory cache sized from the RAM actually free at start-up: an eighth of
 * it, between 3 and 8 MB (64 MB console: ~6 MB; 128 MB console: 8 MB) */
static int auto_memory_cache_size(void)
{
	uint32_t free_kb = nsx_mem_free_kb();
	uint32_t kb = free_kb / 8;
	if (free_kb == 0) kb = 6 * 1024;
	if (kb < 3 * 1024) kb = 3 * 1024;
	if (kb > 8 * 1024) kb = 8 * 1024;
	return (int)(kb * 1024);
}

static nserror set_defaults(struct nsoption_s *defaults)
{
	/* Set defaults for absent option strings */
	nsoption_setnull_charp(cookie_file, strdup("D:\\data\\Cookies"));
	nsoption_setnull_charp(cookie_jar, strdup("D:\\data\\Cookies"));
	nsoption_setnull_charp(homepage_url, strdup("file:///D:/res/home.html"));
	nsoption_setnull_charp(accept_language, strdup("en-US, en"));

	/* Xbox: 64 MB de RAM e um CPU de 733 MHz */
	nsoption_set_int(memory_cache_size, auto_memory_cache_size());
	nsoption_set_int(max_fetchers, 6);
	nsoption_set_int(max_fetchers_per_host, 3);
	/* cache em disco (D:\cache, ao lado do default.xbe): 64 MB chegam e
	 * nao enchem o disco da Xbox; ver Choices para mudar */
	nsoption_set_uint(disc_cache_size, 64 * 1024 * 1024);
	nsoption_set_int(disc_cache_age, 14);
	nsoption_set_int(max_cached_fetch_handles, 0);
	nsoption_set_bool(target_blank, false);          /* uma so janela */
	nsoption_set_bool(block_advertisements, true);
	nsoption_set_bool(search_url_bar, true);
	nsoption_set_bool(animate_images, true);
	nsoption_set_int(font_min_size, 100);             /* legivel numa TV */
	nsoption_set_int(fb_toolbar_size, 32);
	nsoption_set_int(fb_furniture_size, 16);
	nsoption_set_int(fb_font_cachesize, 1536);

	if (nsoption_charp(cookie_file) == NULL ||
	    nsoption_charp(cookie_jar) == NULL) {
		NSLOG(netsurf, INFO, "Failed initialising cookie options");
		return NSERROR_BAD_PARAMETER;
	}

	/* set system colours for framebuffer ui */
	nsoption_set_colour(sys_colour_AccentColor, 0x00666666);
	nsoption_set_colour(sys_colour_AccentColorText, 0x00ffffff);
	nsoption_set_colour(sys_colour_ActiveText, 0x000000ee);
	nsoption_set_colour(sys_colour_ButtonBorder, 0x00aaaaaa);
	nsoption_set_colour(sys_colour_ButtonFace, 0x00dddddd);
	nsoption_set_colour(sys_colour_ButtonText, 0x00000000);
	nsoption_set_colour(sys_colour_Canvas, 0x00aaaaaa);
	nsoption_set_colour(sys_colour_CanvasText, 0x00000000);
	nsoption_set_colour(sys_colour_Field, 0x00f1f1f1);
	nsoption_set_colour(sys_colour_FieldText, 0x00000000);
	nsoption_set_colour(sys_colour_GrayText, 0x00777777);
	nsoption_set_colour(sys_colour_Highlight, 0x00ee0000);
	nsoption_set_colour(sys_colour_HighlightText, 0x00000000);
	nsoption_set_colour(sys_colour_LinkText, 0x00ee0000);
	nsoption_set_colour(sys_colour_Mark, 0x0000ffff);
	nsoption_set_colour(sys_colour_MarkText, 0x00000000);
	nsoption_set_colour(sys_colour_SelectedItem, 0x00e48435);
	nsoption_set_colour(sys_colour_SelectedItemText, 0x00ffffff);
	nsoption_set_colour(sys_colour_VisitedText, 0x008b1a55);

	return NSERROR_OK;
}


/**
 * Ensures output logging stream is correctly configured
 */
static bool nslog_stream_configure(FILE *fptr)
{
	/* tampao: escrever no disco da Xbox linha a linha e muito lento; o
	 * ciclo principal despeja-o a cada segundo (nsx_log_tick) */
	setvbuf(fptr, NULL, _IOFBF, 16 * 1024);
	nsx_log_set_fp(fptr);

	return true;
}

static void fb_update_back_forward(struct gui_window *gw);

/* janela actual (o NetSurfX so usa uma) */
static struct gui_window *fb_current(void)
{
	return window_list;
}

static void fb_go_url_bw(struct browser_window *bw, const char *text)
{
	nsurl *url = NULL;
	nserror err;
	const char *t = text;
	bool looks_like_url;

	while (*t == ' ') t++;
	if (*t == 0) return;
	/* sem espacos e com um ponto (ou esquema/localhost) = endereco; o resto = pesquisa */
	looks_like_url = (strchr(t, ' ') == NULL) &&
		(strchr(t, '.') != NULL || strstr(t, "://") != NULL ||
		 strncmp(t, "about:", 6) == 0 || strncmp(t, "localhost", 9) == 0);
	err = search_web_omni(t, looks_like_url ? SEARCH_WEB_OMNI_NONE : SEARCH_WEB_OMNI_SEARCHONLY, &url);
	if (err != NSERROR_OK) {
		nsx_toast("Invalid address");
		return;
	}
	/* a web address whose host is a single word ("https://google/") can only
	 * be a typing slip on a console: search for the word instead */
	if (looks_like_url && strncmp(t, "about:", 6) != 0 && strncmp(t, "file:", 5) != 0) {
		lwc_string *host = nsurl_get_component(url, NSURL_HOST);
		if (host != NULL) {
			const char *h = lwc_string_data(host);
			size_t hl = lwc_string_length(host);
			bool single = memchr(h, '.', hl) == NULL && memchr(h, ':', hl) == NULL &&
				!(hl == 9 && strncasecmp(h, "localhost", 9) == 0);
			if (single) {
				char word[128];
				nsurl *s2 = NULL;
				snprintf(word, sizeof word, "%.*s", (int)hl, h);
				if (search_web_omni(word, SEARCH_WEB_OMNI_SEARCHONLY, &s2) == NSERROR_OK) {
					nsurl_unref(url);
					url = s2;
				}
			}
			lwc_string_unref(host);
		}
	}
	browser_window_navigate(bw, url, NULL, BW_NAVIGATE_HISTORY, NULL, NULL, NULL);
	nsurl_unref(url);
}

static void fb_go_url(struct gui_window *gw, const char *text)
{
	fb_go_url_bw(gw->bw, text);
}

static void fb_open_url(struct gui_window *gw, const char *u)
{
	nsurl *url;
	if (nsurl_create(u, &url) == NSERROR_OK) {
		browser_window_navigate(gw->bw, url, NULL, BW_NAVIGATE_HISTORY, NULL, NULL, NULL);
		nsurl_unref(url);
	}
}

static void fb_zoom(struct gui_window *gw, float delta)
{
	float sc = browser_window_get_scale(gw->bw) + delta;
	char msg[32];
	if (sc < 0.3f) sc = 0.3f;
	if (sc > 3.0f) sc = 3.0f;
	browser_window_set_scale(gw->bw, sc, true);
	snprintf(msg, sizeof msg, "Zoom %d%%", (int)(sc * 100.0f + 0.5f));
	fbtk_set_text(gw->status, msg);
}

static void widget_scroll_y(struct gui_window *gw, int y, bool abs);
static void widget_scroll_x(struct gui_window *gw, int x, bool abs);

/* ------------------------------------------------------------------ estatisticas no ecra */

static bool g_stats_dirty;
static uint32_t g_stats_last;

/* com a opcao ligada, a barra de estado mostra memoria e tempos quando o
 * browser esta parado (a cada 2 s, ou logo a seguir a carregar uma pagina) */
/*
 * Memory guard. Heavy pages (big news sites) can take most of the 64 MB:
 * below LOW the unused cache is dropped; below CRIT loading of the page is
 * stopped (what has arrived stays on screen) so the console never runs out
 * of memory, which is when network buffers and decoders start failing.
 */
#define MEM_LOW_KB   (12 * 1024)
#define MEM_CRIT_KB  (7 * 1024)

static void fb_memory_guard(void)
{
	static uint32_t last_check, last_purge, last_stop;
	uint32_t now = GetTickCount(), free_kb;
	struct gui_window *gw;

	if (now - last_check < 500) return;
	last_check = now;
	free_kb = nsx_mem_free_kb();
	if (free_kb == 0 || free_kb >= MEM_LOW_KB) return;

	if (now - last_purge > 5000) {
		last_purge = now;
		llcache_clean(true);
		NSLOG(netsurf, WARNING, "[PERF] low memory (%u KB free): cache purged, now %u KB",
		      (unsigned)free_kb, (unsigned)nsx_mem_free_kb());
		free_kb = nsx_mem_free_kb();
	}
	gw = fb_current();
	if (free_kb < MEM_CRIT_KB && gw != NULL && browser_window_stop_available(gw->bw) &&
	    now - last_stop > 10000) {
		last_stop = now;
		browser_window_stop(gw->bw);
		NSLOG(netsurf, WARNING, "[PERF] memory critical (%u KB free): page loading stopped",
		      (unsigned)free_kb);
		nsx_toast("Low memory: stopped loading this page (try its lite/mobile version)");
	}
}

static void fb_stats_tick(void)
{
	fb_memory_guard();

	static unsigned seen_asserts;
	if (nsx_assert_count() != seen_asserts) {
		/* um erro interno foi registado (e ignorado): avisa uma vez */
		if (seen_asserts == 0)
			nsx_toast("Internal error logged to NSX-ERROR.LOG (browser keeps running)");
		seen_asserts = nsx_assert_count();
	}
	struct gui_window *gw = fb_current();
	uint32_t now = GetTickCount();
	char buf[200];
	static char last[200];
	const char *cur;
	if (gw == NULL || !nsoption_bool(xbox_show_stats)) return;
	if (!g_stats_dirty && now - g_stats_last < 3000) return;
	if (nsx_dl_poll(NULL) == NSX_DL_RUNNING) return;   /* a barra mostra o download */
	/* actualizacao periodica so se a barra ainda mostrar as estatisticas
	 * (nao apaga o endereco do link por baixo do ponteiro, etc.) */
	cur = fbtk_get_text(gw->status);
	if (!g_stats_dirty && (cur == NULL || strcmp(cur, last) != 0)) return;
	g_stats_last = now;
	g_stats_dirty = false;
	nsx_perf_status(buf, sizeof buf);
	snprintf(last, sizeof last, "%s", buf);
	fbtk_set_text(gw->status, buf);
}

/* ------------------------------------------------------------------ downloads */

/* pedido de download vindo do nucleo (tratado no ciclo principal) */
static struct { nsurl *url; char *name; unsigned long long size; } g_dl_pending;
static uint32_t g_dl_last_status;

/* nome valido no FATX: max 42 caracteres, sem simbolos proibidos */
static void fatx_name(const char *in, char *out, size_t outsz)
{
	char tmp[256];
	size_t n = 0;
	const char *dot;
	for (const unsigned char *p = (const unsigned char *)in; *p && n < sizeof tmp - 1; p++) {
		unsigned char c = *p;
		if (c >= 0x80) {
			/* acentos (UTF-8): troca por '_' uma vez por caracter */
			if ((c & 0xC0) == 0x80) continue;
			c = '_';
		}
		if (c < 32 || strchr("<>:\"/\\|?*+,;=[]", c)) c = '_';
		tmp[n++] = (char)c;
	}
	tmp[n] = 0;
	if (n == 0) snprintf(tmp, sizeof tmp, "download");
	dot = strrchr(tmp, '.');
	if (strlen(tmp) > 42) {
		/* mantem a extensao */
		size_t el = dot ? strlen(dot) : 0;
		if (el > 10) el = 0;
		snprintf(out, outsz, "%.*s%s", (int)(42 - el), tmp, el ? dot : "");
	} else {
		snprintf(out, outsz, "%s", tmp);
	}
}

static bool path_exists(const char *p)
{
	struct stat st;
	return stat(p, &st) == 0;
}

/* pasta de destino: a das opcoes; se nao der, D:\Downloads */
static const char *dl_dir(void)
{
	static char dir[260];
	const char *opt = nsoption_charp(xbox_download_dir);
	snprintf(dir, sizeof dir, "%s", (opt && opt[0]) ? opt : "E:\\Downloads");
	CreateDirectoryA(dir, NULL);
	if (!path_exists(dir)) {
		snprintf(dir, sizeof dir, "D:\\Downloads");
		CreateDirectoryA(dir, NULL);
	}
	return dir;
}

static void fmt_size(char *b, size_t bs, long long v)
{
	if (v < 0) snprintf(b, bs, "?");
	else if (v < 1024 * 1024) snprintf(b, bs, "%lld KB", (v + 1023) / 1024);
	else snprintf(b, bs, "%lld.%lld MB", v / (1024 * 1024), (v % (1024 * 1024)) * 10 / (1024 * 1024));
}

static struct gui_download_window *fb_download_create(struct download_context *ctx,
						       struct gui_window *parent)
{
	(void)parent;
	if (g_dl_pending.url != NULL) return NULL;
	g_dl_pending.url = nsurl_ref(download_context_get_url(ctx));
	g_dl_pending.name = strdup(download_context_get_filename(ctx) ?
				   download_context_get_filename(ctx) : "download");
	g_dl_pending.size = download_context_get_total_length(ctx);
	/* devolver NULL faz o nucleo cancelar o seu download (que ficaria todo
	 * em memoria); o nosso escreve directamente no disco */
	return NULL;
}

static nserror fb_download_data(struct gui_download_window *dw, const char *data, unsigned int size)
{
	(void)dw; (void)data; (void)size;
	return NSERROR_OK;
}
static void fb_download_error(struct gui_download_window *dw, const char *m) { (void)dw; (void)m; }
static void fb_download_done(struct gui_download_window *dw) { (void)dw; }

static struct gui_download_table download_table = {
	.create = fb_download_create,
	.data = fb_download_data,
	.error = fb_download_error,
	.done = fb_download_done,
};
struct gui_download_table *framebuffer_download_table = &download_table;

static void fb_download_tick(struct gui_window *gw)
{
	struct nsx_dl_info info;
	enum nsx_dl_state st;

	/* novo pedido: confirmar */
	if (g_dl_pending.url != NULL) {
		nsurl *url = g_dl_pending.url;
		char name[64], q[200], sz[32], path[320];
		const char *dir;
		int k = 1;

		g_dl_pending.url = NULL;
		fatx_name(g_dl_pending.name, name, sizeof name);
		free(g_dl_pending.name);
		g_dl_pending.name = NULL;
		fmt_size(sz, sizeof sz, g_dl_pending.size ? (long long)g_dl_pending.size : -1);

		if (nsx_dl_poll(NULL) == NSX_DL_RUNNING) {
			nsx_toast("A download is already in progress");
			nsurl_unref(url);
			return;
		}
		dir = dl_dir();
		{
			char opt[200];
			const char *items[2];
			snprintf(q, sizeof q, "%s (%s)", name, sz);
			snprintf(opt, sizeof opt, "Save to %s", dir);
			items[0] = opt;
			items[1] = "Don\'t save";
			k = nsx_menu(q, items, 2, 0, "Download this file?", NULL);
		}
		if (k == 0) {
			k = 1;
			/* nome livre: "nome (2).ext" se ja existir */
			snprintf(path, sizeof path, "%s\\%s", dir, name);
			while (path_exists(path) && k < 100) {
				const char *dot = strrchr(name, '.');
				int base = dot ? (int)(dot - name) : (int)strlen(name);
				k++;
				if (base > 36) base = 36;
				snprintf(path, sizeof path, "%s\\%.*s(%d)%s", dir, base, name, k, dot ? dot : "");
			}
			if (nsx_dl_begin(url, path, name))
				nsx_toast("Download started (progress is in the Start menu)");
			else
				nsx_toast("Could not start the download");
		}
		nsurl_unref(url);
	}

	st = nsx_dl_poll(&info);
	if (st == NSX_DL_RUNNING) {
		uint32_t now = (uint32_t)GetTickCount();
		if (now - g_dl_last_status > 700) {
			char m[160], a[32], b[32];
			g_dl_last_status = now;
			fmt_size(a, sizeof a, info.got);
			if (info.total > 0) {
				fmt_size(b, sizeof b, info.total);
				snprintf(m, sizeof m, "Download: %s  %d%%  (%s of %s)", info.name,
					 (int)(info.got * 100 / info.total), a, b);
			} else {
				snprintf(m, sizeof m, "Download: %s  %s", info.name, a);
			}
			fbtk_set_text(gw->status, m);
		}
	} else if (st == NSX_DL_DONE || st == NSX_DL_FAILED || st == NSX_DL_CANCELLED) {
		char m[400];
		if (st == NSX_DL_DONE)
			snprintf(m, sizeof m, "Download complete: %s", info.path);
		else if (st == NSX_DL_CANCELLED)
			snprintf(m, sizeof m, "Download cancelled");
		else
			snprintf(m, sizeof m, "Download failed: %s", info.err);
		nsx_dl_ack();
		fbtk_set_text(gw->status, m);
		nsx_toast(m);
	}
}

/* ------------------------------------------------------------------ D-pad: saltar entre links */

/*
 * Navegacao espacial (como nos browsers da Wii/3DS): o D-pad leva o ponteiro
 * ao link ou campo de formulario mais proximo na direccao carregada. Se nao
 * houver nenhum perto, a pagina desloca-se como antes.
 */
struct nav_target { int x0, y0, x1, y1; const void *id; };

#define NAV_MAX 600
static struct nav_target g_nav[NAV_MAX];
static int g_nav_n;

static void nav_add(int x0, int y0, int x1, int y1, const void *id)
{
	/* junta pedacos do mesmo link na mesma zona (texto partido em linhas) */
	for (int i = g_nav_n - 1; i >= 0 && i >= g_nav_n - 8; i--) {
		struct nav_target *t = &g_nav[i];
		/* so na mesma linha (as caixas sobrepoem-se na vertical) */
		if (t->id == id && x0 <= t->x1 + 12 && x1 >= t->x0 - 12 &&
		    y0 < t->y1 - 2 && y1 > t->y0 + 2) {
			if (x0 < t->x0) t->x0 = x0;
			if (y0 < t->y0) t->y0 = y0;
			if (x1 > t->x1) t->x1 = x1;
			if (y1 > t->y1) t->y1 = y1;
			return;
		}
	}
	if (g_nav_n >= NAV_MAX) return;
	g_nav[g_nav_n].x0 = x0; g_nav[g_nav_n].y0 = y0;
	g_nav[g_nav_n].x1 = x1; g_nav[g_nav_n].y1 = y1;
	g_nav[g_nav_n].id = id;
	g_nav_n++;
}

static void nav_collect(struct box *b, int ox, int oy, int depth)
{
	for (; b != NULL; b = b->next) {
		int x = ox + b->x, y = oy + b->y;
		bool target = false;
		const void *id = NULL;

		if (b->style && css_computed_visibility(b->style) == CSS_VISIBILITY_HIDDEN) {
			/* invisivel, mas os filhos podem ser visiveis */
		} else if (b->gadget != NULL && b->type != BOX_TEXT &&
			   b->gadget->type != GADGET_HIDDEN) {
			target = true;
			id = b->gadget;
		} else if (b->href != NULL &&
			   (b->type == BOX_TEXT || b->type == BOX_INLINE_BLOCK ||
			    b->type == BOX_BLOCK || b->object != NULL)) {
			target = true;
			id = b->href;
		}
		if (target && b->width > 0 && b->height > 0) {
			int w = b->width + b->padding[LEFT] + b->padding[RIGHT];
			int h = b->height + b->padding[TOP] + b->padding[BOTTOM];
			nav_add(x, y, x + w, y + h, id);
		}
		if (depth < 200) {
			/* caixas com scroll proprio (overflow) */
			int ssx = b->scroll_x ? scrollbar_get_offset(b->scroll_x) : 0;
			int ssy = b->scroll_y ? scrollbar_get_offset(b->scroll_y) : 0;
			nav_collect(b->children, x - ssx, y - ssy, depth + 1);
		}
	}
}

/* direccao: 0 cima, 1 baixo, 2 esquerda, 3 direita */
static void fb_nav(struct gui_window *gw, int dir)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(gw->browser);
	struct hlcache_handle *h = browser_window_get_content(gw->bw);
	struct box *root;
	float scale = browser_window_get_scale(gw->bw);
	int wx = fbtk_get_absx(gw->browser), wy = fbtk_get_absy(gw->browser);
	int ww = fbtk_get_width(gw->browser), wh = fbtk_get_height(gw->browser);
	int sx = bwidget->scrollx + bwidget->panx, sy = bwidget->scrolly + bwidget->pany;
	int px, py, dpx, dpy, best = -1;
	long best_score = 0;
	const void *cur_id = NULL;
	struct nav_target cur = { 0, 0, 0, 0, NULL };

	if (h == NULL || content_get_type(h) != CONTENT_HTML ||
	    (root = html_get_box_tree(h)) == NULL) {
		goto scroll;
	}
	nsfb_xbox_get_pointer(&px, &py);
	/* ponteiro em coordenadas do documento (sem zoom) */
	dpx = (int)((px - wx + sx) / scale);
	dpy = (int)((py - wy + sy) / scale);

	g_nav_n = 0;
	nav_collect(root, 0, 0, 0);

	for (int i = 0; i < g_nav_n; i++) {
		struct nav_target *t = &g_nav[i];
		if (dpx >= t->x0 && dpx < t->x1 && dpy >= t->y0 && dpy < t->y1) {
			cur = *t;
			cur_id = t->id;
			break;
		}
	}
	if (cur_id == NULL) {
		cur.x0 = cur.x1 = dpx;
		cur.y0 = cur.y1 = dpy;
		if (dir == 1) {
			/* ponteiro fora de um link: "baixo" comeca no canto de cima,
			 * como quem le a pagina (o primeiro link visivel) */
			cur.y0 = cur.y1 = (int)(sy / scale) - 1;
			dpx = (int)(sx / scale);
		}
	}

	{
		/* so procura ate uma "pagina" para la do que se ve, na direccao */
		int vy0 = (int)(sy / scale), vy1 = (int)((sy + wh) / scale);
		int pagey = (int)(wh / scale);

		for (int i = 0; i < g_nav_n; i++) {
			struct nav_target *t = &g_nav[i];
			long primary, secondary, score;
			if (t->id == cur_id && cur_id != NULL) continue;
			switch (dir) {
			case 0:
				if (t->y1 > cur.y0 + 1 || t->y1 < vy0 - pagey / 2) continue;
				primary = cur.y0 - t->y1;
				secondary = t->x1 < dpx ? dpx - t->x1 : (t->x0 > dpx ? t->x0 - dpx : 0);
				break;
			case 1:
				if (t->y0 < cur.y1 - 1 || t->y0 > vy1 + pagey / 2) continue;
				primary = t->y0 - cur.y1;
				secondary = t->x1 < dpx ? dpx - t->x1 : (t->x0 > dpx ? t->x0 - dpx : 0);
				break;
			case 2:
				if (t->x1 > cur.x0 + 1 || t->y1 < vy0 || t->y0 > vy1) continue;
				primary = cur.x0 - t->x1;
				secondary = t->y1 < dpy ? dpy - t->y1 : (t->y0 > dpy ? t->y0 - dpy : 0);
				secondary *= 3;
				break;
			default:
				if (t->x0 < cur.x1 - 1 || t->y1 < vy0 || t->y0 > vy1) continue;
				primary = t->x0 - cur.x1;
				secondary = t->y1 < dpy ? dpy - t->y1 : (t->y0 > dpy ? t->y0 - dpy : 0);
				secondary *= 3;
				break;
			}
			score = primary + 2 * secondary;
			if (best < 0 || score < best_score) {
				best = i;
				best_score = score;
			}
		}
	}

	if (best >= 0) {
		struct nav_target *t = &g_nav[best];
		int tx = t->x0 + ((t->x1 - t->x0) > 40 ? 16 : (t->x1 - t->x0) / 2);
		int ty = (t->y0 + t->y1) / 2;
		int scr_x, scr_y;

		/* se o alvo nao estiver a vista, desloca a pagina ate ele */
		scr_y = (int)(ty * scale) - sy;
		if (scr_y < 8 || scr_y > wh - 8) {
			int want = (int)(ty * scale) - (dir == 0 ? wh * 2 / 3 : wh / 3);
			widget_scroll_y(gw, want < 0 ? 0 : want, true);
		}
		scr_x = (int)(tx * scale) - sx;
		if (scr_x < 4 || scr_x > ww - 4) {
			int want = (int)(tx * scale) - ww / 3;
			widget_scroll_x(gw, want < 0 ? 0 : want, true);
		}
		sx = bwidget->scrollx + bwidget->panx;
		sy = bwidget->scrolly + bwidget->pany;
		scr_x = wx + (int)(tx * scale) - sx;
		scr_y = wy + (int)(ty * scale) - sy;
		if (scr_x < wx) scr_x = wx;
		if (scr_x > wx + ww - 1) scr_x = wx + ww - 1;
		if (scr_y < wy) scr_y = wy;
		if (scr_y > wy + wh - 1) scr_y = wy + wh - 1;
		nsfb_xbox_set_pointer(scr_x, scr_y);
		return;
	}

scroll:
	/* nenhum link nessa direccao: desloca a pagina */
	switch (dir) {
	case 0: widget_scroll_y(gw, -wh / 2, false); break;
	case 1: widget_scroll_y(gw, wh / 2, false); break;
	case 2: widget_scroll_x(gw, -ww / 3, false); break;
	default: widget_scroll_x(gw, ww / 3, false); break;
	}
}

/* ------------------------------------------------------------------ menu Start */

static char g_find[256];        /* ultimo texto procurado na pagina */

static void fb_find(struct gui_window *gw, bool next)
{
	char out[256];
	bool submit;
	if (!next || g_find[0] == 0) {
		if (!nsx_osk_run("Find in page", g_find, out, sizeof out, false, &submit) ||
		    out[0] == 0)
			return;
		snprintf(g_find, sizeof g_find, "%s", out);
		browser_window_search_clear(gw->bw);
	}
	browser_window_search(gw->bw, gw, SEARCH_FLAG_FORWARDS | SEARCH_FLAG_SHOWALL, g_find);
}

static const char *onoff(bool v) { return v ? "On" : "Off"; }

static void fb_save_choices(void)
{
	CreateDirectoryA("D:\\data", NULL);
	nsoption_write("D:\\data\\Choices", NULL, NULL);
}

static void fb_settings_menu(struct gui_window *gw)
{
	enum { S_SEARCH, S_GOOGLE, S_UA, S_DPAD, S_IMAGES, S_ADS, S_HD, S_KBD, S_DLDIR, S_STATS, S_CLEAR, S_COUNT };
	char lbl[S_COUNT][160];
	const char *labels[S_COUNT];
	int sel = 0, dir;
	bool changed = false;

	nsx_menu_keep(true);
	for (;;) {
		const char *prov = nsoption_charp(search_web_provider);
		const char *cur = NULL;
		/* nome do motor actual: o da opcao, ou o primeiro da lista */
		if (prov && prov[0]) cur = prov;
		else search_web_iterate_providers(-1, &cur);
		snprintf(lbl[S_SEARCH], sizeof lbl[0], "Search engine:  %s", cur ? cur : "?");
		snprintf(lbl[S_GOOGLE], sizeof lbl[0], "Google searches:  %s",
			 nsoption_bool(xbox_google_redirect) ? "via DuckDuckGo" : "direct (needs JavaScript)");
		{
			static const char *const uan[] = { "Desktop (NetSurf)", "Mobile", "Lite (old phone)", "Text (Links)" };
			int m = nsoption_int(xbox_ua_mode);
			snprintf(lbl[S_UA], sizeof lbl[0], "Site version:  %s", uan[(m >= 0 && m < NSX_UA_MODES) ? m : 0]);
		}
		snprintf(lbl[S_DPAD], sizeof lbl[0], "D-pad:  %s", nsoption_bool(xbox_dpad_links) ? "jump between links" : "scroll the page");
		snprintf(lbl[S_STATS], sizeof lbl[0], "On-screen statistics:  %s", onoff(nsoption_bool(xbox_show_stats)));
		snprintf(lbl[S_DLDIR], sizeof lbl[0], "Download folder:  %s", nsoption_charp(xbox_download_dir));
		snprintf(lbl[S_IMAGES], sizeof lbl[0], "Show images:  %s", onoff(nsoption_bool(foreground_images)));
		snprintf(lbl[S_ADS], sizeof lbl[0], "Block ads:  %s", onoff(nsoption_bool(block_advertisements)));
		snprintf(lbl[S_HD], sizeof lbl[0], "720p (HD) video:  %s  (after restart)", onoff(nsoption_bool(xbox_hd)));
		snprintf(lbl[S_KBD], sizeof lbl[0], "USB keyboard layout:  %s", nsoption_bool(xbox_kbd_us) ? "US" : "Portuguese");
		snprintf(lbl[S_CLEAR], sizeof lbl[0], "Clear history, cookies and cache");
		for (int i = 0; i < S_COUNT; i++) labels[i] = lbl[i];

		sel = nsx_menu("Settings", labels, S_COUNT, sel,
			       "A / \xe2\x97\x80 \xe2\x96\xb6 change   B back", &dir);
		if (sel < 0) break;
		switch (sel) {
		case S_SEARCH: {
			/* passa ao motor seguinte (ou anterior) da lista */
			const char *names[16], *nm;
			int count = 0, at = 0;
			ssize_t it = search_web_iterate_providers(-1, &nm);
			while (it != -1 && count < 16) {
				names[count] = nm;
				if (cur && strcmp(nm, cur) == 0) at = count;
				count++;
				it = search_web_iterate_providers(it, &nm);
			}
			if (count > 0) {
				at = (at + (dir < 0 ? count - 1 : 1)) % count;
				nsoption_set_charp(search_web_provider, strdup(names[at]));
				search_web_select_provider(names[at]);
			}
			break;
		}
		case S_UA: {
			int m = nsoption_int(xbox_ua_mode) + (dir < 0 ? NSX_UA_MODES - 1 : 1);
			nsoption_set_int(xbox_ua_mode, m % NSX_UA_MODES);
			nsx_set_ua_mode(m % NSX_UA_MODES);
			break;
		}
		case S_GOOGLE:
			nsoption_set_bool(xbox_google_redirect, !nsoption_bool(xbox_google_redirect));
			break;
		case S_STATS:
			nsoption_set_bool(xbox_show_stats, !nsoption_bool(xbox_show_stats));
			break;
		case S_DPAD:
			nsoption_set_bool(xbox_dpad_links, !nsoption_bool(xbox_dpad_links));
			nsfb_xbox_set_dpad_links(nsoption_bool(xbox_dpad_links));
			break;
		case S_DLDIR: {
			static const char *const dirs[] = { "E:\\Downloads", "F:\\Downloads", "G:\\Downloads", "D:\\Downloads" };
			const char *cur = nsoption_charp(xbox_download_dir);
			int at = 0;
			for (int i = 0; i < 4; i++) if (cur && !strcasecmp(cur, dirs[i])) at = i;
			at = (at + (dir < 0 ? 3 : 1)) % 4;
			nsoption_set_charp(xbox_download_dir, strdup(dirs[at]));
			break;
		}
		case S_IMAGES: {
			bool v = !nsoption_bool(foreground_images);
			nsoption_set_bool(foreground_images, v);
			nsoption_set_bool(background_images, v);
			break;
		}
		case S_ADS:
			nsoption_set_bool(block_advertisements, !nsoption_bool(block_advertisements));
			break;
		case S_HD:
			nsoption_set_bool(xbox_hd, !nsoption_bool(xbox_hd));
			break;
		case S_KBD:
			nsoption_set_bool(xbox_kbd_us, !nsoption_bool(xbox_kbd_us));
			nsx_kbd_set_layout(nsoption_bool(xbox_kbd_us) ? NSX_KBD_US : NSX_KBD_PT);
			break;
		case S_CLEAR: {
			static const char *const yn[] = { "No", "Yes, clear on next start" };
			if (nsx_menu("Clear browsing data?", yn, 2, 0, NULL, NULL) == 1) {
				FILE *f = fopen("D:\\data\\CLEAR.TXT", "w");
				if (f) { fputs("1\n", f); fclose(f); }
				nsx_toast("Browsing data will be cleared on the next start");
			}
			break;
		}
		}
		changed = true;
	}
	nsx_menu_keep(false);
	if (changed) {
		fb_save_choices();
		/* imagens/anuncios: aplica-se ao recarregar */
		browser_window_reload(gw->bw, false);
	}
}

static void fb_start_menu(struct gui_window *gw)
{
	enum { M_HOME, M_BOOKMARKS, M_HISTORY, M_FIND, M_FIND_NEXT, M_ZOOM, M_DOWNLOAD,
	       M_SETTINGS, M_HELP, M_EXIT, M_COUNT };
	char dlabel[160];
	struct nsx_dl_info dli;
	char find_next[300];
	char zoom[64];
	const char *labels[M_COUNT];
	int map[M_COUNT], n = 0, sel;
	const char *home = nsoption_charp(homepage_url);

#define ADD(id, txt) do { labels[n] = (txt); map[n] = (id); n++; } while (0)
	ADD(M_HOME, "Home page");
	ADD(M_BOOKMARKS, "Bookmarks");
	ADD(M_HISTORY, "History");
	ADD(M_FIND, "Find in page");
	if (g_find[0]) {
		snprintf(find_next, sizeof find_next, "Find next: \xe2\x80\x9c%.40s\xe2\x80\x9d", g_find);
		ADD(M_FIND_NEXT, find_next);
	}
	snprintf(zoom, sizeof zoom, "Zoom: %d%%  (A resets to 100%%)",
		 (int)(browser_window_get_scale(gw->bw) * 100.0f + 0.5f));
	ADD(M_ZOOM, zoom);
	if (nsx_dl_poll(&dli) == NSX_DL_RUNNING) {
		if (dli.total > 0)
			snprintf(dlabel, sizeof dlabel, "Download %d%%: %.30s  (A to cancel)",
				 (int)(dli.got * 100 / dli.total), dli.name);
		else
			snprintf(dlabel, sizeof dlabel, "Download: %.30s  (A to cancel)", dli.name);
		ADD(M_DOWNLOAD, dlabel);
	}
	ADD(M_SETTINGS, "Settings");
	ADD(M_HELP, "Controller help");
	ADD(M_EXIT, "Exit to dashboard");
#undef ADD

	{
		int dir;
		sel = nsx_menu("NetSurfX", labels, n, 0, "A select   B close   \xe2\x97\x80 \xe2\x96\xb6 zoom", &dir);
		if (sel < 0) return;
		/* esquerda/direita so servem para o zoom */
		if (dir != 0 && map[sel] != M_ZOOM)
			return;
		switch (map[sel]) {
		case M_HOME:
			fb_open_url(gw, (home && home[0]) ? home : NETSURF_HOMEPAGE);
			break;
		case M_BOOKMARKS:
			fb_open_url(gw, "file:///D:/data/bookmarks.html");
			break;
		case M_HISTORY:
			fb_open_url(gw, nsx_history_page());
			break;
		case M_FIND:
			fb_find(gw, false);
			break;
		case M_FIND_NEXT:
			fb_find(gw, true);
			break;
		case M_ZOOM:
			if (dir == 0) {
				browser_window_set_scale(gw->bw, 1.0f, true);
				fbtk_set_text(gw->status, "Zoom 100%");
			} else {
				fb_zoom(gw, dir * 0.1f);
			}
			break;
		case M_DOWNLOAD: {
			static const char *const yn[] = { "No", "Yes, cancel it" };
			if (nsx_menu("Cancel the download?", yn, 2, 0, NULL, NULL) == 1)
				nsx_dl_cancel();
			break;
		}
		case M_SETTINGS:
			fb_settings_menu(gw);
			break;
		case M_HELP:
			nsx_help_show();
			break;
		case M_EXIT: {
			static const char *const yn[] = { "No", "Yes, exit" };
			if (nsx_menu("Exit NetSurfX?", yn, 2, 0, NULL, NULL) == 1)
				nsx_return_to_dashboard();
			break;
		}
		}
	}
}


/* ------------------------------------------------------------------ teclado USB */

static void fb_url_osk(struct gui_window *gw, const char *initial)
{
	char out[2048];
	bool submit;
	if (nsx_osk_run("Address or search", initial ? initial : "", out, sizeof out, false, &submit)) {
		fbtk_set_text(gw->url, out);
		fb_go_url(gw, out);
	}
}

static void fb_handle_keyboard(struct gui_window *gw)
{
	struct nsx_kev k;
	int page = (fbtk_get_height(gw->browser) * 9) / 10;

	while (nsx_kbd_poll(&k)) {
		struct browser_window *bw = gw->bw;
		if (k.type == NSX_KEV_SHORTCUT) {
			switch (k.ucs4) {
			case 'l': fb_url_osk(gw, fbtk_get_text(gw->url)); break;
			case 'r': browser_window_reload(bw, true); break;
			case 'd':
				nsx_toast(nsx_bookmark_add(bw) ? "Added to bookmarks" :
					  "This page cannot be bookmarked");
				break;
			case 'f': fb_find(gw, false); break;
			case 'g': fb_find(gw, true); break;
			case 'h': fb_open_url(gw, nsx_history_page()); break;
			case 'b': fb_open_url(gw, "file:///D:/data/bookmarks.html"); break;
			case '+': fb_zoom(gw, 0.1f); break;
			case '-': fb_zoom(gw, -0.1f); break;
			case '0':
				browser_window_set_scale(bw, 1.0f, true);
				fbtk_set_text(gw->status, "Zoom 100%");
				break;
			default: break;
			}
			continue;
		}
		if (k.type == NSX_KEV_KEY) {
			uint32_t nk = 0;
			if (k.mods & NSX_KM_ALT) {
				if (k.key == NSX_K_LEFT && browser_window_back_available(bw))
					browser_window_history_back(bw, false);
				else if (k.key == NSX_K_RIGHT && browser_window_forward_available(bw))
					browser_window_history_forward(bw, false);
				else if (k.key == NSX_K_HOME) {
					const char *home = nsoption_charp(homepage_url);
					fb_open_url(gw, (home && home[0]) ? home : NETSURF_HOMEPAGE);
				}
				fb_update_back_forward(gw);
				continue;
			}
			switch (k.key) {
			case NSX_K_F1: nsx_help_show(); continue;
			case NSX_K_F3: fb_find(gw, true); continue;
			case NSX_K_F5: browser_window_reload(bw, true); continue;
			case NSX_K_F6: fb_url_osk(gw, fbtk_get_text(gw->url)); continue;
			case NSX_K_F10: fb_start_menu(gw); continue;
			case NSX_K_ESC: browser_window_stop(bw); continue;
			case NSX_K_ENTER: nk = NS_KEY_NL; break;
			case NSX_K_BACKSPACE: nk = NS_KEY_DELETE_LEFT; break;
			case NSX_K_DELETE: nk = NS_KEY_DELETE_RIGHT; break;
			case NSX_K_TAB: nk = (k.mods & NSX_KM_SHIFT) ? NS_KEY_SHIFT_TAB : NS_KEY_TAB; break;
			case NSX_K_LEFT: nk = NS_KEY_LEFT; break;
			case NSX_K_RIGHT: nk = NS_KEY_RIGHT; break;
			case NSX_K_UP: nk = NS_KEY_UP; break;
			case NSX_K_DOWN: nk = NS_KEY_DOWN; break;
			case NSX_K_HOME: nk = NS_KEY_TEXT_START; break;
			case NSX_K_END: nk = NS_KEY_TEXT_END; break;
			case NSX_K_PAGEUP: nk = NS_KEY_PAGE_UP; break;
			case NSX_K_PAGEDOWN: nk = NS_KEY_PAGE_DOWN; break;
			default: continue;
			}
			if (browser_window_key_press(bw, nk))
				continue;
			/* a pagina nao quis a tecla: comportamento de browser */
			switch (k.key) {
			case NSX_K_BACKSPACE:
				if (browser_window_back_available(bw))
					browser_window_history_back(bw, false);
				fb_update_back_forward(gw);
				break;
			case NSX_K_UP: widget_scroll_y(gw, -40, false); break;
			case NSX_K_DOWN: widget_scroll_y(gw, 40, false); break;
			case NSX_K_LEFT: widget_scroll_x(gw, -40, false); break;
			case NSX_K_RIGHT: widget_scroll_x(gw, 40, false); break;
			case NSX_K_PAGEUP: widget_scroll_y(gw, -page, false); break;
			case NSX_K_PAGEDOWN: widget_scroll_y(gw, page, false); break;
			case NSX_K_HOME: widget_scroll_y(gw, 0, true); break;
			case NSX_K_END: widget_scroll_y(gw, 1 << 30, true); break;
			default: break;
			}
			continue;
		}
		if (k.type == NSX_KEV_CHAR) {
			if (browser_window_key_press(bw, k.ucs4))
				continue;
			if (k.ucs4 == ' ') {
				widget_scroll_y(gw, (k.mods & NSX_KM_SHIFT) ? -page : page, false);
				continue;
			}
			/* nenhum campo activo: comecar a escrever abre a barra de endereco */
			{
				char u[8];
				size_t n = utf8_from_ucs4(k.ucs4, u);
				u[n] = 0;
				fb_url_osk(gw, u);
			}
		}
	}
}

static void fb_handle_xbox_input(void)
{
	struct gui_window *gw = fb_current();
	int cmd, dx, dy;

	nsx_check_igr();
	if (gw == NULL) return;

	/* analogico direito: scroll suave na zona por baixo do ponteiro */
	nsfb_xbox_take_scroll(&dx, &dy);
	if (dx != 0 || dy != 0) {
		struct browser_widget_s *bwidget = fbtk_get_userpw(gw->browser);
		int px, py, bx, by;
		nsfb_xbox_get_pointer(&px, &py);
		bx = px - fbtk_get_absx(gw->browser) + bwidget->scrollx;
		by = py - fbtk_get_absy(gw->browser) + bwidget->scrolly;
		if (browser_window_scroll_at_point(gw->bw, bx, by, dx, dy) == false) {
			if (dy) widget_scroll_y(gw, dy, false);
			if (dx) widget_scroll_x(gw, dx, false);
		}
	}

	fb_handle_keyboard(gw);
	fb_download_tick(gw);

	while ((cmd = nsfb_xbox_pop_command()) != NSX_CMD_NONE) {
		/* local history window open: any command but A (a click) closes it */
		if (fb_local_history_visible()) {
			fb_local_history_hide();
			continue;
		}
		switch (cmd) {
		case NSX_CMD_BACK:
			if (browser_window_back_available(gw->bw))
				browser_window_history_back(gw->bw, false);
			fb_update_back_forward(gw);
			break;
		case NSX_CMD_FORWARD:
			if (browser_window_forward_available(gw->bw))
				browser_window_history_forward(gw->bw, false);
			fb_update_back_forward(gw);
			break;
		case NSX_CMD_RELOAD:
			browser_window_reload(gw->bw, true);
			break;
		case NSX_CMD_URL:
			fb_url_osk(gw, fbtk_get_text(gw->url) ? fbtk_get_text(gw->url) : "");
			break;
		case NSX_CMD_MENU:
			fb_start_menu(gw);
			break;
		case NSX_CMD_NAV_UP:    fb_nav(gw, 0); break;
		case NSX_CMD_NAV_DOWN:  fb_nav(gw, 1); break;
		case NSX_CMD_NAV_LEFT:  fb_nav(gw, 2); break;
		case NSX_CMD_NAV_RIGHT: fb_nav(gw, 3); break;
		case NSX_CMD_HELP:
			nsx_help_show();
			break;
		case NSX_CMD_BOOKMARK:
			nsx_toast(nsx_bookmark_add(gw->bw) ? "Added to bookmarks" :
				  "This page cannot be bookmarked");
			break;
		case NSX_CMD_PAGE_UP:
			if (browser_window_key_press(gw->bw, NS_KEY_PAGE_UP) == false)
				widget_scroll_y(gw, -(fbtk_get_height(gw->browser) * 9) / 10, false);
			break;
		case NSX_CMD_PAGE_DOWN:
			if (browser_window_key_press(gw->bw, NS_KEY_PAGE_DOWN) == false)
				widget_scroll_y(gw, (fbtk_get_height(gw->browser) * 9) / 10, false);
			break;
		case NSX_CMD_ZOOM_OUT:
			fb_zoom(gw, -0.1f);
			break;
		case NSX_CMD_ZOOM_IN:
			fb_zoom(gw, 0.1f);
			break;
		default:
			break;
		}
	}
}

static void framebuffer_run(void)
{
	nsfb_event_t event;
	int timeout; /* timeout in miliseconds */

	while (fb_complete != true) {
		/* run the scheduler and discover how long to wait for
		 * the next event.
		 */
		nsx_heartbeat("scheduler");
		timeout = schedule_run();

		/* if redraws are pending do not wait for event,
		 * return immediately
		 */
		if (fbtk_get_redraw_pending(fbtk))
			timeout = 0;

		/* acorda pelo menos a cada 50 ms (IGR, rede, scroll) */
		if (timeout < 0 || timeout > 50)
			timeout = 50;

		nsx_heartbeat("events");
		if (fbtk_event(fbtk, &event, timeout)) {
			if ((event.type == NSFB_EVENT_CONTROL) &&
			    (event.value.controlcode ==  NSFB_CONTROL_QUIT))
				fb_complete = true;
		}

		nsx_heartbeat("controller/keyboard");
		fb_handle_xbox_input();

		nsx_heartbeat("redraw");
		if (fbtk_get_redraw_pending(fbtk)) {
			uint32_t t = GetTickCount();
			fbtk_redraw(fbtk);
			nsx_perf_redraw(GetTickCount() - t);
		}
		nsx_log_tick();
		fb_stats_tick();
	}
}

/* chamado antes de sair para o dashboard (IGR ou fim normal):
 * guarda os cookies e o indice da cache em disco */
void nsx_before_exit(void)
{
	static bool done;
	if (done) return;
	done = true;
	urldb_save_cookies(nsoption_charp(cookie_jar));
	urldb_save("D:\\data\\URLs");
	if (filesystem_llcache_table != NULL &&
	    filesystem_llcache_table->finalise != NULL)
		filesystem_llcache_table->finalise();
	nslog_finalise();
}

static void gui_quit(void)
{
	NSLOG(netsurf, INFO, "gui_quit");

	urldb_save_cookies(nsoption_charp(cookie_jar));

	framebuffer_finalise();
}

/* NetSurfX: hora do ultimo clique (para detectar cliques em campos de texto) */
static uint64_t fb_kbd_armed_time;

static void fb_type_utf8(struct browser_window *bw, const char *t)
{
	size_t len = strlen(t), o = 0;
	while (o < len) {
		uint32_t c = utf8_to_ucs4(t + o, len - o);
		browser_window_key_press(bw, c == '\n' ? NS_KEY_NL : c);
		o = utf8_next(t, len, o);
	}
}

/* abre o teclado no ecra para o campo onde esta o cursor de texto */
static void fb_field_kbd_cb(void *pw)
{
	struct gui_window *g = pw;
	char *cur;
	size_t clen;
	char out[2048];
	bool submit = false;
	SDL_GameController *pad = nsfb_xbox_pad();

	/* o NetSurf tem de receber o "largar" do A antes de o teclado abrir,
	 * senao fica a pensar que o botao continua carregado (arrastar) */
	if (pad != NULL && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A)) {
		framebuffer_schedule(30, fb_field_kbd_cb, g);
		return;
	}

	/* le o texto actual do campo: seleccionar tudo + copiar */
	fb_clipboard_reset();
	browser_window_key_press(g->bw, NS_KEY_SELECT_ALL);
	browser_window_key_press(g->bw, NS_KEY_COPY_SELECTION);
	cur = strdup(fb_clipboard_peek(&clen));
	browser_window_key_press(g->bw, NS_KEY_CLEAR_SELECTION);
	browser_window_key_press(g->bw, NS_KEY_TEXT_END);
	if (cur == NULL) return;

	if (nsx_osk_run("Text field  (Start = submit)", cur, out, sizeof out, true, &submit)) {
		if (strcmp(out, cur) != 0) {
			browser_window_key_press(g->bw, NS_KEY_SELECT_ALL);
			browser_window_key_press(g->bw, NS_KEY_DELETE_LEFT);
			fb_type_utf8(g->bw, out);
		}
		if (submit)
			browser_window_key_press(g->bw, NS_KEY_NL);
	}
	free(cur);
}

/* called back when click in browser window */
static int
fb_browser_window_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;
	struct browser_widget_s *bwidget = fbtk_get_userpw(widget);
	browser_mouse_state mouse;
	int x = cbi->x + bwidget->scrollx;
	int y = cbi->y + bwidget->scrolly;
	uint64_t time_now;
	static struct {
		enum { CLICK_SINGLE, CLICK_DOUBLE, CLICK_TRIPLE } type;
		uint64_t time;
	} last_click;

	if (cbi->event->type != NSFB_EVENT_KEY_DOWN &&
	    cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	NSLOG(netsurf, DEEPDEBUG, "browser window clicked at %d,%d",
			cbi->x, cbi->y);

	switch (cbi->event->type) {
	case NSFB_EVENT_KEY_DOWN:
		switch (cbi->event->value.keycode) {
		case NSFB_KEY_MOUSE_1:
			/* NetSurfX: se o NetSurf puser o cursor de texto ao carregar,
			 * o clique foi num campo: abre o teclado no ecra */
			nsu_getmonotonic_ms(&fb_kbd_armed_time);
			browser_window_mouse_click(gw->bw,
					BROWSER_MOUSE_PRESS_1, x, y);
			gui_drag.state = GUI_DRAG_PRESSED;
			gui_drag.button = 1;
			gui_drag.x = x;
			gui_drag.y = y;
			break;

		case NSFB_KEY_MOUSE_3:
			browser_window_mouse_click(gw->bw,
					BROWSER_MOUSE_PRESS_2, x, y);
			gui_drag.state = GUI_DRAG_PRESSED;
			gui_drag.button = 2;
			gui_drag.x = x;
			gui_drag.y = y;
			break;

		case NSFB_KEY_MOUSE_4:
			/* scroll up */
			if (browser_window_scroll_at_point(gw->bw,
							   x, y,
							   0, -100) == false)
				widget_scroll_y(gw, -100, false);
			break;

		case NSFB_KEY_MOUSE_5:
			/* scroll down */
			if (browser_window_scroll_at_point(gw->bw,
							   x, y,
							   0, 100) == false)
				widget_scroll_y(gw, 100, false);
			break;

		default:
			break;

		}

		break;
	case NSFB_EVENT_KEY_UP:

		mouse = 0;
		nsu_getmonotonic_ms(&time_now);

		switch (cbi->event->value.keycode) {
		case NSFB_KEY_MOUSE_1:
			if (gui_drag.state == GUI_DRAG_DRAG) {
				/* End of a drag, rather than click */

				if (gui_drag.grabbed_pointer) {
					/* need to ungrab pointer */
					fbtk_tgrab_pointer(widget);
					gui_drag.grabbed_pointer = false;
				}

				gui_drag.state = GUI_DRAG_NONE;

				/* Tell core */
				browser_window_mouse_track(gw->bw, 0, x, y);
				break;
			}
			/* This is a click;
			 * clear PRESSED state and pass to core */
			gui_drag.state = GUI_DRAG_NONE;
			mouse = BROWSER_MOUSE_CLICK_1;
			break;

		case NSFB_KEY_MOUSE_3:
			if (gui_drag.state == GUI_DRAG_DRAG) {
				/* End of a drag, rather than click */
				gui_drag.state = GUI_DRAG_NONE;

				if (gui_drag.grabbed_pointer) {
					/* need to ungrab pointer */
					fbtk_tgrab_pointer(widget);
					gui_drag.grabbed_pointer = false;
				}

				/* Tell core */
				browser_window_mouse_track(gw->bw, 0, x, y);
				break;
			}
			/* This is a click;
			 * clear PRESSED state and pass to core */
			gui_drag.state = GUI_DRAG_NONE;
			mouse = BROWSER_MOUSE_CLICK_2;
			break;

		default:
			break;

		}

		/* Determine if it's a double or triple click, allowing
		 * 0.5 seconds (500ms) between clicks
		 */
		if ((time_now < (last_click.time + 500)) &&
		    (cbi->event->value.keycode != NSFB_KEY_MOUSE_4) &&
		    (cbi->event->value.keycode != NSFB_KEY_MOUSE_5)) {
			if (last_click.type == CLICK_SINGLE) {
				/* Set double click */
				mouse |= BROWSER_MOUSE_DOUBLE_CLICK;
				last_click.type = CLICK_DOUBLE;

			} else if (last_click.type == CLICK_DOUBLE) {
				/* Set triple click */
				mouse |= BROWSER_MOUSE_TRIPLE_CLICK;
				last_click.type = CLICK_TRIPLE;
			} else {
				/* Set normal click */
				last_click.type = CLICK_SINGLE;
			}
		} else {
			last_click.type = CLICK_SINGLE;
		}

		if (mouse) {
			browser_window_mouse_click(gw->bw, mouse, x, y);
		}

		last_click.time = time_now;

		break;
	default:
		break;

	}
	return 1;
}

/* called back when movement in browser window */
static int
fb_browser_window_move(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	browser_mouse_state mouse = 0;
	struct gui_window *gw = cbi->context;
	struct browser_widget_s *bwidget = fbtk_get_userpw(widget);
	int x = cbi->x + bwidget->scrollx;
	int y = cbi->y + bwidget->scrolly;

	if (gui_drag.state == GUI_DRAG_PRESSED &&
			(abs(x - gui_drag.x) > 5 ||
			 abs(y - gui_drag.y) > 5)) {
		/* Drag started */
		if (gui_drag.button == 1) {
			browser_window_mouse_click(gw->bw,
					BROWSER_MOUSE_DRAG_1,
					gui_drag.x, gui_drag.y);
		} else {
			browser_window_mouse_click(gw->bw,
					BROWSER_MOUSE_DRAG_2,
					gui_drag.x, gui_drag.y);
		}
		gui_drag.grabbed_pointer = fbtk_tgrab_pointer(widget);
		gui_drag.state = GUI_DRAG_DRAG;
	}

	if (gui_drag.state == GUI_DRAG_DRAG) {
		/* set up mouse state */
		mouse |= BROWSER_MOUSE_DRAG_ON;

		if (gui_drag.button == 1)
			mouse |= BROWSER_MOUSE_HOLDING_1;
		else
			mouse |= BROWSER_MOUSE_HOLDING_2;
	}

	browser_window_mouse_track(gw->bw, mouse, x, y);

	return 0;
}


static int
fb_browser_window_input(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;
	static fbtk_modifier_type modifier = FBTK_MOD_CLEAR;
	int ucs4 = -1;

	NSLOG(netsurf, INFO, "got value %d", cbi->event->value.keycode);

	switch (cbi->event->type) {
	case NSFB_EVENT_KEY_DOWN:
		switch (cbi->event->value.keycode) {

		case NSFB_KEY_DELETE:
			browser_window_key_press(gw->bw, NS_KEY_DELETE_RIGHT);
			break;

		case NSFB_KEY_PAGEUP:
			if (browser_window_key_press(gw->bw,
					NS_KEY_PAGE_UP) == false)
				widget_scroll_y(gw, -fbtk_get_height(
						gw->browser), false);
			break;

		case NSFB_KEY_PAGEDOWN:
			if (browser_window_key_press(gw->bw,
					NS_KEY_PAGE_DOWN) == false)
				widget_scroll_y(gw, fbtk_get_height(
						gw->browser), false);
			break;

		case NSFB_KEY_RIGHT:
			if (modifier & FBTK_MOD_RCTRL ||
					modifier & FBTK_MOD_LCTRL) {
				/* CTRL held */
				if (browser_window_key_press(gw->bw,
						NS_KEY_LINE_END) == false)
					widget_scroll_x(gw, INT_MAX, true);

			} else if (modifier & FBTK_MOD_RSHIFT ||
					modifier & FBTK_MOD_LSHIFT) {
				/* SHIFT held */
				if (browser_window_key_press(gw->bw,
						NS_KEY_WORD_RIGHT) == false)
					widget_scroll_x(gw, fbtk_get_width(
						gw->browser), false);

			} else {
				/* no modifier */
				if (browser_window_key_press(gw->bw,
						NS_KEY_RIGHT) == false)
					widget_scroll_x(gw, 100, false);
			}
			break;

		case NSFB_KEY_LEFT:
			if (modifier & FBTK_MOD_RCTRL ||
					modifier & FBTK_MOD_LCTRL) {
				/* CTRL held */
				if (browser_window_key_press(gw->bw,
						NS_KEY_LINE_START) == false)
					widget_scroll_x(gw, 0, true);

			} else if (modifier & FBTK_MOD_RSHIFT ||
					modifier & FBTK_MOD_LSHIFT) {
				/* SHIFT held */
				if (browser_window_key_press(gw->bw,
						NS_KEY_WORD_LEFT) == false)
					widget_scroll_x(gw, -fbtk_get_width(
						gw->browser), false);

			} else {
				/* no modifier */
				if (browser_window_key_press(gw->bw,
						NS_KEY_LEFT) == false)
					widget_scroll_x(gw, -100, false);
			}
			break;

		case NSFB_KEY_UP:
			if (browser_window_key_press(gw->bw,
					NS_KEY_UP) == false)
				widget_scroll_y(gw, -100, false);
			break;

		case NSFB_KEY_DOWN:
			if (browser_window_key_press(gw->bw,
					NS_KEY_DOWN) == false)
				widget_scroll_y(gw, 100, false);
			break;

		case NSFB_KEY_MINUS:
			if (modifier & FBTK_MOD_RCTRL ||
					modifier & FBTK_MOD_LCTRL) {
				browser_window_set_scale(gw->bw, -0.1, false);
			}
			break;

		case NSFB_KEY_EQUALS: /* PLUS */
			if (modifier & FBTK_MOD_RCTRL ||
					modifier & FBTK_MOD_LCTRL) {
				browser_window_set_scale(gw->bw, 0.1, false);
			}
			break;

		case NSFB_KEY_0:
			if (modifier & FBTK_MOD_RCTRL ||
					modifier & FBTK_MOD_LCTRL) {
				browser_window_set_scale(gw->bw, 1.0, true);
			}
			break;

		case NSFB_KEY_RSHIFT:
			modifier |= FBTK_MOD_RSHIFT;
			break;

		case NSFB_KEY_LSHIFT:
			modifier |= FBTK_MOD_LSHIFT;
			break;

		case NSFB_KEY_RCTRL:
			modifier |= FBTK_MOD_RCTRL;
			break;

		case NSFB_KEY_LCTRL:
			modifier |= FBTK_MOD_LCTRL;
			break;

		case NSFB_KEY_y:
		case NSFB_KEY_z:
			if (cbi->event->value.keycode == NSFB_KEY_z &&
					(modifier & FBTK_MOD_RCTRL ||
					 modifier & FBTK_MOD_LCTRL) &&
					(modifier & FBTK_MOD_RSHIFT ||
					 modifier & FBTK_MOD_LSHIFT)) {
				/* Z pressed with CTRL and SHIFT held */
				browser_window_key_press(gw->bw, NS_KEY_REDO);
				break;

			} else if (cbi->event->value.keycode == NSFB_KEY_z &&
					(modifier & FBTK_MOD_RCTRL ||
					 modifier & FBTK_MOD_LCTRL)) {
				/* Z pressed with CTRL held */
				browser_window_key_press(gw->bw, NS_KEY_UNDO);
				break;

			} else if (cbi->event->value.keycode == NSFB_KEY_y &&
					(modifier & FBTK_MOD_RCTRL ||
					 modifier & FBTK_MOD_LCTRL)) {
				/* Y pressed with CTRL held */
				browser_window_key_press(gw->bw, NS_KEY_REDO);
				break;
			}
			/* Z or Y pressed but not undo or redo; */
			/* Fall through */

		default:
			ucs4 = fbtk_keycode_to_ucs4(cbi->event->value.keycode,
						    modifier);
			if (ucs4 != -1)
				browser_window_key_press(gw->bw, ucs4);
			break;
		}
		break;

	case NSFB_EVENT_KEY_UP:
		switch (cbi->event->value.keycode) {
		case NSFB_KEY_RSHIFT:
			modifier &= ~FBTK_MOD_RSHIFT;
			break;

		case NSFB_KEY_LSHIFT:
			modifier &= ~FBTK_MOD_LSHIFT;
			break;

		case NSFB_KEY_RCTRL:
			modifier &= ~FBTK_MOD_RCTRL;
			break;

		case NSFB_KEY_LCTRL:
			modifier &= ~FBTK_MOD_LCTRL;
			break;

		default:
			break;
		}
		break;

	default:
		break;
	}

	return 0;
}

static void
fb_update_back_forward(struct gui_window *gw)
{
	struct browser_window *bw = gw->bw;

	fbtk_set_bitmap(gw->back,
			(browser_window_back_available(bw)) ?
			&left_arrow : &left_arrow_g);
	fbtk_set_bitmap(gw->forward,
			(browser_window_forward_available(bw)) ?
			&right_arrow : &right_arrow_g);
}

/* left icon click routine */
static int
fb_leftarrow_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;
	struct browser_window *bw = gw->bw;

	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	if (browser_window_back_available(bw))
		browser_window_history_back(bw, false);

	fb_update_back_forward(gw);

	return 1;
}

/* right arrow icon click routine */
static int
fb_rightarrow_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;
	struct browser_window *bw = gw->bw;

	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	if (browser_window_forward_available(bw))
		browser_window_history_forward(bw, false);

	fb_update_back_forward(gw);
	return 1;

}

/* reload icon click routine */
static int
fb_reload_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;

	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	browser_window_reload(bw, true);
	return 1;
}

/* stop icon click routine */
static int
fb_stop_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;

	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	browser_window_stop(bw);
	return 0;
}

static int
fb_osk_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{

	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	map_osk();

	return 0;
}

/* close browser window icon click routine */
static int
fb_close_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	fb_complete = true;

	return 0;
}

static int
fb_scroll_callback(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;

	switch (cbi->type) {
	case FBTK_CBT_SCROLLY:
		widget_scroll_y(gw, cbi->y, true);
		break;

	case FBTK_CBT_SCROLLX:
		widget_scroll_x(gw, cbi->x, true);
		break;

	default:
		break;
	}
	return 0;
}

static int
fb_url_enter(void *pw, char *text)
{
	fb_go_url_bw(pw, text);
	return 0;
}

static int
fb_url_move(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	framebuffer_set_cursor(&caret_image);
	return 0;
}

static int
set_ptr_default_move(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	framebuffer_set_cursor(&pointer_image);
	return 0;
}

static int
fb_localhistory_btn_clik(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;

	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	fb_local_history_present(fbtk, gw->bw);

	return 0;
}


/** Create a toolbar window and populate it with buttons. 
 *
 * The toolbar layout uses a character to define buttons type and position:
 * b - back
 * l - local history
 * f - forward
 * s - stop 
 * r - refresh
 * u - url bar expands to fit remaining space
 * t - throbber/activity indicator
 * c - close the current window
 *
 * The default layout is "blfsrut" there should be no more than a
 * single url bar entry or behaviour will be undefined.
 *
 * @param gw Parent window 
 * @param toolbar_height The height in pixels of the toolbar
 * @param padding The padding in pixels round each element of the toolbar
 * @param frame_col Frame colour.
 * @param toolbar_layout A string defining which buttons and controls
 *                       should be added to the toolbar. May be empty
 *                       string to disable the bar..
 * 
 */
static fbtk_widget_t *
create_toolbar(struct gui_window *gw, 
	       int toolbar_height, 
	       int padding, 
	       colour frame_col,
	       const char *toolbar_layout)
{
	fbtk_widget_t *toolbar;
	fbtk_widget_t *widget;

	int xpos; /* The position of the next widget. */
	int xlhs = 0; /* extent of the left hand side widgets */
	int xdir = 1; /* the direction of movement + or - 1 */
	const char *itmtype; /* type of the next item */

	if (toolbar_layout == NULL) {
		toolbar_layout = NSFB_TOOLBAR_DEFAULT_LAYOUT;
	}

	NSLOG(netsurf, INFO, "Using toolbar layout %s", toolbar_layout);

	itmtype = toolbar_layout;

	/* check for the toolbar being disabled */
	if ((*itmtype == 0) || (*itmtype == 'q')) {
		return NULL;
	}

	toolbar = fbtk_create_window(gw->window, 0, 0, 0, 
				     toolbar_height, 
				     frame_col);

	if (toolbar == NULL) {
		return NULL;
	}

	fbtk_set_handler(toolbar, 
			 FBTK_CBT_POINTERENTER, 
			 set_ptr_default_move, 
			 NULL);


	xpos = padding;

	/* loop proceeds creating widget on the left hand side until
	 * it runs out of layout or encounters a url bar declaration
	 * wherupon it works backwards from the end of the layout
	 * untill the space left is for the url bar
	 */
	while ((itmtype >= toolbar_layout) && 
	       (*itmtype != 0) && 
	       (xdir !=0)) {

		NSLOG(netsurf, INFO, "toolbar adding %c", *itmtype);


		switch (*itmtype) {

		case 'b': /* back */
			widget = fbtk_create_button(toolbar, 
						    (xdir == 1) ? xpos : 
						     xpos - left_arrow.width, 
						    padding, 
						    left_arrow.width, 
						    -padding, 
						    frame_col, 
						    &left_arrow, 
						    fb_leftarrow_click, 
						    gw);
			gw->back = widget; /* keep reference */
			break;

		case 'l': /* local history */
			widget = fbtk_create_button(toolbar,
						    (xdir == 1) ? xpos : 
						     xpos - history_image.width,
						    padding,
						    history_image.width,
						    -padding,
						    frame_col,
						    &history_image,
						    fb_localhistory_btn_clik,
						    gw);
			gw->history = widget;
			break;

		case 'f': /* forward */
			widget = fbtk_create_button(toolbar,
						    (xdir == 1)?xpos : 
						     xpos - right_arrow.width,
						    padding,
						    right_arrow.width,
						    -padding,
						    frame_col,
						    &right_arrow,
						    fb_rightarrow_click,
						    gw);
			gw->forward = widget;
			break;

		case 'c': /* close the current window */
			widget = fbtk_create_button(toolbar,
						    (xdir == 1)?xpos : 
						     xpos - stop_image_g.width,
						    padding,
						    stop_image_g.width,
						    -padding,
						    frame_col,
						    &stop_image_g,
						    fb_close_click,
						    gw->bw);
			gw->close = widget;
			break;

		case 's': /* stop  */
			widget = fbtk_create_button(toolbar,
						    (xdir == 1)?xpos : 
						     xpos - stop_image.width,
						    padding,
						    stop_image.width,
						    -padding,
						    frame_col,
						    &stop_image,
						    fb_stop_click,
						    gw->bw);
			gw->stop = widget;
			break;

		case 'r': /* reload */
			widget = fbtk_create_button(toolbar,
						    (xdir == 1)?xpos : 
						     xpos - reload.width,
						    padding,
						    reload.width,
						    -padding,
						    frame_col,
						    &reload,
						    fb_reload_click,
						    gw->bw);
			gw->reload = widget;
			break;

		case 't': /* throbber/activity indicator */
			widget = fbtk_create_bitmap(toolbar,
						    (xdir == 1)?xpos : 
						     xpos - throbber0.width,
						    padding,
						    throbber0.width,
						    -padding,
						    frame_col, 
						    &throbber0);
			gw->throbber = widget;
			break;


		case 'u': /* url bar*/
			if (xdir == -1) {
				/* met the u going backwards add url
				 * now we know available extent 
				 */ 

				widget = fbtk_create_writable_text(toolbar,
						   xlhs,
						   padding,
						   xpos - xlhs,
						   -padding,
						   FB_COLOUR_WHITE,
						   FB_COLOUR_BLACK,
						   true,
						   fb_url_enter,
						   gw->bw);

				fbtk_set_handler(widget, 
						 FBTK_CBT_POINTERENTER, 
						 fb_url_move, gw->bw);

				gw->url = widget; /* keep reference */

				/* toolbar is complete */
				xdir = 0;
				break;
			}
			/* met url going forwards, note position and
			 * reverse direction 
			 */
			itmtype = toolbar_layout + strlen(toolbar_layout);
			xdir = -1;
			xlhs = xpos;
			xpos = (2 * fbtk_get_width(toolbar));
			widget = toolbar;
			break;

		default:
			widget = NULL;
			xdir = 0;
			NSLOG(netsurf, INFO,
			      "Unknown element %c in toolbar layout",
			      *itmtype);
		        break;

		}

		if (widget != NULL) {
			xpos += (xdir * (fbtk_get_width(widget) + padding));
		}

		NSLOG(netsurf, INFO, "xpos is %d", xpos);

		itmtype += xdir;
	}

	fbtk_set_mapping(toolbar, true);

	return toolbar;
}


/** Resize a toolbar.
 *
 * @param gw Parent window
 * @param toolbar_height The height in pixels of the toolbar
 * @param padding The padding in pixels round each element of the toolbar
 * @param toolbar_layout A string defining which buttons and controls
 *                       should be added to the toolbar. May be empty
 *                       string to disable the bar.
 */
static void
resize_toolbar(struct gui_window *gw,
	       int toolbar_height,
	       int padding,
	       const char *toolbar_layout)
{
	fbtk_widget_t *widget;

	int xpos; /* The position of the next widget. */
	int xlhs = 0; /* extent of the left hand side widgets */
	int xdir = 1; /* the direction of movement + or - 1 */
	const char *itmtype; /* type of the next item */
	int x = 0, y = 0, w = 0, h = 0;

	if (gw->toolbar == NULL) {
		return;
	}

	if (toolbar_layout == NULL) {
		toolbar_layout = NSFB_TOOLBAR_DEFAULT_LAYOUT;
	}

	itmtype = toolbar_layout;

	if (*itmtype == 0) {
		return;
	}

	fbtk_set_pos_and_size(gw->toolbar, 0, 0, 0, toolbar_height);

	xpos = padding;

	/* loop proceeds creating widget on the left hand side until
	 * it runs out of layout or encounters a url bar declaration
	 * wherupon it works backwards from the end of the layout
	 * untill the space left is for the url bar
	 */
	while (itmtype >= toolbar_layout && xdir != 0) {

		switch (*itmtype) {
		case 'b': /* back */
			widget = gw->back;
			x = (xdir == 1) ? xpos : xpos - left_arrow.width;
			y = padding;
			w = left_arrow.width;
			h = -padding;
			break;

		case 'l': /* local history */
			widget = gw->history;
			x = (xdir == 1) ? xpos : xpos - history_image.width;
			y = padding;
			w = history_image.width;
			h = -padding;
			break;

		case 'f': /* forward */
			widget = gw->forward;
			x = (xdir == 1) ? xpos : xpos - right_arrow.width;
			y = padding;
			w = right_arrow.width;
			h = -padding;
			break;

		case 'c': /* close the current window */
			widget = gw->close;
			x = (xdir == 1) ? xpos : xpos - stop_image_g.width;
			y = padding;
			w = stop_image_g.width;
			h = -padding;
			break;

		case 's': /* stop  */
			widget = gw->stop;
			x = (xdir == 1) ? xpos : xpos - stop_image.width;
			y = padding;
			w = stop_image.width;
			h = -padding;
			break;

		case 'r': /* reload */
			widget = gw->reload;
			x = (xdir == 1) ? xpos : xpos - reload.width;
			y = padding;
			w = reload.width;
			h = -padding;
			break;

		case 't': /* throbber/activity indicator */
			widget = gw->throbber;
			x = (xdir == 1) ? xpos : xpos - throbber0.width;
			y = padding;
			w = throbber0.width;
			h = -padding;
			break;


		case 'u': /* url bar*/
			if (xdir == -1) {
				/* met the u going backwards add url
				 * now we know available extent
				 */
				widget = gw->url;
				x = xlhs;
				y = padding;
				w = xpos - xlhs;
				h = -padding;

				/* toolbar is complete */
				xdir = 0;
				break;
			}
			/* met url going forwards, note position and
			 * reverse direction
			 */
			itmtype = toolbar_layout + strlen(toolbar_layout);
			xdir = -1;
			xlhs = xpos;
			w = fbtk_get_width(gw->toolbar);
			xpos = 2 * w;
			widget = gw->toolbar;
			break;

		default:
			widget = NULL;
		        break;

		}

		if (widget != NULL) {
			if (widget != gw->toolbar)
				fbtk_set_pos_and_size(widget, x, y, w, h);
			xpos += xdir * (w + padding);
		}

		itmtype += xdir;
	}
}

/** Routine called when "stripped of focus" event occours for browser widget.
 *
 * @param widget The widget reciving "stripped of focus" event.
 * @param cbi The callback parameters.
 * @return The callback result.
 */
static int
fb_browser_window_strip_focus(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	fbtk_set_caret(widget, false, 0, 0, 0, NULL);

	return 0;
}

static void
create_browser_widget(struct gui_window *gw, int toolbar_height, int furniture_width)
{
	struct browser_widget_s *browser_widget;
	browser_widget = calloc(1, sizeof(struct browser_widget_s));

	/* NetSurfX: sem barras de scroll (o analogico direito faz scroll), a
	 * pagina ocupa a largura toda; so fica a barra de estado em baixo */
	gw->browser = fbtk_create_user(gw->window,
				       0,
				       toolbar_height,
				       0,
				       -furniture_width,
				       browser_widget);

	fbtk_set_handler(gw->browser, FBTK_CBT_REDRAW, fb_browser_window_redraw, gw);
	fbtk_set_handler(gw->browser, FBTK_CBT_DESTROY, fb_browser_window_destroy, gw);
	fbtk_set_handler(gw->browser, FBTK_CBT_INPUT, fb_browser_window_input, gw);
	fbtk_set_handler(gw->browser, FBTK_CBT_CLICK, fb_browser_window_click, gw);
	fbtk_set_handler(gw->browser, FBTK_CBT_STRIP_FOCUS, fb_browser_window_strip_focus, gw);
	fbtk_set_handler(gw->browser, FBTK_CBT_POINTERMOVE, fb_browser_window_move, gw);
}

static void
resize_browser_widget(struct gui_window *gw, int x, int y,
		int width, int height)
{
	fbtk_set_pos_and_size(gw->browser, x, y, width, height);
	browser_window_schedule_reformat(gw->bw);
}

static void
create_normal_browser_window(struct gui_window *gw, int furniture_width)
{
	fbtk_widget_t *widget;
	fbtk_widget_t *toolbar;
	int statusbar_width = 0;
	int toolbar_height = nsoption_int(fb_toolbar_size);

	NSLOG(netsurf, INFO, "Normal window");

	gw->window = fbtk_create_window(fbtk, 0, 0, 0, 0, 0);

	statusbar_width = fbtk_get_width(gw->window);   /* NetSurfX: largura toda */

	/* toolbar */
	toolbar = create_toolbar(gw, 
				 toolbar_height, 
				 2, 
				 FB_FRAME_COLOUR, 
				 nsoption_charp(fb_toolbar_layout));
	gw->toolbar = toolbar;

	/* set the actually created toolbar height */
	if (toolbar != NULL) {
		toolbar_height = fbtk_get_height(toolbar);
	} else {
		toolbar_height = 0;
	}

	/* status bar */
	gw->status = fbtk_create_text(gw->window,
				      0,
				      fbtk_get_height(gw->window) - furniture_width,
				      statusbar_width, furniture_width,
				      FB_FRAME_COLOUR, FB_COLOUR_BLACK,
				      false);
	fbtk_set_handler(gw->status, FBTK_CBT_POINTERENTER, set_ptr_default_move, NULL);

	NSLOG(netsurf, INFO, "status bar %p at %d,%d", gw->status,
	      fbtk_get_absx(gw->status), fbtk_get_absy(gw->status));

	/* create horizontal scrollbar */
	gw->hscroll = fbtk_create_hscroll(gw->window,
					  statusbar_width,
					  fbtk_get_height(gw->window) - furniture_width,
					  fbtk_get_width(gw->window) - statusbar_width - furniture_width,
					  furniture_width,
					  FB_SCROLL_COLOUR,
					  FB_FRAME_COLOUR,
					  fb_scroll_callback,
					  gw);

	/* fill bottom right area */

	if (nsoption_bool(fb_osk) == true) {
		widget = fbtk_create_text_button(gw->window,
						 fbtk_get_width(gw->window) - furniture_width,
						 fbtk_get_height(gw->window) - furniture_width,
						 furniture_width,
						 furniture_width,
						 FB_FRAME_COLOUR, FB_COLOUR_BLACK,
						 fb_osk_click,
						 NULL);
		widget = fbtk_create_button(gw->window,
				fbtk_get_width(gw->window) - furniture_width,
				fbtk_get_height(gw->window) - furniture_width,
				furniture_width,
				furniture_width,
				FB_FRAME_COLOUR,
				&osk_image,
				fb_osk_click,
				NULL);
	} else {
		widget = fbtk_create_fill(gw->window,
					  fbtk_get_width(gw->window) - furniture_width,
					  fbtk_get_height(gw->window) - furniture_width,
					  furniture_width,
					  furniture_width,
					  FB_FRAME_COLOUR);

		fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, set_ptr_default_move, NULL);
	}

	gw->bottom_right = widget;

	/* create vertical scrollbar */
	gw->vscroll = fbtk_create_vscroll(gw->window,
					  fbtk_get_width(gw->window) - furniture_width,
					  toolbar_height,
					  furniture_width,
					  fbtk_get_height(gw->window) - toolbar_height - furniture_width,
					  FB_SCROLL_COLOUR,
					  FB_FRAME_COLOUR,
					  fb_scroll_callback,
					  gw);

	/* NetSurfX: as barras de scroll existem (o NetSurf actualiza-as) mas nao se veem */
	fbtk_set_mapping(gw->hscroll, false);
	fbtk_set_mapping(gw->vscroll, false);
	fbtk_set_mapping(gw->bottom_right, false);

	/* browser widget */
	create_browser_widget(gw, toolbar_height, nsoption_int(fb_furniture_size));

	/* Give browser_window's user widget input focus */
	fbtk_set_focus(gw->browser);
}

static void
resize_normal_browser_window(struct gui_window *gw, int furniture_width)
{
	bool resized;
	int width, height;
	int statusbar_width;
	int toolbar_height = fbtk_get_height(gw->toolbar);

	/* Resize the main window widget */
	resized = fbtk_set_pos_and_size(gw->window, 0, 0, 0, 0);
	if (!resized)
		return;

	width = fbtk_get_width(gw->window);
	height = fbtk_get_height(gw->window);
	statusbar_width = width;

	resize_toolbar(gw, toolbar_height, 2,
			nsoption_charp(fb_toolbar_layout));
	fbtk_set_pos_and_size(gw->status,
			0, height - furniture_width,
			statusbar_width, furniture_width);
	fbtk_reposition_hscroll(gw->hscroll,
			statusbar_width, height - furniture_width,
			width - statusbar_width - furniture_width,
			furniture_width);
	fbtk_set_pos_and_size(gw->bottom_right,
			width - furniture_width, height - furniture_width,
			furniture_width, furniture_width);
	fbtk_reposition_vscroll(gw->vscroll,
			width - furniture_width,
			toolbar_height, furniture_width,
			height - toolbar_height - furniture_width);
	resize_browser_widget(gw,
			0, toolbar_height,
			width,
			height - furniture_width - toolbar_height);
}

static void gui_window_add_to_window_list(struct gui_window *gw)
{
	gw->next = NULL;
	gw->prev = NULL;

	if (window_list == NULL) {
		window_list = gw;
	} else {
		window_list->prev = gw;
		gw->next = window_list;
		window_list = gw;
	}
}

static void gui_window_remove_from_window_list(struct gui_window *gw)
{
	struct gui_window *list;

	for (list = window_list; list != NULL; list = list->next) {
		if (list != gw)
			continue;

		if (list == window_list) {
			window_list = list->next;
			if (window_list != NULL)
				window_list->prev = NULL;
		} else {
			list->prev->next = list->next;
			if (list->next != NULL) {
				list->next->prev = list->prev;
			}
		}
		break;
	}
}


static struct gui_window *
gui_window_create(struct browser_window *bw,
		struct gui_window *existing,
		gui_window_create_flags flags)
{
	struct gui_window *gw;

	gw = calloc(1, sizeof(struct gui_window));

	if (gw == NULL)
		return NULL;

	/* associate the gui window with the underlying browser window
	 */
	gw->bw = bw;

	create_normal_browser_window(gw, nsoption_int(fb_furniture_size));

	/* map and request redraw of gui window */
	fbtk_set_mapping(gw->window, true);

	/* Add it to the window list */
	gui_window_add_to_window_list(gw);

	return gw;
}

static void
gui_window_destroy(struct gui_window *gw)
{
	gui_window_remove_from_window_list(gw);

	fbtk_destroy_widget(gw->window);

	free(gw);
}


/**
 * Invalidates an area of a framebuffer browser window
 *
 * \param g The netsurf window being invalidated.
 * \param rect area to redraw or NULL for the entire window area
 * \return NSERROR_OK on success or appropriate error code
 */
static nserror
fb_window_invalidate_area(struct gui_window *g, const struct rect *rect)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(g->browser);

	if (rect != NULL) {
		fb_queue_redraw(g->browser,
				rect->x0 - bwidget->scrollx,
				rect->y0 - bwidget->scrolly,
				rect->x1 - bwidget->scrollx,
				rect->y1 - bwidget->scrolly);
	} else {
		fb_queue_redraw(g->browser,
				0,
				0,
				fbtk_get_width(g->browser),
				fbtk_get_height(g->browser));
	}
	return NSERROR_OK;
}

static bool
gui_window_get_scroll(struct gui_window *g, int *sx, int *sy)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(g->browser);

	*sx = bwidget->scrollx;
	*sy = bwidget->scrolly;

	return true;
}

/**
 * Set the scroll position of a framebuffer browser window.
 *
 * Scrolls the viewport to ensure the specified rectangle of the
 *   content is shown. The framebuffer implementation scrolls the contents so
 *   the specified point in the content is at the top of the viewport.
 *
 * \param gw gui_window to scroll
 * \param rect The rectangle to ensure is shown.
 * \return NSERROR_OK on success or apropriate error code.
 */
static nserror
gui_window_set_scroll(struct gui_window *gw, const struct rect *rect)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(gw->browser);

	assert(bwidget);

	widget_scroll_x(gw, rect->x0, true);
	widget_scroll_y(gw, rect->y0, true);

	return NSERROR_OK;
}


/**
 * Find the current dimensions of a framebuffer browser window content area.
 *
 * \param gw The gui window to measure content area of.
 * \param width receives width of window
 * \param height receives height of window
 * \return NSERROR_OK on sucess and width and height updated.
 */
static nserror
gui_window_get_dimensions(struct gui_window *gw, int *width, int *height)
{
	*width = fbtk_get_width(gw->browser);
	*height = fbtk_get_height(gw->browser);

	return NSERROR_OK;
}

static void
gui_window_update_extent(struct gui_window *gw)
{
	int w, h;
	browser_window_get_extents(gw->bw, true, &w, &h);

	fbtk_set_scroll_parameters(gw->hscroll, 0, w,
			fbtk_get_width(gw->browser), 100);

	fbtk_set_scroll_parameters(gw->vscroll, 0, h,
			fbtk_get_height(gw->browser), 100);
}

static void
gui_window_set_status(struct gui_window *g, const char *text)
{
	fbtk_set_text(g->status, text);
}

static void
gui_window_set_pointer(struct gui_window *g, gui_pointer_shape shape)
{
	switch (shape) {
	case GUI_POINTER_POINT:
		framebuffer_set_cursor(&hand_image);
		break;

	case GUI_POINTER_CARET:
		framebuffer_set_cursor(&caret_image);
		break;

	case GUI_POINTER_MENU:
		framebuffer_set_cursor(&menu_image);
		break;

	case GUI_POINTER_PROGRESS:
		framebuffer_set_cursor(&progress_image);
		break;

	case GUI_POINTER_MOVE:
		framebuffer_set_cursor(&move_image);
		break;

	default:
		framebuffer_set_cursor(&pointer_image);
		break;
	}
}

static nserror
gui_window_set_url(struct gui_window *g, nsurl *url)
{
	fbtk_set_text(g->url, nsurl_access(url));
	return NSERROR_OK;
}

static void
throbber_advance(void *pw)
{
	struct gui_window *g = pw;
	struct fbtk_bitmap *image;

	switch (g->throbber_index) {
	case 0:
		image = &throbber1;
		g->throbber_index = 1;
		break;

	case 1:
		image = &throbber2;
		g->throbber_index = 2;
		break;

	case 2:
		image = &throbber3;
		g->throbber_index = 3;
		break;

	case 3:
		image = &throbber4;
		g->throbber_index = 4;
		break;

	case 4:
		image = &throbber5;
		g->throbber_index = 5;
		break;

	case 5:
		image = &throbber6;
		g->throbber_index = 6;
		break;

	case 6:
		image = &throbber7;
		g->throbber_index = 7;
		break;

	case 7:
		image = &throbber8;
		g->throbber_index = 0;
		break;

	default:
		return;
	}

	if (g->throbber_index >= 0) {
		fbtk_set_bitmap(g->throbber, image);
		framebuffer_schedule(100, throbber_advance, g);
	}
}

static void
gui_window_start_throbber(struct gui_window *g)
{
	g->throbber_index = 0;
	framebuffer_schedule(100, throbber_advance, g);
}

static void
gui_window_stop_throbber(struct gui_window *gw)
{
	gw->throbber_index = -1;
	fbtk_set_bitmap(gw->throbber, &throbber0);

	fb_update_back_forward(gw);

}

static void
gui_window_remove_caret_cb(fbtk_widget_t *widget)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(widget);
	int c_x, c_y, c_h;

	if (fbtk_get_caret(widget, &c_x, &c_y, &c_h)) {
		/* browser window already had caret:
		 * redraw its area to remove it first */
		fb_queue_redraw(widget,
				c_x - bwidget->scrollx,
				c_y - bwidget->scrolly,
				c_x + 1 - bwidget->scrollx,
				c_y + c_h - bwidget->scrolly);
	}
}

static void
gui_window_place_caret(struct gui_window *g, int x, int y, int height,
		const struct rect *clip)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(g->browser);

	if (fb_kbd_armed_time != 0) {
		uint64_t now;
		nsu_getmonotonic_ms(&now);
		if (now - fb_kbd_armed_time < 600)
			framebuffer_schedule(40, fb_field_kbd_cb, g);
		fb_kbd_armed_time = 0;
	}

	/* set new pos */
	fbtk_set_caret(g->browser, true, x, y, height,
			gui_window_remove_caret_cb);

	/* redraw new caret pos */
	fb_queue_redraw(g->browser,
			x - bwidget->scrollx,
			y - bwidget->scrolly,
			x + 1 - bwidget->scrollx,
			y + height - bwidget->scrolly);
}

static void
gui_window_remove_caret(struct gui_window *g)
{
	int c_x, c_y, c_h;

	if (fbtk_get_caret(g->browser, &c_x, &c_y, &c_h)) {
		/* browser window owns the caret, so can remove it */
		fbtk_set_caret(g->browser, false, 0, 0, 0, NULL);
	}
}

/**
 * process miscellaneous window events
 *
 * \param gw The window receiving the event.
 * \param event The event code.
 * \return NSERROR_OK when processed ok
 */
static nserror
gui_window_event(struct gui_window *gw, enum gui_window_event event)
{
	switch (event) {
	case GW_EVENT_UPDATE_EXTENT:
		gui_window_update_extent(gw);
		break;

	case GW_EVENT_REMOVE_CARET:
		gui_window_remove_caret(gw);
		break;

	case GW_EVENT_START_THROBBER:
		gui_window_start_throbber(gw);
		nsx_perf_page_start(fbtk_get_text(gw->url));
		if (gw->toolbar) fbtk_request_redraw(gw->toolbar);
		break;

	case GW_EVENT_STOP_THROBBER:
		gui_window_stop_throbber(gw);
		nsx_perf_page_done(fbtk_get_text(gw->url));
		g_stats_dirty = true;
		/* NetSurfX: algumas paginas deixavam os icones da barra por desenhar */
		if (gw->toolbar) fbtk_request_redraw(gw->toolbar);
		break;

	default:
		break;
	}
	return NSERROR_OK;
}

static struct gui_window_table framebuffer_window_table = {
	.create = gui_window_create,
	.destroy = gui_window_destroy,
	.invalidate = fb_window_invalidate_area,
	.get_scroll = gui_window_get_scroll,
	.set_scroll = gui_window_set_scroll,
	.get_dimensions = gui_window_get_dimensions,
	.event = gui_window_event,

	.set_url = gui_window_set_url,
	.set_status = gui_window_set_status,
	.set_pointer = gui_window_set_pointer,
	.place_caret = gui_window_place_caret,
};


static struct gui_misc_table framebuffer_misc_table = {
	.schedule = framebuffer_schedule,

	.quit = gui_quit,
};

/**
 * Entry point from OS.
 *
 * /param argc The number of arguments in the string vector.
 * /param argv The argument string vector.
 * /return The return code to the OS
 */
int
main(int argc, char** argv)
{
	struct browser_window *bw;
	nsurl *url;
	nserror ret;
	nsfb_t *nsfb;
	static char *xargv[] = { "default.xbe", "-V", "D:\\NETSURF.LOG", NULL };
	static char *nargv[] = { "default.xbe", NULL };
	int xargc;
	struct netsurf_table framebuffer_table = {
		.misc = &framebuffer_misc_table,
		.window = &framebuffer_window_table,
		.clipboard = framebuffer_clipboard_table,
		.fetch = framebuffer_fetch_table,
		.file = xbox_file_table,
		.utf8 = framebuffer_utf8_table,
		.bitmap = framebuffer_bitmap_table,
		.layout = framebuffer_layout_table,
		.llcache = filesystem_llcache_table,
		.download = framebuffer_download_table,
		.corewindow = framebuffer_core_window_table,
#ifdef FB_USE_FREETYPE
		.canvas = framebuffer_canvas_table,
#endif
	};
	int vw0, vh0;
	(void)argc; (void)argv;

	/* modo de video primeiro: assim ate as mensagens de erro aparecem */
	nsx_video_init(false, &vw0, &vh0);

	/* a rede arranca ja em segundo plano (o DHCP pode demorar) */
	nsx_net_start();
	nsx_mount_drives();
	/* teclado/rato USB: o controlador HID tem de entrar antes do SDL */
	nsx_usbhid_init();

	ret = netsurf_register(&framebuffer_table);
	if (ret != NSERROR_OK) {
		die("NetSurf operation table failed registration");
	}

	/* caminhos dos recursos: D:\res (e D:\res\fonts). Nao usamos ':' como
	 * separador porque as letras de unidade da Xbox o contem. */
	respaths = calloc(4, sizeof(char *));
	respaths[0] = strdup("D:\\data");
	respaths[1] = strdup("D:\\res");
	respaths[2] = strdup("D:\\res\\fonts");

	/* diagnostico (versao de testes: ligado; NODEBUG.TXT desliga): escreve
	 * NETSURF.LOG; o da sessao anterior fica em NETSURF.OLD */
	if (nsx_debug_enabled()) {
		remove("D:\\NETSURF.OLD");
		rename("D:\\NETSURF.LOG", "D:\\NETSURF.OLD");
		remove("D:\\NETSURF.LOG");
		xargc = 3;
		if (nslog_init(nslog_stream_configure, &xargc, xargv) != NSERROR_OK) {
			char m[160];
			snprintf(m, sizeof m, "NetSurfX %s: could not open D:\\NETSURF.LOG (errno %d); "
				 "running without a log\n\n", NSX_VERSION, errno);
			nsx_diag_note("D:\\NSX-ERROR.LOG", m);
		}
	} else {
		xargc = 1;
		nslog_init(nslog_stream_configure, &xargc, nargv);
	}

	nsx_diag_set_version(NSX_VERSION);
	nsx_watch_start();
	NSLOG(netsurf, INFO, "[NSX] NetSurfX %s, screen %dx%d, free memory %u KB of %u KB",
	      NSX_VERSION, vw0, vh0, (unsigned)nsx_mem_free_kb(), (unsigned)nsx_mem_total_kb());

	/* user options setup */
	ret = nsoption_init(set_defaults, &nsoptions, &nsoptions_default);
	if (ret != NSERROR_OK) {
		die("Options failed to initialise");
	}
	/* opcoes do utilizador (opcional): D:\data\Choices */
	nsoption_read("D:\\data\\Choices", nsoptions);
	if (nsoption_int(memory_cache_size) <= 0)      /* 0 = automatic */
		nsoption_set_int(memory_cache_size, auto_memory_cache_size());
	NSLOG(netsurf, INFO, "[NSX] memory cache %d KB", nsoption_int(memory_cache_size) / 1024);
	nsx_kbd_set_layout(nsoption_bool(xbox_kbd_us) ? NSX_KBD_US : NSX_KBD_PT);

	/* message init */
	ret = messages_add_from_file("D:\\res\\Messages");
	if (ret != NSERROR_OK) {
		NSLOG(netsurf, ERROR, "Message translations failed to load");
	}

	/* common initialisation */
	/* "Apagar dados de navegacao" (pedido no menu): faz-se aqui, no arranque,
	 * antes de a cache em disco ser aberta */
	{
		FILE *fl = fopen("D:\\data\\CLEAR.TXT", "r");
		if (fl != NULL) {
			fclose(fl);
			nsx_clear_browsing_data();
			remove("D:\\data\\CLEAR.TXT");
		}
	}
	/* cache em disco: as paginas e imagens ja vistas carregam do disco */
	ret = netsurf_init(nsoption_uint(disc_cache_size) > 0 ? "D:\\cache" : NULL);
	if (ret != NSERROR_OK) {
		die("NetSurf failed to initialise");
	}
	/* versao dos sites (identificacao do browser), ver Definicoes */
	nsx_set_ua_mode(nsoption_int(xbox_ua_mode));

	/* Override, since we have no support for non-core SELECT menu */
	nsoption_set_bool(core_select_menu, true);

	if (process_cmdline(argc,argv) != true)
		die("unable to process command line.\n");

	nsfb_xbox_register();
	nsfb = framebuffer_initialise(fename, fewidth, feheight, febpp);
	if (nsfb == NULL)
		die("Unable to initialise framebuffer");

	/* splash screen while fonts, history and cookies load */
	nsx_splash_show(nsfb);

	if (fb_font_init() == false)
		die("Could not load the fonts (res\\fonts)");

	/* @font-face: o nucleo entrega as fontes descarregadas ao FreeType */
	html_webfont_set_register_cb(fb_font_register_webfont);

	fbtk = fbtk_init(nsfb);

	fbtk_enable_oskb(fbtk);

	nsx_bookmarks_init();
	search_web_init("D:\\res\\SearchEngines");
	search_web_select_provider(nsoption_charp(search_web_provider));

	urldb_load("D:\\data\\URLs");
	urldb_load_cookies(nsoption_charp(cookie_file));

	nsx_splash_finish();
	framebuffer_set_cursor(&pointer_image);     /* no pointer on the splash */

	/* create an initial browser window */

	NSLOG(netsurf, INFO, "calling browser_window_create");

	ret = nsurl_create(feurl, &url);
	if (ret == NSERROR_OK) {
		ret = browser_window_create(BW_CREATE_HISTORY,
					      url,
					      NULL,
					      NULL,
					      &bw);
		nsurl_unref(url);
	}
	if (ret != NSERROR_OK) {
		fb_warn_user("Errorcode:", messages_get_errorcode(ret));
	} else {
		framebuffer_run();

		browser_window_destroy(bw);
	}

	/* nada de netsurf_exit(): vamos direitos ao dashboard */
	nsx_return_to_dashboard();
	return 0;
}

void gui_resize(fbtk_widget_t *root, int width, int height)
{
	struct gui_window *gw;
	nsfb_t *nsfb = fbtk_get_nsfb(root);

	/* Enforce a minimum */
	if (width < 300)
		width = 300;
	if (height < 200)
		height = 200;

	if (framebuffer_resize(nsfb, width, height, febpp) == false) {
		return;
	}

	fbtk_set_pos_and_size(root, 0, 0, width, height);

	fewidth = width;
	feheight = height;

	for (gw = window_list; gw != NULL; gw = gw->next) {
		resize_normal_browser_window(gw,
				nsoption_int(fb_furniture_size));
	}

	fbtk_request_redraw(root);
}


/*
 * Local Variables:
 * c-basic-offset:8
 * End:
 */
