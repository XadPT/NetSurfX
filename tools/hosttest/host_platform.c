/*
 * Banco de testes no PC para o NetSurfX: imita o hardware da Xbox.
 *  - ecra: memoria; o guiao pode gravar capturas PNG do que a TV mostraria
 *  - comando: SDL falso controlado por um guiao (NSX_SCRIPT)
 *  - winapi/kernel: pthreads e relogio do sistema
 *  - caminhos "D:\..." -> ./xroot/...
 */
#define HOST_NO_MACROS
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <zlib.h>
#include <png.h>

#include "shim/SDL.h"
#include "shim/hal/video.h"
#include "shim/windows.h"

/* ------------------------------------------------------------ caminhos */

static void tpath(char *out, size_t n, const char *p)
{
	const char *root = getenv("NSX_ROOT") ? getenv("NSX_ROOT") : "xroot";
	if ((p[0] == 'D' || p[0] == 'd') && p[1] == ':') {
		snprintf(out, n, "%s/%s", root, p + 2 + ((p[2] == '\\' || p[2] == '/') ? 1 : 0));
	} else {
		snprintf(out, n, "%s", p);
	}
	for (char *c = out; *c; c++) if (*c == '\\') *c = '/';
	if (getenv("NSX_TRACE")) fprintf(stderr, "[path] %s -> %s\n", p, out);
}

FILE *nsx_fopen(const char *p, const char *m) { char b[1024]; tpath(b, sizeof b, p); return fopen(b, m); }
int nsx_hstat(const char *p, struct stat *s) { char b[1024]; tpath(b, sizeof b, p); return stat(b, s); }
int nsx_haccess(const char *p, int m) { char b[1024]; tpath(b, sizeof b, p); return access(b, m); }
int __real_open(const char *p, int f, ...);
int nsx_hopen(const char *p, int f, ...)
{
	char b[1024]; int mode = 0644; va_list ap;
	va_start(ap, f); if (f & O_CREAT) mode = va_arg(ap, int); va_end(ap);
	tpath(b, sizeof b, p);
	return __real_open(b, f, mode);
}
/* ligado com -Wl,--wrap=open: todos os open() do NetSurf passam por aqui */
int __wrap_open(const char *p, int f, ...)
{
	int mode = 0644; va_list ap;
	va_start(ap, f); if (f & O_CREAT) mode = va_arg(ap, int); va_end(ap);
	return nsx_hopen(p, f, mode);
}
DIR *nsx_hopendir(const char *p) { char b[1024]; tpath(b, sizeof b, p); return opendir(b); }
int nsx_hmkdir(const char *p, mode_t m) { char b[1024]; tpath(b, sizeof b, p); return mkdir(b, m); }
int nsx_hunlink(const char *p) { char b[1024]; tpath(b, sizeof b, p); return unlink(b); }
char *nsx_hrealpath(const char *p, char *r) { if (r == NULL) return strdup(p); strcpy(r, p); return r; }
char *__wrap_realpath(const char *p, char *r) { return nsx_hrealpath(p, r); }  /* como o NetSurf faz na Xbox */
gzFile nsx_hgzopen(const char *p, const char *m) { char b[1024]; tpath(b, sizeof b, p); return gzopen(b, m); }

DWORD GetFileAttributesA(const char *p)
{
	char b[1024]; struct stat st;
	tpath(b, sizeof b, p);
	if (stat(b, &st) != 0) return INVALID_FILE_ATTRIBUTES;
	return S_ISDIR(st.st_mode) ? 0x10 : 0x80;
}
BOOL CreateDirectoryA(const char *p, void *sa) { char b[1024]; (void)sa; tpath(b, sizeof b, p); return mkdir(b, 0755) == 0; }

/* ------------------------------------------------------------ winapi / kernel */

void InitializeCriticalSection(CRITICAL_SECTION *cs)
{
	pthread_mutexattr_t a;
	pthread_mutex_t *m = malloc(sizeof *m);
	pthread_mutexattr_init(&a);
	pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(m, &a);
	cs->opaque[0] = m;
}
void DeleteCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_destroy(cs->opaque[0]); free(cs->opaque[0]); }
void EnterCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_lock(cs->opaque[0]); }
void LeaveCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_unlock(cs->opaque[0]); }

struct thr { LPTHREAD_START_ROUTINE fn; LPVOID arg; };
static void *thr_main(void *a) { struct thr t = *(struct thr *)a; free(a); t.fn(t.arg); return NULL; }
HANDLE CreateThread(void *attr, size_t stack, LPTHREAD_START_ROUTINE fn, LPVOID arg, DWORD flags, DWORD *id)
{
	pthread_t th; pthread_attr_t pa;
	struct thr *t = malloc(sizeof *t);
	(void)attr; (void)flags; (void)id;
	t->fn = fn; t->arg = arg;
	pthread_attr_init(&pa);
	pthread_attr_setstacksize(&pa, stack < 65536 ? 65536 : stack);  /* mesma pilha que na Xbox */
	pthread_attr_setdetachstate(&pa, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&th, &pa, thr_main, t) != 0) { free(t); return NULL; }
	return (HANDLE)1;
}
BOOL CloseHandle(HANDLE h) { (void)h; return 1; }
void Sleep(DWORD ms) { usleep(ms * 1000); }
static uint64_t now_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000ull + ts.tv_nsec / 1000000; }
DWORD GetTickCount(void) { return (DWORD)now_ms(); }
void KeQuerySystemTime(LARGE_INTEGER *t)
{
	struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
	t->QuadPart = (long long)ts.tv_sec * 10000000LL + ts.tv_nsec / 100 + 116444736000000000LL;
}
unsigned long long KeQueryPerformanceCounter(void) { return now_ms() * 1000; }

/* ------------------------------------------------------------ video */

static unsigned char *g_fb;
static VIDEO_MODE g_vm = { 640, 480, 32, 60 };
bool XVideoSetMode(int w, int h, int bpp, int r)
{
	if (getenv("NSX_NO_HD") && w > 640) return false;
	g_vm.width = w; g_vm.height = h; g_vm.bpp = bpp; g_vm.refresh = r;
	free(g_fb);
	g_fb = calloc((size_t)w * h, 4);
	return true;
}
VIDEO_MODE XVideoGetMode(void) { return g_vm; }
unsigned char *XVideoGetFB(void) { return g_fb; }

static void shot(const char *name)
{
	FILE *f = fopen(name, "wb");
	png_structp p = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	png_infop i = png_create_info_struct(p);
	png_init_io(p, f);
	png_set_IHDR(p, i, g_vm.width, g_vm.height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
		     PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_write_info(p, i);
	unsigned char *row = malloc(g_vm.width * 3);
	for (int y = 0; y < g_vm.height; y++) {
		unsigned char *s = g_fb + (size_t)y * g_vm.width * 4;
		for (int x = 0; x < g_vm.width; x++) {   /* A8R8G8B8 em memoria: B G R A */
			row[x * 3 + 0] = s[x * 4 + 2];
			row[x * 3 + 1] = s[x * 4 + 1];
			row[x * 3 + 2] = s[x * 4 + 0];
		}
		png_write_row(p, row);
	}
	png_write_end(p, NULL);
	png_destroy_write_struct(&p, &i);
	free(row);
	fclose(f);
	fprintf(stderr, "[host] captura %s\n", name);
}

void XLaunchXBE(const char *path) { fprintf(stderr, "[host] XLaunchXBE(%s) -> fim\n", path ? path : "dashboard"); exit(0); }
void XReboot(void) { exit(0); }
void debugPrint(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); }
void debugClearScreen(void) {}

/* ------------------------------------------------------------ comando por guiao */
/*  Formato do guiao (uma accao por linha, tempo em ms desde o arranque):
 *     1500 shot nome.png
 *     2000 axis LEFTX 20000        (eixos: LEFTX LEFTY RIGHTX RIGHTY LT RT)
 *     2300 press A                 (botoes: A B X Y START BACK WHITE BLACK UP DOWN LEFT RIGHT L3 R3)
 *     2350 release A
 *     2400 tap A                   (press + release 80 ms depois)
 *     9000 quit
 */
static int axes[6];
static int buttons[SDL_CONTROLLER_BUTTON_MAX];
static SDL_Event q[64]; static int qn;
static FILE *script; static long next_t = -1; static char next_line[256];
static uint64_t t0;
struct pend { long t; int b; } pend[16]; static int npend;

static int btn_from(const char *s)
{
	static const struct { const char *n; int b; } t[] = {
		{"A",SDL_CONTROLLER_BUTTON_A},{"B",SDL_CONTROLLER_BUTTON_B},{"X",SDL_CONTROLLER_BUTTON_X},
		{"Y",SDL_CONTROLLER_BUTTON_Y},{"START",SDL_CONTROLLER_BUTTON_START},{"BACK",SDL_CONTROLLER_BUTTON_BACK},
		{"WHITE",SDL_CONTROLLER_BUTTON_LEFTSHOULDER},{"BLACK",SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},
		{"UP",SDL_CONTROLLER_BUTTON_DPAD_UP},{"DOWN",SDL_CONTROLLER_BUTTON_DPAD_DOWN},
		{"LEFT",SDL_CONTROLLER_BUTTON_DPAD_LEFT},{"RIGHT",SDL_CONTROLLER_BUTTON_DPAD_RIGHT},
		{"L3",SDL_CONTROLLER_BUTTON_LEFTSTICK},{"R3",SDL_CONTROLLER_BUTTON_RIGHTSTICK} };
	for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) if (!strcmp(s, t[i].n)) return t[i].b;
	return -1;
}
static int axis_from(const char *s)
{
	static const char *n[] = { "LEFTX","LEFTY","RIGHTX","RIGHTY","LT","RT" };
	for (int i = 0; i < 6; i++) if (!strcmp(s, n[i])) return i;
	return -1;
}
static void push_btn(int b, bool down)
{
	buttons[b] = down;
	if (qn < 64) {
		memset(&q[qn], 0, sizeof q[qn]);
		q[qn].type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
		q[qn].cbutton.button = (Uint8)b;
		qn++;
	}
}
static void run_script(void)
{
	long now = (long)(now_ms() - t0);
	for (int i = 0; i < npend; i++) if (pend[i].t <= now) { push_btn(pend[i].b, false); pend[i] = pend[--npend]; i--; }
	if (script == NULL) return;
	for (;;) {
		if (next_t < 0) {
			if (!fgets(next_line, sizeof next_line, script)) { fclose(script); script = NULL; return; }
			if (next_line[0] == '#' || next_line[0] == '\n') continue;
			next_t = strtol(next_line, NULL, 10);
		}
		if (next_t > now) return;
		char a[32] = "", b[64] = ""; int v = 0;
		sscanf(next_line, "%*ld %31s %63s %d", a, b, &v);
		fprintf(stderr, "[host] %ld ms: %s %s %d\n", now, a, b, v);
		if (!strcmp(a, "shot")) shot(b);
		else if (!strcmp(a, "axis")) { int x = axis_from(b); if (x >= 0) axes[x] = v; }
		else if (!strcmp(a, "press")) { int x = btn_from(b); if (x >= 0) push_btn(x, true); }
		else if (!strcmp(a, "release")) { int x = btn_from(b); if (x >= 0) push_btn(x, false); }
		else if (!strcmp(a, "tap")) { int x = btn_from(b); if (x >= 0 && npend < 16) { push_btn(x, true); pend[npend].t = now + 80; pend[npend].b = x; npend++; } }
		else if (!strcmp(a, "ptr")) { extern void nsfb_xbox_set_pointer(int, int); int y = v; nsfb_xbox_set_pointer(atoi(b) - 16, y - 12); }
		else if (!strcmp(a, "quit")) { fprintf(stderr, "[host] fim do guiao\n"); exit(0); }
		next_t = -1;
	}
}

int SDL_InitSubSystem(Uint32 f)
{
	(void)f;
	t0 = now_ms();
	if (getenv("NSX_SCRIPT")) script = fopen(getenv("NSX_SCRIPT"), "r");
	return 0;
}
int SDL_PollEvent(SDL_Event *e)
{
	run_script();
	if (qn == 0) return 0;
	*e = q[0];
	memmove(q, q + 1, sizeof q[0] * (size_t)(--qn));
	return 1;
}
Uint32 SDL_GetTicks(void) { return (Uint32)(now_ms() - t0); }
int SDL_NumJoysticks(void) { return 1; }
SDL_bool SDL_IsGameController(int i) { return i == 0; }
SDL_GameController *SDL_GameControllerOpen(int i) { (void)i; return (SDL_GameController *)1; }
void SDL_GameControllerClose(SDL_GameController *c) { (void)c; }
SDL_bool SDL_GameControllerGetAttached(SDL_GameController *c) { (void)c; return 1; }
Sint16 SDL_GameControllerGetAxis(SDL_GameController *c, SDL_GameControllerAxis a)
{
	(void)c; run_script();
	if (a < 0 || a > 5) return 0;
	int v = axes[a];
	if (a >= 4 && v < 0) v = 0;
	return (Sint16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}
Uint8 SDL_GameControllerGetButton(SDL_GameController *c, SDL_GameControllerButton b) { (void)c; run_script(); return (b >= 0 && b < SDL_CONTROLLER_BUTTON_MAX) ? (Uint8)buttons[b] : 0; }
void SDL_GameControllerUpdate(void) { run_script(); }

/* ------------------------------------------------------------ rede (no PC esta sempre ligada) */
void nsx_net_start(void) {}
int nsx_net_wait(int ms) { (void)ms; return 1; }
int nsx_net_state(void) { return 1; }
void nsx_net_describe(char *buf, size_t n) { snprintf(buf, n, "Rede: (teste no PC)"); }
