/*
 * NetSurfX - teclado e rato USB (protocolo "boot" HID)
 *
 * Os relatorios chegam do controlador USB (xbox_usbhid.c, contexto de
 * interrupcao) e ficam numa fila sem trincos (um produtor, um consumidor).
 * A thread principal chama nsx_kbd_poll(): compara cada relatorio com o
 * anterior, gera teclas premidas, aplica a disposicao (PT ou US), as teclas
 * mortas (acentos) e a repeticao automatica.
 *
 * Ficheiro portavel: tambem e compilado nos testes no PC.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "xbox_kbd.h"

#ifdef _WIN32
#include <windows.h>
static uint32_t now_ms(void) { return GetTickCount(); }
#else
#include <time.h>
static uint32_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
#endif

#define RING 32
#define REPEAT_DELAY 450
#define REPEAT_RATE  45

/* ---------------------------------------------------------------- fila de relatorios */

static uint8_t g_ring[RING][8];
static volatile unsigned g_head, g_tail;      /* produtor escreve head, consumidor tail */
static volatile bool g_present;               /* ha um teclado ligado */

void nsx_kbd_feed_report(const uint8_t *r, int len)
{
	unsigned h = g_head;
	if (len < 3) return;
	if (h - g_tail >= RING) return;           /* cheia: perde-se (raro) */
	memset(g_ring[h % RING], 0, 8);
	memcpy(g_ring[h % RING], r, len > 8 ? 8 : (size_t)len);
	g_head = h + 1;
	g_present = true;
}

void nsx_kbd_set_present(bool present) { g_present = present; }
bool nsx_kbd_present(void) { return g_present; }

/* ---------------------------------------------------------------- rato */

static volatile int m_dx, m_dy, m_wheel, m_buttons;
static volatile unsigned m_seq;
static unsigned m_seen;

void nsx_mouse_feed_report(const uint8_t *r, int len)
{
	if (len < 3) return;
	m_buttons = r[0] & 7;
	m_dx += (int8_t)r[1];
	m_dy += (int8_t)r[2];
	if (len >= 4) m_wheel += (int8_t)r[3];
	m_seq++;
}

bool nsx_mouse_take(int *dx, int *dy, int *wheel, int *buttons)
{
	unsigned s = m_seq;
	if (s == m_seen) return false;
	m_seen = s;
	/* leitura/escrita nao atomica: no pior caso perde-se um pixel */
	*dx = m_dx;     m_dx -= *dx;
	*dy = m_dy;     m_dy -= *dy;
	*wheel = m_wheel; m_wheel -= *wheel;
	*buttons = m_buttons;
	return true;
}

/* ---------------------------------------------------------------- disposicoes */

static int g_layout = NSX_KBD_PT;
void nsx_kbd_set_layout(int layout) { g_layout = layout; }

/* teclas mortas (acentos), guardadas como o caracter do acento */
#define DEAD_ACUTE  0x00B4
#define DEAD_GRAVE  0x0060
#define DEAD_TILDE  0x007E
#define DEAD_CIRC   0x005E
#define DEAD_DIAER  0x00A8
#define DEAD(c)     (0x80000000u | (c))

struct keymap { uint32_t normal, shift, altgr; };

/* simbolos a partir do uso HID 0x1E (1) ate 0x38 (/), mais 0x64 (ISO) */
static const struct keymap us_sym[] = {
	/* 1E */ {'1','!',0},{'2','@',0},{'3','#',0},{'4','$',0},{'5','%',0},
	/* 23 */ {'6','^',0},{'7','&',0},{'8','*',0},{'9','(',0},{'0',')',0},
	/* 28..2C: enter esc bksp tab space (tratadas a parte) */
	{0,0,0},{0,0,0},{0,0,0},{0,0,0},{' ',' ',0},
	/* 2D */ {'-','_',0},{'=','+',0},{'[','{',0},{']','}',0},{'\\','|',0},
	/* 32 */ {'\\','|',0},{';',':',0},{'\'','"',0},{'`','~',0},{',','<',0},
	/* 37 */ {'.','>',0},{'/','?',0},
};

static const struct keymap pt_sym[] = {
	/* 1E */ {'1','!',0},{'2','"','@'},{'3','#',0x00A3},{'4','$',0x00A7},{'5','%',0x20AC},
	/* 23 */ {'6','&',0},{'7','/','{'},{'8','(','['},{'9',')',']'},{'0','=','}'},
	{0,0,0},{0,0,0},{0,0,0},{0,0,0},{' ',' ',0},
	/* 2D */ {'\'','?',0},{0x00AB,0x00BB,0},{'+','*',DEAD(DEAD_DIAER)},
	/* 30 */ {DEAD(DEAD_ACUTE),DEAD(DEAD_GRAVE),0},{DEAD(DEAD_TILDE),DEAD(DEAD_CIRC),0},
	/* 32 */ {DEAD(DEAD_TILDE),DEAD(DEAD_CIRC),0},{0x00E7,0x00C7,0},{0x00BA,0x00AA,0},
	/* 35 */ {'\\','|',0},{',',';',0},
	/* 37 */ {'.',':',0},{'-','_',0},
};

/* acento + letra -> letra acentuada (0 se nao existir) */
static uint32_t compose(uint32_t dead, uint32_t c)
{
	static const struct { uint32_t d; char base; uint32_t out; } t[] = {
		{DEAD_ACUTE,'a',0xE1},{DEAD_ACUTE,'e',0xE9},{DEAD_ACUTE,'i',0xED},{DEAD_ACUTE,'o',0xF3},{DEAD_ACUTE,'u',0xFA},
		{DEAD_ACUTE,'A',0xC1},{DEAD_ACUTE,'E',0xC9},{DEAD_ACUTE,'I',0xCD},{DEAD_ACUTE,'O',0xD3},{DEAD_ACUTE,'U',0xDA},
		{DEAD_GRAVE,'a',0xE0},{DEAD_GRAVE,'e',0xE8},{DEAD_GRAVE,'i',0xEC},{DEAD_GRAVE,'o',0xF2},{DEAD_GRAVE,'u',0xF9},
		{DEAD_GRAVE,'A',0xC0},{DEAD_GRAVE,'E',0xC8},{DEAD_GRAVE,'I',0xCC},{DEAD_GRAVE,'O',0xD2},{DEAD_GRAVE,'U',0xD9},
		{DEAD_TILDE,'a',0xE3},{DEAD_TILDE,'o',0xF5},{DEAD_TILDE,'n',0xF1},
		{DEAD_TILDE,'A',0xC3},{DEAD_TILDE,'O',0xD5},{DEAD_TILDE,'N',0xD1},
		{DEAD_CIRC,'a',0xE2},{DEAD_CIRC,'e',0xEA},{DEAD_CIRC,'i',0xEE},{DEAD_CIRC,'o',0xF4},{DEAD_CIRC,'u',0xFB},
		{DEAD_CIRC,'A',0xC2},{DEAD_CIRC,'E',0xCA},{DEAD_CIRC,'I',0xCE},{DEAD_CIRC,'O',0xD4},{DEAD_CIRC,'U',0xDB},
		{DEAD_DIAER,'a',0xE4},{DEAD_DIAER,'e',0xEB},{DEAD_DIAER,'i',0xEF},{DEAD_DIAER,'o',0xF6},{DEAD_DIAER,'u',0xFC},
		{DEAD_DIAER,'A',0xC4},{DEAD_DIAER,'E',0xCB},{DEAD_DIAER,'I',0xCF},{DEAD_DIAER,'O',0xD6},{DEAD_DIAER,'U',0xDC},
	};
	for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
		if (t[i].d == dead && (uint32_t)(unsigned char)t[i].base == c) return t[i].out;
	return 0;
}

/* uso HID de uma tecla especial -> NSX_K_* (0 se nao for especial) */
static int special_key(uint8_t u)
{
	switch (u) {
	case 0x28: case 0x58: return NSX_K_ENTER;
	case 0x29: return NSX_K_ESC;
	case 0x2A: return NSX_K_BACKSPACE;
	case 0x2B: return NSX_K_TAB;
	case 0x4F: return NSX_K_RIGHT;
	case 0x50: return NSX_K_LEFT;
	case 0x51: return NSX_K_DOWN;
	case 0x52: return NSX_K_UP;
	case 0x4A: return NSX_K_HOME;
	case 0x4D: return NSX_K_END;
	case 0x4B: return NSX_K_PAGEUP;
	case 0x4E: return NSX_K_PAGEDOWN;
	case 0x4C: return NSX_K_DELETE;
	default:
		if (u >= 0x3A && u <= 0x45) return NSX_K_F1 + (u - 0x3A);
		return 0;
	}
}

/* uso HID -> caracter (0 se nada) para os modificadores dados */
static uint32_t usage_char(uint8_t u, bool shift, bool altgr, bool caps)
{
	if (u >= 0x04 && u <= 0x1D) {               /* letras */
		uint32_t c = 'a' + (u - 0x04);
		if (altgr) return (g_layout == NSX_KBD_PT && u == 0x08) ? 0x20AC : 0;  /* AltGr+E = euro */
		if (shift ^ caps) c -= 32;
		return c;
	}
	if (u >= 0x1E && u <= 0x38) {
		const struct keymap *tbl = (g_layout == NSX_KBD_PT) ? pt_sym : us_sym;
		const struct keymap *m = tbl + (u - 0x1E);
		if (altgr) return m->altgr;
		return shift ? m->shift : m->normal;
	}
	if (u == 0x64) {                             /* tecla ISO ao lado do Shift esquerdo */
		if (g_layout == NSX_KBD_PT) return shift ? '>' : '<';
		return shift ? '|' : '\\';
	}
	/* teclado numerico (com Num Lock assumido) */
	if (u >= 0x59 && u <= 0x61) return '1' + (u - 0x59);
	switch (u) {
	case 0x62: return '0';
	case 0x63: return g_layout == NSX_KBD_PT ? ',' : '.';
	case 0x54: return '/';
	case 0x55: return '*';
	case 0x56: return '-';
	case 0x57: return '+';
	}
	return 0;
}

/* ---------------------------------------------------------------- consumo */

static uint8_t g_prev[8];
static bool g_caps;
static uint32_t g_dead;                         /* acento pendente (0 = nenhum) */
static uint8_t g_rep_usage;                     /* tecla a repetir */
static uint32_t g_rep_since, g_rep_last;

#define EVQ 64
static struct nsx_kev g_evq[EVQ];
static unsigned g_evh, g_evn;

static void ev_push(const struct nsx_kev *e)
{
	if (g_evn >= EVQ) return;
	g_evq[(g_evh + g_evn) % EVQ] = *e;
	g_evn++;
}

static void emit_usage(uint8_t u, uint8_t mods)
{
	struct nsx_kev e;
	bool ctrl = mods & 0x11, shift = mods & 0x22, alt = mods & 0x04, altgr = mods & 0x40;
	int sp;

	memset(&e, 0, sizeof e);
	e.mods = (ctrl ? NSX_KM_CTRL : 0) | (shift ? NSX_KM_SHIFT : 0) | (alt ? NSX_KM_ALT : 0);

	if (u == 0x39) { g_caps = !g_caps; return; }  /* Caps Lock */

	sp = special_key(u);
	if (sp) {
		e.type = NSX_KEV_KEY;
		e.key = sp;
		ev_push(&e);
		return;
	}
	if (ctrl || alt) {
		/* atalhos: entregamos a letra/numero base (minuscula, US) */
		uint32_t c = 0;
		if (u >= 0x04 && u <= 0x1D) c = 'a' + (u - 0x04);
		else if (u >= 0x1E && u <= 0x26) c = '1' + (u - 0x1E);
		else if (u == 0x27) c = '0';
		else if (u == 0x2D || u == 0x56) c = '-';
		else if (u == 0x2E || u == 0x57 || (g_layout == NSX_KBD_PT && u == 0x2F)) c = '+';
		if (c == 0) return;
		e.type = NSX_KEV_SHORTCUT;
		e.ucs4 = c;
		ev_push(&e);
		return;
	}
	{
		uint32_t c = usage_char(u, shift, altgr, g_caps);
		if (c == 0) return;
		if (c & 0x80000000u) {                    /* tecla morta */
			uint32_t acc = c & 0x7fffffff;
			if (g_dead) {                         /* duas seguidas: escreve o acento */
				e.type = NSX_KEV_CHAR;
				e.ucs4 = g_dead;
				ev_push(&e);
				g_dead = 0;
				if (acc == e.ucs4) return;
			}
			g_dead = acc;
			return;
		}
		if (g_dead) {
			uint32_t k = compose(g_dead, c);
			if (k) {
				c = k;
			} else {
				/* sem composicao: escreve o acento e depois a tecla */
				e.type = NSX_KEV_CHAR;
				e.ucs4 = g_dead;
				ev_push(&e);
				if (c == ' ') { g_dead = 0; return; }
			}
			g_dead = 0;
		}
		e.type = NSX_KEV_CHAR;
		e.ucs4 = c;
		ev_push(&e);
	}
}

static bool in_report(const uint8_t *r, uint8_t u)
{
	for (int i = 2; i < 8; i++) if (r[i] == u) return true;
	return false;
}

static void process_report(const uint8_t *r)
{
	/* "phantom state" (demasiadas teclas): ignora */
	if (r[2] == 0x01) return;
	for (int i = 2; i < 8; i++) {
		uint8_t u = r[i];
		if (u < 0x04 || in_report(g_prev, u)) continue;
		emit_usage(u, r[0]);
		/* repeticao: a ultima tecla premida (excepto modificadores) */
		g_rep_usage = u;
		g_rep_since = g_rep_last = now_ms();
	}
	if (g_rep_usage && !in_report(r, g_rep_usage)) g_rep_usage = 0;
	memcpy(g_prev, r, 8);
}

bool nsx_kbd_poll(struct nsx_kev *out)
{
	while (g_tail != g_head) {
		process_report(g_ring[g_tail % RING]);
		g_tail = g_tail + 1;
	}
	if (g_evn == 0 && g_rep_usage && g_rep_usage != 0x39) {
		uint32_t t = now_ms();
		if (t - g_rep_since > REPEAT_DELAY && t - g_rep_last > REPEAT_RATE) {
			g_rep_last = t;
			emit_usage(g_rep_usage, g_prev[0]);
		}
	}
	if (g_evn == 0) return false;
	*out = g_evq[g_evh];
	g_evh = (g_evh + 1) % EVQ;
	g_evn--;
	return true;
}

bool nsx_kbd_pending(void)
{
	return g_evn > 0 || g_tail != g_head;
}

void nsx_kbd_flush(void)
{
	struct nsx_kev e;
	while (nsx_kbd_poll(&e)) { }
	g_rep_usage = 0;
	g_dead = 0;
}
