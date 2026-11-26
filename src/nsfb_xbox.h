/* NetSurfX - interface da superficie Xbox para o frontend */
#ifndef NSFB_XBOX_H
#define NSFB_XBOX_H

#include <stdbool.h>
#include <stdint.h>

enum nsx_cmd {
	NSX_CMD_NONE = 0,
	NSX_CMD_BACK,       /* B */
	NSX_CMD_FORWARD,    /* White */
	NSX_CMD_RELOAD,     /* X */
	NSX_CMD_URL,        /* Y: teclado para o endereco */
	NSX_CMD_MENU,       /* Start: pagina inicial / favoritos */
	NSX_CMD_HELP,       /* Back: ajuda */
	NSX_CMD_BOOKMARK,   /* Black: guardar nos favoritos */
	NSX_CMD_PAGE_UP,    /* gatilho L */
	NSX_CMD_PAGE_DOWN,  /* gatilho R */
	NSX_CMD_ZOOM_OUT,   /* clique no analogico esquerdo */
	NSX_CMD_ZOOM_IN,    /* clique no analogico direito */
	NSX_CMD_NAV_UP,     /* D-pad no modo "saltar entre links" */
	NSX_CMD_NAV_DOWN,
	NSX_CMD_NAV_LEFT,
	NSX_CMD_NAV_RIGHT
};

struct nsfb_s;
struct _SDL_GameController;

void nsfb_xbox_register(void);
/* D-pad: true = saltar entre links (comandos NAV), false = setas */
void nsfb_xbox_set_dpad_links(bool on);
int  nsfb_xbox_pop_command(void);
void nsfb_xbox_take_scroll(int *dx, int *dy);
void nsfb_xbox_get_pointer(int *x, int *y);
void nsfb_xbox_set_pointer(int x, int y);
void nsfb_xbox_block_input(bool block);
/* screensaver: time since the last input, and "the user is here" */
uint32_t nsfb_xbox_idle_ms(void);
void nsfb_xbox_note_activity(void);
void nsfb_xbox_present_now(struct nsfb_s *nsfb);
struct _SDL_GameController *nsfb_xbox_pad(void);
void nsfb_xbox_handle_hotplug(const void *sdl_event);

#endif
