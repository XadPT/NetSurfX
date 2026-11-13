#!/usr/bin/env python3
"""NetSurfX: ajustes ao nxdk (idempotente).

O lwIP do nxdk usa o valor por omissao MEMP_NUM_NETCONN = 4, o que limita
o programa inteiro a 4 sockets abertos ao mesmo tempo. Um browser precisa de
mais (varios pedidos em paralelo + ligacoes keep-alive). Os pools do lwIP
usam malloc (MEMP_MEM_MALLOC), por isso so a tabela de sockets cresce.
"""
import os, sys, glob
NX = sys.argv[1] if len(sys.argv) > 1 else '/home/claude/nxdk'
p = os.path.join(NX, 'lib/net/nforceif/include/lwipopts.h')
s = open(p).read()
tag = '/* NetSurfX: mais sockets */'
if tag not in s:
    s = s.replace('#define LWIP_SOCKET                     1',
                  '#define LWIP_SOCKET                     1\n\n' + tag +
                  '\n#define MEMP_NUM_NETCONN                16\n', 1)
    open(p, 'w').write(s)
    # obriga a recompilar o lwIP
    n = 0
    for o in glob.glob(os.path.join(NX, 'lib/net/**/*.obj'), recursive=True):
        os.remove(o); n += 1
    for o in glob.glob(os.path.join(NX, 'lib/libnxdk_net*.lib')) + glob.glob(os.path.join(NX, 'lib/liblwip*.lib')):
        os.remove(o)
    print('lwipopts alterado; objectos removidos:', n)
else:
    print('lwipopts ja alterado')
