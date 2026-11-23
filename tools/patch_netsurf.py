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

# 10. llcache.c: se a cache em disco falhar (disco cheio, DVD, ficheiros
#     estragados) o browser continua sem ela em vez de nao arrancar
edit('netsurf/content/llcache.c',
     '\treturn guit->llcache->initialise(&prm->store);\n}',
     '\tif (guit->llcache->initialise(&prm->store) != NSERROR_OK) { /* NSX storeinit */\n\t\tNSLOG(llcache, ERROR, "Disk cache unavailable; continuing without it");\n\t}\n\treturn NSERROR_OK;\n}',
     'NSX storeinit')

# 11. llcache.c: an empty resource (e.g. an empty user.css) arrives as a NULL
#     buffer of length 0; memcpy(dst, NULL, 0) is undefined behaviour
edit('netsurf/content/llcache.c',
     '\t/* Append this data chunk to source buffer */\n\tmemcpy(object->source_data + object->source_len, data, len);\n',
     '\t/* Append this data chunk to source buffer */\n\tif (len > 0) /* NSX emptychunk */\n\t\tmemcpy(object->source_data + object->source_len, data, len);\n',
     'NSX emptychunk')

# 12. content.c: a content destroyed without being closed first (window
#     closed mid-search, exit) leaked its find-in-page context
edit('netsurf/content/content.c',
     '\tassert(c->locked == false);\n\n\tif (c->handler->destroy != NULL)\n\t\tc->handler->destroy(c);\n',
     '\tassert(c->locked == false);\n\n\tif (c->textsearch.context != NULL) { /* NSX searchleak */\n\t\tcontent_textsearch_destroy(c->textsearch.context);\n\t\tc->textsearch.context = NULL;\n\t}\n\tfree(c->textsearch.string);\n\tc->textsearch.string = NULL;\n\n\tif (c->handler->destroy != NULL)\n\t\tc->handler->destroy(c);\n',
     'NSX searchleak')

# 13. textsearch.c: the head of the match list was never freed (48 bytes
#     leaked by every find-in-page)
edit('netsurf/content/textsearch.c',
     '\tfree_matches(textsearch);\n\tfree(textsearch);\n',
     '\tfree_matches(textsearch);\n\tfree(textsearch->found); /* NSX searchhead */\n\tfree(textsearch);\n',
     'NSX searchhead')

# 15. layout_grid.c (netsurf-switch fork): the grid item sort compared item
#     ADDRESSES for document order and never returned 0, even for an item
#     against itself. That is not a valid ordering; pdclib's quicksort then
#     ran off the array and crashed (seen on html.duckduckgo.com, v0.8.1).
#     Keep the document index in the item and compare that.
edit('netsurf/content/handlers/html/layout_grid.c',
     '\tbool placed_r, placed_c;\n\tint order;\n};\n',
     '\tbool placed_r, placed_c;\n\tint order;\n\tint idx; /* NSX gridsort: document order */\n};\n',
     'NSX gridsort: document order')
edit('netsurf/content/handlers/html/layout_grid.c',
     '\tif (x->order != y->order)\n\t\treturn x->order - y->order;\n\t/* stable: keep document order */\n\treturn (x < y) ? -1 : 1;\n',
     '\tif (x->order != y->order) /* NSX gridsort cmp */\n\t\treturn x->order < y->order ? -1 : 1;\n\t/* stable: keep document order */\n\treturn (x->idx > y->idx) - (x->idx < y->idx);\n',
     'NSX gridsort cmp')
edit('netsurf/content/handlers/html/layout_grid.c',
     '\t\tit->box = b;\n\t\tif (b->style != NULL)\n\t\t\tcss_computed_order(b->style, &order);\n',
     '\t\tit->box = b;\n\t\tit->idx = i - 1; /* NSX gridsort idx */\n\t\tif (b->style != NULL)\n\t\t\tcss_computed_order(b->style, &order);\n',
     'NSX gridsort idx')

# 16. utils/log.c: when the -V log file could not be opened, logfile is
#     NULL and nslog_finalise() called fclose(NULL) (crash on exit to the
#     dashboard, v0.8.2). Also never write through a NULL stream.
edit('netsurf/utils/log.c',
     '\tverbose_log = true;\n\tif (logfile != stderr) {\n\t\tfclose(logfile);\n',
     '\tverbose_log = (logfile != NULL); /* NSX logfinal */\n\tif (logfile != NULL && logfile != stderr) {\n\t\tfclose(logfile);\n',
     'NSX logfinal')

# 17. quickjs.c (netsurf-switch JS glue): the Switch allows each JS runtime
#     64 MB; on a 64 MB Xbox the limit comes from the xbox_js_memory option
#     (MB, default 6). The JS stack is capped well inside the 1 MB main
#     thread stack, and the GC runs more often to keep the heap small.
edit('netsurf/content/handlers/javascript/quickjs/quickjs.c',
     '\tJS_SetMemoryLimit(heap->rt, QJS_MEMORY_LIMIT);\n',
     '#if defined(XBOX) || defined(WITH_XBOXFETCH) /* NSX jslimits */\n'
     '\t{\n'
     '\t\tint mb = nsoption_int(xbox_js_memory);\n'
     '\t\tif (mb < 2) mb = 2;\n'
     '\t\tif (mb > 24) mb = 24;\n'
     '\t\tJS_SetMemoryLimit(heap->rt, (size_t)mb * 1024 * 1024);\n'
     '\t\tJS_SetMaxStackSize(heap->rt, 384 * 1024);\n'
     '\t\tJS_SetGCThreshold(heap->rt, 512 * 1024);\n'
     '\t}\n'
     '#else\n'
     '\tJS_SetMemoryLimit(heap->rt, QJS_MEMORY_LIMIT);\n'
     '#endif\n',
     'NSX jslimits')

# 18. fs_backing_store.c write_blocks(): the success path leaked both path
#     strings (found by ASan on exit).
edit('netsurf/content/fs_backing_store.c',
     '\tif (rename(tname, fname) != 0) {\n\t\tunlink(tname);\n\t\tfree(tname);\n\t\tfree(fname);\n\t\treturn NSERROR_SAVE_FAILED;\n\t}\n\n\treturn NSERROR_OK;\n}\n',
     '\tif (rename(tname, fname) != 0) {\n\t\tunlink(tname);\n\t\tfree(tname);\n\t\tfree(fname);\n\t\treturn NSERROR_SAVE_FAILED;\n\t}\n\n\tfree(tname); /* NSX blockleak */\n\tfree(fname);\n\treturn NSERROR_OK;\n}\n',
     'NSX blockleak')

edit('netsurf/content/fs_backing_store.c',
     '\tNSLOG(netsurf, INFO, "Wrote out %"PRIsizet" entries", weistate.written);\n',
     '\tNSLOG(netsurf, INFO, "Wrote out %"PRIsizet" entries", weistate.written);\n\tfree(tname); /* NSX entryleak */\n\tfree(fname);\n',
     'NSX entryleak')

# 19. rebuild.c (netsurf-switch, box tree rebuilt after script DOM changes):
#     controls of form-less inputs/buttons belong only to their box; the old
#     tree was freed with talloc without them, leaking one control per such
#     element on every rebuild (found by ASan with JavaScript on). Controls
#     in a form are kept (the form owns them and the new tree reuses them).
edit('netsurf/content/handlers/html/rebuild.c',
     '\t\tif (box->list_marker != NULL)\n\t\t\thtml_rebuild_forget_boxes(box->list_marker);\n',
     '\t\tif (box->gadget != NULL && box->gadget->form == NULL &&\n'
     '\t\t    box->gadget->box == box) { /* NSX rebuildleak */\n'
     '\t\t\tform_free_control(box->gadget);\n'
     '\t\t\tbox->gadget = NULL;\n'
     '\t\t}\n'
     '\t\tif (box->list_marker != NULL)\n\t\t\thtml_rebuild_forget_boxes(box->list_marker);\n',
     'NSX rebuildleak')
edit('netsurf/content/handlers/html/rebuild.c',
     '#include "html/rebuild.h"\n',
     '#include "html/rebuild.h"\n#include "html/form_internal.h" /* NSX rebuildleak inc */\n',
     'NSX rebuildleak inc')

# 20. interaction.c: a <button> outside any form (normal on pages that use
#     it with JavaScript) showed "Warning: form can not be submitted"
#     whenever the pointer passed over it. Browsers show nothing; keep the
#     hand pointer instead.
edit('netsurf/content/handlers/html/interaction.c',
     '\t\t} else {\n\t\t\tmas->result.status = messages_get("FormBadSubmit");\n\t\t}\n',
     '\t\t} else { /* NSX formless */\n\t\t\tmas->result.pointer = get_pointer_shape(mas->gadget.box,\n\t\t\t\t\t\t\t\tfalse);\n\t\t}\n',
     'NSX formless')

# 21. quickjs.c: log what setting up a page's script context costs (the
#     netsurf-switch runtime.js shim is ~130 KB of JavaScript run per page)
edit('netsurf/content/handlers/javascript/quickjs/quickjs.c',
     '\tqjs_setup_console(thread->ctx);\n\tqjs_dom_setup(thread);\n',
     '\t{ /* NSX jscost */\n'
     '\t\tuint64_t t0 = 0, t1 = 0;\n'
     '\t\tJSMemoryUsage mu;\n'
     '\t\tnsu_getmonotonic_ms(&t0);\n'
     '\t\tqjs_setup_console(thread->ctx);\n'
     '\t\tqjs_dom_setup(thread);\n'
     '\t\tnsu_getmonotonic_ms(&t1);\n'
     '\t\tJS_ComputeMemoryUsage(heap->rt, &mu);\n'
     '\t\tNSLOG(netsurf, INFO, "[PERF] JS context ready: %u ms, runtime %u KB",\n'
     '\t\t      (unsigned)(t1 - t0), (unsigned)(mu.malloc_size / 1024));\n'
     '\t}\n',
     'NSX jscost')

# 14. Fixes from upstream NetSurf and its libraries (git.netsurf-browser.org,
#     mirrored on github.com/netsurf-browser) that are newer than the
#     netsurf-switch fork. Kept as patches in tools/upstream/<tree>/, applied
#     in file-name order; each is skipped when already present. Highlights:
#     libcss 113dc45 calc() division by zero (SIGFPE crash), 499f1c4/006ffc4
#     reads past the token vector, cbbeaaf vh/vw swapped, rounding fixes;
#     libnsgif 2255b2f LZW off-by-one, 69c1120 missing previous frame;
#     libnsbmp RLE padding and odd widths; libsvgtiny leaks and error paths;
#     netsurf 597329f https :443 default port, 703224c bounded realpath.
import subprocess, glob
UP = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'upstream')
def _order(pf):
    b = os.path.basename(pf)
    n = b.split('-', 1)[0]
    return (int(n) if n.isdigit() else 0, b)
for tree in sorted(os.listdir(UP)):
    tdir = os.path.join(ROOT, tree)
    stamp = os.path.join(tdir, '.nsx-upstream-applied')
    applied = set(open(stamp).read().split()) if os.path.exists(stamp) else set()
    for pf in sorted(glob.glob(os.path.join(UP, tree, '*.patch')), key=_order):
        name = os.path.basename(pf)
        if name in applied:
            continue
        r = subprocess.run(['git', 'apply', pf], cwd=tdir, capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit('upstream patch %s/%s failed: %s' % (tree, name, r.stderr))
        applied.add(name)
        with open(stamp, 'a') as fh:
            fh.write(name + '\n')
        done.append('upstream %s/%s' % (tree, name))

print('alterados:', ', '.join(sorted(set(done))) or 'nada (ja aplicado)')
