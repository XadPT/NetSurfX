/* NetSurfX - diagnostics: performance, memory, durable log, watchdog, crashes */
#ifndef NSX_XBOX_PERF_H
#define NSX_XBOX_PERF_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* microsecond clock (wraps: use differences only) */
uint32_t nsx_perf_us(void);

/* physical memory (KB) */
uint32_t nsx_mem_free_kb(void);
uint32_t nsx_mem_total_kb(void);

/* NETSURF.LOG: periodic writes that reach the disk (NtFlushBuffersFile),
 * so the log survives a freeze or a power cycle, and a size cap */
void nsx_log_set_fp(FILE *fp);
void nsx_log_tick(void);          /* main thread, once per loop */
void nsx_log_flush(void);         /* to the disk now */

/* counters */
void nsx_perf_fetch_done(uint32_t ms, uint32_t bytes, bool ok);
void nsx_perf_redraw(uint32_t ms);
void nsx_perf_present(uint32_t us);
void nsx_perf_page_start(const char *url);
void nsx_perf_page_done(const char *url);
/* short line for the status bar (on-screen statistics) */
void nsx_perf_status(char *buf, size_t len);

/* Watchdog: if the main thread stops for more than 8 s it is written to
 * D:\NSX-HANG.LOG and shown on screen; after 3 minutes the console returns
 * to the dashboard. Also installs the crash handler (D:\NSX-CRASH.LOG). */
void nsx_watch_start(void);
void nsx_heartbeat(const char *phase);

/* true once a crash screen is up: nobody else may draw */
bool nsx_screen_frozen(void);
/* true (once) after a notice was drawn over the page: repaint everything */
bool nsx_take_full_redraw(void);
/* version shown in hang and crash reports */
void nsx_diag_set_version(const char *v);

/* failed assertions so far (written to D:\NSX-ERROR.LOG) */
unsigned nsx_assert_count(void);

#endif
