/*
 * NetSurfX - diagnostico (versao de testes: ligado por omissao)
 *
 * - NETSURF.LOG: escrito em blocos e despejado no disco a cada segundo
 *   (num crash perde-se no maximo ~1 s); o da sessao anterior fica em
 *   NETSURF.OLD; acima de 8 MB deixa de escrever (o disco nao enche).
 * - [PERF]: memoria livre, tempo de cada pagina, redesenhos lentos, resumo
 *   periodico; [REDE] (fetch_xbox.c): tempos de cada pedido.
 * - Vigilante: se a thread principal parar mais de 8 s, outra thread
 *   regista em NSX-BLOQUEIO.LOG em que fase estava e o ultimo endereco.
 */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "utils/log.h"
#include "xbox_perf.h"

#ifdef XBOX
#include <xboxkrnl/xboxkrnl.h>
#endif

#define LOG_MAX_BYTES (8L * 1024 * 1024)
#define SUMMARY_MS    30000
#define HANG_MS       8000

/* ---------------------------------------------------------------- relogio fino */

uint32_t nsx_perf_us(void)
{
	static LARGE_INTEGER freq;
	LARGE_INTEGER c;
	if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&c);
	if (freq.QuadPart == 0) return GetTickCount() * 1000u;
	return (uint32_t)(c.QuadPart * 1000000 / freq.QuadPart);
}

/* ---------------------------------------------------------------- memoria */

static void mem_stats(uint32_t *free_kb, uint32_t *total_kb)
{
#ifdef XBOX
	MM_STATISTICS ms;
	memset(&ms, 0, sizeof ms);
	ms.Length = sizeof ms;
	if (MmQueryStatistics(&ms) == 0) {
		*free_kb = ms.AvailablePages * 4;
		*total_kb = ms.TotalPhysicalPages * 4;
		return;
	}
#endif
	*free_kb = 0;
	*total_kb = 0;
}

/* heap do programa (dlmalloc da pdclib): em uso e reservado ao sistema */
#ifdef XBOX
struct nsx_mallinfo {
	size_t arena, ordblks, smblks, hblks, hblkhd, usmblks, fsmblks, uordblks, fordblks, keepcost;
};
extern struct nsx_mallinfo dlmallinfo(void);
extern size_t dlmalloc_footprint(void);
#endif

static void heap_stats(uint32_t *used_kb, uint32_t *foot_kb)
{
#ifdef XBOX
	struct nsx_mallinfo mi = dlmallinfo();
	*used_kb = (uint32_t)(mi.uordblks / 1024);
	*foot_kb = (uint32_t)(dlmalloc_footprint() / 1024);
#else
	*used_kb = *foot_kb = 0;
#endif
}

uint32_t nsx_mem_free_kb(void) { uint32_t f, t; mem_stats(&f, &t); return f; }
uint32_t nsx_mem_total_kb(void) { uint32_t f, t; mem_stats(&f, &t); return t; }

/* ---------------------------------------------------------------- registo */

static FILE *g_fp;
static uint32_t g_last_flush, g_last_summary;
static bool g_capped;

void nsx_log_set_fp(FILE *fp) { g_fp = fp; }

void nsx_log_flush(void)
{
	if (g_fp && verbose_log) fflush(g_fp);
}

/* ---------------------------------------------------------------- contadores */

static struct {
	uint32_t fetches, fetch_fail, fetch_ms;
	uint64_t fetch_bytes;
	uint32_t redraws, redraw_ms, redraw_max, slow_redraws;
	uint32_t presents;
	uint64_t present_us;
	uint32_t page_t0, last_page_ms, pages;
	char page_url[160];
	uint32_t min_free_kb;
	bool low_mem_warned;
} P;

void nsx_perf_fetch_done(uint32_t ms, uint32_t bytes, bool ok)
{
	P.fetches++;
	if (!ok) P.fetch_fail++;
	P.fetch_ms += ms;
	P.fetch_bytes += bytes;
}

void nsx_perf_redraw(uint32_t ms)
{
	P.redraws++;
	P.redraw_ms += ms;
	if (ms > P.redraw_max) P.redraw_max = ms;
	if (ms >= 150) {
		P.slow_redraws++;
		NSLOG(netsurf, INFO, "[PERF] redesenho lento: %u ms (%s)", (unsigned)ms, P.page_url);
	}
}

void nsx_perf_present(uint32_t us)
{
	P.presents++;
	P.present_us += us;
}

void nsx_perf_page_start(const char *url)
{
	P.page_t0 = GetTickCount();
	snprintf(P.page_url, sizeof P.page_url, "%s", url ? url : "");
	nsx_heartbeat(NULL);
}

void nsx_perf_page_done(const char *url)
{
	uint32_t free_kb, total_kb;
	if (P.page_t0 == 0) return;
	if (url && url[0]) snprintf(P.page_url, sizeof P.page_url, "%s", url);
	P.last_page_ms = GetTickCount() - P.page_t0;
	P.page_t0 = 0;
	P.pages++;
	mem_stats(&free_kb, &total_kb);
	{
		uint32_t used, foot;
		heap_stats(&used, &foot);
		NSLOG(netsurf, INFO, "[PERF] pagina %s: %u ms, memoria livre %u KB de %u KB, heap em uso %u KB (reservado %u KB)",
		      P.page_url, (unsigned)P.last_page_ms, (unsigned)free_kb, (unsigned)total_kb,
		      (unsigned)used, (unsigned)foot);
	}
	nsx_log_flush();
}

void nsx_perf_status(char *buf, size_t len)
{
	uint32_t free_kb, total_kb;
	mem_stats(&free_kb, &total_kb);
	snprintf(buf, len, "Livre %u,%u MB | p\xc3\xa1gina %u,%u s | desenho m\xc3\xa1x %u ms | %u pedidos",
		 (unsigned)(free_kb / 1024), (unsigned)((free_kb % 1024) * 10 / 1024),
		 (unsigned)(P.last_page_ms / 1000), (unsigned)((P.last_page_ms % 1000) / 100),
		 (unsigned)P.redraw_max, (unsigned)P.fetches);
}

static void summary(void)
{
	uint32_t free_kb, total_kb, used, foot;
	mem_stats(&free_kb, &total_kb);
	heap_stats(&used, &foot);
	if (P.min_free_kb == 0 || free_kb < P.min_free_kb) P.min_free_kb = free_kb;
	NSLOG(netsurf, INFO,
	      "[PERF] resumo: livre %u KB (min %u), heap %u/%u KB, paginas %u, pedidos %u (falhas %u, %u KB, media %u ms), "
	      "redesenhos %u (media %u ms, max %u, lentos %u), ecra %u (media %u us)",
	      (unsigned)free_kb, (unsigned)P.min_free_kb, (unsigned)used, (unsigned)foot, (unsigned)P.pages,
	      (unsigned)P.fetches, (unsigned)P.fetch_fail, (unsigned)(P.fetch_bytes / 1024),
	      P.fetches ? (unsigned)(P.fetch_ms / P.fetches) : 0,
	      (unsigned)P.redraws, P.redraws ? (unsigned)(P.redraw_ms / P.redraws) : 0,
	      (unsigned)P.redraw_max, (unsigned)P.slow_redraws,
	      (unsigned)P.presents, P.presents ? (unsigned)(P.present_us / P.presents) : 0);
	if (free_kb && free_kb < 4096 && !P.low_mem_warned) {
		P.low_mem_warned = true;
		NSLOG(netsurf, WARNING, "[PERF] POUCA MEMORIA: %u KB livres", (unsigned)free_kb);
	}
}

void nsx_log_tick(void)
{
	uint32_t now = GetTickCount();
	if (now - g_last_summary >= SUMMARY_MS) {
		g_last_summary = now;
		summary();
	}
	if (g_fp && verbose_log && now - g_last_flush >= 1000) {
		g_last_flush = now;
		fflush(g_fp);
		if (!g_capped && ftell(g_fp) > LOG_MAX_BYTES) {
			g_capped = true;
			fputs("\n*** NETSURF.LOG chegou a 8 MB: registo parado ***\n", g_fp);
			fflush(g_fp);
			verbose_log = false;
		}
	}
	nsx_heartbeat(NULL);
}

/* ---------------------------------------------------------------- vigilante */

static volatile uint32_t g_beat;
static const char *volatile g_phase = "arranque";
static bool g_watch;

void nsx_heartbeat(const char *phase)
{
	g_beat = GetTickCount();
	if (phase) g_phase = phase;
}

static DWORD WINAPI watch_thread(LPVOID arg)
{
	bool reported = false;
	(void)arg;
	for (;;) {
		uint32_t now = GetTickCount(), beat = g_beat;
		Sleep(1000);
		if (beat && now - beat > HANG_MS) {
			if (!reported) {
				FILE *f = fopen("D:\\NSX-BLOQUEIO.LOG", "a");
				reported = true;
				if (f) {
					uint32_t fk, tk;
					mem_stats(&fk, &tk);
					fprintf(f, "Bloqueio: a thread principal nao responde ha %u ms\n"
						"  fase: %s\n  pagina: %s\n  memoria livre: %u KB\n"
						"  pedidos: %u, redesenhos: %u (max %u ms)\n\n",
						(unsigned)(now - beat), g_phase, P.page_url, (unsigned)fk,
						(unsigned)P.fetches, (unsigned)P.redraws, (unsigned)P.redraw_max);
					fclose(f);
				}
			}
		} else if (reported) {
			FILE *f = fopen("D:\\NSX-BLOQUEIO.LOG", "a");
			if (f) {
				fprintf(f, "  ... recuperou (fase %s)\n\n", g_phase);
				fclose(f);
			}
			reported = false;
		}
	}
	return 0;
}

void nsx_watch_start(void)
{
	if (g_watch) return;
	g_watch = true;
	g_beat = GetTickCount();
	CreateThread(NULL, 32 * 1024, watch_thread, NULL, 0, NULL);
}

/* ---------------------------------------------------------------- asserts */

/*
 * O assert() do nxdk escreve a mensagem e PARA a consola (cli; hlt). Na
 * versao de testes preferimos registar e continuar: muitas verificacoes
 * do NetSurf sao de situacoes que se recuperam, e assim ficamos com o
 * registo do problema em vez de uma Xbox encravada.
 */
static volatile unsigned g_asserts;

unsigned nsx_assert_count(void) { return g_asserts; }

#ifdef XBOX
void _xbox_assert(char const *const expression, char const *const file_name,
		  char const *const function_name, unsigned long line)
{
	unsigned n = ++g_asserts;
	if (n > 200) return;              /* em ciclo: nao encher o disco */
	NSLOG(netsurf, ERROR, "[ERRO] assert falhou: '%s' em %s (%s:%lu) - pagina %s",
	      expression, function_name, file_name, line, P.page_url);
	nsx_log_flush();
	{
		FILE *f = fopen("D:\\NSX-ERRO.LOG", "a");
		if (f) {
			fprintf(f, "assert falhou: '%s'\n  em %s (%s:%lu)\n  pagina: %s\n\n",
				expression, function_name, file_name, line, P.page_url);
			fclose(f);
		}
	}
}
#endif
