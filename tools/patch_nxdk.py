#!/usr/bin/env python3
"""NetSurfX: patches applied to the nxdk tree (idempotent).

1. MEMP_NUM_NETCONN 16: nxdk's lwIP defaults to 4 netconns, which caps the
   whole program at 4 open sockets. A browser needs more (parallel requests
   plus keep-alive connections). lwIP pools use malloc (MEMP_MEM_MALLOC), so
   only the socket table grows.
2. LWIP_DEBUG removed: every LWIPDEBUGF site is compiled in otherwise, and
   the platform diag hook draws on the screen with debugPrint().
3. Receive path sized for a browser: 128 NIC receive buffers (2 KB each,
   256 KB contiguous) and a 24-segment TCP window (34 KB). nxdk's default is
   64 buffers shared by every socket with a 64 KB window per socket, so two
   busy connections could hold every buffer and stall all the others.
4. rx_callback drops the frame when the pbuf pool allocation fails, instead
   of asserting at DISPATCH_LEVEL and then dereferencing NULL. TCP simply
   retransmits a dropped frame.

Any change deletes the network objects so the next make rebuilds them.
"""
import os, sys, glob

NX = sys.argv[1] if len(sys.argv) > 1 else '/home/claude/nxdk'
changed = False


def patch(rel, tag, old, new):
    global changed
    p = os.path.join(NX, rel)
    s = open(p).read()
    if tag in s:
        return
    if old not in s:
        sys.exit('patch_nxdk: pattern not found in %s for %s' % (rel, tag))
    s = s.replace(old, new, 1)
    open(p, 'w').write(s)
    changed = True
    print('patched', rel, '-', tag)


LWIPOPTS = 'lib/net/nforceif/include/lwipopts.h'

# Older NetSurfX builds used a Portuguese tag for patch 1: keep it valid.
if '/* NetSurfX: mais sockets */' not in open(os.path.join(NX, LWIPOPTS)).read():
    patch(LWIPOPTS, '/* NetSurfX: more sockets */',
          '#define LWIP_SOCKET                     1',
          '#define LWIP_SOCKET                     1\n\n/* NetSurfX: more sockets */\n'
          '#define MEMP_NUM_NETCONN                16\n')

patch(LWIPOPTS, '/* NetSurfX: LWIP_DEBUG off */',
      '#define LWIP_DEBUG 1\n',
      '/* NetSurfX: LWIP_DEBUG off */\n')

patch(LWIPOPTS, '/* NetSurfX: receive window */',
      '#define TCP_WND                         (45 * TCP_MSS)',
      '/* NetSurfX: receive window */\n'
      '#define TCP_WND                         (24 * TCP_MSS)\n'
      '#define RX_BUFF_CNT                     128')

patch('lib/net/nvnetdrv/nvnetdrv_lwip.c', '/* NetSurfX: drop on OOM */',
      '    LWIP_ASSERT("RX_POOL full\\n", rx_pbuf != NULL);\n',
      '    /* NetSurfX: drop on OOM */\n'
      '    if (rx_pbuf == NULL) {\n'
      '        nvnetdrv_rx_release(buffer);\n'
      '        LINK_STATS_INC(link.memerr);\n'
      '        LINK_STATS_INC(link.drop);\n'
      '        return;\n'
      '    }\n')

# 5. Unhandled exceptions: every thread created through CreateThread (main()
#    included, via thrd_create) runs inside an SEH frame that calls the filter
#    set with SetUnhandledExceptionFilter. Without one, behaviour is unchanged.
patch('lib/winapi/errhandlingapi.h', '/* NetSurfX: unhandled filter */',
      'VOID RaiseException (DWORD dwExceptionCode, DWORD dwExceptionFlags, DWORD nNumberOfArguments, const ULONG_PTR *lpArguments);\n',
      'VOID RaiseException (DWORD dwExceptionCode, DWORD dwExceptionFlags, DWORD nNumberOfArguments, const ULONG_PTR *lpArguments);\n\n'
      '/* NetSurfX: unhandled filter */\n'
      'typedef LONG (__stdcall *LPTOP_LEVEL_EXCEPTION_FILTER)(struct _EXCEPTION_POINTERS *ExceptionInfo);\n'
      'LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter (LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter);\n'
      'LONG UnhandledExceptionFilter (struct _EXCEPTION_POINTERS *ExceptionInfo);\n')
patch('lib/winapi/thread.c', '/* NetSurfX: SEH thread frame */',
      'extern const IMAGE_TLS_DIRECTORY_32 _tls_used;\n',
      'extern const IMAGE_TLS_DIRECTORY_32 _tls_used;\n\n'
      '/* NetSurfX: SEH thread frame */\n'
      '#include <excpt.h>\n'
      '#include <errhandlingapi.h>\n'
      'static LPTOP_LEVEL_EXCEPTION_FILTER volatile top_filter;\n\n'
      'LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter (LPTOP_LEVEL_EXCEPTION_FILTER f)\n'
      '{\n'
      '    return (LPTOP_LEVEL_EXCEPTION_FILTER)InterlockedExchangePointer((PVOID volatile *)&top_filter, (PVOID)f);\n'
      '}\n\n'
      'LONG UnhandledExceptionFilter (struct _EXCEPTION_POINTERS *e)\n'
      '{\n'
      '    LPTOP_LEVEL_EXCEPTION_FILTER f = top_filter;\n'
      '    return f ? f(e) : EXCEPTION_CONTINUE_SEARCH;\n'
      '}\n')
patch('lib/winapi/thread.c', '/* NetSurfX: run under SEH */',
      '    int res;\n    res = (*(LPTHREAD_START_ROUTINE)StartRoutine)(StartContext);\n',
      '    int res;\n    /* NetSurfX: run under SEH */\n'
      '    __try {\n'
      '        res = (*(LPTHREAD_START_ROUTINE)StartRoutine)(StartContext);\n'
      '    } __except (UnhandledExceptionFilter(GetExceptionInformation())) {\n'
      '        res = -1;\n'
      '    }\n')

if changed:
    for o in ('lib/winapi/thread.obj',):
        q = os.path.join(NX, o)
        if os.path.exists(q): os.remove(q)
    n = 0
    for o in glob.glob(os.path.join(NX, 'lib/net/**/*.obj'), recursive=True):
        os.remove(o)
        n += 1
    for o in glob.glob(os.path.join(NX, 'lib/libnxdk_net*.lib')) + glob.glob(os.path.join(NX, 'lib/liblwip*.lib')):
        os.remove(o)
    print('network objects removed:', n)
else:
    print('nxdk already patched')
