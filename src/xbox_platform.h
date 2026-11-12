#ifndef XBOX_PLATFORM_H
#define XBOX_PLATFORM_H
#include <stdbool.h>
bool nsx_debug_enabled(void);
void nsx_video_init(bool want_hd, int *w, int *h);
void nsx_fatal(const char *msg);
#endif
