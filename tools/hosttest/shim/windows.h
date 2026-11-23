/* winapi minima para testes no PC (pthreads por baixo) */
#ifndef FAKE_WINDOWS_H
#define FAKE_WINDOWS_H
#include <stdint.h>
#include <stddef.h>
typedef uint32_t DWORD; typedef int BOOL; typedef void *HANDLE; typedef void *LPVOID; typedef long LONG;
typedef unsigned long long ULONGLONG; typedef long long LONGLONG;
typedef union { struct { uint32_t LowPart; int32_t HighPart; }; long long QuadPart; } LARGE_INTEGER;
typedef struct { void *opaque[8]; } CRITICAL_SECTION;
#define WINAPI
#define MAX_PATH 260
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID);
void InitializeCriticalSection(CRITICAL_SECTION *cs);
void DeleteCriticalSection(CRITICAL_SECTION *cs);
void EnterCriticalSection(CRITICAL_SECTION *cs);
void LeaveCriticalSection(CRITICAL_SECTION *cs);
HANDLE CreateThread(void *attr, size_t stack, LPTHREAD_START_ROUTINE fn, LPVOID arg, DWORD flags, DWORD *id);
BOOL CloseHandle(HANDLE h);
void Sleep(DWORD ms);
DWORD GetTickCount(void);
DWORD GetFileAttributesA(const char *p);
BOOL CreateDirectoryA(const char *p, void *sa);
/* files (diagnostics) */
#define GENERIC_WRITE 0x40000000
#define FILE_SHARE_READ 1
#define OPEN_ALWAYS 4
#define FILE_ATTRIBUTE_NORMAL 0x80
#define FILE_END 2
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
HANDLE CreateFileA(const char *p, DWORD acc, DWORD share, void *sa, DWORD disp, DWORD attr, HANDLE tmpl);
BOOL WriteFile(HANDLE h, const void *b, DWORD n, DWORD *wr, void *ov);
DWORD SetFilePointer(HANDLE h, LONG d, LONG *hi, DWORD how);
static inline LONG InterlockedExchange(volatile LONG *t, LONG v) { return __atomic_exchange_n(t, v, __ATOMIC_SEQ_CST); }
static inline LONG InterlockedIncrement(volatile LONG *t) { return __atomic_add_fetch(t, 1, __ATOMIC_SEQ_CST); }
#endif
