#ifndef FAKE_HAL_VIDEO_H
#define FAKE_HAL_VIDEO_H
#include <stdbool.h>
#include <stdint.h>
typedef struct { int width, height, bpp, refresh; } VIDEO_MODE;
#define REFRESH_DEFAULT 0
bool XVideoSetMode(int w, int h, int bpp, int refresh);
VIDEO_MODE XVideoGetMode(void);
unsigned char *XVideoGetFB(void);
#endif
