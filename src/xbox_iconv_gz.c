/*
 * NetSurfX - iconv minimo (UTF-8/UTF-16/UCS-4/Latin-1/CP1252/ASCII) e leitura
 * gz* para o nxdk (o pdclib nao tem iconv e o zlib do nxdk e Z_SOLO).
 * Nao depende da winapi: tambem e testado no PC.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <zlib.h>

#include "iconv.h"

FILE *nsx_fopen(const char *path, const char *mode);

static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* ------------------------------------------------------------------ */
/* iconv minimo                                                        */

enum nsx_cs { CS_BAD, CS_UTF8, CS_UTF16LE, CS_UTF16BE, CS_UCS4LE, CS_UCS4BE,
	      CS_LATIN1, CS_CP1252, CS_ASCII };

static const unsigned short cp1252_hi[32] = {
	0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
	0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178 };

static enum nsx_cs cs_from_name(const char *n)
{
	char b[32];
	size_t i, j = 0;
	if (n == NULL) return CS_BAD;
	for (i = 0; n[i] && j + 1 < sizeof b; i++) {
		char c = n[i];
		if (c == '/') break;            /* "UTF-8//TRANSLIT" */
		if (c == '-' || c == '_') continue;
		b[j++] = (char)lc((unsigned char)c);
	}
	b[j] = 0;
	if (!strcmp(b, "utf8")) return CS_UTF8;
	if (!strcmp(b, "utf16le") || !strcmp(b, "utf16")) return CS_UTF16LE;
	if (!strcmp(b, "utf16be")) return CS_UTF16BE;
	if (!strcmp(b, "ucs4le") || !strcmp(b, "utf32le") || !strcmp(b, "ucs4internal")) return CS_UCS4LE;
	if (!strcmp(b, "ucs4") || !strcmp(b, "ucs4be") || !strcmp(b, "utf32be") || !strcmp(b, "utf32")) return CS_UCS4BE;
	if (!strcmp(b, "iso88591") || !strcmp(b, "latin1") || !strcmp(b, "l1") || !strcmp(b, "iso885915")) return CS_LATIN1;
	if (!strcmp(b, "windows1252") || !strcmp(b, "cp1252")) return CS_CP1252;
	if (!strcmp(b, "usascii") || !strcmp(b, "ascii") || !strcmp(b, "ansix3.41968")) return CS_ASCII;
	return CS_BAD;
}

struct nsx_iconv { enum nsx_cs from, to; };

iconv_t iconv_open(const char *tocode, const char *fromcode)
{
	struct nsx_iconv *cd;
	enum nsx_cs t = cs_from_name(tocode), f = cs_from_name(fromcode);
	if (t == CS_BAD || f == CS_BAD) { errno = EINVAL; return (iconv_t)-1; }
	cd = malloc(sizeof *cd);
	if (cd == NULL) return (iconv_t)-1;
	cd->from = f; cd->to = t;
	return cd;
}

int iconv_close(iconv_t cd)
{
	if (cd != (iconv_t)-1) free(cd);
	return 0;
}

/* le 1 caracter; devolve bytes consumidos, 0 se incompleto, -1 se invalido */
static int dec(enum nsx_cs cs, const unsigned char *s, size_t n, unsigned *cp)
{
	switch (cs) {
	case CS_UTF8: {
		unsigned c = s[0];
		int len;
		if (c < 0x80) { *cp = c; return 1; }
		if ((c & 0xE0) == 0xC0) { len = 2; c &= 0x1F; }
		else if ((c & 0xF0) == 0xE0) { len = 3; c &= 0x0F; }
		else if ((c & 0xF8) == 0xF0) { len = 4; c &= 0x07; }
		else return -1;
		if (n < (size_t)len) return 0;
		for (int i = 1; i < len; i++) {
			if ((s[i] & 0xC0) != 0x80) return -1;
			c = (c << 6) | (s[i] & 0x3F);
		}
		*cp = c;
		return len;
	}
	case CS_UTF16LE: case CS_UTF16BE: {
		if (n < 2) return 0;
		unsigned w = cs == CS_UTF16LE ? (s[0] | (s[1] << 8)) : ((s[0] << 8) | s[1]);
		if (w >= 0xD800 && w < 0xDC00) {
			if (n < 4) return 0;
			unsigned w2 = cs == CS_UTF16LE ? (s[2] | (s[3] << 8)) : ((s[2] << 8) | s[3]);
			if (w2 < 0xDC00 || w2 > 0xDFFF) return -1;
			*cp = 0x10000 + ((w - 0xD800) << 10) + (w2 - 0xDC00);
			return 4;
		}
		*cp = w;
		return 2;
	}
	case CS_UCS4LE: if (n < 4) return 0; *cp = s[0] | (s[1] << 8) | (s[2] << 16) | ((unsigned)s[3] << 24); return 4;
	case CS_UCS4BE: if (n < 4) return 0; *cp = ((unsigned)s[0] << 24) | (s[1] << 16) | (s[2] << 8) | s[3]; return 4;
	case CS_LATIN1: *cp = s[0]; return 1;
	case CS_CP1252: *cp = (s[0] >= 0x80 && s[0] < 0xA0) ? cp1252_hi[s[0] - 0x80] : s[0]; return 1;
	case CS_ASCII: if (s[0] > 0x7F) return -1; *cp = s[0]; return 1;
	default: return -1;
	}
}

/* escreve 1 caracter; devolve bytes escritos, 0 sem espaco, -1 se nao representavel */
static int enc(enum nsx_cs cs, unsigned cp, unsigned char *o, size_t n)
{
	switch (cs) {
	case CS_UTF8:
		if (cp < 0x80) { if (n < 1) return 0; o[0] = (unsigned char)cp; return 1; }
		if (cp < 0x800) { if (n < 2) return 0; o[0] = 0xC0 | (cp >> 6); o[1] = 0x80 | (cp & 0x3F); return 2; }
		if (cp < 0x10000) { if (n < 3) return 0; o[0] = 0xE0 | (cp >> 12); o[1] = 0x80 | ((cp >> 6) & 0x3F); o[2] = 0x80 | (cp & 0x3F); return 3; }
		if (n < 4) return 0;
		o[0] = 0xF0 | (cp >> 18); o[1] = 0x80 | ((cp >> 12) & 0x3F); o[2] = 0x80 | ((cp >> 6) & 0x3F); o[3] = 0x80 | (cp & 0x3F);
		return 4;
	case CS_UTF16LE: case CS_UTF16BE: {
		unsigned short w[2]; int k = 1;
		if (cp >= 0x10000) { cp -= 0x10000; w[0] = 0xD800 + (cp >> 10); w[1] = 0xDC00 + (cp & 0x3FF); k = 2; }
		else w[0] = (unsigned short)cp;
		if (n < (size_t)(2 * k)) return 0;
		for (int i = 0; i < k; i++) {
			if (cs == CS_UTF16LE) { o[2*i] = w[i] & 0xFF; o[2*i+1] = w[i] >> 8; }
			else { o[2*i] = w[i] >> 8; o[2*i+1] = w[i] & 0xFF; }
		}
		return 2 * k;
	}
	case CS_UCS4LE: if (n < 4) return 0; o[0] = cp; o[1] = cp >> 8; o[2] = cp >> 16; o[3] = cp >> 24; return 4;
	case CS_UCS4BE: if (n < 4) return 0; o[0] = cp >> 24; o[1] = cp >> 16; o[2] = cp >> 8; o[3] = cp; return 4;
	case CS_LATIN1: if (cp > 0xFF) return -1; if (n < 1) return 0; o[0] = (unsigned char)cp; return 1;
	case CS_ASCII: if (cp > 0x7F) return -1; if (n < 1) return 0; o[0] = (unsigned char)cp; return 1;
	case CS_CP1252:
		if (n < 1) return 0;
		if (cp < 0x80 || (cp >= 0xA0 && cp <= 0xFF)) { o[0] = (unsigned char)cp; return 1; }
		for (int i = 0; i < 32; i++) if (cp1252_hi[i] == cp && cp != 0xFFFD) { o[0] = 0x80 + i; return 1; }
		return -1;
	default: return -1;
	}
}

size_t iconv(iconv_t cdv, char **inbuf, size_t *inleft, char **outbuf, size_t *outleft)
{
	struct nsx_iconv *cd = cdv;
	if (cd == NULL || cdv == (iconv_t)-1) { errno = EBADF; return (size_t)-1; }
	if (inbuf == NULL || *inbuf == NULL) return 0;   /* reset de estado: nada a fazer */
	while (*inleft > 0) {
		unsigned cp;
		int r = dec(cd->from, (unsigned char *)*inbuf, *inleft, &cp);
		if (r == 0) { errno = EINVAL; return (size_t)-1; }
		if (r < 0) { errno = EILSEQ; return (size_t)-1; }
		int w = enc(cd->to, cp, (unsigned char *)*outbuf, *outleft);
		if (w == 0) { errno = E2BIG; return (size_t)-1; }
		if (w < 0) {
			/* nao representavel: '?' (como //TRANSLIT simples) */
			w = enc(cd->to, '?', (unsigned char *)*outbuf, *outleft);
			if (w <= 0) { errno = E2BIG; return (size_t)-1; }
		}
		*inbuf += r; *inleft -= r;
		*outbuf += w; *outleft -= w;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* gz* (so leitura). O zlib do nxdk e Z_SOLO. Aceita ficheiros normais    */
/* ou comprimidos com gzip.                                              */

struct nsx_gzfile {
	FILE *f;
	bool gz, eof;
	z_stream zs;
	unsigned char in[16384];
	unsigned char out[16384];
	size_t opos, olen;
};

/* com Z_SOLO o zlib nao tem alocador por defeito */
static voidpf nsx_zalloc(voidpf o, uInt items, uInt size) { (void)o; return calloc(items, size); }
static void nsx_zfree(voidpf o, voidpf p) { (void)o; free(p); }

gzFile gzopen(const char *path, const char *mode)
{
	struct nsx_gzfile *g;
	unsigned char hdr[2];
	FILE *f;
	(void)mode;
	f = nsx_fopen(path, "rb");
	if (f == NULL) return NULL;
	g = calloc(1, sizeof *g);
	if (g == NULL) { fclose(f); return NULL; }
	g->f = f;
	if (fread(hdr, 1, 2, f) == 2 && hdr[0] == 0x1f && hdr[1] == 0x8b) {
		g->gz = true;
		g->zs.zalloc = nsx_zalloc;
		g->zs.zfree = nsx_zfree;
		if (inflateInit2(&g->zs, 15 + 32) != Z_OK) { fclose(f); free(g); return NULL; }
	}
	fseek(f, 0, SEEK_SET);
	return g;
}

static bool gz_fill(struct nsx_gzfile *g)
{
	if (g->eof) return false;
	if (!g->gz) {
		g->olen = fread(g->out, 1, sizeof g->out, g->f);
		g->opos = 0;
		if (g->olen == 0) { g->eof = true; return false; }
		return true;
	}
	for (;;) {
		if (g->zs.avail_in == 0) {
			size_t n = fread(g->in, 1, sizeof g->in, g->f);
			g->zs.next_in = g->in;
			g->zs.avail_in = (uInt)n;
		}
		g->zs.next_out = g->out;
		g->zs.avail_out = sizeof g->out;
		int r = inflate(&g->zs, Z_NO_FLUSH);
		g->olen = sizeof g->out - g->zs.avail_out;
		g->opos = 0;
		if (r == Z_STREAM_END || (r != Z_OK && r != Z_BUF_ERROR)) g->eof = true;
		if (g->olen > 0) return true;
		if (g->eof || (r == Z_BUF_ERROR && g->zs.avail_in == 0 && feof(g->f))) {
			g->eof = true;
			return false;
		}
	}
}

int gzread(gzFile g, void *buf, unsigned len)
{
	unsigned char *b = buf;
	unsigned done = 0;
	while (done < len) {
		if (g->opos >= g->olen && !gz_fill(g)) break;
		size_t k = g->olen - g->opos;
		if (k > len - done) k = len - done;
		memcpy(b + done, g->out + g->opos, k);
		g->opos += k;
		done += (unsigned)k;
	}
	return (int)done;
}

char *gzgets(gzFile g, char *buf, int len)
{
	int i = 0;
	if (len <= 0) return NULL;
	while (i < len - 1) {
		if (g->opos >= g->olen && !gz_fill(g)) break;
		char c = (char)g->out[g->opos++];
		buf[i++] = c;
		if (c == '\n') break;
	}
	buf[i] = 0;
	return i ? buf : NULL;
}

int gzeof(gzFile g) { return g->opos >= g->olen && g->eof; }

int gzclose(gzFile g)
{
	if (g == NULL) return -1;
	if (g->gz) inflateEnd(&g->zs);
	fclose(g->f);
	free(g);
	return 0;
}
