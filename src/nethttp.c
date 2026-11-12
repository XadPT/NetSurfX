// Cliente HTTP/HTTPS minimo sobre lwIP + mbedTLS (fase 3).
#include "nethttp.h"
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int strncasecmp(const char *a, const char *b, size_t n);
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"

#define MAX_BODY (4 * 1024 * 1024)
#define IO_TIMEOUT_S 20

static mbedtls_entropy_context g_entropy;
static mbedtls_ctr_drbg_context g_drbg;
static mbedtls_x509_crt g_ca;
static int g_tls_ready = 0;      // 0 nao iniciado, 1 ok, -1 falhou (CA)
static int g_ca_certs_ok = 0;

static int tls_global_init(int *err)
{
    if (g_tls_ready) return g_tls_ready == 1 ? 0 : -1;
    mbedtls_entropy_init(&g_entropy);
    mbedtls_ctr_drbg_init(&g_drbg);
    mbedtls_x509_crt_init(&g_ca);
    int rc = mbedtls_ctr_drbg_seed(&g_drbg, mbedtls_entropy_func, &g_entropy,
                                   (const unsigned char *)"NetSurfX", 8);
    if (rc) { *err = rc; g_tls_ready = -1; return -1; }

    FILE *f = fopen("D:\\cacert.pem", "rb");
    if (!f) { *err = -1; g_tls_ready = -1; return -2; }   // sem ficheiro de certificados
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); *err = -2; g_tls_ready = -1; return -2; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    int bad = mbedtls_x509_crt_parse(&g_ca, buf, got + 1);   // >0 = certificados ignorados
    free(buf);
    if (bad < 0) { *err = bad; g_tls_ready = -1; return -2; }
    g_ca_certs_ok = 1;
    g_tls_ready = 1;
    return 0;
}

typedef struct { int fd; } net_ctx;

static int wait_fd(int fd, int for_write)
{
    fd_set s;
    FD_ZERO(&s);
    FD_SET(fd, &s);
    struct timeval tv = { IO_TIMEOUT_S, 0 };
    int r = for_write ? select(fd + 1, NULL, &s, NULL, &tv) : select(fd + 1, &s, NULL, NULL, &tv);
    return r > 0 ? 0 : -1;
}

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    int fd = ((net_ctx *)ctx)->fd;
    int n = send(fd, buf, len, 0);
    return n < 0 ? MBEDTLS_ERR_NET_SEND_FAILED : n;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    int fd = ((net_ctx *)ctx)->fd;
    if (wait_fd(fd, 0)) return MBEDTLS_ERR_SSL_TIMEOUT;
    int n = recv(fd, buf, len, 0);
    if (n < 0) return MBEDTLS_ERR_NET_RECV_FAILED;
    return n;   // 0 = ligacao fechada
}

typedef struct { char scheme_https; char host[128]; int port; char path[512]; } url_t;

static int parse_url(const char *u, url_t *o)
{
    memset(o, 0, sizeof *o);
    if (!strncmp(u, "https://", 8)) { o->scheme_https = 1; u += 8; o->port = 443; }
    else if (!strncmp(u, "http://", 7)) { u += 7; o->port = 80; }
    else return -1;
    const char *slash = strchr(u, '/');
    size_t hl = slash ? (size_t)(slash - u) : strlen(u);
    char hostport[160];
    if (hl == 0 || hl >= sizeof hostport) return -1;
    memcpy(hostport, u, hl); hostport[hl] = 0;
    char *colon = strrchr(hostport, ':');
    if (colon) { *colon = 0; o->port = atoi(colon + 1); }
    if (strlen(hostport) >= sizeof o->host || !hostport[0]) return -1;
    strcpy(o->host, hostport);
    snprintf(o->path, sizeof o->path, "%s", slash ? slash : "/");
    return 0;
}

static int append(uint8_t **buf, size_t *len, size_t *cap, const uint8_t *src, size_t n)
{
    if (*len + n > MAX_BODY + 8192) return -1;
    if (*len + n + 1 > *cap) {
        size_t nc = *cap ? *cap * 2 : 16384;
        while (nc < *len + n + 1) nc *= 2;
        uint8_t *nb = realloc(*buf, nc);
        if (!nb) return -1;
        *buf = nb; *cap = nc;
    }
    memcpy(*buf + *len, src, n);
    *len += n;
    (*buf)[*len] = 0;
    return 0;
}

static char *find_header(char *headers, const char *name)   // devolve valor ou NULL
{
    size_t nl = strlen(name);
    for (char *p = headers; p && *p; ) {
        char *eol = strstr(p, "\r\n");
        if (!strncasecmp(p, name, nl) && p[nl] == ':') {
            char *v = p + nl + 1;
            while (*v == ' ') v++;
            return v;
        }
        p = eol ? eol + 2 : NULL;
    }
    return NULL;
}

static int one_request(const url_t *u, ns_http_resp *r, char *location, size_t locsz)
{
    char port[8];
    snprintf(port, sizeof port, "%d", u->port);
    struct addrinfo hints, *ai = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int gr = getaddrinfo(u->host, port, &hints, &ai);
    if (gr != 0 || !ai) { r->stage = 2; r->err = gr; return -1; }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { freeaddrinfo(ai); r->stage = 3; r->err = -1; return -1; }
    if (connect(fd, ai->ai_addr, ai->ai_addrlen) != 0) {
        freeaddrinfo(ai); close(fd); r->stage = 3; r->err = -2; return -1;
    }
    freeaddrinfo(ai);

    net_ctx nc = { fd };
    mbedtls_ssl_context ssl; mbedtls_ssl_config conf;
    int use_tls = u->scheme_https;
    int rc = 0;
    if (use_tls) {
        int e = 0;
        if (tls_global_init(&e)) { close(fd); r->stage = 8; r->err = e; return -1; }
        mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&conf);
        mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&conf, &g_ca, NULL);
        mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &g_drbg);
        mbedtls_ssl_setup(&ssl, &conf);
        mbedtls_ssl_set_hostname(&ssl, u->host);
        mbedtls_ssl_set_bio(&ssl, &nc, bio_send, bio_recv, NULL);
        while ((rc = mbedtls_ssl_handshake(&ssl)) != 0) {
            if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
                r->stage = 4; r->err = rc;
                mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf); close(fd);
                return -1;
            }
        }
    }

    char req[1024];
    int rl = snprintf(req, sizeof req,
        "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: NetSurfX/0.1 (Xbox)\r\n"
        "Accept: text/html,*/*;q=0.8\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
        u->path, u->host);
    int sent = 0;
    while (sent < rl) {
        int n = use_tls ? mbedtls_ssl_write(&ssl, (unsigned char *)req + sent, (size_t)(rl - sent))
                        : send(fd, req + sent, (size_t)(rl - sent), 0);
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (n <= 0) { r->stage = 5; r->err = n; goto fail; }
        sent += n;
    }

    uint8_t *raw = NULL; size_t rawlen = 0, rawcap = 0;
    unsigned char tmp[4096];
    for (;;) {
        int n;
        if (use_tls) {
            n = mbedtls_ssl_read(&ssl, tmp, sizeof tmp);
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || n == 0) break;
            if (n < 0) { if (rawlen) break; r->stage = 6; r->err = n; free(raw); goto fail; }
        } else {
            if (wait_fd(fd, 0)) { if (rawlen) break; r->stage = 6; r->err = -3; free(raw); goto fail; }
            n = recv(fd, tmp, sizeof tmp, 0);
            if (n == 0) break;
            if (n < 0) { if (rawlen) break; r->stage = 6; r->err = n; free(raw); goto fail; }
        }
        if (append(&raw, &rawlen, &rawcap, tmp, (size_t)n)) break;   // limite de tamanho
    }
    if (use_tls) { mbedtls_ssl_close_notify(&ssl); mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf); }
    close(fd);

    // cabecalhos
    char *hdr_end = raw ? strstr((char *)raw, "\r\n\r\n") : NULL;
    if (!hdr_end || strncmp((char *)raw, "HTTP/1.", 7)) { r->stage = 7; r->err = 0; free(raw); return -1; }
    *hdr_end = 0;
    char *body = hdr_end + 4;
    size_t body_len = rawlen - (size_t)(body - (char *)raw);
    r->status = atoi((char *)raw + 9);
    char *loc = find_header((char *)raw, "Location");
    if (loc && location) {
        size_t i = 0;
        while (loc[i] && loc[i] != '\r' && i + 1 < locsz) { location[i] = loc[i]; i++; }
        location[i] = 0;
    }
    char *te = find_header((char *)raw, "Transfer-Encoding");
    uint8_t *out = malloc(body_len + 1);
    if (!out) { r->stage = 7; free(raw); return -1; }
    size_t olen = 0;
    if (te && !strncasecmp(te, "chunked", 7)) {
        char *p = body, *end = body + body_len;
        while (p < end) {
            char *eol = strstr(p, "\r\n");
            if (!eol) break;
            unsigned long cs = strtoul(p, NULL, 16);
            p = eol + 2;
            if (cs == 0 || p + cs > end) { if (cs && p < end) { memcpy(out + olen, p, (size_t)(end - p)); olen += (size_t)(end - p); } break; }
            memcpy(out + olen, p, cs); olen += cs;
            p += cs + 2;
        }
    } else {
        memcpy(out, body, body_len); olen = body_len;
    }
    out[olen] = 0;
    free(raw);
    free(r->data);
    r->data = out; r->len = olen;
    return 0;

fail:
    if (use_tls) { mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf); }
    close(fd);
    return -1;
}

int ns_http_get(const char *url, ns_http_resp *r)
{
    memset(r, 0, sizeof *r);
    char cur[256];
    snprintf(cur, sizeof cur, "%s", url);
    for (int hop = 0; hop < 6; hop++) {
        url_t u;
        if (parse_url(cur, &u)) { r->stage = 1; return -1; }
        char loc[256] = "";
        snprintf(r->final_url, sizeof r->final_url, "%s", cur);
        r->stage = 0; r->err = 0;
        if (one_request(&u, r, loc, sizeof loc)) return -1;
        if ((r->status == 301 || r->status == 302 || r->status == 303 || r->status == 307 || r->status == 308) && loc[0]) {
            if (loc[0] == '/') {   // relativo ao mesmo servidor
                char abs[256];
                snprintf(abs, sizeof abs, "%s://%s%s", u.scheme_https ? "https" : "http", u.host, loc);
                snprintf(cur, sizeof cur, "%s", abs);
            } else snprintf(cur, sizeof cur, "%s", loc);
            continue;
        }
        return 0;
    }
    r->stage = 7; r->err = 310;   // demasiados redirecionamentos
    return -1;
}

void ns_http_free(ns_http_resp *r) { free(r->data); r->data = NULL; r->len = 0; }
