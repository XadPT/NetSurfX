/*
 * NetSurfX - diagnostics (test builds: on by default, off with NODEBUG.TXT)
 *
 * - NETSURF.LOG is written in blocks and pushed to the disk itself
 *   (NtFlushBuffersFile) every 2 s and at every important event: fflush()
 *   alone only reaches the kernel's file cache, which a freeze or a power
 *   cycle throws away. The previous session is kept as NETSURF.OLD; above
 *   8 MB logging stops so the disk never fills.
 * - [PERF] lines: free memory, time per page, slow redraws, a periodic
 *   summary; [NET] lines (fetch_xbox.c): timings of every request.
 * - Watchdog thread: if the main thread stops for more than 8 s it writes
 *   D:\NSX-HANG.LOG (phase, page, memory) and shows a notice on screen; if
 *   the hang lasts 3 minutes it returns to the dashboard instead of leaving
 *   the console stuck.
 * - Crash handler: every thread runs inside an SEH frame (nxdk patch in
 *   tools/patch_nxdk.py). An unhandled exception writes D:\NSX-CRASH.LOG
 *   (exception, registers, likely call stack), shows a crash screen and
 *   returns to the dashboard. At DISPATCH_LEVEL (network interrupt path)
 *   nothing touches the file system: screen and reboot only.
 * - Failed assertions are logged and the program continues (nxdk's own
 *   assert halts the CPU with cli/hlt); at DISPATCH_LEVEL they are only
 *   counted and written later by the main thread.
 *
 * Addresses in NSX-CRASH.LOG are virtual addresses: look them up in the
 * nsx.map produced by the same build (tools/symbolize.py).
 */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "utils/log.h"
#include "xbox_perf.h"

#ifdef XBOX
#include <xboxkrnl/xboxkrnl.h>
#include <hal/debug.h>
#include <hal/video.h>
#include <errhandlingapi.h>
#endif

#define LOG_MAX_BYTES  (8L * 1024 * 1024)
#define SUMMARY_MS     30000
#define DISK_FLUSH_MS  2000
#define HANG_MS        8000
#define HANG_GIVEUP_MS (180u * 1000)
#define CRASH_WAIT_S   30

static const char *g_version = "?";
void nsx_diag_set_version(const char *v) { g_version = v ? v : "?"; }

/* ---------------------------------------------------------------- clock */

uint32_t nsx_perf_us(void)
{
	static LARGE_INTEGER freq;
	LARGE_INTEGER c;
	if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&c);
	if (freq.QuadPart == 0) return GetTickCount() * 1000u;
	return (uint32_t)(c.QuadPart * 1000000 / freq.QuadPart);
}

/* ---------------------------------------------------------------- memory */

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
#else
	{
		/* PC test harness: pretend free memory (memory guard tests) */
		const char *e = getenv("NSX_FAKE_FREE_KB");
		if (e != NULL) {
			*free_kb = (uint32_t)strtoul(e, NULL, 10);
			*total_kb = 65536;
			return;
		}
	}
#endif
	*free_kb = 0;
	*total_kb = 0;
}

/* program heap (pdclib's dlmalloc): in use and taken from the system */
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

#ifdef XBOX
extern int dlmalloc_trim(size_t pad);
#endif

/*
 * Memory the browser can still use: free system pages plus the part of the
 * heap that is free inside it. dlmalloc keeps freed memory for reuse and
 * seldom gives it back to the system, so after a heavy page the system
 * figure stays low although the heap has megabytes free. The heap walk
 * costs time, so callers use this only when the system figure is low.
 */
uint32_t nsx_mem_avail_kb(void)
{
	uint32_t f = nsx_mem_free_kb();
#ifdef XBOX
	uint32_t used, foot;
	heap_stats(&used, &foot);
	if (foot > used)
		f += foot - used;
#endif
	return f;
}

/* gives whole free heap segments back to the system (dlmalloc_trim) */
void nsx_mem_trim(void)
{
#ifdef XBOX
	dlmalloc_trim(64 * 1024);
#endif
}
uint32_t nsx_mem_total_kb(void) { uint32_t f, t; mem_stats(&f, &t); return t; }

/* ---------------------------------------------------------------- IRQL */

static bool can_do_io(void)
{
#ifdef XBOX
	return KeGetCurrentIrql() < DISPATCH_LEVEL;
#else
	return true;
#endif
}

/* ---------------------------------------------------------------- files that reach the disk */

/* push a stdio stream's data, already handed to the kernel, onto the disk */
static void disk_flush_fp(FILE *fp)
{
#ifdef XBOX
	/* pdclib's FILE starts with the kernel HANDLE (struct _PDCLIB_file_t) */
	HANDLE h = *(HANDLE *)fp;
	IO_STATUS_BLOCK io;
	if (h != NULL && h != INVALID_HANDLE_VALUE)
		NtFlushBuffersFile(h, &io);
#else
	(void)fp;
#endif
}

/* append text to a file and put it on the disk (PASSIVE_LEVEL only; no
 * stdio, no heap: usable from the crash handler) */
static void append_durable(const char *path, const char *text, size_t len)
{
	HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
			       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD wr;
	if (h == INVALID_HANDLE_VALUE) return;
	SetFilePointer(h, 0, NULL, FILE_END);
	WriteFile(h, text, (DWORD)len, &wr, NULL);
#ifdef XBOX
	{
		IO_STATUS_BLOCK io;
		NtFlushBuffersFile(h, &io);
	}
#endif
	CloseHandle(h);
}

void nsx_diag_note(const char *path, const char *text)
{
	if (path && text && can_do_io())
		append_durable(path, text, strlen(text));
}

/* ---------------------------------------------------------------- NETSURF.LOG */

static FILE *g_fp;
static uint32_t g_last_flush, g_last_disk, g_last_summary;
static long g_disk_pos;
static bool g_capped;

void nsx_log_set_fp(FILE *fp) { g_fp = fp; }

void nsx_log_flush(void)
{
	if (g_fp == NULL || !verbose_log || !can_do_io()) return;
	fflush(g_fp);
	disk_flush_fp(g_fp);
	g_last_flush = g_last_disk = GetTickCount();
	g_disk_pos = ftell(g_fp);
}

/* ---------------------------------------------------------------- counters */

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
		NSLOG(netsurf, INFO, "[PERF] slow redraw: %u ms (%s)", (unsigned)ms, P.page_url);
	}
}

void nsx_perf_present(uint32_t us)
{
	P.presents++;
	P.present_us += us;
}

/* the page URL is also read by the watchdog thread */
static CRITICAL_SECTION g_url_lock;
static bool g_url_lock_ok;

static void set_page_url(const char *url)
{
	if (g_url_lock_ok) EnterCriticalSection(&g_url_lock);
	snprintf(P.page_url, sizeof P.page_url, "%s", url ? url : "");
	if (g_url_lock_ok) LeaveCriticalSection(&g_url_lock);
}

static void get_page_url(char *out, size_t n)
{
	if (g_url_lock_ok) EnterCriticalSection(&g_url_lock);
	snprintf(out, n, "%s", P.page_url);
	if (g_url_lock_ok) LeaveCriticalSection(&g_url_lock);
}

void nsx_perf_page_start(const char *url)
{
	P.page_t0 = GetTickCount();
	set_page_url(url);
	NSLOG(netsurf, INFO, "[PERF] loading %s", P.page_url);
	nsx_log_flush();        /* the page being opened is on disk before anything else */
	nsx_heartbeat(NULL);
}

void nsx_perf_page_done(const char *url)
{
	uint32_t free_kb, total_kb, used, foot;
	if (P.page_t0 == 0) return;
	if (url && url[0]) set_page_url(url);
	P.last_page_ms = GetTickCount() - P.page_t0;
	P.page_t0 = 0;
	P.pages++;
	mem_stats(&free_kb, &total_kb);
	heap_stats(&used, &foot);
	NSLOG(netsurf, INFO, "[PERF] page %s: %u ms, free memory %u KB of %u KB, heap in use %u KB (reserved %u KB)",
	      P.page_url, (unsigned)P.last_page_ms, (unsigned)free_kb, (unsigned)total_kb,
	      (unsigned)used, (unsigned)foot);
	nsx_log_flush();
}

void nsx_perf_status(char *buf, size_t len)
{
	uint32_t free_kb, total_kb;
	mem_stats(&free_kb, &total_kb);
	if (free_kb < 12 * 1024)          /* low: add the heap's free space */
		free_kb = nsx_mem_avail_kb();
	snprintf(buf, len, "Free %u.%u MB | page %u.%u s | paint max %u ms | %u requests",
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
	      "[PERF] summary: free %u KB (min %u), heap %u/%u KB, pages %u, requests %u (failed %u, %u KB, avg %u ms), "
	      "redraws %u (avg %u ms, max %u, slow %u), presents %u (avg %u us), asserts %u",
	      (unsigned)free_kb, (unsigned)P.min_free_kb, (unsigned)used, (unsigned)foot, (unsigned)P.pages,
	      (unsigned)P.fetches, (unsigned)P.fetch_fail, (unsigned)(P.fetch_bytes / 1024),
	      P.fetches ? (unsigned)(P.fetch_ms / P.fetches) : 0,
	      (unsigned)P.redraws, P.redraws ? (unsigned)(P.redraw_ms / P.redraws) : 0,
	      (unsigned)P.redraw_max, (unsigned)P.slow_redraws,
	      (unsigned)P.presents, P.presents ? (unsigned)(P.present_us / P.presents) : 0,
	      nsx_assert_count());
	if (free_kb && free_kb < 4096 && !P.low_mem_warned) {
		P.low_mem_warned = true;
		NSLOG(netsurf, WARNING, "[PERF] LOW MEMORY: %u KB free", (unsigned)free_kb);
	}
}

static void write_pending_asserts(void);

void nsx_log_tick(void)
{
	uint32_t now = GetTickCount();
	write_pending_asserts();
	if (now - g_last_summary >= SUMMARY_MS) {
		g_last_summary = now;
		summary();
	}
	if (g_fp && verbose_log && now - g_last_flush >= 1000) {
		long pos;
		g_last_flush = now;
		fflush(g_fp);
		pos = ftell(g_fp);
		if (pos != g_disk_pos && now - g_last_disk >= DISK_FLUSH_MS) {
			disk_flush_fp(g_fp);
			g_last_disk = now;
			g_disk_pos = pos;
		}
		if (!g_capped && pos > LOG_MAX_BYTES) {
			g_capped = true;
			fputs("\n*** NETSURF.LOG reached 8 MB: logging stopped ***\n", g_fp);
			nsx_log_flush();
			verbose_log = false;
		}
	}
	nsx_heartbeat(NULL);
}

/* ---------------------------------------------------------------- screen messages */

static volatile LONG g_frozen;
static volatile LONG g_overlay;

bool nsx_screen_frozen(void) { return __atomic_load_n(&g_frozen, __ATOMIC_RELAXED) != 0; }

bool nsx_take_full_redraw(void)
{
	return InterlockedExchange(&g_overlay, 0) != 0;
}

#ifdef XBOX
/* black band over the top lines of the screen, then text on it */
static void screen_band(int lines)
{
	VIDEO_MODE vm = XVideoGetMode();
	uint8_t *fb = XVideoGetFB();
	int bpp = (vm.bpp + 7) / 8;
	int rows = lines * 17 + 24;
	if (fb == NULL || vm.width <= 0) return;
	if (rows > vm.height) rows = vm.height;
	memset(fb, 0, (size_t)rows * vm.width * bpp);
	debugMoveCursor(24, 12);
}
#endif

/* ---------------------------------------------------------------- watchdog */

/* written by the main thread, read by the watchdog: atomic, no lock */
static uint32_t g_beat;
static const char *g_phase = "start-up";
#define BEAT_GET()   __atomic_load_n(&g_beat, __ATOMIC_RELAXED)
#define PHASE_GET()  __atomic_load_n(&g_phase, __ATOMIC_RELAXED)
static bool g_watch;

void nsx_heartbeat(const char *phase)
{
	__atomic_store_n(&g_beat, GetTickCount(), __ATOMIC_RELAXED);
	if (phase) __atomic_store_n(&g_phase, phase, __ATOMIC_RELAXED);
}

static void reboot_to_dashboard(void)
{
#ifdef XBOX
	HalReturnToFirmware(HalRebootRoutine);
#endif
}

static DWORD WINAPI watch_thread(LPVOID arg)
{
	bool reported = false;
	(void)arg;
	for (;;) {
		uint32_t now, beat;
		Sleep(1000);
		if (nsx_screen_frozen()) continue;
		now = GetTickCount();
		beat = BEAT_GET();
		if (beat && now - beat > HANG_MS) {
			if (!reported) {
				char t[640], url[160];
				uint32_t fk, tk;
				int n;
				reported = true;
				mem_stats(&fk, &tk);
				get_page_url(url, sizeof url);
				n = snprintf(t, sizeof t,
					"NetSurfX %s - hang: the main thread has not responded for %u ms\n"
					"  phase: %s\n  page: %s\n  free memory: %u KB, asserts: %u\n\n",
					g_version, (unsigned)(now - beat), PHASE_GET(), url, (unsigned)fk,
					nsx_assert_count());
				append_durable("D:\\NSX-HANG.LOG", t, n > 0 ? (size_t)n : 0);
				if (g_fp) disk_flush_fp(g_fp);
#ifdef XBOX
				InterlockedExchange(&g_overlay, 1);
				screen_band(3);
				debugPrint("NetSurfX is not responding (%s). Still waiting...\n", PHASE_GET());
				debugPrint("Saved to D:\\NSX-HANG.LOG. Back to the dashboard after 3 minutes.\n");
#endif
			} else if (now - beat > HANG_GIVEUP_MS) {
				static const char m[] = "  ... still hung after 3 minutes: returning to the dashboard\n\n";
				append_durable("D:\\NSX-HANG.LOG", m, sizeof m - 1);
				if (g_fp) disk_flush_fp(g_fp);
				reboot_to_dashboard();
			}
		} else if (reported) {
			char t[160];
			int n = snprintf(t, sizeof t, "  ... recovered after %u ms (phase %s)\n\n",
					 (unsigned)(GetTickCount() - beat), PHASE_GET());
			append_durable("D:\\NSX-HANG.LOG", t, n > 0 ? (size_t)n : 0);
			reported = false;
		}
	}
	return 0;
}

/* ---------------------------------------------------------------- crashes */

#ifdef XBOX
static const char *exc_name(DWORD code)
{
	switch (code) {
	case 0xC0000005: return "access violation";
	case 0xC00000FD: return "stack overflow";
	case 0xC0000094: return "integer divide by zero";
	case 0xC0000095: return "integer overflow";
	case 0xC000001D: return "illegal instruction";
	case 0xC0000096: return "privileged instruction";
	case 0xC000008C: return "array bounds exceeded";
	case 0xC000008E: return "float divide by zero";
	case 0xC0000090: return "invalid float operation";
	case 0x80000003: return "breakpoint";
	case 0x80000004: return "single step";
	case 0xC0000006: return "page fault on I/O";
	default:         return "exception";
	}
}

/* is p inside the loaded XBE image? */
static bool in_image(uint32_t p)
{
	uint32_t lo = CURRENT_XBE_HEADER->ImageBase;
	uint32_t hi = lo + CURRENT_XBE_HEADER->SizeOfImage;
	return p >= lo + 0x1000 && p < hi;
}

/* a return address follows a call instruction */
static bool after_call(uint32_t ra)
{
	const uint8_t *c = (const uint8_t *)ra;
	if (!in_image(ra - 7)) return false;
	if (c[-5] == 0xE8) return true;                              /* call rel32 */
	if (c[-2] == 0xFF && (c[-1] & 0x38) == 0x10) return true;    /* call reg / [reg] */
	if (c[-3] == 0xFF && (c[-2] & 0x38) == 0x10) return true;    /* call [reg+d8] */
	if (c[-6] == 0xFF && (c[-5] & 0x38) == 0x10) return true;    /* call [d32] / [reg+d32] */
	if (c[-7] == 0xFF && (c[-6] & 0x38) == 0x10) return true;    /* call [sib+d32] */
	return false;
}

static volatile LONG g_crashing;

static LONG __stdcall crash_filter(struct _EXCEPTION_POINTERS *e)
{
	static char t[4096];
	const EXCEPTION_RECORD *er = e->ExceptionRecord;
	const CONTEXT *c = e->ContextRecord;
	bool io = can_do_io();
	PKTHREAD th = KeGetCurrentThread();
	int n = 0, frames = 0;
	uint32_t fk = 0, tk = 0;

	if (InterlockedExchange(&g_crashing, 1)) {
		/* a second thread crashing while the first one reports */
		if (io) for (;;) Sleep(1000);
		for (;;) __asm__ volatile("pause");
	}
	InterlockedExchange(&g_frozen, 1);
	mem_stats(&fk, &tk);

#define ADD(...) do { if (n < (int)sizeof t - 1) { int k_ = snprintf(t + n, sizeof t - n, __VA_ARGS__); if (k_ > 0) n += k_; if (n > (int)sizeof t - 1) n = sizeof t - 1; } } while (0)
	ADD("==== NetSurfX %s crash ====\n", g_version);
	ADD("exception %08lx (%s) at %08lx, flags %lx, irql %u, thread %p\n",
	    (unsigned long)er->ExceptionCode, exc_name(er->ExceptionCode),
	    (unsigned long)(uintptr_t)er->ExceptionAddress, (unsigned long)er->ExceptionFlags,
	    (unsigned)KeGetCurrentIrql(), (void *)th);
	if (er->ExceptionCode == 0xC0000005 && er->NumberParameters >= 2)
		ADD("  %s address %08lx\n", er->ExceptionInformation[0] == 0 ? "reading" :
		    er->ExceptionInformation[0] == 1 ? "writing" : "executing",
		    (unsigned long)er->ExceptionInformation[1]);
	ADD("eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx\n",
	    c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi);
	ADD("eip %08lx esp %08lx ebp %08lx eflags %08lx\n", c->Eip, c->Esp, c->Ebp, c->EFlags);
	ADD("phase: %s\npage: %s\nfree memory: %u KB of %u KB, asserts %u\n",
	    PHASE_GET(), P.page_url, (unsigned)fk, (unsigned)tk, nsx_assert_count());

	/* likely call stack: words on the stack that point just after a call */
	ADD("stack (likely return addresses, look up in nsx.map):\n ");
	{
		uint32_t sp = c->Esp & ~3u;
		uint32_t top = th ? (uint32_t)(uintptr_t)th->StackBase : sp + 16384;
		uint32_t lim = th ? (uint32_t)(uintptr_t)th->StackLimit : sp;
		if (sp < lim || sp >= top) top = sp;     /* not this thread's stack: don't walk */
		if (top - sp > 32768) top = sp + 32768;
		for (uint32_t a = sp; a + 4 <= top && frames < 32; a += 4) {
			uint32_t v = *(const uint32_t *)a;
			if (in_image(v) && after_call(v)) {
				ADD(" %08lx", (unsigned long)v);
				if (++frames % 8 == 0) ADD("\n ");
			}
		}
	}
	ADD("\n\n");
#undef ADD

	if (io) {
		append_durable("D:\\NSX-CRASH.LOG", t, (size_t)n);
		if (g_fp) disk_flush_fp(g_fp);
	}

	debugClearScreen();
	debugMoveCursor(24, 24);
	debugPrint("NetSurfX has crashed.\n\n");
	debugPrint("%s at %08lx", exc_name(er->ExceptionCode), (unsigned long)c->Eip);
	if (er->ExceptionCode == 0xC0000005 && er->NumberParameters >= 2)
		debugPrint(" (address %08lx)", (unsigned long)er->ExceptionInformation[1]);
	debugPrint("\nphase: %s\npage: %.60s\n\n", PHASE_GET(), P.page_url);
	if (io)
		debugPrint("The details are in D:\\NSX-CRASH.LOG.\nPlease send it, with D:\\NETSURF.LOG, to the developer.\n\n");
	else
		debugPrint("This happened in the network driver; nothing could be saved.\nA photo of this screen helps.\n\n");
	debugPrint("Returning to the dashboard in %d seconds...\n", CRASH_WAIT_S);

	if (io) {
		Sleep(CRASH_WAIT_S * 1000);
	} else {
		for (int i = 0; i < CRASH_WAIT_S * 1000; i++) KeStallExecutionProcessor(1000);
	}
	reboot_to_dashboard();
	return EXCEPTION_EXECUTE_HANDLER;
}
#endif

void nsx_watch_start(void)
{
	if (g_watch) return;
	g_watch = true;
	InitializeCriticalSection(&g_url_lock);
	g_url_lock_ok = true;
	nsx_heartbeat(NULL);
#ifdef XBOX
	SetUnhandledExceptionFilter(crash_filter);
#endif
	CreateThread(NULL, 32 * 1024, watch_thread, NULL, 0, NULL);
}

/* ---------------------------------------------------------------- asserts */

/*
 * nxdk's assert() prints the message and HALTS the console (cli; hlt).
 * Test builds log and carry on instead: many NetSurf checks guard
 * recoverable situations, and a log beats a frozen Xbox. At DISPATCH_LEVEL
 * (network interrupt path) no file may be touched: the assertion is kept
 * in memory and written by the main thread on its next loop.
 */
static volatile LONG g_asserts, g_asserts_written;
static struct { const char *expr, *file, *func; unsigned long line; } g_dpc_assert;

unsigned nsx_assert_count(void) { return (unsigned)__atomic_load_n(&g_asserts, __ATOMIC_RELAXED); }

static void write_assert(const char *expression, const char *file_name,
			 const char *function_name, unsigned long line)
{
	char t[512];
	int n;
	NSLOG(netsurf, ERROR, "[ERROR] assertion failed: '%s' in %s (%s:%lu) - page %s",
	      expression, function_name, file_name, line, P.page_url);
	nsx_log_flush();
	n = snprintf(t, sizeof t, "assertion failed: '%s'\n  in %s (%s:%lu)\n  page: %s\n\n",
		     expression, function_name, file_name, line, P.page_url);
	append_durable("D:\\NSX-ERROR.LOG", t, n > 0 ? (size_t)n : 0);
}

static void write_pending_asserts(void)
{
	if (g_dpc_assert.expr != NULL) {
		write_assert(g_dpc_assert.expr, g_dpc_assert.file ? g_dpc_assert.file : "?",
			     g_dpc_assert.func ? g_dpc_assert.func : "?", g_dpc_assert.line);
		g_dpc_assert.expr = NULL;
	}
}

#ifdef XBOX
void _xbox_assert(char const *const expression, char const *const file_name,
		  char const *const function_name, unsigned long line)
{
	LONG n = InterlockedIncrement(&g_asserts);
	if (!can_do_io()) {
		if (g_dpc_assert.expr == NULL) {
			g_dpc_assert.file = file_name;
			g_dpc_assert.func = function_name;
			g_dpc_assert.line = line;
			g_dpc_assert.expr = expression;
		}
		return;
	}
	if (n > 200) return;              /* in a loop: don't fill the disk */
	InterlockedIncrement(&g_asserts_written);
	write_assert(expression, file_name, function_name, line);
}
#endif
