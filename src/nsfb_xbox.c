/*
 * NetSurfX - superficie libnsfb para a Xbox classica.
 *
 * Desenha numa copia em RAM (a libnsfb precisa de ler o que desenhou para o
 * cursor e para o scroll por copia) e copia so a zona alterada para o ecra,
 * uma vez por volta do ciclo de eventos (licao do port da Switch: apresentar
 * a cada pequena actualizacao torna tudo lento).
 *
 * Entrada: comando via SDL2 GameController.
 *   - analogico esquerdo -> ponteiro (com aceleracao)
 *   - analogico direito  -> scroll suave (lido pelo frontend)
 *   - A -> clique; D-pad -> setas; restantes botoes -> fila de comandos
 * Licao do port da Switch: nunca emitir movimentos repetidos sem mudanca de
 * posicao, senao o ciclo do fbtk (que junta movimentos com timeout 0) nunca
 * acaba.
 *
 * Licenca: MIT (como a libnsfb).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <SDL.h>
#include <hal/video.h>
#include <windows.h>

#include "libnsfb.h"
#include "libnsfb_event.h"
#include "libnsfb_plot.h"
#include "libnsfb_plot_util.h"
#include "nsfb.h"
#include "surface.h"
#include "plot.h"
#include "cursor.h"

#include "nsfb_xbox.h"

#define STICK_DEADZONE   7000
#define TRIGGER_ON       20000
#define TRIGGER_OFF      12000
#define PTR_MAX_PXPS     780.0f   /* velocidade maxima do ponteiro (px/s a 640 de largura) */
#define SCROLL_MAX_PXPS  1400.0f  /* velocidade maxima de scroll (px/s) */
#define REPEAT_DELAY_MS  400
#define REPEAT_RATE_MS   90
#define QUEUE_MAX        32

typedef struct {
	uint8_t *fb;          /* copia em RAM */
	int pitch;
	SDL_GameController *pad;

	nsfb_event_t q[QUEUE_MAX];
	int qh, qn;

	int cmdq[QUEUE_MAX];
	int ch, cn;

	float px, py;         /* ponteiro */
	int last_x, last_y;   /* ultimo movimento emitido */
	float scroll_x, scroll_y; /* scroll pendente (px) */
	uint32_t last_ticks;

	bool lt, rt;          /* gatilhos (estado digital) */
	bool dirty;
	nsfb_bbox_t dbox;     /* zona alterada desde a ultima apresentacao */

	/* repeticao do D-pad */
	int held_key;
	uint32_t held_since, held_last;

	int ox, oy;           /* margem de overscan */
	bool input_blocked;   /* teclado no ecra aberto: o frontend le o comando directamente */
} xb_priv;

static xb_priv *g_xb;     /* so existe uma superficie */

/* ---------------------------------------------------------------- filas */

static void qpush(xb_priv *p, const nsfb_event_t *e)
{
	if (p->qn >= QUEUE_MAX) return;
	p->q[(p->qh + p->qn) % QUEUE_MAX] = *e;
	p->qn++;
}

static bool qpop(xb_priv *p, nsfb_event_t *e)
{
	if (p->qn == 0) return false;
	*e = p->q[p->qh];
	p->qh = (p->qh + 1) % QUEUE_MAX;
	p->qn--;
	return true;
}

static void qkey(xb_priv *p, enum nsfb_event_type_e t, enum nsfb_key_code_e k)
{
	nsfb_event_t e;
	e.type = t;
	e.value.keycode = k;
	qpush(p, &e);
}

static void qmove(xb_priv *p, int x, int y)
{
	nsfb_event_t e;
	e.type = NSFB_EVENT_MOVE_ABSOLUTE;
	e.value.vector.x = x;
	e.value.vector.y = y;
	e.value.vector.z = 0;
	qpush(p, &e);
	p->last_x = x;
	p->last_y = y;
}

static void cmdpush(xb_priv *p, int cmd)
{
	if (p->cn >= QUEUE_MAX) return;
	p->cmdq[(p->ch + p->cn) % QUEUE_MAX] = cmd;
	p->cn++;
}

int nsfb_xbox_pop_command(void)
{
	xb_priv *p = g_xb;
	int c;
	if (p == NULL || p->cn == 0) return NSX_CMD_NONE;
	c = p->cmdq[p->ch];
	p->ch = (p->ch + 1) % QUEUE_MAX;
	p->cn--;
	return c;
}

void nsfb_xbox_take_scroll(int *dx, int *dy)
{
	xb_priv *p = g_xb;
	*dx = *dy = 0;
	if (p == NULL) return;
	*dx = (int)p->scroll_x;
	*dy = (int)p->scroll_y;
	p->scroll_x -= *dx;
	p->scroll_y -= *dy;
}

void nsfb_xbox_get_pointer(int *x, int *y)
{
	*x = g_xb ? (int)g_xb->px : 0;
	*y = g_xb ? (int)g_xb->py : 0;
}

void nsfb_xbox_set_pointer(int x, int y)
{
	if (g_xb == NULL) return;
	g_xb->px = (float)x;
	g_xb->py = (float)y;
	qmove(g_xb, x, y);
}

void nsfb_xbox_block_input(bool block)
{
	if (g_xb == NULL) return;
	g_xb->input_blocked = block;
	g_xb->qn = 0;
	g_xb->cn = 0;
	g_xb->scroll_x = g_xb->scroll_y = 0;
}

SDL_GameController *nsfb_xbox_pad(void)
{
	return g_xb ? g_xb->pad : NULL;
}

/* ---------------------------------------------------------------- comando */

static float curve(float v)
{
	/* zona morta e curva quadratica: movimentos pequenos sao precisos */
	float a = fabsf(v);
	float d = STICK_DEADZONE / 32767.0f;
	if (a <= d) return 0.0f;
	a = (a - d) / (1.0f - d);
	if (a > 1.0f) a = 1.0f;
	a = a * a;
	return v < 0 ? -a : a;
}

static void open_pad(xb_priv *p)
{
	if (p->pad) return;
	for (int i = 0; i < SDL_NumJoysticks(); i++) {
		if (SDL_IsGameController(i)) {
			p->pad = SDL_GameControllerOpen(i);
			if (p->pad) return;
		}
	}
}

static void pump_sticks(nsfb_t *nsfb, xb_priv *p)
{
	uint32_t now = SDL_GetTicks();
	float dt = (now - p->last_ticks) / 1000.0f;
	p->last_ticks = now;
	if (dt > 0.1f) dt = 0.1f;
	if (p->pad == NULL || p->input_blocked) return;

	float lx = curve(SDL_GameControllerGetAxis(p->pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f);
	float ly = curve(SDL_GameControllerGetAxis(p->pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f);
	float rx = curve(SDL_GameControllerGetAxis(p->pad, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f);
	float ry = curve(SDL_GameControllerGetAxis(p->pad, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f);
	float scale = nsfb->width / 640.0f;

	if (lx != 0.0f || ly != 0.0f) {
		p->px += lx * PTR_MAX_PXPS * scale * dt;
		p->py += ly * PTR_MAX_PXPS * scale * dt;
		if (p->px < 0) p->px = 0;
		if (p->py < 0) p->py = 0;
		if (p->px > nsfb->width - 1) p->px = (float)(nsfb->width - 1);
		if (p->py > nsfb->height - 1) p->py = (float)(nsfb->height - 1);
		if ((int)p->px != p->last_x || (int)p->py != p->last_y)
			qmove(p, (int)p->px, (int)p->py);
	}

	p->scroll_x += rx * SCROLL_MAX_PXPS * dt;
	p->scroll_y += ry * SCROLL_MAX_PXPS * dt;

	/* gatilhos analogicos com histerese: pagina acima/abaixo */
	int lt = SDL_GameControllerGetAxis(p->pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
	int rt = SDL_GameControllerGetAxis(p->pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
	bool ltn = p->lt ? (lt > TRIGGER_OFF) : (lt > TRIGGER_ON);
	bool rtn = p->rt ? (rt > TRIGGER_OFF) : (rt > TRIGGER_ON);
	if (ltn && !p->lt) cmdpush(p, NSX_CMD_PAGE_UP);
	if (rtn && !p->rt) cmdpush(p, NSX_CMD_PAGE_DOWN);
	p->lt = ltn;
	p->rt = rtn;

	/* repeticao do D-pad enquanto premido */
	if (p->held_key && now - p->held_since > REPEAT_DELAY_MS &&
	    now - p->held_last > REPEAT_RATE_MS) {
		qkey(p, NSFB_EVENT_KEY_DOWN, p->held_key);
		qkey(p, NSFB_EVENT_KEY_UP, p->held_key);
		p->held_last = now;
	}
}

static bool stick_active(xb_priv *p)
{
	if (p->pad == NULL || p->input_blocked) return false;
	return abs(SDL_GameControllerGetAxis(p->pad, SDL_CONTROLLER_AXIS_LEFTX)) > STICK_DEADZONE ||
	       abs(SDL_GameControllerGetAxis(p->pad, SDL_CONTROLLER_AXIS_LEFTY)) > STICK_DEADZONE ||
	       abs(SDL_GameControllerGetAxis(p->pad, SDL_CONTROLLER_AXIS_RIGHTX)) > STICK_DEADZONE ||
	       abs(SDL_GameControllerGetAxis(p->pad, SDL_CONTROLLER_AXIS_RIGHTY)) > STICK_DEADZONE ||
	       p->held_key != 0;
}

static void handle_sdl(xb_priv *p, const SDL_Event *ev)
{
	switch (ev->type) {
	case SDL_CONTROLLERDEVICEADDED:
		open_pad(p);
		return;

	case SDL_CONTROLLERDEVICEREMOVED:
		if (p->pad && !SDL_GameControllerGetAttached(p->pad)) {
			SDL_GameControllerClose(p->pad);
			p->pad = NULL;
			open_pad(p);
		}
		return;

	case SDL_CONTROLLERBUTTONDOWN:
	case SDL_CONTROLLERBUTTONUP: {
		bool down = ev->type == SDL_CONTROLLERBUTTONDOWN;
		int key = 0;
		if (p->input_blocked) return;
		switch (ev->cbutton.button) {
		case SDL_CONTROLLER_BUTTON_A:
			/* clique sempre na posicao actual do ponteiro */
			if (down) qmove(p, (int)p->px, (int)p->py);
			qkey(p, down ? NSFB_EVENT_KEY_DOWN : NSFB_EVENT_KEY_UP, NSFB_KEY_MOUSE_1);
			return;
		case SDL_CONTROLLER_BUTTON_DPAD_UP:    key = NSFB_KEY_UP; break;
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  key = NSFB_KEY_DOWN; break;
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  key = NSFB_KEY_LEFT; break;
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: key = NSFB_KEY_RIGHT; break;
		default: break;
		}
		if (key) {
			qkey(p, down ? NSFB_EVENT_KEY_DOWN : NSFB_EVENT_KEY_UP, key);
			if (down) {
				p->held_key = key;
				p->held_since = p->held_last = SDL_GetTicks();
			} else if (p->held_key == key) {
				p->held_key = 0;
			}
			return;
		}
		if (!down) return;
		switch (ev->cbutton.button) {
		case SDL_CONTROLLER_BUTTON_B:             cmdpush(p, NSX_CMD_BACK); break;
		case SDL_CONTROLLER_BUTTON_X:             cmdpush(p, NSX_CMD_RELOAD); break;
		case SDL_CONTROLLER_BUTTON_Y:             cmdpush(p, NSX_CMD_URL); break;
		case SDL_CONTROLLER_BUTTON_START:         cmdpush(p, NSX_CMD_MENU); break;
		case SDL_CONTROLLER_BUTTON_BACK:          cmdpush(p, NSX_CMD_HELP); break;
		case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  cmdpush(p, NSX_CMD_FORWARD); break;  /* White */
		case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: cmdpush(p, NSX_CMD_BOOKMARK); break; /* Black */
		case SDL_CONTROLLER_BUTTON_LEFTSTICK:     cmdpush(p, NSX_CMD_ZOOM_OUT); break;
		case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    cmdpush(p, NSX_CMD_ZOOM_IN); break;
		default: break;
		}
		return;
	}
	default:
		return;
	}
}

/* os ecras modais tambem leem eventos: o (des)ligar do comando vem para aqui */
void nsfb_xbox_handle_hotplug(const void *sdl_event)
{
	if (g_xb) handle_sdl(g_xb, (const SDL_Event *)sdl_event);
}

/* ---------------------------------------------------------------- ecra */

static void present(nsfb_t *nsfb, xb_priv *p)
{
	if (!p->dirty) return;
	VIDEO_MODE vm = XVideoGetMode();
	uint8_t *vfb = XVideoGetFB();
	int vpitch = vm.width * 4;
	nsfb_bbox_t b = p->dbox;
	if (b.x0 < 0) b.x0 = 0;
	if (b.y0 < 0) b.y0 = 0;
	if (b.x1 > nsfb->width) b.x1 = nsfb->width;
	if (b.y1 > nsfb->height) b.y1 = nsfb->height;
	p->dirty = false;
	if (vfb == NULL || b.x1 <= b.x0 || b.y1 <= b.y0) return;
	size_t len = (size_t)(b.x1 - b.x0) * 4;
	for (int y = b.y0; y < b.y1; y++) {
		memcpy(vfb + (size_t)(y + p->oy) * vpitch + (size_t)(b.x0 + p->ox) * 4,
		       p->fb + (size_t)y * p->pitch + (size_t)b.x0 * 4, len);
	}
}

static void mark(xb_priv *p, const nsfb_bbox_t *box)
{
	if (!p->dirty) {
		p->dbox = *box;
		p->dirty = true;
		return;
	}
	if (box->x0 < p->dbox.x0) p->dbox.x0 = box->x0;
	if (box->y0 < p->dbox.y0) p->dbox.y0 = box->y0;
	if (box->x1 > p->dbox.x1) p->dbox.x1 = box->x1;
	if (box->y1 > p->dbox.y1) p->dbox.y1 = box->y1;
}

void nsfb_xbox_present_now(nsfb_t *nsfb)
{
	if (g_xb) present(nsfb, g_xb);
}

/* ---------------------------------------------------------------- rotinas libnsfb */

static int xb_defaults(nsfb_t *nsfb)
{
	nsfb->width = 640;
	nsfb->height = 480;
	nsfb->format = NSFB_FMT_XRGB8888;
	select_plotters(nsfb);
	return 0;
}

static int xb_geometry(nsfb_t *nsfb, int width, int height, enum nsfb_format_e format)
{
	xb_priv *p = nsfb->surface_priv;
	if (width <= 0 || height <= 0) return -1;
	nsfb->width = width;
	nsfb->height = height;
	nsfb->format = NSFB_FMT_XRGB8888;   /* o ecra da Xbox e A8R8G8B8 */
	(void)format;
	select_plotters(nsfb);
	if (p != NULL) {
		uint8_t *nfb = realloc(p->fb, (size_t)width * height * 4);
		if (nfb == NULL) return -1;
		p->fb = nfb;
		p->pitch = width * 4;
		memset(p->fb, 0xff, (size_t)width * height * 4);
		nsfb->ptr = p->fb;
		nsfb->linelen = p->pitch;
	}
	return 0;
}

static int xb_initialise(nsfb_t *nsfb)
{
	xb_priv *p;
	VIDEO_MODE vm;

	if (nsfb->surface_priv != NULL) return -1;
	p = calloc(1, sizeof *p);
	if (p == NULL) return -1;

	if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
		free(p);
		return -1;
	}
	open_pad(p);

	vm = XVideoGetMode();
	/* a superficie e mais pequena que o ecra pela margem de overscan */
	p->ox = (vm.width - nsfb->width) / 2;
	p->oy = (vm.height - nsfb->height) / 2;
	if (p->ox < 0) p->ox = 0;
	if (p->oy < 0) p->oy = 0;

	p->px = nsfb->width / 2.0f;
	p->py = nsfb->height / 2.0f;
	p->last_x = p->last_y = -1;
	p->last_ticks = SDL_GetTicks();

	nsfb->surface_priv = p;
	g_xb = p;

	/* limpa o ecra todo (as margens ficam pretas) */
	{
		uint8_t *vfb = XVideoGetFB();
		if (vfb) memset(vfb, 0, (size_t)vm.width * vm.height * 4);
	}

	if (xb_geometry(nsfb, nsfb->width, nsfb->height, nsfb->format) != 0) {
		free(p);
		nsfb->surface_priv = NULL;
		g_xb = NULL;
		return -1;
	}
	return 0;
}

static int xb_finalise(nsfb_t *nsfb)
{
	xb_priv *p = nsfb->surface_priv;
	if (p) {
		if (p->pad) SDL_GameControllerClose(p->pad);
		free(p->fb);
		free(p);
		nsfb->surface_priv = NULL;
	}
	g_xb = NULL;
	return 0;
}

static bool xb_input(nsfb_t *nsfb, nsfb_event_t *event, int timeout)
{
	xb_priv *p = nsfb->surface_priv;
	SDL_Event ev;
	uint32_t start;

	if (p == NULL) return false;

	/* mostra tudo o que foi desenhado desde a ultima vez */
	present(nsfb, p);

	start = SDL_GetTicks();
	for (;;) {
		while (SDL_PollEvent(&ev)) handle_sdl(p, &ev);
		pump_sticks(nsfb, p);
		if (qpop(p, event)) return true;
		/* comandos e scroll sao lidos pelo frontend: acorda-o */
		if (p->cn > 0 || fabsf(p->scroll_x) >= 1.0f || fabsf(p->scroll_y) >= 1.0f) {
			event->type = NSFB_EVENT_CONTROL;
			event->value.controlcode = NSFB_CONTROL_TIMEOUT;
			return true;
		}
		if (timeout == 0) return false;
		uint32_t el = SDL_GetTicks() - start;
		if (timeout > 0 && (int)el >= timeout) return false;
		/* com o analogico activo acordamos depressa para o movimento ser suave */
		Sleep(stick_active(p) ? 4 : 8);
		if (stick_active(p) && timeout < 0 && el > 16) return false;
	}
}

static int xb_claim(nsfb_t *nsfb, nsfb_bbox_t *box)
{
	struct nsfb_cursor_s *cursor = nsfb->cursor;
	if (cursor != NULL && cursor->plotted && nsfb_plot_bbox_intersect(box, &cursor->loc))
		nsfb_cursor_clear(nsfb, cursor);
	return 0;
}

static int xb_update(nsfb_t *nsfb, nsfb_bbox_t *box)
{
	xb_priv *p = nsfb->surface_priv;
	struct nsfb_cursor_s *cursor = nsfb->cursor;
	if (cursor != NULL && cursor->plotted == false)
		nsfb_cursor_plot(nsfb, cursor);
	if (p) mark(p, box);
	return 0;
}

static int xb_cursor(nsfb_t *nsfb, struct nsfb_cursor_s *cursor)
{
	xb_priv *p = nsfb->surface_priv;
	nsfb_bbox_t redraw, fbarea;
	if (cursor != NULL && cursor->plotted) {
		nsfb_bbox_t loc = cursor->loc;
		loc.x0 -= cursor->hotspot_x;
		loc.y0 -= cursor->hotspot_y;
		loc.x1 -= cursor->hotspot_x;
		loc.y1 -= cursor->hotspot_y;
		nsfb_plot_add_rect(&cursor->savloc, &loc, &redraw);
		fbarea.x0 = 0; fbarea.y0 = 0;
		fbarea.x1 = nsfb->width; fbarea.y1 = nsfb->height;
		nsfb_plot_clip(&fbarea, &redraw);
		nsfb_cursor_clear(nsfb, cursor);
		nsfb_cursor_plot(nsfb, cursor);
		if (p) mark(p, &redraw);
	}
	return 0;
}

static const nsfb_surface_rtns_t xbox_rtns = {
	.defaults = xb_defaults,
	.initialise = xb_initialise,
	.finalise = xb_finalise,
	.geometry = xb_geometry,
	.input = xb_input,
	.claim = xb_claim,
	.update = xb_update,
	.cursor = xb_cursor,
};

/* registo explicito (nao dependemos de construtores globais) */
void nsfb_ram_register(void);   /* libnsfb (patch do NetSurfX) */

void nsfb_xbox_register(void)
{
	static bool done = false;
	if (done) return;
	done = true;
	/* as imagens do NetSurf sao superficies "ram": tem de existir */
	nsfb_ram_register();
	_nsfb_register_surface(NSFB_SURFACE_ABLE, &xbox_rtns, "xbox");
}
