/*
 * NetSurfX - fetcher HTTP/HTTPS para a Xbox (substitui o fetcher curl).
 *
 * Cada pedido corre numa thread propria (rede lwIP + mbedTLS) e deixa os
 * cabecalhos e os dados numa zona partilhada. O NetSurf (single-thread) chama
 * poll() a cada 10 ms e entrega tudo a partir da thread principal, por isso
 * nenhuma funcao do NetSurf e chamada fora da thread principal (urldb, cookies,
 * mensagens...).
 *
 * Suporta: GET/POST (urlencoded e multipart), redireccionamentos, cookies,
 * autenticacao basica, chunked, gzip, 100-continue, TLS 1.2/1.3 com
 * verificacao de certificados (a pagina de erro do NetSurf permite continuar),
 * retoma de sessao TLS por servidor (handshakes muito mais rapidos) e
 * cancelamento rapido.
 *
 * Este ficheiro e GPLv2 (faz parte do NetSurfX, derivado do NetSurf).
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <zlib.h>

#include "utils/inet.h"   /* tipos de rede (lwIP) */
#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/threading.h"
#include "psa/crypto.h"

#include "utils/errors.h"
#include "utils/log.h"
#include "utils/nsurl.h"
#include "utils/corestrings.h"
#include "utils/messages.h"
#include "utils/nsoption.h"
#include "utils/useragent.h"
#include "utils/ring.h"
#include "netsurf/ssl_certs.h"
#include "content/fetch.h"
#include "content/fetchers.h"
#include "content/urldb.h"
#include <nsutils/base64.h>

#include "xbox_net.h"

#define CONNECT_TIMEOUT_MS 20000
#define IO_TIMEOUT_MS      30000
#define SLICE_MS           200
#define MAX_PENDING_DATA   (1024 * 1024)
#define MAX_CERT_DEPTH     10
#define TLS_CACHE_SIZE     8
#define POOL_MAX           6       /* ligacoes livres guardadas */
#define SOCK_LIMIT         12      /* o lwIP tem 16 (ver tools/patch_nxdk.py) */
#define KEEPALIVE_IDLE_MS  12000   /* muitos servidores fecham aos 15 s */
#define KEEPALIVE_MAX_USES 100
#define CA_FILE            "D:\\res\\cacert.pem"

/* ---------------------------------------------------------------- estado global */

static CRITICAL_SECTION g_lock;
static bool g_inited;
static int g_tls_state;          /* 0 por iniciar, 1 pronto, -1 falhou */
static mbedtls_entropy_context g_entropy;
static mbedtls_ctr_drbg_context g_drbg;
static mbedtls_x509_crt g_ca;
static char *g_user_agent;

struct tls_cache_ent { char host[128]; int port; mbedtls_ssl_session sess; bool used; DWORD stamp; };
static struct tls_cache_ent g_tls_cache[TLS_CACHE_SIZE];

/* ---------------------------------------------------------------- mutexes do mbedTLS */

static void mt_init(mbedtls_threading_mutex_t *m)
{
	m->cs = malloc(sizeof(CRITICAL_SECTION));
	if (m->cs) InitializeCriticalSection((CRITICAL_SECTION *)m->cs);
	m->is_valid = m->cs != NULL;
}
static void mt_free(mbedtls_threading_mutex_t *m)
{
	if (m->is_valid) { DeleteCriticalSection((CRITICAL_SECTION *)m->cs); free(m->cs); }
	m->cs = NULL; m->is_valid = 0;
}
static int mt_lock(mbedtls_threading_mutex_t *m)
{
	if (!m->is_valid) return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
	EnterCriticalSection((CRITICAL_SECTION *)m->cs);
	return 0;
}
static int mt_unlock(mbedtls_threading_mutex_t *m)
{
	if (!m->is_valid) return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
	LeaveCriticalSection((CRITICAL_SECTION *)m->cs);
	return 0;
}

/* ---------------------------------------------------------------- estrutura de um pedido */

enum xf_result { XR_RUNNING = 0, XR_OK, XR_ERROR, XR_CERT, XR_SSL, XR_TIMEOUT };

struct xf {
	struct xf *r_next, *r_prev;   /* anel de pedidos activos (thread principal) */

	/* --- so thread principal --- */
	struct fetch *parent;
	nsurl *url;
	bool only_2xx;
	bool aborted;
	bool locked;
	bool started;
	bool certs_sent;
	bool headers_done;           /* cabecalhos ja processados (codigo HTTP decidido) */
	bool skip_body;              /* redirect/401/304 tratados: ignorar o resto */
	long http_code;
	char *location;
	char *realm;
	bool is_post;

	/* --- pedido (preparado na thread principal, so lido pela thread) --- */
	char host[256];
	int port;
	bool https;
	bool downgrade_tls;
	bool cert_override;
	char *req;
	size_t req_len;
	char *post;                  /* corpo do POST (montado em setup) */
	size_t post_len;

	/* --- partilhado (g_lock) --- */
	int refs;
	volatile bool cancel;
	char *hdr;  size_t hdr_len, hdr_cap;     /* linhas de cabecalho por entregar */
	bool hdr_complete;
	uint8_t *data; size_t data_len, data_cap;
	enum xf_result result;
	char err[160];
	struct cert_chain *chain;
	bool chain_ready;
	HANDLE thread;
};

static struct xf *g_ring;

static void xf_release(struct xf *f)   /* chamar com g_lock */
{
	if (--f->refs > 0) return;
	if (f->thread) CloseHandle(f->thread);
	free(f->req);
	free(f->post);
	free(f->hdr);
	free(f->data);
	free(f);
}

/* ---------------------------------------------------------------- tampoes partilhados */

static bool buf_append(char **b, size_t *len, size_t *cap, const void *src, size_t n)
{
	if (*len + n + 1 > *cap) {
		size_t nc = *cap ? *cap : 4096;
		while (nc < *len + n + 1) nc *= 2;
		char *nb = realloc(*b, nc);
		if (nb == NULL) return false;
		*b = nb; *cap = nc;
	}
	memcpy(*b + *len, src, n);
	*len += n;
	(*b)[*len] = 0;
	return true;
}

static void set_error(struct xf *f, enum xf_result r, const char *msg)
{
	EnterCriticalSection(&g_lock);
	if (f->result == XR_RUNNING) {
		f->result = r;
		snprintf(f->err, sizeof f->err, "%s", msg);
	}
	LeaveCriticalSection(&g_lock);
}

/* entrega dados com controlo de fluxo: se a thread principal estiver atrasada, espera */
static bool push_data(struct xf *f, const uint8_t *p, size_t n)
{
	for (;;) {
		EnterCriticalSection(&g_lock);
		if (f->cancel) { LeaveCriticalSection(&g_lock); return false; }
		if (f->data_len < MAX_PENDING_DATA) {
			bool ok = buf_append((char **)&f->data, &f->data_len, &f->data_cap, p, n);
			LeaveCriticalSection(&g_lock);
			if (!ok) set_error(f, XR_ERROR, "Out of memory");
			return ok;
		}
		LeaveCriticalSection(&g_lock);
		Sleep(10);
	}
}

/* ---------------------------------------------------------------- rede */

static void sock_close(int s) { if (s >= 0) lwip_close(s); }

/* espera ate o socket estar pronto; devolve 1 pronto, 0 cancelado, -1 timeout/erro */
static int sock_wait(struct xf *f, int s, bool wr, int total_ms)
{
	int waited = 0;
	while (!f->cancel) {
		fd_set set;
		struct timeval tv = { 0, SLICE_MS * 1000 };
		FD_ZERO(&set);
		FD_SET(s, &set);
		int r = wr ? lwip_select(s + 1, NULL, &set, NULL, &tv)
			   : lwip_select(s + 1, &set, NULL, NULL, &tv);
		if (r > 0) return 1;
		if (r < 0) return -1;
		waited += SLICE_MS;
		if (waited >= total_ms) return -1;
	}
	return 0;
}

/* ---------------------------------------------------------------- cache de DNS */

/* O lwIP so guarda 4 nomes; uma pagina moderna usa facilmente 10 servidores. */
#define DNS_CACHE_SIZE 32
#define DNS_CACHE_TTL_MS (10 * 60 * 1000)

struct dns_ent { char host[128]; struct in_addr addr; DWORD stamp; bool used; };
static struct dns_ent g_dns[DNS_CACHE_SIZE];

static bool dns_cache_get(const char *host, struct in_addr *out)
{
	bool ok = false;
	DWORD now = GetTickCount();
	EnterCriticalSection(&g_lock);
	for (int i = 0; i < DNS_CACHE_SIZE; i++) {
		if (g_dns[i].used && strcasecmp(g_dns[i].host, host) == 0) {
			if (now - g_dns[i].stamp < DNS_CACHE_TTL_MS) {
				*out = g_dns[i].addr;
				ok = true;
			} else {
				g_dns[i].used = false;
			}
			break;
		}
	}
	LeaveCriticalSection(&g_lock);
	return ok;
}

static void dns_cache_put(const char *host, const struct in_addr *addr)
{
	int slot = -1, oldest = 0;
	if (strlen(host) >= sizeof g_dns[0].host) return;
	EnterCriticalSection(&g_lock);
	for (int i = 0; i < DNS_CACHE_SIZE; i++) {
		if (g_dns[i].used && strcasecmp(g_dns[i].host, host) == 0) { slot = i; break; }
		if (!g_dns[i].used) { if (slot < 0) slot = i; }
		else if (g_dns[i].stamp < g_dns[oldest].stamp) oldest = i;
	}
	if (slot < 0) slot = oldest;
	snprintf(g_dns[slot].host, sizeof g_dns[slot].host, "%s", host);
	g_dns[slot].addr = *addr;
	g_dns[slot].stamp = GetTickCount();
	g_dns[slot].used = true;
	LeaveCriticalSection(&g_lock);
}

static void dns_cache_drop(const char *host)
{
	EnterCriticalSection(&g_lock);
	for (int i = 0; i < DNS_CACHE_SIZE; i++)
		if (g_dns[i].used && strcasecmp(g_dns[i].host, host) == 0)
			g_dns[i].used = false;
	LeaveCriticalSection(&g_lock);
}

static int connect_host(struct xf *f)
{
	char port[8];
	struct addrinfo hints, *ai = NULL, *a;
	int s = -1;

	struct addrinfo cached;
	struct sockaddr_in cached_sa;

	snprintf(port, sizeof port, "%d", f->port);
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (dns_cache_get(f->host, &cached_sa.sin_addr)) {
		/* endereco ja conhecido: evita perguntar outra vez ao DNS */
		memset(&cached, 0, sizeof cached);
		cached_sa.sin_family = AF_INET;
		cached_sa.sin_port = htons((uint16_t)f->port);
		cached.ai_family = AF_INET;
		cached.ai_socktype = SOCK_STREAM;
		cached.ai_addr = (struct sockaddr *)&cached_sa;
		cached.ai_addrlen = sizeof cached_sa;
	} else if (lwip_getaddrinfo(f->host, port, &hints, &ai) != 0 || ai == NULL) {
		set_error(f, XR_ERROR, "Could not resolve the server name (DNS)");
		return -1;
	}
	for (a = ai ? ai : &cached; a != NULL && !f->cancel; a = a->ai_next) {
		s = lwip_socket(a->ai_family, SOCK_STREAM, 0);
		if (s < 0) continue;
		/* ligacao nao bloqueante: assim respeitamos o tempo limite e o cancelamento */
		int nb = 1;
		lwip_ioctl(s, FIONBIO, &nb);
		int r = lwip_connect(s, a->ai_addr, a->ai_addrlen);
		if (r == 0 || sock_wait(f, s, true, CONNECT_TIMEOUT_MS) == 1) {
			int soerr = 0;
			socklen_t sl = sizeof soerr;
			lwip_getsockopt(s, SOL_SOCKET, SO_ERROR, &soerr, &sl);
			if (soerr == 0) {
				nb = 0;
				lwip_ioctl(s, FIONBIO, &nb);
				break;
			}
		}
		sock_close(s);
		s = -1;
	}
	if (s >= 0 && ai != NULL && a != NULL && a->ai_family == AF_INET)
		dns_cache_put(f->host, &((struct sockaddr_in *)a->ai_addr)->sin_addr);
	if (s < 0 && ai == NULL)
		dns_cache_drop(f->host);    /* o endereco guardado deixou de servir */
	if (ai) lwip_freeaddrinfo(ai);
	if (s < 0 && !f->cancel)
		set_error(f, XR_TIMEOUT, "Could not connect to the server");
	return s;
}

struct io { struct xf *f; int s; };

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
	struct io *io = ctx;
	if (io->f->cancel) return MBEDTLS_ERR_NET_SEND_FAILED;
	int r = sock_wait(io->f, io->s, true, IO_TIMEOUT_MS);
	if (r != 1) return MBEDTLS_ERR_NET_SEND_FAILED;
	int n = lwip_send(io->s, buf, len, 0);
	return n < 0 ? MBEDTLS_ERR_NET_SEND_FAILED : n;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
	struct io *io = ctx;
	int r = sock_wait(io->f, io->s, false, IO_TIMEOUT_MS);
	if (r == 0) return MBEDTLS_ERR_NET_RECV_FAILED;
	if (r < 0) return MBEDTLS_ERR_SSL_TIMEOUT;
	int n = lwip_recv(io->s, buf, len, 0);
	if (n < 0) return MBEDTLS_ERR_NET_RECV_FAILED;
	return n;   /* 0 = ligacao fechada */
}

/* ---------------------------------------------------------------- TLS */

static bool tls_global_init(void)
{
	FILE *fp;
	long n;
	unsigned char *buf;

	EnterCriticalSection(&g_lock);
	if (g_tls_state != 0) {
		bool ok = g_tls_state == 1;
		LeaveCriticalSection(&g_lock);
		return ok;
	}
	g_tls_state = -1;
	mbedtls_x509_crt_init(&g_ca);
	fp = fopen(CA_FILE, "rb");
	if (fp != NULL) {
		fseek(fp, 0, SEEK_END);
		n = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		buf = malloc((size_t)n + 1);
		if (buf != NULL) {
			size_t got = fread(buf, 1, (size_t)n, fp);
			buf[got] = 0;
			if (mbedtls_x509_crt_parse(&g_ca, buf, got + 1) >= 0)
				g_tls_state = 1;
			free(buf);
		}
		fclose(fp);
	}
	LeaveCriticalSection(&g_lock);
	if (g_tls_state != 1)
		NSLOG(netsurf, ERROR, "Nao foi possivel ler %s", CA_FILE);
	return g_tls_state == 1;
}

struct vchain { int depth; struct { uint32_t flags; unsigned char *der; size_t len; } c[MAX_CERT_DEPTH]; };

static int verify_cb(void *ctx, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
	struct vchain *vc = ctx;
	if (depth >= 0 && depth < MAX_CERT_DEPTH) {
		vc->c[depth].flags = *flags;
		if (vc->c[depth].der == NULL) {
			vc->c[depth].der = malloc(crt->raw.len);
			if (vc->c[depth].der) {
				memcpy(vc->c[depth].der, crt->raw.p, crt->raw.len);
				vc->c[depth].len = crt->raw.len;
			}
		}
		if (depth + 1 > vc->depth) vc->depth = depth + 1;
	}
	return 0;
}

static ssl_cert_err map_flags(uint32_t fl)
{
	if (fl == 0) return SSL_CERT_ERR_OK;
	if (fl & MBEDTLS_X509_BADCERT_EXPIRED) return SSL_CERT_ERR_TOO_OLD;
	if (fl & MBEDTLS_X509_BADCERT_FUTURE) return SSL_CERT_ERR_TOO_YOUNG;
	if (fl & MBEDTLS_X509_BADCERT_CN_MISMATCH) return SSL_CERT_ERR_HOSTNAME_MISMATCH;
	if (fl & MBEDTLS_X509_BADCERT_REVOKED) return SSL_CERT_ERR_REVOKED;
	if (fl & MBEDTLS_X509_BADCERT_NOT_TRUSTED) return SSL_CERT_ERR_BAD_ISSUER;
	if (fl & (MBEDTLS_X509_BADCERT_BAD_MD | MBEDTLS_X509_BADCERT_BAD_PK | MBEDTLS_X509_BADCERT_BAD_KEY))
		return SSL_CERT_ERR_BAD_SIG;
	return SSL_CERT_ERR_UNKNOWN;
}

/* publica a cadeia uma so vez (a thread principal pode ja estar a usa-la) */
static void set_chain(struct xf *f, struct cert_chain *chain)
{
	EnterCriticalSection(&g_lock);
	if (!f->chain_ready) {
		f->chain = chain;
		f->chain_ready = true;
		chain = NULL;
	}
	LeaveCriticalSection(&g_lock);
	if (chain) cert_chain_free(chain);
}

static void publish_chain(struct xf *f, struct vchain *vc, uint32_t result_flags,
			  struct cert_chain **keep)
{
	struct cert_chain *chain = NULL;
	if (vc->depth > 0 && cert_chain_alloc(vc->depth, &chain) == NSERROR_OK) {
		for (int i = 0; i < vc->depth; i++) {
			chain->certs[i].err = map_flags(vc->c[i].flags);
			chain->certs[i].der = vc->c[i].der;   /* passa a pertencer a cadeia */
			chain->certs[i].der_length = vc->c[i].len;
			vc->c[i].der = NULL;
		}
		/* o erro global (ex.: nome errado) fica no certificado do servidor */
		if (result_flags && chain->certs[0].err == SSL_CERT_ERR_OK)
			chain->certs[0].err = map_flags(result_flags);
	}
	for (int i = 0; i < MAX_CERT_DEPTH; i++) { free(vc->c[i].der); vc->c[i].der = NULL; }
	if (keep != NULL && chain != NULL)
		cert_chain_dup(chain, keep);
	set_chain(f, chain);
}

static void tls_cache_get(struct xf *f, mbedtls_ssl_context *ssl)
{
	EnterCriticalSection(&g_lock);
	for (int i = 0; i < TLS_CACHE_SIZE; i++) {
		if (g_tls_cache[i].used && g_tls_cache[i].port == f->port &&
		    strcasecmp(g_tls_cache[i].host, f->host) == 0) {
			mbedtls_ssl_set_session(ssl, &g_tls_cache[i].sess);
			break;
		}
	}
	LeaveCriticalSection(&g_lock);
}

static void tls_cache_put(struct xf *f, mbedtls_ssl_context *ssl)
{
	mbedtls_ssl_session s;
	mbedtls_ssl_session_init(&s);
	if (mbedtls_ssl_get_session(ssl, &s) != 0) { mbedtls_ssl_session_free(&s); return; }
	EnterCriticalSection(&g_lock);
	int slot = -1, oldest = 0;
	for (int i = 0; i < TLS_CACHE_SIZE; i++) {
		if (g_tls_cache[i].used && g_tls_cache[i].port == f->port &&
		    strcasecmp(g_tls_cache[i].host, f->host) == 0) { slot = i; break; }
		if (!g_tls_cache[i].used) { if (slot < 0) slot = i; }
		else if (g_tls_cache[i].stamp < g_tls_cache[oldest].stamp) oldest = i;
	}
	if (slot < 0) slot = oldest;
	if (g_tls_cache[slot].used) mbedtls_ssl_session_free(&g_tls_cache[slot].sess);
	g_tls_cache[slot].sess = s;           /* a copia passa a ser da cache */
	snprintf(g_tls_cache[slot].host, sizeof g_tls_cache[slot].host, "%s", f->host);
	g_tls_cache[slot].port = f->port;
	g_tls_cache[slot].used = true;
	g_tls_cache[slot].stamp = GetTickCount();
	LeaveCriticalSection(&g_lock);
}

/* ---------------------------------------------------------------- ligacoes (keep-alive) */

/*
 * Uma ligacao (socket + TLS opcional) pode servir varios pedidos seguidos ao
 * mesmo servidor (HTTP/1.1 keep-alive). Poupa o DNS, o connect e sobretudo o
 * handshake TLS, que no Pentium III de 733 MHz custa centenas de ms.
 * As ligacoes livres ficam numa lista (g_pool) protegida por g_lock.
 */
struct conn {
	struct conn *next;
	char host[256];
	int port;
	bool https;
	struct io io;
	bool tls;
	bool tls_inited;
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
	struct cert_chain *chain;    /* copia da cadeia (para reutilizacoes) */
	DWORD idle_since;
	int uses;
};

static struct conn *g_pool;
static int g_pool_len;
static int g_socks;                  /* sockets abertos por nos (g_lock) */

static void conn_destroy(struct conn *c, bool graceful)
{
	if (c == NULL) return;
	if (c->tls_inited) {
		if (graceful) {
			/* close_notify sem bloquear: so tentamos enviar */
			struct xf dummy;
			memset(&dummy, 0, sizeof dummy);
			c->io.f = &dummy;
			dummy.cancel = true;     /* bio_send desiste logo se bloquear */
			mbedtls_ssl_close_notify(&c->ssl);
		}
		mbedtls_ssl_free(&c->ssl);
		mbedtls_ssl_config_free(&c->conf);
	}
	if (c->io.s >= 0) {
		sock_close(c->io.s);
		EnterCriticalSection(&g_lock);
		g_socks--;
		LeaveCriticalSection(&g_lock);
	}
	if (c->chain) cert_chain_free(c->chain);
	free(c);
}

/* uma ligacao livre ainda esta boa? (o servidor pode te-la fechado) */
static bool conn_alive(struct conn *c)
{
	fd_set set;
	struct timeval tv = { 0, 0 };
	FD_ZERO(&set);
	FD_SET(c->io.s, &set);
	/* legivel numa ligacao parada = fechada (EOF) ou lixo: nao serve */
	return lwip_select(c->io.s + 1, &set, NULL, NULL, &tv) == 0;
}

/* tira da lista uma ligacao livre para host:port; NULL se nao houver */
static struct conn *pool_take(struct xf *f)
{
	struct conn *found = NULL, *dead = NULL;
	struct conn **pp;
	DWORD now = GetTickCount();

	EnterCriticalSection(&g_lock);
	for (pp = &g_pool; *pp; ) {
		struct conn *c = *pp;
		bool stale = (now - c->idle_since) > KEEPALIVE_IDLE_MS;
		if (stale || (found == NULL && c->port == f->port && c->https == f->https &&
			      strcasecmp(c->host, f->host) == 0)) {
			*pp = c->next;
			g_pool_len--;
			if (stale) { c->next = dead; dead = c; }
			else found = c;
			continue;
		}
		pp = &c->next;
	}
	LeaveCriticalSection(&g_lock);

	while (dead) { struct conn *n = dead->next; conn_destroy(dead, false); dead = n; }
	if (found && !conn_alive(found)) {
		conn_destroy(found, false);
		found = NULL;
	}
	return found;
}

static void pool_put(struct conn *c)
{
	struct conn *evict = NULL;
	c->idle_since = GetTickCount();
	EnterCriticalSection(&g_lock);
	c->next = g_pool;
	g_pool = c;
	g_pool_len++;
	if (g_pool_len > POOL_MAX) {
		/* tira a mais antiga (a ultima da lista) */
		struct conn **pp = &g_pool;
		while ((*pp)->next) pp = &(*pp)->next;
		evict = *pp;
		*pp = NULL;
		g_pool_len--;
	}
	LeaveCriticalSection(&g_lock);
	if (evict) conn_destroy(evict, false);
}

/* fecha uma ligacao livre (a mais antiga) para libertar um socket */
static bool pool_evict_one(void)
{
	struct conn *evict = NULL, **pp;
	EnterCriticalSection(&g_lock);
	if (g_pool) {
		pp = &g_pool;
		while ((*pp)->next) pp = &(*pp)->next;
		evict = *pp;
		*pp = NULL;
		g_pool_len--;
	}
	LeaveCriticalSection(&g_lock);
	if (evict == NULL) return false;
	conn_destroy(evict, false);
	return true;
}

static void pool_clear(void)
{
	while (pool_evict_one()) ;
}

/* reserva um socket (o lwIP tem uma tabela fixa); espera se for preciso */
static bool sock_reserve(struct xf *f)
{
	for (;;) {
		EnterCriticalSection(&g_lock);
		if (g_socks < SOCK_LIMIT) {
			g_socks++;
			LeaveCriticalSection(&g_lock);
			return true;
		}
		LeaveCriticalSection(&g_lock);
		if (pool_evict_one()) continue;
		if (f->cancel) return false;
		Sleep(20);
	}
}

/* devolve bytes lidos, 0 no fim, <0 erro */
static int conn_read(struct conn *c, unsigned char *b, size_t n)
{
	if (!c->tls) {
		int r = sock_wait(c->io.f, c->io.s, false, IO_TIMEOUT_MS);
		if (r == 0) return -1;
		if (r < 0) { set_error(c->io.f, XR_TIMEOUT, "The server stopped responding"); return -1; }
		r = lwip_recv(c->io.s, b, n, 0);
		return r < 0 ? -1 : r;
	}
	for (;;) {
		int r = mbedtls_ssl_read(&c->ssl, b, n);
		if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE ||
		    r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
			continue;
		if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || r == 0) return 0;
		if (r == MBEDTLS_ERR_SSL_TIMEOUT) {
			set_error(c->io.f, XR_TIMEOUT, "The server stopped responding");
			return -1;
		}
		if (r == MBEDTLS_ERR_NET_RECV_FAILED && !c->io.f->cancel) return 0; /* fecho abrupto */
		return r < 0 ? -1 : r;
	}
}

static bool conn_write(struct conn *c, const unsigned char *b, size_t n)
{
	size_t sent = 0;
	while (sent < n) {
		int r;
		if (c->tls) {
			r = mbedtls_ssl_write(&c->ssl, b + sent, n - sent);
			if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
		} else {
			if (sock_wait(c->io.f, c->io.s, true, IO_TIMEOUT_MS) != 1) return false;
			r = lwip_send(c->io.s, b + sent, n - sent, 0);
		}
		if (r <= 0) return false;
		sent += (size_t)r;
	}
	return true;
}

/* abre uma ligacao nova (DNS, connect, TLS); NULL com o erro ja marcado */
static struct conn *conn_open(struct xf *f)
{
	struct conn *c = calloc(1, sizeof *c);
	struct vchain vc;
	int r;

	if (c == NULL) { set_error(f, XR_ERROR, "Out of memory"); return NULL; }
	memset(&vc, 0, sizeof vc);
	c->io.s = -1;
	c->io.f = f;
	snprintf(c->host, sizeof c->host, "%s", f->host);
	c->port = f->port;
	c->https = f->https;

	if (!sock_reserve(f)) { free(c); return NULL; }
	c->io.s = connect_host(f);
	if (c->io.s < 0) {
		EnterCriticalSection(&g_lock);
		g_socks--;
		LeaveCriticalSection(&g_lock);
		free(c);
		return NULL;
	}
	if (!f->https) return c;

	{
		uint32_t vflags;
		c->tls = true;
		c->tls_inited = true;
		mbedtls_ssl_init(&c->ssl);
		mbedtls_ssl_config_init(&c->conf);
		mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT,
					    MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
		/* verificamos nos proprios para poder mostrar a pagina de aviso do NetSurf */
		mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
		mbedtls_ssl_conf_ca_chain(&c->conf, &g_ca, NULL);
		mbedtls_ssl_conf_rng(&c->conf, mbedtls_ctr_drbg_random, &g_drbg);
		mbedtls_ssl_conf_verify(&c->conf, verify_cb, &vc);
		if (f->downgrade_tls)
			mbedtls_ssl_conf_max_tls_version(&c->conf, MBEDTLS_SSL_VERSION_TLS1_2);
		if (mbedtls_ssl_setup(&c->ssl, &c->conf) != 0 ||
		    mbedtls_ssl_set_hostname(&c->ssl, f->host) != 0) {
			set_error(f, XR_SSL, "TLS setup failed");
			goto fail;
		}
		mbedtls_ssl_set_bio(&c->ssl, &c->io, bio_send, bio_recv, NULL);
		tls_cache_get(f, &c->ssl);
		while ((r = mbedtls_ssl_handshake(&c->ssl)) != 0) {
			if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
			if (f->cancel) goto fail;
			if (r == MBEDTLS_ERR_SSL_TIMEOUT) set_error(f, XR_TIMEOUT, "Secure connection timed out");
			else {
				char e[100];
				snprintf(e, sizeof e, "Secure connection failed (TLS error -0x%04x)", -r);
				set_error(f, XR_SSL, e);
			}
			publish_chain(f, &vc, 0, NULL);
			goto fail;
		}
		/* a verificacao so acontece no handshake; vc deixa de existir */
		mbedtls_ssl_conf_verify(&c->conf, NULL, NULL);
		vflags = mbedtls_ssl_get_verify_result(&c->ssl);
		/* so guardamos a cadeia (para reutilizar a ligacao) se estiver toda boa */
		publish_chain(f, &vc, vflags, vflags == 0 ? &c->chain : NULL);
		if (vflags != 0) {
			if (!f->cert_override) {
				set_error(f, XR_CERT, "Certificate verification failed");
				goto fail;
			}
			c->uses = -1;   /* certificado aceite a mao: nunca reutilizar */
		}
		tls_cache_put(f, &c->ssl);
	}
	return c;

fail:
	for (int i = 0; i < MAX_CERT_DEPTH; i++) free(vc.c[i].der);
	conn_destroy(c, false);
	return NULL;
}

/* descodificacao do corpo: chunked e/ou gzip */
struct body {
	bool chunked, gzip, gzip_ok, done;
	long long remaining;          /* Content-Length (-1 se desconhecido) */
	int cstate;                   /* 0 tamanho, 1 dados, 2 CRLF apos dados, 3 trailers */
	unsigned long long csize;
	char line[64]; int llen;
	z_stream zs;
};

static voidpf zalloc_cb(voidpf o, uInt i, uInt s) { (void)o; return calloc(i, s); }
static void zfree_cb(voidpf o, voidpf p) { (void)o; free(p); }

static bool emit(struct xf *f, struct body *bd, const unsigned char *p, size_t n)
{
	if (n == 0) return true;
	if (!bd->gzip) return push_data(f, p, n);
	unsigned char out[16384];
	bd->zs.next_in = (Bytef *)p;
	bd->zs.avail_in = (uInt)n;
	while (bd->zs.avail_in > 0) {
		bd->zs.next_out = out;
		bd->zs.avail_out = sizeof out;
		int r = inflate(&bd->zs, Z_NO_FLUSH);
		size_t got = sizeof out - bd->zs.avail_out;
		if (got && !push_data(f, out, got)) return false;
		if (r == Z_STREAM_END) { bd->zs.avail_in = 0; break; }
		if (r != Z_OK && r != Z_BUF_ERROR) {
			/* dados estragados no fim (acontece): fica com o que ja temos */
			return true;
		}
		if (r == Z_BUF_ERROR && got == 0) break;
	}
	return true;
}

/* processa bytes do corpo; devolve false se for para parar */
static bool body_feed(struct xf *f, struct body *bd, const unsigned char *p, size_t n)
{
	if (!bd->chunked) {
		if (bd->remaining >= 0) {
			if ((long long)n > bd->remaining) n = (size_t)bd->remaining;
			bd->remaining -= n;
			if (bd->remaining == 0) bd->done = true;
		}
		return emit(f, bd, p, n);
	}
	size_t i = 0;
	while (i < n && !bd->done) {
		if (bd->cstate == 1) {
			size_t k = n - i;
			if (k > bd->csize) k = (size_t)bd->csize;
			if (!emit(f, bd, p + i, k)) return false;
			i += k;
			bd->csize -= k;
			if (bd->csize == 0) bd->cstate = 2;
			continue;
		}
		char ch = (char)p[i++];
		if (bd->cstate == 2) {             /* CRLF depois do bloco */
			if (ch == '\n') bd->cstate = 0;
			continue;
		}
		if (ch == '\n') {
			bd->line[bd->llen] = 0;
			if (bd->cstate == 0) {
				bd->csize = strtoull(bd->line, NULL, 16);
				bd->cstate = bd->csize ? 1 : 3;
			} else if (bd->cstate == 3) {
				/* trailers: termina na linha vazia */
				if (bd->llen == 0 || (bd->llen == 1 && bd->line[0] == '\r')) bd->done = true;
			}
			bd->llen = 0;
		} else if (bd->llen < (int)sizeof bd->line - 1) {
			bd->line[bd->llen++] = ch;
		}
	}
	return true;
}

/* a ligacao pode continuar aberta depois desta resposta? */
static bool resp_keepalive(const char *hb, size_t hdr_bytes)
{
	bool http11 = strncmp(hb, "HTTP/1.1", 8) == 0;
	bool ka = http11;
	for (const char *line = hb; line < hb + hdr_bytes; ) {
		const char *nl = memchr(line, '\n', (size_t)(hb + hdr_bytes - line));
		char tmp[96];
		if (nl == NULL) break;
		size_t ll = (size_t)(nl - line);
		if (ll >= sizeof tmp) ll = sizeof tmp - 1;
		memcpy(tmp, line, ll);
		tmp[ll] = 0;
		for (char *t = tmp; *t; t++) if (*t >= 'A' && *t <= 'Z') *t += 32;
		if (!strncmp(tmp, "connection:", 11)) {
			if (strstr(tmp + 11, "close")) ka = false;
			else if (strstr(tmp + 11, "keep-alive")) ka = true;
		}
		line = nl + 1;
	}
	return ka;
}

enum rr { RR_DONE, RR_RETRY };

/* faz o pedido numa ligacao; RR_RETRY = a ligacao reutilizada estava morta
 * antes de chegar qualquer resposta (pode tentar-se numa ligacao nova) */
static enum rr run_on_conn(struct xf *f, struct conn *c, bool reused, bool *reusable)
{
	unsigned char buf[8192];
	char *hb = NULL; size_t hlen = 0, hcap = 0;   /* cabecalhos ainda por terminar */
	struct body bd;
	int status = 0;
	bool keepalive = false;
	enum rr ret = RR_DONE;

	memset(&bd, 0, sizeof bd);
	*reusable = false;

	if (!conn_write(c, (unsigned char *)f->req, f->req_len)) {
		if (reused && !f->cancel) { ret = RR_RETRY; goto out; }
		if (!f->cancel) set_error(f, XR_ERROR, "Could not send the request");
		goto out;
	}

	/* ---- cabecalhos ---- */
	for (;;) {
		int n = conn_read(c, buf, sizeof buf);
		if (n <= 0) {
			if (reused && hlen == 0 && !f->cancel) {
				/* o servidor fechou a ligacao parada: tenta de novo */
				EnterCriticalSection(&g_lock);
				if (f->result == XR_TIMEOUT) { f->result = XR_RUNNING; f->err[0] = 0; }
				LeaveCriticalSection(&g_lock);
				ret = RR_RETRY;
				goto out;
			}
			if (!f->cancel) set_error(f, XR_ERROR, "The server closed the connection without a reply");
			goto out;
		}
		if (!buf_append(&hb, &hlen, &hcap, buf, (size_t)n)) { set_error(f, XR_ERROR, "Out of memory"); goto out; }
		char *end;
	again:
		end = strstr(hb, "\r\n\r\n");
		if (end == NULL) {
			char *e2 = strstr(hb, "\n\n");   /* servidores mal-educados */
			if (e2 == NULL) {
				if (hlen > 256 * 1024) { set_error(f, XR_ERROR, "Response headers too large"); goto out; }
				continue;
			}
			end = e2;
		}
		size_t hdr_bytes = (size_t)(end - hb) + ((end[0] == '\r') ? 4 : 2);
		if (strncmp(hb, "HTTP/", 5) != 0) { set_error(f, XR_ERROR, "Invalid reply from the server"); goto out; }
		status = atoi(strchr(hb, ' ') ? strchr(hb, ' ') + 1 : "0");
		if (status >= 100 && status < 200 && status != 101) {
			/* resposta intermedia (100 Continue): descarta e continua */
			memmove(hb, hb + hdr_bytes, hlen - hdr_bytes + 1);
			hlen -= hdr_bytes;
			if (hlen > 0) goto again;
			continue;
		}
		keepalive = resp_keepalive(hb, hdr_bytes);
		/* 1.a passagem: como vem o corpo? (a ordem dos cabecalhos e livre) */
		bd.remaining = -1;
		for (char *line = hb; line < hb + hdr_bytes; ) {
			char *nl = memchr(line, '\n', (size_t)(hb + hdr_bytes - line));
			char tmp[160];
			if (nl == NULL) break;
			size_t ll = (size_t)(nl - line);
			if (ll && line[ll - 1] == '\r') ll--;
			snprintf(tmp, sizeof tmp, "%.*s", (int)ll, line);
			for (char *t = tmp; *t; t++) if (*t >= 'A' && *t <= 'Z') *t += 32;
			if (!strncmp(tmp, "transfer-encoding:", 18) && strstr(tmp + 18, "chunked"))
				bd.chunked = true;
			else if (!strncmp(tmp, "content-length:", 15))
				bd.remaining = strtoll(tmp + 15, NULL, 10);
			else if (!strncmp(tmp, "content-encoding:", 17) &&
				 (strstr(tmp + 17, "gzip") || strstr(tmp + 17, "deflate")))
				bd.gzip = true;
			line = nl + 1;
		}
		/* 2.a passagem: publica as linhas. O NetSurf recebe o corpo ja descomprimido,
		 * por isso escondemos Content-Encoding e o Content-Length comprimido. */
		EnterCriticalSection(&g_lock);
		for (char *line = hb; line < hb + hdr_bytes; ) {
			char *nl = memchr(line, '\n', (size_t)(hb + hdr_bytes - line));
			if (nl == NULL) break;
			size_t ll = (size_t)(nl - line);
			if (ll && line[ll - 1] == '\r') ll--;
			bool hide = bd.gzip && (!strncasecmp(line, "Content-Encoding:", 17) ||
						!strncasecmp(line, "Content-Length:", 15));
			if (ll > 0 && !hide) {
				buf_append(&f->hdr, &f->hdr_len, &f->hdr_cap, line, ll);
				buf_append(&f->hdr, &f->hdr_len, &f->hdr_cap, "\r\n", 2);
			}
			line = nl + 1;
		}
		f->hdr_complete = true;
		LeaveCriticalSection(&g_lock);

		if (bd.chunked) bd.remaining = -1;
		if (bd.gzip) {
			bd.zs.zalloc = zalloc_cb;
			bd.zs.zfree = zfree_cb;
			if (inflateInit2(&bd.zs, 15 + 32) != Z_OK) { set_error(f, XR_ERROR, "Out of memory"); goto out; }
			bd.gzip_ok = true;
		}
		if (status == 204 || status == 304) bd.done = true;
		if (bd.remaining == 0) bd.done = true;
		/* sem tamanho nem chunked: o corpo acaba quando o servidor fechar */
		if (!bd.done && !bd.chunked && bd.remaining < 0) keepalive = false;
		/* o resto do que ja lemos e corpo */
		if (!bd.done && hlen > hdr_bytes)
			if (!body_feed(f, &bd, (unsigned char *)hb + hdr_bytes, hlen - hdr_bytes)) goto out;
		break;
	}

	/* ---- corpo ---- */
	while (!bd.done && !f->cancel) {
		int n = conn_read(c, buf, sizeof buf);
		if (n < 0) {
			EnterCriticalSection(&g_lock);
			bool have = f->result != XR_RUNNING;
			LeaveCriticalSection(&g_lock);
			if (!have && !f->cancel) set_error(f, XR_ERROR, "The connection was interrupted");
			goto out;
		}
		if (n == 0) {
			/* fim da ligacao: normal sem Content-Length; chunked cortado aceita-se */
			keepalive = false;
			break;
		}
		if (!body_feed(f, &bd, buf, (size_t)n)) goto out;
	}
	if (f->cancel) goto out;

	set_error(f, XR_OK, "");
	*reusable = keepalive && bd.done && c->uses >= 0;

out:
	if (bd.gzip_ok) inflateEnd(&bd.zs);
	free(hb);
	return ret;
}

static void run_request(struct xf *f)
{
	struct conn *c = NULL;
	bool reused = false, reusable = false;

	if (f->https && !tls_global_init()) {
		set_error(f, XR_SSL, "Missing or invalid certificate file (res\\cacert.pem)");
		return;
	}

	/* a rede pode ainda estar a arrancar (DHCP) */
	if (!nsx_net_wait(30000)) {
		set_error(f, XR_ERROR, "No network connection (check the cable and the router's DHCP)");
		return;
	}

	/* 1.a tentativa: ligacao livre (se houver); se estiver morta, uma nova */
	for (int attempt = 0; attempt < 2 && !f->cancel; attempt++) {
		c = attempt == 0 ? pool_take(f) : NULL;
		reused = c != NULL;
		if (c == NULL) {
			c = conn_open(f);
			if (c == NULL) return;
		} else {
			c->io.f = f;
			if (f->https) {
				/* a cadeia de certificados da ligacao, para o "info da pagina" */
				struct cert_chain *dup = NULL;
				if (c->chain) cert_chain_dup(c->chain, &dup);
				set_chain(f, dup);
			}
		}
		c->uses += (c->uses >= 0) ? 1 : 0;
		if (run_on_conn(f, c, reused, &reusable) == RR_RETRY) {
			conn_destroy(c, false);
			c = NULL;
			continue;
		}
		break;
	}
	if (c == NULL) {
		if (!f->cancel) set_error(f, XR_ERROR, "The server closed the connection without a reply");
		return;
	}
	if (reusable && !f->cancel && c->uses < KEEPALIVE_MAX_USES) {
		c->io.f = NULL;
		pool_put(c);
	} else {
		conn_destroy(c, !f->cancel);
	}
}

static DWORD WINAPI worker(LPVOID arg)
{
	struct xf *f = arg;
	run_request(f);
	EnterCriticalSection(&g_lock);
	if (f->result == XR_RUNNING) {
		f->result = XR_ERROR;
		snprintf(f->err, sizeof f->err, "Fetch failed");
	}
	xf_release(f);
	LeaveCriticalSection(&g_lock);
	return 0;
}

/* ---------------------------------------------------------------- construcao do pedido */

static bool req_add(char **b, size_t *len, size_t *cap, const char *fmt, ...)
{
	char tmp[2048];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
	va_end(ap);
	if (n < 0) return false;
	if ((size_t)n >= sizeof tmp) {
		char *big = malloc((size_t)n + 1);
		if (big == NULL) return false;
		va_start(ap, fmt);
		vsnprintf(big, (size_t)n + 1, fmt, ap);
		va_end(ap);
		bool ok = buf_append(b, len, cap, big, (size_t)n);
		free(big);
		return ok;
	}
	return buf_append(b, len, cap, tmp, (size_t)n);
}

static char *read_whole_file(const char *path, size_t *len)
{
	FILE *fp = fopen(path, "rb");
	char *d = NULL;
	long n;
	*len = 0;
	if (fp == NULL) return NULL;
	fseek(fp, 0, SEEK_END);
	n = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (n >= 0 && (d = malloc((size_t)n + 1)) != NULL)
		*len = fread(d, 1, (size_t)n, fp);
	fclose(fp);
	return d;
}

static void *xf_setup(struct fetch *parent, nsurl *url, bool only_2xx, bool downgrade_tls,
		      const char *post_urlenc, const struct fetch_multipart_data *post_multipart,
		      const char **headers)
{
	struct xf *f = calloc(1, sizeof *f);
	char *path = NULL; size_t plen = 0;
	lwc_string *scheme, *host, *port;
	char *b = NULL; size_t bl = 0, bc = 0;
	char *body = NULL; size_t body_len = 0, body_cap = 0;
	const char *ctype = NULL;
	char boundary[48];

	if (f == NULL) return NULL;
	f->parent = parent;
	f->url = nsurl_ref(url);
	f->only_2xx = only_2xx;
	f->downgrade_tls = downgrade_tls;
	f->refs = 1;

	scheme = nsurl_get_component(url, NSURL_SCHEME);
	host = nsurl_get_component(url, NSURL_HOST);
	port = nsurl_get_component(url, NSURL_PORT);
	if (scheme == NULL || host == NULL) goto fail;
	f->https = lwc_string_length(scheme) == 5 && !strncasecmp(lwc_string_data(scheme), "https", 5);
	snprintf(f->host, sizeof f->host, "%.*s", (int)lwc_string_length(host), lwc_string_data(host));
	f->port = port ? atoi(lwc_string_data(port)) : (f->https ? 443 : 80);
	lwc_string_unref(scheme);
	lwc_string_unref(host);
	if (port) lwc_string_unref(port);
	scheme = host = port = NULL;

	if (nsurl_get(url, NSURL_PATH | NSURL_QUERY, &path, &plen) != NSERROR_OK) goto fail;

	/* corpo do POST */
	if (post_urlenc != NULL) {
		f->is_post = true;
		ctype = "application/x-www-form-urlencoded";
		buf_append(&body, &body_len, &body_cap, post_urlenc, strlen(post_urlenc));
	} else if (post_multipart != NULL) {
		f->is_post = true;
		snprintf(boundary, sizeof boundary, "----NetSurfX%08lx%08lx",
			 (unsigned long)GetTickCount(), (unsigned long)(uintptr_t)f);
		for (const struct fetch_multipart_data *m = post_multipart; m; m = m->next) {
			req_add(&body, &body_len, &body_cap, "--%s\r\n", boundary);
			if (m->file) {
				size_t fl = 0;
				char *fd = (m->rawfile && m->rawfile[0]) ? read_whole_file(m->rawfile, &fl) : NULL;
				req_add(&body, &body_len, &body_cap,
					"Content-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n"
					"Content-Type: application/octet-stream\r\n\r\n", m->name, m->value);
				if (fd) buf_append(&body, &body_len, &body_cap, fd, fl);
				free(fd);
				req_add(&body, &body_len, &body_cap, "\r\n");
			} else {
				req_add(&body, &body_len, &body_cap,
					"Content-Disposition: form-data; name=\"%s\"\r\n\r\n%s\r\n", m->name, m->value);
			}
		}
		req_add(&body, &body_len, &body_cap, "--%s--\r\n", boundary);
	}

	/* linha de pedido e cabecalhos fixos */
	if (f->port == (f->https ? 443 : 80))
		req_add(&b, &bl, &bc, "%s %s HTTP/1.1\r\nHost: %s\r\n", f->is_post ? "POST" : "GET", path, f->host);
	else
		req_add(&b, &bl, &bc, "%s %s HTTP/1.1\r\nHost: %s:%d\r\n", f->is_post ? "POST" : "GET", path, f->host, f->port);
	req_add(&b, &bl, &bc, "User-Agent: %s\r\n", g_user_agent ? g_user_agent : "NetSurf");
	req_add(&b, &bl, &bc, "Accept-Encoding: gzip\r\nConnection: keep-alive\r\n");
	if (nsoption_charp(accept_language) && nsoption_charp(accept_language)[0])
		req_add(&b, &bl, &bc, "Accept-Language: %s, *;q=0.1\r\n", nsoption_charp(accept_language));
	if (nsoption_charp(accept_charset) && nsoption_charp(accept_charset)[0])
		req_add(&b, &bl, &bc, "Accept-Charset: %s, *;q=0.1\r\n", nsoption_charp(accept_charset));
	if (nsoption_bool(do_not_track))
		req_add(&b, &bl, &bc, "DNT: 1\r\n");
	bool have_accept = false;
	for (int i = 0; headers && headers[i]; i++) {
		if (!strncasecmp(headers[i], "Accept:", 7)) have_accept = true;
		req_add(&b, &bl, &bc, "%s\r\n", headers[i]);
	}
	if (!have_accept)
		req_add(&b, &bl, &bc, "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/svg+xml,image/png,image/jpeg,image/gif,*/*;q=0.8\r\n");
	if (body != NULL || f->is_post) {
		if (post_multipart)
			req_add(&b, &bl, &bc, "Content-Type: multipart/form-data; boundary=%s\r\n", boundary);
		else
			req_add(&b, &bl, &bc, "Content-Type: %s\r\n", ctype);
		req_add(&b, &bl, &bc, "Content-Length: %u\r\n", (unsigned)body_len);
	}
	/* o fim dos cabecalhos (e cookies/autenticacao) e acrescentado em start() */
	free(path);
	f->req = b;
	f->req_len = bl;
	f->post = body;
	f->post_len = body_len;

	RING_INSERT(g_ring, f);
	return f;

fail:
	if (scheme) lwc_string_unref(scheme);
	if (host) lwc_string_unref(host);
	if (port) lwc_string_unref(port);
	free(path);
	free(b);
	free(body);
	nsurl_unref(f->url);
	free(f);
	return NULL;
}

static bool xf_start(void *vf)
{
	struct xf *f = vf;
	char *b = NULL; size_t bl = 0, bc = 0;
	char *cookie;
	const char *auth;

	buf_append(&b, &bl, &bc, f->req, f->req_len);

	/* cookies e autenticacao: so podem ser lidos na thread principal */
	cookie = urldb_get_cookie(f->url, true);
	if (cookie != NULL) {
		req_add(&b, &bl, &bc, "Cookie: %s\r\n", cookie);
		free(cookie);
	}
	auth = urldb_get_auth_details(f->url, NULL);
	if (auth != NULL) {
		uint8_t *enc = NULL;
		size_t elen = 0;
		if (nsu_base64_encode_alloc((const uint8_t *)auth, strlen(auth), &enc, &elen) == NSUERROR_OK) {
			req_add(&b, &bl, &bc, "Authorization: Basic %.*s\r\n", (int)elen, enc);
			free(enc);
		}
	}
	f->cert_override = urldb_get_cert_permissions(f->url);
	req_add(&b, &bl, &bc, "\r\n");
	if (f->post != NULL && f->post_len)
		buf_append(&b, &bl, &bc, f->post, f->post_len);
	free(f->post);
	f->post = NULL;
	f->post_len = 0;
	free(f->req);
	f->req = b;
	f->req_len = bl;

	f->refs = 2;   /* NetSurf + thread */
	f->thread = CreateThread(NULL, 192 * 1024, worker, f, 0, NULL);
	if (f->thread == NULL) {
		f->refs = 1;
		EnterCriticalSection(&g_lock);
		f->result = XR_ERROR;
		snprintf(f->err, sizeof f->err, "Could not start the network thread");
		LeaveCriticalSection(&g_lock);
	}
	f->started = true;
	return true;
}

static void xf_abort(void *vf)
{
	struct xf *f = vf;
	f->aborted = true;
	f->cancel = true;   /* a thread desiste no proximo intervalo (<= 200 ms) */
}

static void xf_free(void *vf)
{
	struct xf *f = vf;
	nsurl_unref(f->url);
	free(f->location);
	free(f->realm);
	EnterCriticalSection(&g_lock);
	f->cancel = true;
	if (f->chain) { cert_chain_free(f->chain); f->chain = NULL; }
	xf_release(f);
	LeaveCriticalSection(&g_lock);
}

/* ---------------------------------------------------------------- entrega na thread principal */

static void send_msg(struct xf *f, fetch_msg *msg)
{
	f->locked = true;
	fetch_send_callback(msg, f->parent);
	f->locked = false;
}

static void finish(struct xf *f)
{
	RING_REMOVE(g_ring, f);
	fetch_remove_from_queues(f->parent);
	fetch_free(f->parent);   /* chama xf_free */
}

/* processa o codigo HTTP; devolve true se a resposta ja ficou tratada */
static bool process_headers(struct xf *f)
{
	fetch_msg msg;
	f->headers_done = true;
	fetch_set_http_code(f->parent, f->http_code);
	if (f->http_code == 304 && !f->is_post) {
		msg.type = FETCH_NOTMODIFIED;
		send_msg(f, &msg);
		return true;
	}
	if (f->http_code >= 300 && f->http_code < 400 && f->location != NULL) {
		msg.type = FETCH_REDIRECT;
		msg.data.redirect = f->location;
		send_msg(f, &msg);
		return true;
	}
	if (f->http_code == 401) {
		msg.type = FETCH_AUTH;
		msg.data.auth.realm = f->realm ? f->realm : "";
		send_msg(f, &msg);
		return true;
	}
	if (f->only_2xx && (f->http_code < 200 || f->http_code > 299)) {
		msg.type = FETCH_ERROR;
		msg.data.error = messages_get("Not2xx");
		send_msg(f, &msg);
		return true;
	}
	return false;
}

static void deliver_header_line(struct xf *f, char *line, size_t len)
{
	fetch_msg msg;
	char *v;

	if (!strncmp(line, "HTTP/", 5)) {
		char *sp = memchr(line, ' ', len);
		if (sp) f->http_code = atol(sp + 1);
	}
	msg.type = FETCH_HEADER;
	msg.data.header_or_data.buf = (const uint8_t *)line;
	msg.data.header_or_data.len = len;
	send_msg(f, &msg);
	if (f->aborted) return;

	line[len] = 0;
	if (!strncasecmp(line, "Location:", 9)) {
		v = line + 9;
		while (*v == ' ' || *v == '\t') v++;
		free(f->location);
		f->location = strdup(v);
	} else if (!strncasecmp(line, "Set-Cookie:", 11)) {
		v = line + 11;
		while (*v == ' ' || *v == '\t') v++;
		fetch_set_cookie(f->parent, v);
	} else if (!strncasecmp(line, "WWW-Authenticate:", 17)) {
		char *r = strstr(line, "realm=\"");
		if (r == NULL) r = strstr(line, "Realm=\"");
		if (r != NULL) {
			r += 7;
			char *e = strchr(r, '"');
			if (e) {
				free(f->realm);
				f->realm = strndup(r, (size_t)(e - r));
			}
		}
	}
}

static void xf_poll_one(struct xf *f)
{
	char *hdr = NULL; size_t hdr_len = 0;
	uint8_t *data = NULL; size_t data_len = 0;
	bool hdr_complete, chain_ready;
	struct cert_chain *chain;
	enum xf_result result;
	char err[160];
	fetch_msg msg;

	if (f->aborted) { finish(f); return; }
	if (!f->started) return;

	EnterCriticalSection(&g_lock);
	hdr = f->hdr; hdr_len = f->hdr_len;
	f->hdr = NULL; f->hdr_len = f->hdr_cap = 0;
	data = f->data; data_len = f->data_len;
	f->data = NULL; f->data_len = f->data_cap = 0;
	hdr_complete = f->hdr_complete;
	chain_ready = f->chain_ready;
	chain = f->chain;
	result = f->result;
	memcpy(err, f->err, sizeof err);
	LeaveCriticalSection(&g_lock);

	/* cadeia de certificados (para a pagina de informacao/aviso) */
	if (chain_ready && !f->certs_sent) {
		f->certs_sent = true;
		if (chain != NULL) {
			msg.type = FETCH_CERTS;
			msg.data.chain = chain;
			send_msg(f, &msg);
			if (f->aborted) goto out;
		}
	}

	/* linhas de cabecalho */
	if (hdr != NULL && !f->skip_body) {
		char *p = hdr, *end = hdr + hdr_len;
		while (p < end) {
			char *nl = strstr(p, "\r\n");
			if (nl == NULL) break;
			deliver_header_line(f, p, (size_t)(nl - p));
			if (f->aborted) goto out;
			p = nl + 2;
		}
	}

	if (hdr_complete && !f->headers_done) {
		if (process_headers(f)) {
			f->skip_body = true;
			f->cancel = true;          /* nao precisamos do resto */
			if (!f->aborted) finish(f);
			goto out;
		}
		if (f->aborted) goto out;
	}

	if (data_len && f->headers_done && !f->skip_body) {
		msg.type = FETCH_DATA;
		msg.data.header_or_data.buf = data;
		msg.data.header_or_data.len = data_len;
		send_msg(f, &msg);
		if (f->aborted) goto out;
	}

	if (result != XR_RUNNING) {
		switch (result) {
		case XR_OK:
			if (!f->headers_done && process_headers(f)) {
				if (!f->aborted) finish(f);
				goto out;
			}
			msg.type = FETCH_FINISHED;
			break;
		case XR_CERT:
			msg.type = FETCH_CERT_ERR;
			break;
		case XR_SSL:
			msg.type = FETCH_SSL_ERR;
			break;
		case XR_TIMEOUT:
			msg.type = FETCH_TIMEDOUT;
			msg.data.error = err;
			break;
		default:
			msg.type = FETCH_ERROR;
			msg.data.error = err;
			break;
		}
		NSLOG(netsurf, INFO, "fetch %s: resultado %d %s", nsurl_access(f->url), result, err);
		send_msg(f, &msg);
		finish(f);
	}

out:
	free(hdr);
	free(data);
}

static void xf_poll(lwc_string *scheme)
{
	struct xf *f, *next;
	(void)scheme;
	if (g_ring == NULL) return;
	/* o anel pode mudar durante as chamadas: percorre uma copia da ordem */
	f = g_ring;
	int count = 0;
	do { count++; f = f->r_next; } while (f != g_ring);
	struct xf **list = malloc(sizeof *list * count);
	if (list == NULL) return;
	f = g_ring;
	for (int i = 0; i < count; i++) { list[i] = f; f = f->r_next; }
	for (int i = 0; i < count; i++) {
		next = list[i];
		/* pode ter sido libertado por uma chamada anterior: verifica no anel */
		bool alive = false;
		if (g_ring) {
			struct xf *q = g_ring;
			do { if (q == next) { alive = true; break; } q = q->r_next; } while (q != g_ring);
		}
		if (alive && !next->locked) xf_poll_one(next);
	}
	free(list);
}

/* ---------------------------------------------------------------- registo */

static bool xf_initialise(lwc_string *scheme)
{
	(void)scheme;
	return true;
}

static void xf_finalise(lwc_string *scheme)
{
	(void)scheme;
	pool_clear();
}

static bool xf_acceptable(const nsurl *url)
{
	(void)url;
	return true;
}

static int xf_fdset(lwc_string *scheme, fd_set *r, fd_set *w, fd_set *e)
{
	(void)scheme; (void)r; (void)w; (void)e;
	return -1;
}

nserror fetch_xbox_register(void)
{
	static const struct fetcher_operation_table ops = {
		.initialise = xf_initialise,
		.acceptable = xf_acceptable,
		.setup = xf_setup,
		.start = xf_start,
		.abort = xf_abort,
		.free = xf_free,
		.poll = xf_poll,
		.fdset = xf_fdset,
		.finalise = xf_finalise,
	};
	nserror err;

	if (!g_inited) {
		InitializeCriticalSection(&g_lock);
		mbedtls_threading_set_alt(mt_init, mt_free, mt_lock, mt_unlock);
		psa_crypto_init();
		mbedtls_entropy_init(&g_entropy);
		mbedtls_ctr_drbg_init(&g_drbg);
		if (mbedtls_ctr_drbg_seed(&g_drbg, mbedtls_entropy_func, &g_entropy,
					  (const unsigned char *)"NetSurfX", 8) != 0)
			NSLOG(netsurf, ERROR, "Falha a iniciar o gerador aleatorio do TLS");
		g_user_agent = strdup(user_agent_string());
		g_inited = true;
	}

	err = fetcher_add(lwc_string_ref(corestring_lwc_http), &ops);
	if (err != NSERROR_OK) return err;
	return fetcher_add(lwc_string_ref(corestring_lwc_https), &ops);
}
