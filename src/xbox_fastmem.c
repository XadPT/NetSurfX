/*
 * NetSurfX - memcpy/memmove/memset/memcmp/strlen rapidos para a Xbox
 *
 * A pdclib do nxdk implementa estas funcoes byte a byte (e o nxdk compila
 * com -fno-builtin, por isso nada e optimizado). Um browser passa a vida a
 * copiar memoria (rede, cache, imagens, ecra), por isso estas versoes com
 * "rep movsd"/"rep stosd" (4 bytes de cada vez) substituem as da pdclib:
 * o linker so vai buscar a biblioteca os simbolos que ainda faltam.
 *
 * Em testes no PC (NSX_FASTMEM_TEST) os nomes levam o prefixo nsx_.
 */
#include <stddef.h>
#include <stdint.h>

#ifdef NSX_FASTMEM_TEST
#define F(name) nsx_##name
#else
#define F(name) name
#endif

/* copia para a frente: n bytes de s para d */
static inline void copy_fwd(unsigned char *d, const unsigned char *s, size_t n)
{
	if (n >= 16) {
		/* alinhar o destino a 4 bytes (escritas desalinhadas custam mais) */
		size_t a = (size_t)(-(uintptr_t)d) & 3;
		size_t words;
		n -= a;
		while (a--) *d++ = *s++;
		words = n >> 2;
		n &= 3;
		__asm__ __volatile__("cld\n\trep movsl"
				     : "+D"(d), "+S"(s), "+c"(words)
				     :
				     : "memory");
	}
	while (n--) *d++ = *s++;
}

void *F(memcpy)(void *restrict dst, const void *restrict src, size_t n)
{
	copy_fwd((unsigned char *)dst, (const unsigned char *)src, n);
	return dst;
}

void *F(memmove)(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;

	if (d == s || n == 0)
		return dst;
	if (d < s || d >= s + n) {
		/* sem sobreposicao perigosa: copia para a frente */
		copy_fwd(d, s, n);
		return dst;
	}
	/* destino depois da origem e sobreposto: copia de tras para a frente */
	d += n;
	s += n;
	if (n >= 16) {
		size_t a = (uintptr_t)d & 3;
		size_t words;
		n -= a;
		while (a--) *--d = *--s;
		words = n >> 2;
		n &= 3;
		d -= 4;
		s -= 4;
		__asm__ __volatile__("std\n\trep movsl\n\tcld"
				     : "+D"(d), "+S"(s), "+c"(words)
				     :
				     : "memory");
		d += 4;
		s += 4;
	}
	while (n--) *--d = *--s;
	return dst;
}

void *F(memset)(void *dst, int c, size_t n)
{
	unsigned char *d = dst;
	unsigned char b = (unsigned char)c;

	if (n >= 16) {
		size_t a = (size_t)(-(uintptr_t)d) & 3;
		uint32_t v = b * 0x01010101u;
		size_t words;
		n -= a;
		while (a--) *d++ = b;
		words = n >> 2;
		n &= 3;
		__asm__ __volatile__("cld\n\trep stosl"
				     : "+D"(d), "+c"(words)
				     : "a"(v)
				     : "memory");
	}
	while (n--) *d++ = b;
	return dst;
}

int F(memcmp)(const void *a, const void *b, size_t n)
{
	const unsigned char *p = a, *q = b;

	/* compara 4 bytes de cada vez enquanto forem iguais */
	if ((((uintptr_t)p | (uintptr_t)q) & 3) == 0) {
		while (n >= 4 && *(const uint32_t *)p == *(const uint32_t *)q) {
			p += 4;
			q += 4;
			n -= 4;
		}
	}
	while (n--) {
		if (*p != *q)
			return (int)*p - (int)*q;
		p++;
		q++;
	}
	return 0;
}

size_t F(strlen)(const char *s)
{
	const char *p = s;
	const uint32_t *w;

	while ((uintptr_t)p & 3) {
		if (*p == 0)
			return (size_t)(p - s);
		p++;
	}
	/* palavra a palavra: (x - 0x01..) & ~x & 0x80.. != 0 se houver um zero.
	 * Ler a palavra inteira e seguro: alinhada a 4 nunca atravessa paginas. */
	w = (const uint32_t *)p;
	for (;;) {
		uint32_t x = *w;
		if ((x - 0x01010101u) & ~x & 0x80808080u)
			break;
		w++;
	}
	p = (const char *)w;
	while (*p)
		p++;
	return (size_t)(p - s);
}
