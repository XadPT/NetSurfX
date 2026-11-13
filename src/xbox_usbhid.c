/*
 * NetSurfX - teclado e rato USB na Xbox (pilha USB do nxdk)
 *
 * A Xbox so precisa de um adaptador (porta do comando -> USB). Usamos o
 * protocolo "boot" que todos os teclados e ratos suportam: relatorios de
 * 8 bytes (teclado) e 3-4 bytes (rato), tratados em xbox_kbd.c.
 *
 * O controlador HID tem de ser registado ANTES de o SDL arrancar a pilha
 * USB (senao os aparelhos ja ligados nao sao reconhecidos), por isso
 * nsx_usbhid_init() e chamada no inicio do main().
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <usbh_lib.h>
#include <usbh_hid.h>

#include "xbox_kbd.h"

void usbh_core_init(void);
void usbh_xid_init(void);

#define MAX_HID 4

static struct {
	HID_DEV_T *dev;
	bool started;
	bool keyboard;
	int tries;
} g_dev[MAX_HID];

static void hid_read_cb(HID_DEV_T *hdev, uint16_t ep, int status, uint8_t *data, uint32_t len)
{
	(void)ep;
	if (status < 0 || data == NULL) return;
	/* bProtocolCode: 1 teclado, 2 rato */
	if (hdev->bProtocolCode == 1) nsx_kbd_feed_report(data, (int)len);
	else nsx_mouse_feed_report(data, (int)len);
}

/* chamadas durante a enumeracao: so registamos; a configuracao (pedidos de
 * controlo) faz-se depois em nsx_usbhid_poll(), fora do contexto USB */
static void hid_conn(HID_DEV_T *hdev, int param)
{
	(void)param;
	if (hdev->bSubClassCode != 1 || (hdev->bProtocolCode != 1 && hdev->bProtocolCode != 2))
		return;   /* so aparelhos "boot" (teclado/rato) */
	for (int i = 0; i < MAX_HID; i++) {
		if (g_dev[i].dev == NULL) {
			g_dev[i].dev = hdev;
			g_dev[i].started = false;
			g_dev[i].keyboard = hdev->bProtocolCode == 1;
			g_dev[i].tries = 0;
			return;
		}
	}
}

static void hid_disconn(HID_DEV_T *hdev, int param)
{
	(void)param;
	for (int i = 0; i < MAX_HID; i++) {
		if (g_dev[i].dev == hdev) {
			if (g_dev[i].keyboard) nsx_kbd_set_present(false);
			g_dev[i].dev = NULL;
		}
	}
}

void nsx_usbhid_init(void)
{
	usbh_core_init();         /* o SDL volta a chamar: nao faz nada da segunda vez */
	usbh_xid_init();          /* comandos Xbox (o SDL regista outra vez, sem problema) */
	usbh_hid_init();
	usbh_install_hid_conn_callback(hid_conn, hid_disconn);
}

void nsx_usbhid_poll(void)
{
	for (int i = 0; i < MAX_HID; i++) {
		HID_DEV_T *d = g_dev[i].dev;
		if (d == NULL || g_dev[i].started || g_dev[i].tries > 5) continue;
		g_dev[i].tries++;
		usbh_hid_set_protocol(d, 0);     /* 0 = boot */
		usbh_hid_set_idle(d, 0, 0);      /* so relatorios quando algo muda */
		int r = usbh_hid_start_int_read(d, 0, hid_read_cb);
		if (r == 0 || r == HID_RET_XFER_IS_RUNNING) {
			g_dev[i].started = true;
			if (g_dev[i].keyboard) nsx_kbd_set_present(true);
		}
	}
}
