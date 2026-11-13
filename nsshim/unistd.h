#ifndef NSSHIM_UNISTD_H
#define NSSHIM_UNISTD_H
#include <stddef.h>
#include <sys/types.h>
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
int access(const char *path, int mode);
int unlink(const char *path);
int rmdir(const char *path);
int close(int fd);
ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
off_t lseek(int fd, off_t off, int whence);
char *getcwd(char *buf, size_t size);
unsigned int sleep(unsigned int s);
int usleep(unsigned int us);
int isatty(int fd);
long sysconf(int name);
int getpid(void);
#endif
