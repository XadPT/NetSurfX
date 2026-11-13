#ifndef NSX_XBOX_WOFF_H
#define NSX_XBOX_WOFF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* verdadeiro se os dados comecam com a assinatura WOFF 1.0 */
bool nsx_is_woff(const uint8_t *data, size_t len);

/* converte WOFF 1.0 para sfnt; *out e alocado com malloc (libertar com free) */
bool nsx_woff_to_sfnt(const uint8_t *data, size_t len, size_t max_out,
		      uint8_t **out, size_t *out_len);

#endif
