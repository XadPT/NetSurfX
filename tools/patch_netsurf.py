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

# 6. gui_factory.c (fork): sockets por omissao so existem com curl; na Xbox
#    o nosso fetcher abre os seus proprios sockets lwIP
p6 = os.path.join(ROOT, 'netsurf/desktop/gui_factory.c')
if os.path.exists(p6) and 'gui_default_socket_open' in open(p6).read():
    edit('netsurf/desktop/gui_factory.c',
         'static int gui_default_socket_open(int domain, int type, int protocol)\n{\n',
         'static int gui_default_socket_open(int domain, int type, int protocol)\n{\n#ifdef XBOX /* NSX */\n\treturn -1;\n#endif\n',
         '#ifdef XBOX /* NSX */\n\treturn -1;\n#endif\n')
    s6 = open(p6).read()
    if 'NSX sockclose' not in s6:
        s6 = s6.replace('\treturn (int) socket(domain, type, protocol);', '#ifndef XBOX\n\treturn (int) socket(domain, type, protocol);\n#endif')
        s6 = s6.replace('static int gui_default_socket_close(int fd)\n{\n\treturn (int) ns_close_socket(fd);',
                        'static int gui_default_socket_close(int fd)\n{ /* NSX sockclose */\n#ifdef XBOX\n\treturn -1;\n#else\n\treturn (int) ns_close_socket(fd);\n#endif')
        open(p6, 'w').write(s6); done.append('netsurf/desktop/gui_factory.c')

# 7. box_construct.c: itens de flex/grid sao "blockificados" (CSS Display 3),
#    incluindo elementos inline como <span> e <a>
p7 = os.path.join(ROOT, 'netsurf/content/handlers/html/box_construct.c')
if os.path.exists(p7) and 'Blockification */' in open(p7).read():
    edit('netsurf/content/handlers/html/box_construct.c',
         '\t\t\tcase BOX_INLINE_BLOCK:\n\t\t\t\tbox->type = BOX_BLOCK;\n\t\t\t\tbreak;\n',
         '\t\t\tcase BOX_INLINE_BLOCK:\n\t\t\tcase BOX_INLINE: /* NSX blockify */\n\t\t\t\tbox->type = BOX_BLOCK;\n\t\t\t\tbreak;\n',
         'NSX blockify')

# 8. layout_flex.c: a "flex base size" nao e limitada ao min-content (o limite
#    minimo aplica-se depois, na resolucao) -> flex:1 da colunas iguais
p8 = os.path.join(ROOT, 'netsurf/content/handlers/html/layout_flex.c')
if os.path.exists(p8):
    edit('netsurf/content/handlers/html/layout_flex.c',
         '\t\titem->base_size = max(item->base_size, content_min_width);\n',
         '\t\titem->base_size = max(item->base_size, 0); /* NSX flexbase */\n',
         'NSX flexbase')

# 9. webfont.c: limites de memoria das fontes web na Xbox (64 MB)
p9 = os.path.join(ROOT, 'netsurf/content/handlers/html/webfont.c')
if os.path.exists(p9):
    edit('netsurf/content/handlers/html/webfont.c',
         '#define WEBFONT_MAX_SIZE (8 * 1024 * 1024)\n',
         '#ifdef XBOX /* NSX wflimits */\n#define WEBFONT_MAX_SIZE (1024 * 1024)\n#else\n#define WEBFONT_MAX_SIZE (8 * 1024 * 1024)\n#endif\n',
         'NSX wflimits')
    edit('netsurf/content/handlers/html/webfont.c',
         '#define WEBFONT_MAX_FETCHES 32\n',
         '#ifdef XBOX /* NSX wffetches */\n#define WEBFONT_MAX_FETCHES 48\n#else\n#define WEBFONT_MAX_FETCHES 32\n#endif\n',
         'NSX wffetches')

print('alterados:', ', '.join(sorted(set(done))) or 'nada (ja aplicado)')
