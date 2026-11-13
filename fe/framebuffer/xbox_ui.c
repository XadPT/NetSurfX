/*
 * NetSurfX - interface propria da Xbox: teclado no ecra, ajuda, favoritos,
 * regresso ao dashboard (IGR) e mensagens rapidas.
 *
 * Os ecras deste ficheiro sao modais: desenham por cima da pagina e lem o
 * comando directamente. Quando fecham, pedem ao fbtk para redesenhar tudo.
 *
 * GPLv2 (parte do NetSurfX).
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

#include <SDL.h>
#include <windows.h>
#include <hal/xbox.h>

#include <libnsfb.h>
#include <libnsfb_plot.h>

#include "utils/utils.h"
#include "utils/nsurl.h"
#include "utils/utf8.h"
#include "utils/log.h"
#include "netsurf/browser_window.h"
#include "netsurf/plotters.h"
#include "netsurf/layout.h"
#include "netsurf/keypress.h"
#include "netsurf/url_db.h"
#include "netsurf/content_type.h"

#include "framebuffer/gui.h"
#include "framebuffer/fbtk.h"
#include "framebuffer/framebuffer.h"
#include "framebuffer/font.h"
#include "framebuffer/xbox_ui.h"

#include "nsfb_xbox.h"
#include "xbox_net.h"
#include "xbox_kbd.h"

extern fbtk_widget_t *fbtk;

/* cores (0xAABBGGRR para a libnsfb, 0x00BBGGRR para o texto) */
#define C_PANEL   0xff2a2a2a
#define C_KEY     0xff4a4a4a
#define C_KEYSEL  0xff10a010      /* verde Xbox */
#define C_KEYSPEC 0xff5a4030
#define C_BOX     0xffffffff
#define C_TITLE   0xff1a1a1a
#define T_WHITE   0x00ffffff
#define T_BLACK   0x00000000
#define T_GREY    0x00b0b0b0

#define TRIG_ON   20000

/* ------------------------------------------------------------------ desenho */

static nsfb_t *ui_fb(void) { return fbtk_get_nsfb(fbtk); }

/* o fbtk deixa o recorte (clip) na ultima zona que desenhou: os ecras modais
 * desenham sempre no ecra inteiro */
static void ui_noclip(void)
{
	int w = 0, h = 0;
	nsfb_get_geometry(ui_fb(), &w, &h, NULL);
	nsfb_bbox_t all = { 0, 0, w, h };
	nsfb_plot_set_clip(ui_fb(), &all);
}

static void ui_fill(int x0, int y0, int x1, int y1, nsfb_colour_t c)
{
	nsfb_bbox_t r = { x0, y0, x1, y1 };
	ui_noclip();
	nsfb_plot_rectangle_fill(ui_fb(), &r, c);
}

static void ui_style(plot_font_style_t *fs, int pt, bool bold, colour fg, colour bg)
{
	memset(fs, 0, sizeof *fs);
	fs->family = PLOT_FONT_FAMILY_SANS_SERIF;
	fs->size = pt * PLOT_STYLE_SCALE;
	fs->weight = bold ? 700 : 400;
	fs->flags = FONTF_NONE;
	fs->foreground = fg;
	fs->background = bg;
}

static int ui_text_width(const plot_font_style_t *fs, const char *s, size_t len)
{
	int w = 0;
	framebuffer_layout_table->width(fs, s, len, &w);
	return w;
}

static void ui_text(int x, int y, const plot_font_style_t *fs, const char *s, size_t len)
{
	struct redraw_context ctx = { .interactive = true, .background_images = false, .plot = &fb_plotters };
	struct rect clip = { 0, 0, nsfb_get_width_safe(), nsfb_get_height_safe() };
	fb_plotters.clip(&ctx, &clip);
	fb_plotters.text(&ctx, fs, x, y, s, len);
}

int nsfb_get_width_safe(void)  { int w = 0, h = 0; nsfb_get_geometry(ui_fb(), &w, &h, NULL); return w; }
int nsfb_get_height_safe(void) { int w = 0, h = 0; nsfb_get_geometry(ui_fb(), &w, &h, NULL); return h; }

static void ui_text_centered(int x0, int x1, int y, const plot_font_style_t *fs, const char *s)
{
	int w = ui_text_width(fs, s, strlen(s));
	ui_text(x0 + ((x1 - x0) - w) / 2, y, fs, s, strlen(s));
}

static void ui_present(void)
{
	nsfb_bbox_t all = { 0, 0, nsfb_get_width_safe(), nsfb_get_height_safe() };
	nsfb_update(ui_fb(), &all);
	nsfb_xbox_present_now(ui_fb());
}

/* depois de um ecra modal, o fbtk redesenha tudo por baixo */
static void ui_restore(void)
{
	fbtk_request_redraw(fbtk);   /* fbtk e o widget raiz */
	fbtk_redraw(fbtk);
	nsfb_xbox_present_now(ui_fb());
}

/* ------------------------------------------------------------------ IGR e dashboard */

void nsx_return_to_dashboard(void)
{
	char path[200] = "";

	nsx_before_exit();
	FILE *f = fopen("D:\\DASH.TXT", "r");
	if (f) {
		if (fgets(path, sizeof path, f)) {
			size_t n = strlen(path);
			while (n && (path[n - 1] == '\n' || path[n - 1] == '\r' || path[n - 1] == ' '))
				path[--n] = 0;
		}
		fclose(f);
	}
	/* o reset rapido nao precisa de mais nada (nao usamos audio) */
	for (;;) {
		XLaunchXBE(path[0] ? path : NULL);
		Sleep(200);
	}
}

void nsx_check_igr(void)
{
	SDL_GameController *p = nsfb_xbox_pad();
	if (p == NULL) return;
	SDL_GameControllerUpdate();
	if (SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > TRIG_ON &&
	    SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > TRIG_ON &&
	    SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_BACK) &&
	    SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER))
		nsx_return_to_dashboard();
}

/* ------------------------------------------------------------------ leitura do comando nos ecras modais */

enum ui_btn { UB_NONE, UB_A, UB_B, UB_X, UB_Y, UB_START, UB_BACK, UB_WHITE, UB_BLACK,
	      UB_UP, UB_DOWN, UB_LEFT, UB_RIGHT, UB_LT, UB_RT, UB_KBD };

/* teclado USB nos ecras modais: o teclado no ecra quer as teclas em bruto
 * (UB_KBD); os menus recebem-nas traduzidas em botoes */
static bool g_ui_raw_kbd;

static enum ui_btn kbd_to_button(void)
{
	struct nsx_kev k;
	while (nsx_kbd_poll(&k)) {
		if (k.type != NSX_KEV_KEY) continue;
		switch (k.key) {
		case NSX_K_UP: return UB_UP;
		case NSX_K_DOWN: return UB_DOWN;
		case NSX_K_LEFT: return UB_LEFT;
		case NSX_K_RIGHT: return UB_RIGHT;
		case NSX_K_ENTER: return UB_A;
		case NSX_K_ESC: case NSX_K_BACKSPACE: return UB_B;
		case NSX_K_PAGEUP: return UB_LT;
		case NSX_K_PAGEDOWN: return UB_RT;
		default: break;
		}
	}
	return UB_NONE;
}

/* espera por um botao (com repeticao no D-pad e no analogico esquerdo) */
static enum ui_btn ui_wait_button(void)
{
	static uint32_t last_nav = 0;
	static bool lt = false, rt = false;
	SDL_Event ev;
	for (;;) {
		nsx_check_igr();
		nsx_usbhid_poll();
		if (nsx_kbd_pending()) {
			if (g_ui_raw_kbd) return UB_KBD;
			enum ui_btn kb = kbd_to_button();
			if (kb != UB_NONE) return kb;
		}
		{ int dx, dy, wh, mb; nsx_mouse_take(&dx, &dy, &wh, &mb); }   /* rato ignorado aqui */
		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_CONTROLLERDEVICEADDED || ev.type == SDL_CONTROLLERDEVICEREMOVED) {
				nsfb_xbox_handle_hotplug(&ev);
				continue;
			}
			if (ev.type != SDL_CONTROLLERBUTTONDOWN) continue;
			switch (ev.cbutton.button) {
			case SDL_CONTROLLER_BUTTON_A: return UB_A;
			case SDL_CONTROLLER_BUTTON_B: return UB_B;
			case SDL_CONTROLLER_BUTTON_X: return UB_X;
			case SDL_CONTROLLER_BUTTON_Y: return UB_Y;
			case SDL_CONTROLLER_BUTTON_START: return UB_START;
			case SDL_CONTROLLER_BUTTON_BACK: return UB_BACK;
			case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return UB_WHITE;
			case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return UB_BLACK;
			case SDL_CONTROLLER_BUTTON_DPAD_UP: last_nav = SDL_GetTicks() + 250; return UB_UP;
			case SDL_CONTROLLER_BUTTON_DPAD_DOWN: last_nav = SDL_GetTicks() + 250; return UB_DOWN;
			case SDL_CONTROLLER_BUTTON_DPAD_LEFT: last_nav = SDL_GetTicks() + 250; return UB_LEFT;
			case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: last_nav = SDL_GetTicks() + 250; return UB_RIGHT;
			default: break;
			}
		}
		SDL_GameController *p = nsfb_xbox_pad();
		if (p != NULL) {
			uint32_t now = SDL_GetTicks();
			int ltv = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
			int rtv = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
			bool l = lt ? ltv > 12000 : ltv > TRIG_ON;
			bool r = rt ? rtv > 12000 : rtv > TRIG_ON;
			bool lt_edge = l && !lt, rt_edge = r && !rt;
			lt = l; rt = r;
			if (lt_edge) return UB_LT;
			if (rt_edge) return UB_RT;
			/* analogico esquerdo e D-pad mantido: navegacao com repeticao */
			int ax = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTX);
			int ay = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTY);
			enum ui_btn nav = UB_NONE;
			if (ay < -16000 || SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_UP)) nav = UB_UP;
			else if (ay > 16000 || SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) nav = UB_DOWN;
			else if (ax < -16000 || SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) nav = UB_LEFT;
			else if (ax > 16000 || SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) nav = UB_RIGHT;
			if (nav != UB_NONE && (int32_t)(now - last_nav) >= 0) {
				last_nav = now + 140;
				return nav;
			}
			if (nav == UB_NONE && ax > -8000 && ax < 8000 && ay > -8000 && ay < 8000)
				last_nav = now;   /* soltou: o proximo toque e imediato */
		}
		Sleep(10);
	}
}

/* ao fechar um ecra modal: espera que os botoes sejam largados e deita fora os
 * eventos pendentes. Sem isto, o "largar" do A que fechou o teclado chegava ao
 * NetSurf como um clique e voltava a abrir o teclado (barra de endereco/campo). */
static void ui_drain(void)
{
	static const SDL_GameControllerButton btns[] = {
		SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X,
		SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK,
		SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER };
	SDL_Event ev;
	uint32_t t0 = SDL_GetTicks();
	for (;;) {
		SDL_GameController *p = nsfb_xbox_pad();
		bool held = false;
		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_CONTROLLERDEVICEADDED || ev.type == SDL_CONTROLLERDEVICEREMOVED)
				nsfb_xbox_handle_hotplug(&ev);
		}
		if (p != NULL)
			for (size_t i = 0; i < sizeof btns / sizeof btns[0]; i++)
				if (SDL_GameControllerGetButton(p, btns[i])) held = true;
		if (!held || SDL_GetTicks() - t0 > 3000) break;
		Sleep(10);
	}
	while (SDL_PollEvent(&ev)) { }
	nsx_kbd_flush();
}

/* ------------------------------------------------------------------ texto UTF-8 com cursor */

struct ebuf { char s[2048]; size_t len; size_t cur; };

static void eb_set(struct ebuf *e, const char *t)
{
	snprintf(e->s, sizeof e->s, "%s", t ? t : "");
	e->len = strlen(e->s);
	e->cur = e->len;
}

static void eb_insert(struct ebuf *e, const char *t)
{
	size_t n = strlen(t);
	if (e->len + n >= sizeof e->s) return;
	memmove(e->s + e->cur + n, e->s + e->cur, e->len - e->cur + 1);
	memcpy(e->s + e->cur, t, n);
	e->len += n;
	e->cur += n;
}

static void eb_left(struct ebuf *e)  { if (e->cur > 0) e->cur = utf8_prev(e->s, e->cur); }
static void eb_right(struct ebuf *e) { if (e->cur < e->len) e->cur = utf8_next(e->s, e->len, e->cur); }

static void eb_backspace(struct ebuf *e)
{
	if (e->cur == 0) return;
	size_t p = utf8_prev(e->s, e->cur);
	memmove(e->s + p, e->s + e->cur, e->len - e->cur + 1);
	e->len -= e->cur - p;
	e->cur = p;
}

/* ------------------------------------------------------------------ teclado no ecra */

#define OSK_COLS 12
#define OSK_ROWS 4

static const char *const osk_layers[3][OSK_ROWS][OSK_COLS] = {
	{ { "1","2","3","4","5","6","7","8","9","0","-","_" },
	  { "q","w","e","r","t","y","u","i","o","p","@","/" },
	  { "a","s","d","f","g","h","j","k","l",".",":","?" },
	  { "z","x","c","v","b","n","m",",",";","!","&","=" } },
	{ { "1","2","3","4","5","6","7","8","9","0","-","_" },
	  { "Q","W","E","R","T","Y","U","I","O","P","@","/" },
	  { "A","S","D","F","G","H","J","K","L",".",":","?" },
	  { "Z","X","C","V","B","N","M",",",";","!","&","=" } },
	{ { "!","\"","#","$","%","&","'","(",")","*","+","~" },
	  { "[","]","{","}","<",">","^","`","|","\\","\xe2\x86\xb5","\xe2\x82\xac" },
	  { "\xc3\xa1","\xc3\xa0","\xc3\xa2","\xc3\xa3","\xc3\xa9","\xc3\xaa","\xc3\xad","\xc3\xb3","\xc3\xb4","\xc3\xb5","\xc3\xba","\xc3\xa7" },
	  { "\xc3\x81","\xc3\x80","\xc3\x82","\xc3\x83","\xc3\x89","\xc3\x8a","\xc3\x8d","\xc3\x93","\xc3\x94","\xc3\x95","\xc3\x9a","\xc3\x87" } },
};

/* linha especial (5.a): cada tecla ocupa N colunas */
enum { SK_SHIFT, SK_SYM, SK_SPACE, SK_DEL, SK_WWW, SK_COM, SK_OK, SK_COUNT };
static const int sk_cols[SK_COUNT] = { 2, 2, 3, 1, 1, 1, 2 };
static const char *const sk_label[SK_COUNT] = { "Shift", "#@\xc3\xa1", "Espa\xc3\xa7o", "\xe2\x86\x90", "www.", ".com", "OK" };

static int sk_at_col(int col)
{
	int c = 0;
	for (int i = 0; i < SK_COUNT; i++) {
		if (col < c + sk_cols[i]) return i;
		c += sk_cols[i];
	}
	return SK_COUNT - 1;
}

static int sk_first_col(int k)
{
	int c = 0;
	for (int i = 0; i < k; i++) c += sk_cols[i];
	return c;
}

static void osk_draw(const char *title, const struct ebuf *e, int layer, int row, int col, bool multiline)
{
	int W = nsfb_get_width_safe(), H = nsfb_get_height_safe();
	int ph = 252;                       /* altura do painel */
	int py = H - ph;
	int kw = (W - 16) / OSK_COLS;
	int kh = 32;
	int kx0 = 8 + ((W - 16) - kw * OSK_COLS) / 2;
	int ky0 = py + 58;
	plot_font_style_t fs, fsb, fsh;
	char hint[160];

	ui_fill(0, py, W, H, C_PANEL);
	ui_style(&fsh, 9, false, T_GREY, 0x2a2a2a);
	ui_style(&fs, 12, false, T_WHITE, 0x4a4a4a);
	ui_style(&fsb, 12, true, T_BLACK, 0xffffff);

	/* titulo e caixa de texto */
	ui_text(10, py + 15, &fsh, title, strlen(title));
	ui_fill(8, py + 20, W - 8, py + 50, C_BOX);
	{
		/* mostra o fim do texto se nao couber, com o cursor visivel */
		const char *s = e->s;
		size_t start = 0;
		int avail = W - 32;
		while (start < e->cur && ui_text_width(&fsb, s + start, e->cur - start) > avail - 20)
			start = utf8_next(s, e->len, start);
		size_t end = e->len;
		while (end > e->cur && ui_text_width(&fsb, s + start, end - start) > avail)
			end = utf8_prev(s, end);
		/* quebras de linha aparecem como um simbolo */
		char view[2100];
		size_t vl = 0;
		for (size_t i = start; i < end && vl < sizeof view - 4; i++) {
			if (s[i] == '\n') { memcpy(view + vl, "\xc2\xb6", 2); vl += 2; }
			else view[vl++] = s[i];
		}
		ui_text(16, py + 41, &fsb, view, vl);
		/* cursor */
		size_t cv = 0;
		for (size_t i = start; i < e->cur; i++) cv += (s[i] == '\n') ? 2 : 1;
		int cx = 16 + ui_text_width(&fsb, view, cv);
		ui_fill(cx, py + 24, cx + 2, py + 47, 0xff10a010);
	}

	/* teclas */
	for (int r = 0; r < OSK_ROWS; r++) {
		for (int c = 0; c < OSK_COLS; c++) {
			int x = kx0 + c * kw, y = ky0 + r * (kh + 4);
			bool sel = (r == row && c == col);
			ui_fill(x + 2, y, x + kw - 2, y + kh, sel ? C_KEYSEL : C_KEY);
			fs.background = sel ? 0x10a010 : 0x4a4a4a;
			ui_text_centered(x, x + kw, y + 22, &fs, osk_layers[layer][r][c]);
		}
	}
	{
		int r = OSK_ROWS, y = ky0 + r * (kh + 4);
		int selk = (row == OSK_ROWS) ? sk_at_col(col) : -1;
		for (int k = 0; k < SK_COUNT; k++) {
			int x = kx0 + sk_first_col(k) * kw, x1 = x + sk_cols[k] * kw;
			bool sel = (k == selk);
			bool on = (k == SK_SHIFT && layer == 1) || (k == SK_SYM && layer == 2);
			ui_fill(x + 2, y, x1 - 2, y + kh, sel ? C_KEYSEL : (on ? 0xff806040 : C_KEYSPEC));
			fs.background = sel ? 0x10a010 : (on ? 0x406080 : 0x30405a);
			const char *lab = sk_label[k];
			if (k == SK_OK) lab = multiline ? "OK" : "Ir";
			ui_text_centered(x, x1, y + 22, &fs, lab);
		}
	}
	snprintf(hint, sizeof hint, "A tecla  B apagar  X espa\xc3\xa7o  Y mai\xc3\xbasc.  Branco s\xc3\xadmb.  "
		 "Preto limpar  L/R cursor  Start %s  Back sair", multiline ? "enviar" : "ir");
	/* encolhe a letra ate a linha de ajuda caber */
	for (int pt = 9; pt >= 6; pt--) {
		ui_style(&fsh, pt, false, T_GREY, 0x2a2a2a);
		if (ui_text_width(&fsh, hint, strlen(hint)) <= W - 16) break;
	}
	ui_text(8, H - 6, &fsh, hint, strlen(hint));
	ui_present();
}

bool nsx_osk_run(const char *title, const char *initial, char *out, size_t outsz,
		 bool multiline, bool *submit)
{
	static struct ebuf e;
	int layer = 0, row = 1, col = 0;
	bool done = false, ok = false;

	if (submit) *submit = false;
	eb_set(&e, initial);
	nsfb_xbox_block_input(true);

	g_ui_raw_kbd = true;
	while (!done) {
		osk_draw(title, &e, layer, row, col, multiline);
		switch (ui_wait_button()) {
		case UB_KBD: {
			/* teclado USB: escreve directamente */
			struct nsx_kev k;
			while (!done && nsx_kbd_poll(&k)) {
				if (k.type == NSX_KEV_CHAR) {
					char u[8];
					size_t n = utf8_from_ucs4(k.ucs4, u);
					u[n] = 0;
					eb_insert(&e, u);
				} else if (k.type == NSX_KEV_KEY) {
					switch (k.key) {
					case NSX_K_ENTER:
						if (multiline && !(k.mods & NSX_KM_CTRL)) eb_insert(&e, "\n");
						else { ok = done = true; if (submit) *submit = true; }
						break;
					case NSX_K_ESC: done = true; break;
					case NSX_K_BACKSPACE: eb_backspace(&e); break;
					case NSX_K_DELETE:
						if (e.cur < e.len) { eb_right(&e); eb_backspace(&e); }
						break;
					case NSX_K_LEFT: eb_left(&e); break;
					case NSX_K_RIGHT: eb_right(&e); break;
					case NSX_K_HOME: e.cur = 0; break;
					case NSX_K_END: e.cur = e.len; break;
					default: break;
					}
				}
			}
			break;
		}
		case UB_UP:    row = (row + OSK_ROWS) % (OSK_ROWS + 1);
			if (row == OSK_ROWS) col = sk_first_col(sk_at_col(col));
			break;
		case UB_DOWN:  row = (row + 1) % (OSK_ROWS + 1);
			if (row == OSK_ROWS) col = sk_first_col(sk_at_col(col));
			break;
		case UB_LEFT:
			if (row == OSK_ROWS) {
				int k = sk_at_col(col);
				k = (k + SK_COUNT - 1) % SK_COUNT;
				col = sk_first_col(k);
			} else col = (col + OSK_COLS - 1) % OSK_COLS;
			break;
		case UB_RIGHT:
			if (row == OSK_ROWS) {
				int k = sk_at_col(col);
				k = (k + 1) % SK_COUNT;
				col = sk_first_col(k);
			} else col = (col + 1) % OSK_COLS;
			break;
		case UB_A:
			if (row < OSK_ROWS) {
				const char *k = osk_layers[layer][row][col];
				if (!strcmp(k, "\xe2\x86\xb5")) {      /* nova linha */
					if (multiline) eb_insert(&e, "\n");
				} else
					eb_insert(&e, k);
				if (layer == 1) layer = 0;    /* maiuscula so para uma tecla */
			} else {
				switch (sk_at_col(col)) {
				case SK_SHIFT: layer = (layer == 1) ? 0 : 1; break;
				case SK_SYM:   layer = (layer == 2) ? 0 : 2; break;
				case SK_SPACE: eb_insert(&e, " "); break;
				case SK_DEL:   eb_backspace(&e); break;
				case SK_WWW:   eb_insert(&e, "www."); break;
				case SK_COM:   eb_insert(&e, ".com"); break;
				case SK_OK:    ok = done = true; if (submit) *submit = !multiline; break;
				}
			}
			break;
		case UB_B:     eb_backspace(&e); break;
		case UB_X:     eb_insert(&e, " "); break;
		case UB_Y:     layer = (layer == 1) ? 0 : 1; break;
		case UB_WHITE: layer = (layer == 2) ? 0 : 2; break;
		case UB_BLACK: eb_set(&e, ""); break;            /* limpar tudo */
		case UB_LT:    eb_left(&e); break;
		case UB_RT:    eb_right(&e); break;
		case UB_START: ok = done = true; if (submit) *submit = true; break;
		case UB_BACK:  done = true; break;
		default: break;
		}
	}

	g_ui_raw_kbd = false;
	ui_drain();
	nsfb_xbox_block_input(false);
	ui_restore();
	if (ok) snprintf(out, outsz, "%s", e.s);
	return ok;
}

/* ------------------------------------------------------------------ ajuda */

void nsx_help_show(void)
{
	static const char *const lines[] = {
		"Controlos do NetSurfX",
		"",
		"Anal\xc3\xb3gico esquerdo   mover o ponteiro",
		"A                     clicar / abrir liga\xc3\xa7\xc3\xa3o / escrever num campo",
		"Anal\xc3\xb3gico direito    deslocar a p\xc3\xa1gina",
		"Gatilhos L / R        p\xc3\xa1gina acima / abaixo",
		"D-pad                 setas (deslocar ou mover no texto)",
		"B                     voltar atr\xc3\xa1s",
		"Branco                avan\xc3\xa7" "ar",
		"X                     recarregar",
		"Y                     escrever endere\xc3\xa7o",
		"Preto                 guardar nos favoritos",
		"Start                 menu: favoritos, hist\xc3\xb3rico, procurar, defini\xc3\xa7\xc3\xb5" "es",
		"Cliques nos anal\xc3\xb3gicos  menos zoom / mais zoom",
		"Back                  esta ajuda",
		"L + R + Back + Preto  voltar ao dashboard",
	};
	int W = nsfb_get_width_safe(), H = nsfb_get_height_safe();
	plot_font_style_t fs, ft;
	char net[120];
	int y;

	nsfb_xbox_block_input(true);
	ui_fill(16, 16, W - 16, H - 16, 0xff202020);
	ui_style(&ft, 14, true, T_WHITE, 0x202020);
	ui_style(&fs, 10, false, T_WHITE, 0x202020);
	y = 52;
	ui_text(40, y, &ft, lines[0], strlen(lines[0]));
	y += 8;
	for (size_t i = 2; i < sizeof lines / sizeof lines[0]; i++) {
		const char *l = lines[i];
		const char *sp = strstr(l, "  ");
		y += (H - 150) / 15;
		if (sp) {
			/* duas colunas: comando | accao */
			ui_text(40, y, &fs, l, (size_t)(sp - l));
			while (*sp == ' ') sp++;
			ui_text(230, y, &fs, sp, strlen(sp));
		} else {
			ui_text(40, y, &fs, l, strlen(l));
		}
	}
	nsx_net_describe(net, sizeof net);
	fs.foreground = 0x80ff80;
	ui_text(40, H - 52, &fs, net, strlen(net));
	fs.foreground = T_GREY;
	{ const char *c = "Carrega em qualquer bot\xc3\xa3o para fechar"; ui_text(40, H - 36, &fs, c, strlen(c)); }
	ui_present();
	ui_wait_button();
	ui_drain();
	nsfb_xbox_block_input(false);
	ui_restore();
}

/* ------------------------------------------------------------------ favoritos */

#define BM_TXT  "D:\\data\\bookmarks.txt"
#define BM_HTML "D:\\data\\bookmarks.html"

static void html_escape(FILE *f, const char *s)
{
	for (; *s; s++) {
		switch (*s) {
		case '<': fputs("&lt;", f); break;
		case '>': fputs("&gt;", f); break;
		case '&': fputs("&amp;", f); break;
		case '"': fputs("&quot;", f); break;
		default: fputc(*s, f);
		}
	}
}

/* regenera a pagina de favoritos a partir da lista (url<TAB>titulo por linha) */
static void bm_write_html(void)
{
	FILE *in = fopen(BM_TXT, "r");
	FILE *out = fopen(BM_HTML, "w");
	char line[1200];
	int n = 0;
	if (out == NULL) { if (in) fclose(in); return; }
	fputs("<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>Favoritos</title>"
	      "<link rel=\"stylesheet\" href=\"file:///D:/res/home.css\"></head><body>\n"
	      "<h1>Favoritos</h1>\n<p><a href=\"file:///D:/res/home.html\">&larr; P\xc3\xa1gina inicial</a></p>\n<ul class=\"bm\">\n", out);
	if (in) {
		while (fgets(line, sizeof line, in)) {
			char *tab = strchr(line, '\t');
			size_t l = strlen(line);
			while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
			if (l == 0) continue;
			if (tab) *tab = 0;
			fputs("<li><a href=\"", out);
			html_escape(out, line);
			fputs("\">", out);
			html_escape(out, (tab && tab[1]) ? tab + 1 : line);
			fputs("</a></li>\n", out);
			n++;
		}
		fclose(in);
	}
	if (n == 0)
		fputs("<li>Ainda n\xc3\xa3o h\xc3\xa1 favoritos. Numa p\xc3\xa1gina, carrega no bot\xc3\xa3o Preto para a guardar.</li>\n", out);
	fputs("</ul>\n<p class=\"small\">Para apagar favoritos, edita data\\bookmarks.txt por FTP.</p>\n</body></html>\n", out);
	fclose(out);
}

void nsx_bookmarks_init(void)
{
	CreateDirectoryA("D:\\data", NULL);
	bm_write_html();
}

bool nsx_bookmark_add(struct browser_window *bw)
{
	nsurl *url;
	const char *title;
	FILE *f;

	if (bw == NULL || (url = browser_window_access_url(bw)) == NULL) return false;
	if (strncmp(nsurl_access(url), "file:", 5) == 0 ||
	    strncmp(nsurl_access(url), "about:", 6) == 0) return false;
	title = browser_window_get_title(bw);
	f = fopen(BM_TXT, "a");
	if (f == NULL) return false;
	fprintf(f, "%s\t", nsurl_access(url));
	for (const char *t = title ? title : ""; *t; t++)
		fputc((*t == '\t' || *t == '\n' || *t == '\r') ? ' ' : *t, f);
	fputc('\n', f);
	fclose(f);
	bm_write_html();
	return true;
}

/* ------------------------------------------------------------------ mensagem rapida */

void nsx_toast(const char *msg)
{
	int W = nsfb_get_width_safe(), H = nsfb_get_height_safe();
	plot_font_style_t fs;
	ui_style(&fs, 12, true, T_WHITE, 0x10a010);
	int w = ui_text_width(&fs, msg, strlen(msg)) + 32;
	int x0 = (W - w) / 2, y0 = H / 2 - 22;
	ui_fill(x0, y0, x0 + w, y0 + 44, 0xff10a010);
	ui_text(x0 + 16, y0 + 28, &fs, msg, strlen(msg));
	ui_present();
	Sleep(700);
	ui_restore();
}

/* ------------------------------------------------------------------ menu (lista) */

/* lista vertical modal. Devolve o indice escolhido (A, ou esquerda/direita
 * se dir != NULL: *dir = -1/+1, 0 para A) ou -1 se fechado (B/Start/Back). */
int nsx_menu(const char *title, const char *const *labels, int n, int sel,
	     const char *hint, int *dir)
{
	int W = nsfb_get_width_safe(), H = nsfb_get_height_safe();
	plot_font_style_t ft, fs, fsel, fh;
	int row_h, pw, ph, x0, y0, first = 0, visible;
	int result = -1;

	if (n <= 0) return -1;
	if (sel < 0 || sel >= n) sel = 0;
	if (dir) *dir = 0;
	ui_style(&ft, 14, true, T_WHITE, 0x202020);
	ui_style(&fs, 12, false, T_WHITE, 0x202020);
	ui_style(&fsel, 12, true, T_WHITE, 0x10a010);
	ui_style(&fh, 9, false, T_GREY, 0x202020);
	row_h = H >= 700 ? 40 : 28;
	pw = W >= 1200 ? 640 : W - 160;
	visible = (H - 170) / row_h;
	if (visible > n) visible = n;
	ph = 70 + visible * row_h + 40;
	x0 = (W - pw) / 2;
	y0 = (H - ph) / 2;

	nsfb_xbox_block_input(true);
	for (;;) {
		if (sel < first) first = sel;
		if (sel >= first + visible) first = sel - visible + 1;
		ui_fill(x0 - 2, y0 - 2, x0 + pw + 2, y0 + ph + 2, 0xff10a010);
		ui_fill(x0, y0, x0 + pw, y0 + ph, 0xff202020);
		ui_text(x0 + 24, y0 + 36, &ft, title, strlen(title));
		for (int i = 0; i < visible; i++) {
			int k = first + i;
			int ry = y0 + 56 + i * row_h;
			const char *l = labels[k];
			if (k == sel) {
				ui_fill(x0 + 12, ry, x0 + pw - 12, ry + row_h - 4, 0xff10a010);
				ui_text(x0 + 28, ry + row_h - 12, &fsel, l, strlen(l));
			} else {
				ui_text(x0 + 28, ry + row_h - 12, &fs, l, strlen(l));
			}
		}
		if (first > 0)
			ui_text(x0 + pw - 40, y0 + 36, &fh, "\xe2\x96\xb2", 3);
		if (first + visible < n)
			ui_text(x0 + pw - 40, y0 + ph - 14, &fh, "\xe2\x96\xbc", 3);
		if (hint)
			ui_text(x0 + 24, y0 + ph - 14, &fh, hint, strlen(hint));
		ui_present();

		switch (ui_wait_button()) {
		case UB_UP:    sel = (sel + n - 1) % n; break;
		case UB_DOWN:  sel = (sel + 1) % n; break;
		case UB_LT:    sel = sel - visible < 0 ? 0 : sel - visible; break;
		case UB_RT:    sel = sel + visible >= n ? n - 1 : sel + visible; break;
		case UB_LEFT:  if (dir) { *dir = -1; result = sel; goto done; } break;
		case UB_RIGHT: if (dir) { *dir = 1; result = sel; goto done; } break;
		case UB_A:     result = sel; goto done;
		case UB_B: case UB_START: case UB_BACK: result = -1; goto done;
		default: break;
		}
	}
done:
	ui_drain();
	nsfb_xbox_block_input(false);
	ui_restore();
	return result;
}

/* ------------------------------------------------------------------ historico */

#define HIST_HTML "D:\\data\\history.html"
#define HIST_MAX 80

struct hist_ent { nsurl *url; const char *title; time_t when; };
static struct hist_ent *g_hist;
static int g_hist_n, g_hist_cap;

static bool hist_collect(nsurl *url, const struct url_data *d)
{
	const char *u = nsurl_access(url);
	if (d->last_visit <= 0 || d->visits == 0) return true;
	if (strncmp(u, "http", 4) != 0) return true;
	if (d->type != CONTENT_HTML && d->type != CONTENT_TEXTPLAIN) return true;
	if (g_hist_n == g_hist_cap) {
		int nc = g_hist_cap ? g_hist_cap * 2 : 256;
		struct hist_ent *ne = realloc(g_hist, nc * sizeof *ne);
		if (ne == NULL) return false;
		g_hist = ne;
		g_hist_cap = nc;
	}
	g_hist[g_hist_n].url = nsurl_ref(url);
	g_hist[g_hist_n].title = d->title;
	g_hist[g_hist_n].when = d->last_visit;
	g_hist_n++;
	return true;
}

static int hist_cmp(const void *a, const void *b)
{
	const struct hist_ent *x = a, *y = b;
	return (x->when < y->when) - (x->when > y->when);   /* mais recente primeiro */
}

/* escreve a pagina do historico (paginas visitadas, mais recentes primeiro) */
const char *nsx_history_page(void)
{
	FILE *out;
	int n;

	g_hist_n = 0;
	urldb_iterate_entries(hist_collect);
	qsort(g_hist, g_hist_n, sizeof *g_hist, hist_cmp);
	out = fopen(HIST_HTML, "w");
	if (out != NULL) {
		fputs("<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>Hist\xc3\xb3rico</title>"
		      "<link rel=\"stylesheet\" href=\"file:///D:/res/home.css\"></head><body>\n"
		      "<h1>Hist\xc3\xb3rico</h1>\n<p><a href=\"file:///D:/res/home.html\">&larr; P\xc3\xa1gina inicial</a></p>\n<ul class=\"bm\">\n", out);
		n = g_hist_n < HIST_MAX ? g_hist_n : HIST_MAX;
		for (int i = 0; i < n; i++) {
			char when[32] = "";
			struct tm *tm = localtime(&g_hist[i].when);
			if (tm) strftime(when, sizeof when, "%d/%m %H:%M", tm);
			fputs("<li><a href=\"", out);
			html_escape(out, nsurl_access(g_hist[i].url));
			fputs("\">", out);
			html_escape(out, (g_hist[i].title && g_hist[i].title[0]) ?
				    g_hist[i].title : nsurl_access(g_hist[i].url));
			fprintf(out, "</a> <span class=\"small\">%s</span></li>\n", when);
		}
		if (n == 0)
			fputs("<li>Ainda n\xc3\xa3o h\xc3\xa1 p\xc3\xa1ginas no hist\xc3\xb3rico.</li>\n", out);
		fputs("</ul>\n</body></html>\n", out);
		fclose(out);
	}
	for (int i = 0; i < g_hist_n; i++) nsurl_unref(g_hist[i].url);
	g_hist_n = 0;
	return "file:///D:/data/history.html";
}

/* ------------------------------------------------------------------ apagar dados */

static void rm_tree(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	char p[MAX_PATH];
	if (d == NULL) return;
	while ((e = readdir(d)) != NULL) {
		struct stat st;
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		snprintf(p, sizeof p, "%s\\%s", dir, e->d_name);
		if (stat(p, &st) == 0 && S_ISDIR(st.st_mode))
			rm_tree(p);
		else
			unlink(p);
	}
	closedir(d);
	rmdir(dir);
}

/* apaga cookies, historico e cache (chamado no arranque, antes da cache abrir) */
void nsx_clear_browsing_data(void)
{
	unlink("D:\\data\\Cookies");
	unlink("D:\\data\\URLs");
	unlink("D:\\data\\history.html");
	rm_tree("D:\\cache");
}
