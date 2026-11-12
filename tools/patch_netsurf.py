#!/usr/bin/env python3
"""Aplica (de forma idempotente) as alteracoes do NetSurfX a arvore netsurf-all-3.11."""
import sys, os
ROOT = sys.argv[1] if len(sys.argv) > 1 else '/home/claude/ns-src/netsurf-all-3.11'
NS = ROOT + '/netsurf'
done = []

def edit(path, old, new, tag):
    p = os.path.join(ROOT, path)
    s = open(p).read()
    if tag in s:
        return
    if old not in s:
        sys.exit('PATCH FALHOU: %s (%s)' % (path, tag))
    s = s.replace(old, new, 1)
    open(p, 'w').write(s)
    done.append(path)

# 1. fetch.c: curl opcional + fetcher Xbox
edit('netsurf/content/fetch.c', '#include "content/fetchers/curl.h"',
     '#ifdef WITH_CURL\n#include "content/fetchers/curl.h"\n#endif\n'
     '#ifdef WITH_XBOXFETCH /* NSX */\nnserror fetch_xbox_register(void);\n#endif', 'WITH_XBOXFETCH /* NSX */')
edit('netsurf/content/fetch.c', '''	ret = fetch_data_register();''',
     '''#ifdef WITH_XBOXFETCH
	ret = fetch_xbox_register(); /* NSX-REG */
	if (ret != NSERROR_OK) {
		return ret;
	}
#endif
	ret = fetch_data_register();''', 'NSX-REG')

# 2. config.h: a Xbox nao tem regex, mmap, etc. (a maior parte ja vem do ramo _WIN32)
edit('netsurf/utils/config.h', '#define HAVE_REGEX\n',
     '#define HAVE_REGEX\n#if defined(XBOX) /* NSX */\n#undef HAVE_REGEX\n#endif\n', 'defined(XBOX) /* NSX */')

# 3. inttypes.h: o pdclib usa %zu, nao o %Iu do MSVCRT
edit('netsurf/include/netsurf/inttypes.h', '#if defined(_WIN32)\n',
     '#if defined(_WIN32) && !defined(XBOX) /* NSX */\n', '!defined(XBOX) /* NSX */')

# 4. utils.h: mkdir com 2 argumentos (o nosso shim e POSIX)
edit('netsurf/utils/utils.h', '/* Windows does not have POSIX mkdir so work around that */\n#if defined(_WIN32)',
     '/* Windows does not have POSIX mkdir so work around that */\n#if defined(_WIN32) && !defined(XBOX) /* NSX */',
     '!defined(XBOX) /* NSX */')

# 5. libnsfb: superficie "ram" (usada para as imagens) com registo explicito,
#    sem depender de construtores globais
p5 = os.path.join(ROOT, 'libnsfb/src/surface/ram.c')
s5 = open(p5).read()
if 'nsfb_ram_register' not in s5:
    s5 = s5.rstrip() + '\n\n/* NetSurfX: registo explicito */\nvoid nsfb_ram_register(void);\nvoid nsfb_ram_register(void)\n{\n    ram_register_surface();\n}\n'
    open(p5, 'w').write(s5)
    done.append('libnsfb/src/surface/ram.c')

print('alterados:', ', '.join(sorted(set(done))) or 'nada (ja aplicado)')
