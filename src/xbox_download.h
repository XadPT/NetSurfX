/* NetSurfX - downloads directos para o disco (ver fetch_xbox.c) */
#ifndef NSX_XBOX_DOWNLOAD_H
#define NSX_XBOX_DOWNLOAD_H

#include <stdbool.h>
#include <stdint.h>
#include "utils/nsurl.h"

enum nsx_dl_state { NSX_DL_NONE, NSX_DL_RUNNING, NSX_DL_DONE, NSX_DL_FAILED, NSX_DL_CANCELLED };

struct nsx_dl_info {
	enum nsx_dl_state state;
	int64_t got, total;     /* total < 0: desconhecido */
	const char *name, *path, *err;
};

bool nsx_dl_begin(nsurl *url, const char *path, const char *name);
enum nsx_dl_state nsx_dl_poll(struct nsx_dl_info *info);
void nsx_dl_cancel(void);
void nsx_dl_ack(void);

/* identificacao do browser: 0 normal, 1 movel, 2 leve, 3 texto */
void nsx_set_ua_mode(int mode);
#define NSX_UA_MODES 4

#endif
