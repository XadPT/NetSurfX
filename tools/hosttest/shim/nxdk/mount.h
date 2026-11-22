#pragma once
#include <stdbool.h>
static inline bool nxIsDriveMounted(char d) { (void)d; return true; }
static inline bool nxMountDrive(char d, const char *p) { (void)d; (void)p; return true; }
