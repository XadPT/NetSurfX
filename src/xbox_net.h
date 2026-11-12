#ifndef XBOX_NET_H
#define XBOX_NET_H
#include <stddef.h>
void nsx_net_start(void);
int  nsx_net_wait(int ms);     /* 1 se ha rede */
int  nsx_net_state(void);      /* 0 a ligar, 1 ligado, -1 sem endereco */
void nsx_net_describe(char *buf, size_t n);
#endif
