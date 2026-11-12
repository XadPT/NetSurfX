#ifndef NSSHIM_DIRENT_H
#define NSSHIM_DIRENT_H
#include <sys/types.h>
struct dirent { char d_name[260]; unsigned char d_type; };
#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8
typedef struct nsx_dir DIR;
DIR *opendir(const char *path);
struct dirent *readdir(DIR *d);
int closedir(DIR *d);
#endif
