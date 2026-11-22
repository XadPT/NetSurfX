/* Testes no PC: os caminhos "D:\..." da Xbox sao traduzidos para ./xroot/... */
#ifndef HOST_COMPAT_H
#define HOST_COMPAT_H
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <zlib.h>
FILE *nsx_fopen(const char *p, const char *m);
int nsx_hstat(const char *p, struct stat *s);
int nsx_haccess(const char *p, int m);
int nsx_hopen(const char *p, int f, ...);
DIR *nsx_hopendir(const char *p);
int nsx_hmkdir(const char *p, mode_t m);
int nsx_hunlink(const char *p);
gzFile nsx_hgzopen(const char *p, const char *m);
char *nsx_hrealpath(const char *p, char *r);
int nsx_hrename(const char *a, const char *b);
int nsx_hremove(const char *p);
int nsx_hrmdir(const char *p);
#ifndef HOST_NO_MACROS
#define fopen(p, m) nsx_fopen(p, m)
#define stat(p, s) nsx_hstat(p, s)
#define access(p, m) nsx_haccess(p, m)
#define opendir(p) nsx_hopendir(p)
#define mkdir(p, m) nsx_hmkdir(p, m)
#define unlink(p) nsx_hunlink(p)
#define gzopen(p, m) nsx_hgzopen(p, m)
#define rename(a, b) nsx_hrename(a, b)
#define remove(p) nsx_hremove(p)
#define rmdir(p) nsx_hrmdir(p)
#endif
#endif
