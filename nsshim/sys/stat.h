#ifndef NSSHIM_SYS_STAT_H
#define NSSHIM_SYS_STAT_H
#include <sys/types.h>
#include <time.h>
#define S_IFMT   0170000
#define S_IFDIR  0040000
#define S_IFREG  0100000
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRWXU 0700
#define S_IRGRP 040
#define S_IWGRP 020
#define S_IXGRP 010
#define S_IRWXG 070
#define S_IROTH 04
#define S_IWOTH 02
#define S_IXOTH 01
#define S_IRWXO 07
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
struct stat {
	mode_t st_mode;
	off_t  st_size;
	time_t st_mtime;
	time_t st_atime;
	time_t st_ctime;
	int    st_nlink;
	int    st_dev;
	int    st_ino;
};
int stat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);
int mkdir(const char *path, mode_t mode);
#endif
