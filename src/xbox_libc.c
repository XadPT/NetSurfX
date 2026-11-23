/*
 * NetSurfX - funcoes da libc que faltam (ou estao incompletas) no nxdk
 *
 * 1) strtod/strtof/strtold: no nxdk sao "assert(0)" - a primeira pagina com
 *    CSS moderno (o fork da Switch usa strtof nos gradientes, sombras e
 *    grelhas) parava o programa ("Assertion failed ... strtof").
 * 2) printf e familia: a pdclib ignora %f/%e/%g sem consumir o argumento,
 *    o que desalinha os argumentos seguintes (texto errado e, com um %s a
 *    seguir, um crash). Substituimos pela stb_sprintf (dominio publico),
 *    que suporta numeros reais. O linker usa estas e ignora as da pdclib.
 *
 * Em testes no PC (NSX_LIBC_TEST) os nomes levam o prefixo nsx_.
 */
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef NSX_LIBC_TEST
#define F(name) nsx_##name
#define FS(name) nsx_##name
#else
#define F(name) name
#ifdef NSX_HOST_OVERRIDE
/* teste no PC: substitui directamente as da glibc em todo o programa */
#define FS(name) name
#else
/* strtod & cia ficam com prefixo: o objecto do nxdk que tem as versoes
 * "assert(0)" tambem tem outras funcoes de que precisamos (rand_s...), por
 * isso o xbox_compat.h redirecciona strtod -> nsx_strtod em todo o codigo */
#define FS(name) nsx_##name
#endif
#undef strtod
#undef strtof
#undef strtold
#undef atof
#undef lroundf
#undef llroundf
#endif

/* ================================================================ strtod */

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int match_ci(const char *s, const char *word)
{
	int n = 0;
	while (word[n]) {
		if (lower((unsigned char)s[n]) != word[n]) return 0;
		n++;
	}
	return n;
}

/* 10^e exacto ate 10^22; acima multiplica (precisao suficiente aqui) */
static double pow10i(int e)
{
	static const double exact[] = {
		1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
		1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22
	};
	double r = 1.0;
	if (e < 0) return 1.0 / pow10i(-e);
	while (e > 22) {
		r *= 1e22;
		e -= 22;
		if (r > DBL_MAX / 1e22) return HUGE_VAL;
	}
	return r * exact[e];
}

double FS(strtod)(const char *nptr, char **endptr)
{
	const char *s = nptr;
	int neg = 0, any = 0, k;
	uint64_t mant = 0;
	int digits = 0, exp10 = 0;
	double r;

	while (isspace((unsigned char)*s)) s++;
	if (*s == '+' || *s == '-') neg = (*s++ == '-');

	/* infinito e "not a number" */
	if ((k = match_ci(s, "infinity")) || (k = match_ci(s, "inf"))) {
		if (endptr) *endptr = (char *)(s + k);
		return neg ? -HUGE_VAL : HUGE_VAL;
	}
	if ((k = match_ci(s, "nan"))) {
		s += k;
		if (*s == '(') {
			const char *p = s + 1;
			while (isalnum((unsigned char)*p) || *p == '_') p++;
			if (*p == ')') s = p + 1;
		}
		if (endptr) *endptr = (char *)s;
		return neg ? -NAN : NAN;
	}

	/* hexadecimal (0x1Ap-2) */
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X') && isxdigit((unsigned char)s[2]) ) {
		double v = 0;
		int e2 = 0;
		s += 2;
		for (; isxdigit((unsigned char)*s); s++)
			v = v * 16 + (isdigit((unsigned char)*s) ? *s - '0' : lower(*s) - 'a' + 10);
		if (*s == '.') {
			for (s++; isxdigit((unsigned char)*s); s++) {
				v = v * 16 + (isdigit((unsigned char)*s) ? *s - '0' : lower(*s) - 'a' + 10);
				e2 -= 4;
			}
		}
		if ((*s == 'p' || *s == 'P') && (isdigit((unsigned char)s[1]) ||
		    ((s[1] == '+' || s[1] == '-') && isdigit((unsigned char)s[2])))) {
			int en = 0, eneg = 0;
			s++;
			if (*s == '+' || *s == '-') eneg = (*s++ == '-');
			for (; isdigit((unsigned char)*s); s++) if (en < 100000) en = en * 10 + (*s - '0');
			e2 += eneg ? -en : en;
		}
		if (endptr) *endptr = (char *)s;
		v = ldexp(v, e2);
		return neg ? -v : v;
	}

	/* parte inteira */
	for (; *s >= '0' && *s <= '9'; s++) {
		any = 1;
		if (digits < 19) {
			if (mant || *s != '0') { mant = mant * 10 + (uint64_t)(*s - '0'); digits++; }
		} else {
			exp10++;           /* algarismos a mais: so contam para a escala */
		}
	}
	/* parte decimal */
	if (*s == '.') {
		const char *d = s + 1;
		for (; *d >= '0' && *d <= '9'; d++) {
			any = 1;
			if (digits < 19) {
				if (mant || *d != '0') { mant = mant * 10 + (uint64_t)(*d - '0'); digits++; }
				exp10--;
			}
		}
		if (any) s = d;
	}
	if (!any) {
		if (endptr) *endptr = (char *)nptr;
		return 0.0;
	}
	/* expoente (so se tiver algarismos) */
	if (*s == 'e' || *s == 'E') {
		const char *e = s + 1;
		int eneg = 0, ev = 0, edig = 0;
		if (*e == '+' || *e == '-') eneg = (*e++ == '-');
		for (; *e >= '0' && *e <= '9'; e++) {
			edig = 1;
			if (ev < 100000) ev = ev * 10 + (*e - '0');
		}
		if (edig) {
			exp10 += eneg ? -ev : ev;
			s = e;
		}
	}
	if (endptr) *endptr = (char *)s;

	r = (double)mant;
	if (mant != 0 && exp10 != 0) {
		if (exp10 < -300) {
			/* dois passos para nao passar por zero antes do tempo */
			r = r / pow10i(300);
			r = r / pow10i(-exp10 - 300);
		} else if (exp10 < 0) {
			r = r / pow10i(-exp10);
		} else {
			r = r * pow10i(exp10);
		}
		if (r == HUGE_VAL || r == 0.0) errno = ERANGE;
	}
	return neg ? -r : r;
}

float FS(strtof)(const char *nptr, char **endptr)
{
	double d = FS(strtod)(nptr, endptr);
	if (d > FLT_MAX) { errno = ERANGE; return HUGE_VALF; }
	if (d < -FLT_MAX) { errno = ERANGE; return -HUGE_VALF; }
	return (float)d;
}

long double FS(strtold)(const char *nptr, char **endptr)
{
	return (long double)FS(strtod)(nptr, endptr);
}

double FS(atof)(const char *nptr)
{
	return FS(strtod)(nptr, NULL);
}

/* arredonda para o inteiro mais proximo (metades para longe de zero) */
long FS(lroundf)(float x)
{
	return (long)(x >= 0 ? (double)x + 0.5 : (double)x - 0.5);
}

long long FS(llroundf)(float x)
{
	return (long long)(x >= 0 ? (double)x + 0.5 : (double)x - 0.5);
}

/* ================================================================ printf */

#ifndef NSX_LIBC_TEST
#define STB_SPRINTF_IMPLEMENTATION
#define STB_SPRINTF_DECORATE(name) nsx_stb_##name
#include "stb/stb_sprintf.h"
#define V_SN nsx_stb_vsnprintf
#else
#define V_SN vsnprintf
#endif

int F(vsnprintf)(char *buf, size_t n, const char *fmt, va_list ap)
{
	if (n > INT_MAX) n = INT_MAX;
	return V_SN(buf, (int)n, fmt, ap);
}

int F(snprintf)(char *buf, size_t n, const char *fmt, ...)
{
	va_list ap;
	int r;
	va_start(ap, fmt);
	r = F(vsnprintf)(buf, n, fmt, ap);
	va_end(ap);
	return r;
}

int F(vsprintf)(char *buf, const char *fmt, va_list ap)
{
	return F(vsnprintf)(buf, INT_MAX, fmt, ap);
}

int F(sprintf)(char *buf, const char *fmt, ...)
{
	va_list ap;
	int r;
	va_start(ap, fmt);
	r = F(vsnprintf)(buf, INT_MAX, fmt, ap);
	va_end(ap);
	return r;
}

int F(vfprintf)(FILE *fp, const char *fmt, va_list ap)
{
	char sbuf[512];
	char *big = NULL;
	va_list ap2;
	int n;

	va_copy(ap2, ap);
	n = F(vsnprintf)(sbuf, sizeof sbuf, fmt, ap);
	if (n < 0) { va_end(ap2); return n; }
	if ((size_t)n >= sizeof sbuf) {
		big = malloc((size_t)n + 1);
		if (big == NULL) { va_end(ap2); return -1; }
		F(vsnprintf)(big, (size_t)n + 1, fmt, ap2);
	}
	va_end(ap2);
	if (fwrite(big ? big : sbuf, 1, (size_t)n, fp) != (size_t)n) n = -1;
	free(big);
	return n;
}

int F(fprintf)(FILE *fp, const char *fmt, ...)
{
	va_list ap;
	int r;
	va_start(ap, fmt);
	r = F(vfprintf)(fp, fmt, ap);
	va_end(ap);
	return r;
}

int F(vprintf)(const char *fmt, va_list ap)
{
	return F(vfprintf)(stdout, fmt, ap);
}

int F(printf)(const char *fmt, ...)
{
	va_list ap;
	int r;
	va_start(ap, fmt);
	r = F(vfprintf)(stdout, fmt, ap);
	va_end(ap);
	return r;
}

/* ================================================================ qsort */

/*
 * pdclib's qsort scans for the partition point without bounds checks and
 * relies on the comparison being a consistent ordering. A comparison that
 * is not (cmp(x, x) != 0, as NetSurf's grid layout had) makes it walk off
 * the array and crash. This version can never leave the array whatever the
 * comparison returns: insertion sort for short runs, heapsort otherwise
 * (O(n log n) worst case, no recursion, no extra memory).
 */
static void qs_swap(unsigned char *a, unsigned char *b, size_t size)
{
	while (size >= 4) {
		uint32_t t;
		memcpy(&t, a, 4); memcpy(a, b, 4); memcpy(b, &t, 4);
		a += 4; b += 4; size -= 4;
	}
	while (size--) {
		unsigned char t = *a; *a++ = *b; *b++ = t;
	}
}

static void qs_sift(unsigned char *base, size_t root, size_t n, size_t size,
		    int (*cmp)(const void *, const void *))
{
	for (;;) {
		size_t child = 2 * root + 1, big = root;
		if (child < n && cmp(base + child * size, base + big * size) > 0)
			big = child;
		if (child + 1 < n && cmp(base + (child + 1) * size, base + big * size) > 0)
			big = child + 1;
		if (big == root)
			return;
		qs_swap(base + root * size, base + big * size, size);
		root = big;
	}
}

void F(qsort)(void *vbase, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
	unsigned char *base = vbase;
	if (base == NULL || n < 2 || size == 0 || cmp == NULL)
		return;
	if (n <= 12) {
		for (size_t i = 1; i < n; i++)
			for (size_t j = i; j > 0 && cmp(base + (j - 1) * size, base + j * size) > 0; j--)
				qs_swap(base + (j - 1) * size, base + j * size, size);
		return;
	}
	for (size_t i = n / 2; i-- > 0; )
		qs_sift(base, i, n, size, cmp);
	for (size_t end = n - 1; end > 0; end--) {
		qs_swap(base, base + end * size, size);
		qs_sift(base, 0, end, size, cmp);
	}
}
