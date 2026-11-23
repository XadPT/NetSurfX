/* NetSurfX - start-up splash screen */
#ifndef NSX_XBOX_SPLASH_H
#define NSX_XBOX_SPLASH_H

#include "libnsfb.h"

/* draw res\splash.png now (once the screen exists) */
void nsx_splash_show(nsfb_t *nsfb);
/* keep it up for the minimum time unless a button is pressed */
void nsx_splash_finish(void);

#endif
