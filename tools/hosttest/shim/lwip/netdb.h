#ifndef FAKE_LWIP_NETDB_H
#define FAKE_LWIP_NETDB_H
#include <netdb.h>
#define lwip_getaddrinfo getaddrinfo
#define lwip_freeaddrinfo freeaddrinfo
#endif
