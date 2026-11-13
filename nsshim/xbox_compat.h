/* NetSurfX: compatibilidade POSIX minima para o pdclib do nxdk.
 * Incluido a forca (-include) em todos os ficheiros do NetSurf e das suas libs. */
#ifndef XBOX_COMPAT_H
#define XBOX_COMPAT_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* a struct timeval do lwIP e a nossa sao identicas; usamos a nossa em todo o lado */
#ifndef LWIP_TIMEVAL_PRIVATE
#define LWIP_TIMEVAL_PRIVATE 0
#endif
#ifndef NSX_TIMEVAL_DEFINED
#define NSX_TIMEVAL_DEFINED
struct timeval { long tv_sec; long tv_usec; };
#endif

int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t n);
char *strdup(const char *s);
size_t strnlen(const char *s, size_t maxlen);
char *strndup(const char *s, size_t n);
int getpid(void);
int ftruncate(int fd, off_t length);
int close(int fd);
ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
off_t lseek(int fd, off_t off, int whence);
char *strtok_r(char *str, const char *delim, char **saveptr);

#ifndef R_OK
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1
int access(const char *path, int mode);
#endif
int unlink(const char *path);
int rmdir(const char *path);

/* todos os caminhos passam por estas funcoes ('/' -> '\\' para o kernel da Xbox) */
FILE *nsx_fopen(const char *path, const char *mode);
int nsx_remove(const char *path);
int nsx_rename(const char *from, const char *to);
#ifndef NSX_NO_FILE_MACROS
#define fopen nsx_fopen
#define remove nsx_remove
#define rename nsx_rename
#endif

#ifndef PATH_MAX
#define PATH_MAX 260
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif
#ifndef M_PI_4
#define M_PI_4 0.78539816339744830962
#endif
#ifndef M_SQRT2
#define M_SQRT2 1.41421356237309504880
#endif

#ifdef __cplusplus
}
#endif
#endif
