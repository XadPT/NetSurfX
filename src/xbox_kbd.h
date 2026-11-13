/* NetSurfX - teclado e rato USB (ver xbox_kbd.c) */
#ifndef NSX_XBOX_KBD_H
#define NSX_XBOX_KBD_H

#include <stdbool.h>
#include <stdint.h>

enum { NSX_KBD_PT = 0, NSX_KBD_US = 1 };

enum nsx_kev_type { NSX_KEV_CHAR = 1, NSX_KEV_KEY, NSX_KEV_SHORTCUT };

enum nsx_kkey {
	NSX_K_ENTER = 1, NSX_K_ESC, NSX_K_BACKSPACE, NSX_K_TAB, NSX_K_DELETE,
	NSX_K_LEFT, NSX_K_RIGHT, NSX_K_UP, NSX_K_DOWN,
	NSX_K_HOME, NSX_K_END, NSX_K_PAGEUP, NSX_K_PAGEDOWN,
	NSX_K_F1, NSX_K_F2, NSX_K_F3, NSX_K_F4, NSX_K_F5, NSX_K_F6,
	NSX_K_F7, NSX_K_F8, NSX_K_F9, NSX_K_F10, NSX_K_F11, NSX_K_F12
};

#define NSX_KM_CTRL  1
#define NSX_KM_SHIFT 2
#define NSX_KM_ALT   4

struct nsx_kev {
	int type;        /* NSX_KEV_* */
	uint32_t ucs4;   /* CHAR: caracter; SHORTCUT: letra/numero base */
	int key;         /* KEY: NSX_K_* */
	int mods;        /* NSX_KM_* */
};

/* produtor (USB) */
void nsx_kbd_feed_report(const uint8_t *r, int len);
void nsx_mouse_feed_report(const uint8_t *r, int len);
void nsx_kbd_set_present(bool present);

/* consumidor (thread principal) */
bool nsx_kbd_poll(struct nsx_kev *out);
bool nsx_kbd_pending(void);
void nsx_kbd_flush(void);
bool nsx_kbd_present(void);
bool nsx_mouse_take(int *dx, int *dy, int *wheel, int *buttons);
void nsx_kbd_set_layout(int layout);

/* xbox_usbhid.c: liga o suporte USB (antes do SDL) e trata ligacoes pendentes */
void nsx_usbhid_init(void);
void nsx_usbhid_poll(void);

#endif
