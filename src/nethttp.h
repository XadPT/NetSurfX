#ifndef NETHTTP_H
#define NETHTTP_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *data;      // corpo (terminado em 0), a libertar com ns_http_free
    size_t len;
    int status;         // codigo HTTP, 0 se falhou
    int stage;          // onde falhou: 1 URL, 2 DNS, 3 ligar, 4 TLS, 5 enviar, 6 receber, 7 resposta invalida, 8 CA
    int err;            // codigo de erro (mbedTLS negativo, ou errno)
    char final_url[256];
} ns_http_resp;

// GET http:// ou https://. Segue ate 5 redirecionamentos. Devolve 0 se obteve resposta HTTP.
int ns_http_get(const char *url, ns_http_resp *r);
void ns_http_free(ns_http_resp *r);
#endif
