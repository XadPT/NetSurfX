/*
 * NetSurfX - conversao WOFF (1.0) -> sfnt (TTF/OTF)
 *
 * O FreeType 2.4.12 do nxdk nao le WOFF, que e o formato mais comum das
 * fontes web a seguir ao WOFF2. O WOFF 1.0 e apenas um sfnt com cada
 * tabela comprimida em zlib, por isso reconstruimos o ficheiro original.
 * WOFF2 (brotli) continua sem suporte.
 *
 * Especificacao: https://www.w3.org/TR/WOFF/
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "xbox_woff.h"

#define WOFF_HDR_LEN 44
#define WOFF_DIR_LEN 20
#define WOFF_MAX_TABLES 64

static uint32_t rd32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t rd16(const uint8_t *p)
{
	return (uint16_t)((p[0] << 8) | p[1]);
}

static void wr32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static void wr16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}

static voidpf woff_zalloc(voidpf opaque, uInt items, uInt size)
{
	(void)opaque;
	return calloc(items, size);
}

static void woff_zfree(voidpf opaque, voidpf address)
{
	(void)opaque;
	free(address);
}

static int woff_inflate(const uint8_t *src, size_t srclen,
			uint8_t *dst, size_t dstlen)
{
	z_stream zs;
	int ret;

	memset(&zs, 0, sizeof(zs));
	zs.zalloc = woff_zalloc;
	zs.zfree = woff_zfree;
	if (inflateInit(&zs) != Z_OK)
		return -1;
	zs.next_in = (Bytef *)src;
	zs.avail_in = (uInt)srclen;
	zs.next_out = dst;
	zs.avail_out = (uInt)dstlen;
	ret = inflate(&zs, Z_FINISH);
	inflateEnd(&zs);
	if (ret != Z_STREAM_END || zs.total_out != dstlen)
		return -1;
	return 0;
}

bool nsx_is_woff(const uint8_t *data, size_t len)
{
	return len >= 4 && memcmp(data, "wOFF", 4) == 0;
}

bool nsx_woff_to_sfnt(const uint8_t *data, size_t len, size_t max_out,
		      uint8_t **out, size_t *out_len)
{
	uint32_t flavor, total_sfnt;
	uint16_t num_tables, i;
	uint16_t search_range = 1, entry_selector = 0;
	size_t hdr_len, pos;
	uint8_t *buf;

	*out = NULL;
	*out_len = 0;

	if (len < WOFF_HDR_LEN || !nsx_is_woff(data, len))
		return false;
	if (rd32(data + 8) != len)          /* campo length */
		return false;

	flavor = rd32(data + 4);
	num_tables = rd16(data + 12);
	total_sfnt = rd32(data + 16);

	if (num_tables == 0 || num_tables > WOFF_MAX_TABLES)
		return false;
	if ((size_t)WOFF_HDR_LEN + (size_t)num_tables * WOFF_DIR_LEN > len)
		return false;
	hdr_len = 12 + (size_t)num_tables * 16;
	if (total_sfnt < hdr_len || total_sfnt > max_out ||
	    (total_sfnt & 3) != 0)
		return false;

	buf = calloc(1, total_sfnt);
	if (buf == NULL)
		return false;

	while ((search_range << 1) <= num_tables) {
		search_range <<= 1;
		entry_selector++;
	}
	wr32(buf, flavor);
	wr16(buf + 4, num_tables);
	wr16(buf + 6, (uint16_t)(search_range * 16));
	wr16(buf + 8, entry_selector);
	wr16(buf + 10, (uint16_t)(num_tables * 16 - search_range * 16));

	pos = hdr_len;
	for (i = 0; i < num_tables; i++) {
		const uint8_t *e = data + WOFF_HDR_LEN + (size_t)i * WOFF_DIR_LEN;
		uint32_t tag = rd32(e);
		uint32_t off = rd32(e + 4);
		uint32_t comp = rd32(e + 8);
		uint32_t orig = rd32(e + 12);
		uint32_t csum = rd32(e + 16);
		uint8_t *d = buf + 12 + (size_t)i * 16;

		if (off > len || comp > len - off || comp > orig)
			goto fail;
		if (orig > total_sfnt - pos)
			goto fail;

		if (comp == orig) {
			memcpy(buf + pos, data + off, orig);
		} else if (woff_inflate(data + off, comp, buf + pos, orig) != 0) {
			goto fail;
		}

		wr32(d, tag);
		wr32(d + 4, csum);
		wr32(d + 8, (uint32_t)pos);
		wr32(d + 12, orig);

		pos += orig;
		pos = (pos + 3) & ~(size_t)3;      /* tabelas alinhadas a 4 */
		if (pos > total_sfnt)
			goto fail;
	}

	*out = buf;
	*out_len = pos;
	return true;

fail:
	free(buf);
	return false;
}
