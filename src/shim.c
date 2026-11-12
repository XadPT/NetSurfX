// Funcoes POSIX em falta no pdclib do nxdk
#include <stddef.h>
static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int strncasecmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        int d = lc((unsigned char)*a) - lc((unsigned char)*b);
        if (d || !*a) return d;
    }
    return 0;
}
int strcasecmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int d = lc((unsigned char)*a) - lc((unsigned char)*b);
        if (d || !*a) return d;
    }
}
