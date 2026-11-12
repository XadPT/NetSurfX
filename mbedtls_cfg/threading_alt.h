/* mutex do mbedTLS na Xbox: aponta para um CRITICAL_SECTION alocado (ver fetch_xbox.c) */
#ifndef NS_THREADING_ALT_H
#define NS_THREADING_ALT_H
typedef struct {
	void *cs;
	char is_valid;
} mbedtls_threading_mutex_t;
#endif
