/*
 * NetSurfX: forced include for building QuickJS-ng with nxdk.
 *
 * nxdk targets i386-pc-win32, so QuickJS-ng takes its _WIN32 paths. The
 * engine core only needs _msize; the rest (executable path, threads) is
 * used by the command-line tools and quickjs-libc, which NetSurfX does not
 * build: they are stubs that report failure.
 */
#ifndef NSX_QJS_XBOX_H
#define NSX_QJS_XBOX_H

#include <limits.h>
#include <stddef.h>
#include <windows.h>

size_t dlmalloc_usable_size(void *);
#define _msize(p) dlmalloc_usable_size(p)

#ifndef CP_UTF8
#define CP_UTF8 65001
#endif
#ifndef DUPLICATE_SAME_ACCESS
#define DUPLICATE_SAME_ACCESS 2
#endif

static inline DWORD nsx_qjs_GetModuleFileNameW(void *m, wchar_t *b, DWORD n)
{ (void)m; (void)b; (void)n; return 0; }
#define GetModuleFileNameW nsx_qjs_GetModuleFileNameW

static inline int nsx_qjs_WideCharToMultiByte(unsigned cp, DWORD f, const wchar_t *w, int wl,
					      char *o, int ol, const char *d, int *u)
{ (void)cp; (void)f; (void)w; (void)wl; (void)o; (void)ol; (void)d; (void)u; return 0; }
#define WideCharToMultiByte nsx_qjs_WideCharToMultiByte

static inline uintptr_t nsx_qjs_beginthread(void (*s)(void *), unsigned st, void *a)
{ (void)s; (void)st; (void)a; return 0; }
#define _beginthread nsx_qjs_beginthread

static inline HANDLE nsx_qjs_GetCurrentProcess(void) { return NULL; }
#define GetCurrentProcess nsx_qjs_GetCurrentProcess

static inline BOOL nsx_qjs_DuplicateHandle(HANDLE a, HANDLE b, HANDLE c, HANDLE *d, DWORD e, BOOL f, DWORD g)
{ (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; return FALSE; }
#define DuplicateHandle nsx_qjs_DuplicateHandle

#endif
