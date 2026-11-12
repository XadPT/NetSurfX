#ifndef NSSHIM_SYS_TYPES_H
#define NSSHIM_SYS_TYPES_H
#include <stddef.h>
#include <stdint.h>
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef intptr_t ssize_t;
#endif
#ifndef _OFF_T_DEFINED
#define _OFF_T_DEFINED
typedef long off_t;
#endif
#endif
