/* lwIP -> sockets POSIX (testes no PC) */
#ifndef FAKE_LWIP_SOCKETS_H
#define FAKE_LWIP_SOCKETS_H
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <unistd.h>
#define lwip_socket socket
#define lwip_connect connect
#define lwip_send send
#define lwip_recv recv
#define lwip_select select
#define lwip_close close
#define lwip_ioctl ioctl
#define lwip_getsockopt getsockopt
#endif
