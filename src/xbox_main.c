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

#include "xbox_platform.h"

static bool file_exists(const char *p)
{
	return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}

bool nsx_debug_enabled(void)
{
	static int cached = -1;
	if (cached < 0) cached = file_exists("D:\\DEBUG.TXT") ? 1 : 0;
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
