/*
 * NetSurfX - camada POSIX minima sobre o nxdk (pdclib + winapi).
 *
 * O NetSurf espera um sistema POSIX. O nxdk tem pdclib (C puro) e uma winapi
 * reduzida, por isso aqui ficam: strings, ficheiros com descritores, stat,
 * diretorios, relogio, iconv (UTF-8/UTF-16/Latin-1/CP1252) e leitura gz*.
 *
 * Todos os caminhos passam por nsx_path(), que troca '/' por '\' (o kernel da
 * Xbox so aceita '\') e mantem o resto igual.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <zlib.h>

#include "sys/stat.h"
#include "sys/time.h"
#include "unistd.h"
#include "fcntl.h"
#include "dirent.h"
#include "iconv.h"

#undef fopen
#undef remove
#undef rename

/* ------------------------------------------------------------------ */
/* caminhos                                                            */

static void nsx_path(char *dst, size_t n, const char *src)
{
	size_t i = 0;
	if (n == 0) return;
	for (; src[i] && i + 1 < n; i++)
		dst[i] = (src[i] == '/') ? '\\' : src[i];
	dst[i] = 0;
	/* "D:\x\\y" -> "D:\x\y" (o filepath do NetSurf junta com '/') */
	for (char *p = dst; *p; p++) {
		while (p[0] == '\\' && p[1] == '\\' && p != dst)
			memmove(p, p + 1, strlen(p));
	}
	/* sem barra final, excepto na raiz "D:\" */
	size_t l = strlen(dst);
	if (l > 3 && dst[l - 1] == '\\') dst[l - 1] = 0;
}

FILE *nsx_fopen(const char *path, const char *mode)
{
	char p[MAX_PATH];
	if (path == NULL) return NULL;
	nsx_path(p, sizeof p, path);
	return fopen(p, mode);
}

int nsx_remove(const char *path)
{
	char p[MAX_PATH];
	nsx_path(p, sizeof p, path);
	return DeleteFileA(p) ? 0 : -1;
}

int nsx_rename(const char *from, const char *to)
{
	char a[MAX_PATH], b[MAX_PATH];
	nsx_path(a, sizeof a, from);
	nsx_path(b, sizeof b, to);
	DeleteFileA(b);
	return MoveFileA(a, b) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* strings                                                             */

static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

int strncasecmp(const char *a, const char *b, size_t n)
{
	for (; n; n--, a++, b++) {
		int d = lc((unsigned char)*a) - lc((unsigned char)*b);
		if (d || !*a) return d;
	}
	return 0;
}

int strcasecmp(const char *a, const char *b)
{
	for (;; a++, b++) {
		int d = lc((unsigned char)*a) - lc((unsigned char)*b);
		if (d || !*a) return d;
	}
}

char *strdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *d = malloc(n);
	if (d) memcpy(d, s, n);
	return d;
}

size_t strnlen(const char *s, size_t maxlen)
{
	size_t i = 0;
	while (i < maxlen && s[i]) i++;
	return i;
}

char *strtok_r(char *str, const char *delim, char **saveptr)
{
	char *s = str ? str : *saveptr;
	if (s == NULL) return NULL;
	s += strspn(s, delim);
	if (*s == 0) { *saveptr = s; return NULL; }
	char *e = s + strcspn(s, delim);
	if (*e) { *e = 0; *saveptr = e + 1; } else *saveptr = e;
	return s;
}

/* ------------------------------------------------------------------ */
/* tempo                                                               */

/* FILETIME (100ns desde 1601) -> segundos Unix */
static time_t ft_to_unix(const FILETIME *ft)
{
	ULONGLONG v = ((ULONGLONG)ft->dwHighDateTime << 32) | ft->dwLowDateTime;
	if (v < 116444736000000000ULL) return 0;
	return (time_t)((v - 116444736000000000ULL) / 10000000ULL);
}

/* relogio de parede monotono: hora do sistema no arranque + contador de desempenho.
 * Assim o NetSurf nunca ve o tempo a andar para tras. */
int gettimeofday(struct timeval *tv, void *tz)
{
	static LONGLONG base_us = -1, freq = 0, start = 0;
	LARGE_INTEGER c;
	(void)tz;
	if (base_us < 0) {
		LARGE_INTEGER now, f;
		KeQuerySystemTime(&now);
		QueryPerformanceFrequency(&f);
		QueryPerformanceCounter(&c);
		freq = f.QuadPart ? f.QuadPart : 1;
		start = c.QuadPart;
		base_us = (now.QuadPart - 116444736000000000LL) / 10;
		if (base_us < 0) base_us = 0;
	}
	QueryPerformanceCounter(&c);
	LONGLONG d = c.QuadPart - start;
	LONGLONG us = base_us + (d / freq) * 1000000LL + ((d % freq) * 1000000LL) / freq;
	tv->tv_sec = (long)(us / 1000000LL);
	tv->tv_usec = (long)(us % 1000000LL);
	return 0;
}

unsigned int sleep(unsigned int s) { Sleep(s * 1000); return 0; }
int usleep(unsigned int us) { Sleep((us + 999) / 1000); return 0; }
int isatty(int fd) { (void)fd; return 0; }
long sysconf(int name) { (void)name; return -1; }

char *getcwd(char *buf, size_t size)
{
	if (buf == NULL || size < 3) return NULL;
	strcpy(buf, "D:\\");
	return buf;
}

/* ------------------------------------------------------------------ */
/* ficheiros                                                           */

static void fill_stat(struct stat *st, DWORD attr, ULONGLONG size, const FILETIME *mt)
{
	memset(st, 0, sizeof *st);
	st->st_mode = (attr & FILE_ATTRIBUTE_DIRECTORY) ? (S_IFDIR | 0755) : (S_IFREG | 0644);
	st->st_size = (off_t)size;
	st->st_nlink = 1;
	if (mt) st->st_mtime = st->st_atime = st->st_ctime = ft_to_unix(mt);
}

int stat(const char *path, struct stat *st)
{
	char p[MAX_PATH];
	WIN32_FILE_ATTRIBUTE_DATA a;
	if (path == NULL || st == NULL) { errno = EINVAL; return -1; }
	nsx_path(p, sizeof p, path);
	/* a raiz de uma particao ("D:\") nao tem atributos mas e um diretorio */
	if (strlen(p) == 3 && p[1] == ':' && p[2] == '\\') {
		fill_stat(st, FILE_ATTRIBUTE_DIRECTORY, 0, NULL);
		return 0;
	}
	if (!GetFileAttributesExA(p, GetFileExInfoStandard, &a)) {
		errno = ENOENT;
		return -1;
	}
	fill_stat(st, a.dwFileAttributes,
		  ((ULONGLONG)a.nFileSizeHigh << 32) | a.nFileSizeLow, &a.ftLastWriteTime);
	return 0;
}

int access(const char *path, int mode)
{
	struct stat st;
	(void)mode;
	return stat(path, &st);
}

int mkdir(const char *path, mode_t mode)
{
	char p[MAX_PATH];
	(void)mode;
	nsx_path(p, sizeof p, path);
	return CreateDirectoryA(p, NULL) ? 0 : -1;
}

int unlink(const char *path) { return nsx_remove(path); }

int rmdir(const char *path)
{
	char p[MAX_PATH];
	nsx_path(p, sizeof p, path);
	return RemoveDirectoryA(p) ? 0 : -1;
}

/* tabela de descritores -> HANDLE */
#define NSX_MAXFD 64
static HANDLE fdtab[NSX_MAXFD];

int open(const char *path, int flags, ...)
{
	char p[MAX_PATH];
	DWORD access_m, disp;
	HANDLE h;
	int fd;

	nsx_path(p, sizeof p, path);
	switch (flags & 3) {
	case O_WRONLY: access_m = GENERIC_WRITE; break;
	case O_RDWR:   access_m = GENERIC_READ | GENERIC_WRITE; break;
	default:       access_m = GENERIC_READ; break;
	}
	if ((flags & O_CREAT) && (flags & O_EXCL)) disp = CREATE_NEW;
	else if ((flags & O_CREAT) && (flags & O_TRUNC)) disp = CREATE_ALWAYS;
	else if (flags & O_CREAT) disp = OPEN_ALWAYS;
	else if (flags & O_TRUNC) disp = TRUNCATE_EXISTING;
	else disp = OPEN_EXISTING;

	for (fd = 3; fd < NSX_MAXFD; fd++)
		if (fdtab[fd] == NULL) break;
	if (fd >= NSX_MAXFD) { errno = EMFILE; return -1; }

	h = CreateFileA(p, access_m, FILE_SHARE_READ, NULL, disp,
			FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) { errno = ENOENT; return -1; }
	if (flags & O_APPEND) SetFilePointer(h, 0, NULL, FILE_END);
	fdtab[fd] = h;
	return fd;
}

static HANDLE fd_handle(int fd)
{
	if (fd < 3 || fd >= NSX_MAXFD) return NULL;
	return fdtab[fd];
}

int close(int fd)
{
	HANDLE h = fd_handle(fd);
	if (h == NULL) return -1;
	CloseHandle(h);
	fdtab[fd] = NULL;
	return 0;
}

ssize_t read(int fd, void *buf, size_t n)
{
	DWORD got = 0;
	HANDLE h = fd_handle(fd);
	if (h == NULL) return -1;
	if (!ReadFile(h, buf, (DWORD)n, &got, NULL)) return -1;
	return (ssize_t)got;
}

ssize_t write(int fd, const void *buf, size_t n)
{
	DWORD put = 0;
	HANDLE h = fd_handle(fd);
	if (h == NULL) return (fd == 1 || fd == 2) ? (ssize_t)n : -1;
	if (!WriteFile(h, buf, (DWORD)n, &put, NULL)) return -1;
	return (ssize_t)put;
}

off_t lseek(int fd, off_t off, int whence)
{
	HANDLE h = fd_handle(fd);
	DWORD m = (whence == SEEK_CUR) ? FILE_CURRENT : (whence == SEEK_END) ? FILE_END : FILE_BEGIN;
	if (h == NULL) return -1;
	DWORD r = SetFilePointer(h, (LONG)off, NULL, m);
	return (r == INVALID_SET_FILE_POINTER) ? -1 : (off_t)r;
}

int fstat(int fd, struct stat *st)
{
	BY_HANDLE_FILE_INFORMATION bi;
	HANDLE h = fd_handle(fd);
	if (h == NULL || !GetFileInformationByHandle(h, &bi)) return -1;
	fill_stat(st, bi.dwFileAttributes,
		  ((ULONGLONG)bi.nFileSizeHigh << 32) | bi.nFileSizeLow, &bi.ftLastWriteTime);
	return 0;
}

int fcntl(int fd, int cmd, ...) { (void)fd; (void)cmd; return 0; }

/* ------------------------------------------------------------------ */
/* diretorios                                                          */

struct nsx_dir {
	HANDLE h;
	WIN32_FIND_DATAA fd;
	bool first;
	struct dirent ent;
};

DIR *opendir(const char *path)
{
	char p[MAX_PATH];
	DIR *d = calloc(1, sizeof *d);
	if (d == NULL) return NULL;
	nsx_path(p, sizeof p - 4, path);
	size_t l = strlen(p);
	if (l && p[l - 1] != '\\') strcat(p, "\\");
	strcat(p, "*");
	d->h = FindFirstFileA(p, &d->fd);
	if (d->h == INVALID_HANDLE_VALUE) { free(d); errno = ENOENT; return NULL; }
	d->first = true;
	return d;
}

struct dirent *readdir(DIR *d)
{
	if (d == NULL) return NULL;
	if (!d->first) {
		if (!FindNextFileA(d->h, &d->fd)) return NULL;
	}
	d->first = false;
	snprintf(d->ent.d_name, sizeof d->ent.d_name, "%s", d->fd.cFileName);
	d->ent.d_type = (d->fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? DT_DIR : DT_REG;
	return &d->ent;
}

int closedir(DIR *d)
{
	if (d == NULL) return -1;
	FindClose(d->h);
	free(d);
	return 0;
}

/* scandir() e alphasort() vem do proprio NetSurf (utils/utils.c) */


int getpid(void)
{
	return 1;
}
