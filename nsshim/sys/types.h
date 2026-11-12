#ifndef NSSHIM_SYS_TYPES_H
#define NSSHIM_SYS_TYPES_H
#include <stddef.h>
#include <stdint.h>
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef int ssize_t;
#ifndef SSIZE_MAX
#define SSIZE_MAX 0x7fffffff
#endif
#endif
#ifndef _OFF_T_DEFINED
#define _OFF_T_DEFINED
typedef long off_t;
#endif
#ifndef _MODE_T_DEFINED
#define _MODE_T_DEFINED
typedef unsigned int mode_t;
#endif
typedef int pid_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef long suseconds_t;
#endif
