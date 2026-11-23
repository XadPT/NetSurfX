/*
 * NetSurfX - start-up splash screen.
 *
 * res\splash.png (made by tools/make_splash.py from the NetSurf logo) is
 * shown as soon as the screen exists, while fonts, history and cookies
 * load. It stays at least SPLASH_MIN_MS unless a button or key is pressed.
 * If the file is missing the browser simply starts without it.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <windows.h>

#include "libnsfb.h"
#include "libnsfb_event.h"

#include "utils/log.h"
#include "xbox_splash.h"

#define SPLASH_PATH   "D:\\res\\splash.png"
#define SPLASH_MIN_MS 2000

static DWORD g_shown;
static nsfb_t *g_nsfb;

void nsx_splash_show(nsfb_t *nsfb)
{
	png_image img;
	uint32_t *px = NULL;
	uint8_t *fb;
	int stride, w, h, ox, oy;
	enum nsfb_format_e fmt;

	FILE *fp = fopen(SPLASH_PATH, "rb");

	if (fp == NULL) {
		NSLOG(netsurf, INFO, "[NSX] no splash screen (%s)", SPLASH_PATH);
		return;
	}
	memset(&img, 0, sizeof img);
	img.version = PNG_IMAGE_VERSION;
	if (!png_image_begin_read_from_stdio(&img, fp)) {
		NSLOG(netsurf, INFO, "[NSX] splash screen: %s", img.message);
		fclose(fp);
		return;
	}
	img.format = PNG_FORMAT_BGRA;          /* = XRGB8888 in memory */
	px = malloc(PNG_IMAGE_SIZE(img));
	if (px == NULL || !png_image_finish_read(&img, NULL, px, 0, NULL)) {
		png_image_free(&img);
		free(px);
		fclose(fp);
		return;
	}
	fclose(fp);
	if (nsfb_get_buffer(nsfb, &fb, &stride) != 0 ||
	    nsfb_get_geometry(nsfb, &w, &h, &fmt) != 0 || fb == NULL ||
	    (fmt != NSFB_FMT_XRGB8888 && fmt != NSFB_FMT_ARGB8888)) {
		free(px);
		return;
	}

	/* centre it; outside the picture its edge pixels are repeated, so the
	 * gradient carries on to the screen edges in larger modes (720p) */
	ox = (w - (int)img.width) / 2;
	oy = (h - (int)img.height) / 2;
	for (int y = 0; y < h; y++) {
		uint32_t *row = (uint32_t *)(fb + (size_t)y * stride);
		int sy = y - oy;
		const uint32_t *src;
		if (sy < 0) sy = 0;
		if (sy >= (int)img.height) sy = (int)img.height - 1;
		src = px + (size_t)sy * img.width;
		for (int x = 0; x < w; x++) {
			int sx = x - ox;
			if (sx < 0) sx = 0;
			if (sx >= (int)img.width) sx = (int)img.width - 1;
			row[x] = src[sx] | 0xff000000u;
		}
	}
	free(px);

	{
		nsfb_bbox_t box = { 0, 0, w, h };
		nsfb_update(nsfb, &box);
	}
	g_nsfb = nsfb;
	g_shown = GetTickCount();
}

void nsx_splash_finish(void)
{
	if (g_nsfb == NULL)
		return;
	while (GetTickCount() - g_shown < SPLASH_MIN_MS) {
		nsfb_event_t ev;
		if (nsfb_event(g_nsfb, &ev, 20) &&
		    (ev.type == NSFB_EVENT_KEY_DOWN))
			break;                  /* any button or key skips it */
	}
	g_nsfb = NULL;
}
