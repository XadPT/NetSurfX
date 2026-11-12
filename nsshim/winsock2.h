/* winsock2.h: no NetSurf so precisamos dos tipos (in_addr, in6_addr, AF_*) do lwIP.
 * As funcoes inet_aton/inet_pton sao fornecidas pelo proprio NetSurf (utils/utils.c). */
#ifndef NSSHIM_WINSOCK2_H
#define NSSHIM_WINSOCK2_H
#include "xbox_compat.h"
#include <lwip/sockets.h>
#include <lwip/inet.h>
/* o lwIP define macros com nomes POSIX (close, read, poll...) que colidem com o NetSurf */
#undef accept
#undef bind
#undef shutdown
#undef getpeername
#undef getsockname
#undef setsockopt
#undef getsockopt
#undef closesocket
#undef connect
#undef listen
#undef recv
#undef recvmsg
#undef recvfrom
#undef send
#undef sendmsg
#undef sendto
#undef socket
#undef select
#undef poll
#undef ioctlsocket
#undef read
#undef readv
#undef write
#undef writev
#undef close
#undef fcntl
#undef ioctl
#undef inet_aton
#undef inet_pton
#undef inet_ntop
#undef inet_ntoa
#undef inet_addr
#endif
