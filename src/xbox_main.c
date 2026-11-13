/*
 * NetSurfX - arranque especifico da Xbox (video, diagnostico, erros fatais).
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <windows.h>
#include <hal/video.h>
#include <hal/debug.h>
#include <hal/xbox.h>

#include <nxdk/mount.h>

#include "xbox_platform.h"

static bool file_exists(const char *p)
{
	return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}

/* versao de testes: o registo esta ligado por omissao; um NODEBUG.TXT ao
 * lado do default.xbe desliga-o */
bool nsx_debug_enabled(void)
{
	static int cached = -1;
	if (cached < 0) cached = file_exists("D:\\NODEBUG.TXT") ? 0 : 1;
	return cached == 1;
}

/* escolhe o modo de video; devolve o tamanho do ecra */
void nsx_video_init(bool want_hd, int *w, int *h)
{
	if (want_hd && XVideoSetMode(1280, 720, 32, REFRESH_DEFAULT)) {
		*w = 1280; *h = 720;
		return;
	}
	if (!XVideoSetMode(640, 480, 32, REFRESH_DEFAULT))
		XVideoSetMode(640, 480, 16, REFRESH_DEFAULT);
	*w = 640; *h = 480;
}

void nsx_fatal(const char *msg)
{
	/* sem a interface grafica: escreve no ecra com a consola do nxdk */
	debugClearScreen();
	debugPrint("\n\n   NetSurfX nao conseguiu arrancar:\n\n   %s\n\n", msg);
	debugPrint("   Confirma que a pasta 'res' esta ao lado do default.xbe.\n");
	debugPrint("   A voltar ao dashboard dentro de 15 segundos...\n");
	Sleep(15000);
	for (;;) {
		XLaunchXBE(NULL);
		Sleep(500);
	}
}

/* o nxdk so liga D: (a pasta do XBE); E:, F: e G: sao as particoes do
 * disco onde ficam os downloads (F:/G: so existem em discos maiores) */
void nsx_mount_drives(void)
{
	static const struct { char l; const char *p; } d[] = {
		{ 'E', "\\Device\\Harddisk0\\Partition1\\" },
		{ 'F', "\\Device\\Harddisk0\\Partition6\\" },
		{ 'G', "\\Device\\Harddisk0\\Partition7\\" },
	};
	for (unsigned i = 0; i < sizeof d / sizeof d[0]; i++)
		if (!nxIsDriveMounted(d[i].l))
			nxMountDrive(d[i].l, d[i].p);
}
