#ifndef NSSHIM_SYS_TIME_H
#define NSSHIM_SYS_TIME_H
#include <time.h>
#include "xbox_compat.h"
struct timezone { int tz_minuteswest; int tz_dsttime; };
int gettimeofday(struct timeval *tv, void *tz);
#define timerisset(tvp) ((tvp)->tv_sec || (tvp)->tv_usec)
#endif
