/* iconv minimo: UTF-8, UTF-16(LE/BE), UCS-4, ISO-8859-1, Windows-1252, ASCII */
#ifndef NSSHIM_ICONV_H
#define NSSHIM_ICONV_H
#include <stddef.h>
typedef void *iconv_t;
iconv_t iconv_open(const char *tocode, const char *fromcode);
size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft, char **outbuf, size_t *outbytesleft);
int iconv_close(iconv_t cd);
#endif
