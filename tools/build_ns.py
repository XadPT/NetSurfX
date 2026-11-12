#!/usr/bin/env python3
# Cross-compila o nucleo do NetSurf + frontend Xbox com nxdk-cc.
import subprocess, os, sys, shlex, glob, concurrent.futures as cf
NS   = '/home/claude/ns-src/netsurf-all-3.11/netsurf'
PRJ  = '/home/claude/netsurfx'
NX   = '/home/claude/nxdk'
OUT  = PRJ + '/nsbuild'
os.makedirs(OUT + '/obj', exist_ok=True)

srcs = [l.strip() for l in open(PRJ + '/tools/ns_sources.txt') if l.strip() and not l.startswith('#')]

defs = ['-DWITH_UTF8PROC', '-DUTF8PROC_STATIC', '-DWITH_PNG', '-DWITH_BMP', '-DWITH_GIF', '-DWITH_JPEG',
        '-DWITH_XBOXFETCH', '-DFB_USE_FREETYPE', '-Dnsframebuffer', '-Dsmall', '-DSTMTEXPR=1',
        '-DNETSURF_UA_FORMAT_STRING="Mozilla/5.0 (%s) NetSurf/%d.%d"',
        '-DNETSURF_HOMEPAGE="about:welcome"', '-DNETSURF_LOG_LEVEL=INFO',
        '-DNETSURF_BUILTIN_LOG_FILTER="(level:WARNING)"', '-DNETSURF_BUILTIN_VERBOSE_FILTER="(level:VERBOSE)"',
        '-DNETSURF_FB_RESPATH="D:\\\\res\\\\"', '-DNETSURF_FB_FONTPATH="D:\\\\res\\\\fonts\\\\"',
        '-DNETSURF_FB_FONT_SANS_SERIF="DejaVuSans.ttf"', '-DNETSURF_FB_FONT_SANS_SERIF_BOLD="DejaVuSans-Bold.ttf"',
        '-DNETSURF_FB_FONT_SANS_SERIF_ITALIC="DejaVuSans-Oblique.ttf"',
        '-DNETSURF_FB_FONT_SANS_SERIF_ITALIC_BOLD="DejaVuSans-BoldOblique.ttf"',
        '-DNETSURF_FB_FONT_SERIF="DejaVuSerif.ttf"', '-DNETSURF_FB_FONT_SERIF_BOLD="DejaVuSerif-Bold.ttf"',
        '-DNETSURF_FB_FONT_MONOSPACE="DejaVuSansMono.ttf"', '-DNETSURF_FB_FONT_MONOSPACE_BOLD="DejaVuSansMono-Bold.ttf"',
        '-DNETSURF_FB_FONT_CURSIVE="DejaVuSerif-Italic.ttf"', '-DNETSURF_FB_FONT_FANTASY="DejaVuSans-Bold.ttf"']
incs = ['-I' + PRJ + '/nsshim', '-I' + PRJ + '/fe', '-I' + PRJ + '/gen', '-I' + PRJ + '/src',
        '-I' + NS, '-I' + NS + '/include', '-I' + NS + '/frontends', '-I' + NS + '/content/handlers',
        '-I' + PRJ + '/nslibs/include', '-I' + PRJ + '/mbedtls_cfg',
        '-I/home/claude/ns-src/netsurf-all-3.11/libnsfb/src', '-I/home/claude/ns-src/netsurf-all-3.11/libnsfb/include',
        '-I' + NX + '/lib/sdl/SDL_ttf/external/freetype-2.4.12/include', '-I' + NX + '/lib/sdl',
        '-I' + NX + '/lib/libpng/libpng', '-I' + NX + '/lib/libpng',
        '-isystem', NX + '/lib/libjpeg/libjpeg-turbo', '-I' + NX + '/lib/libjpeg',
        '-I' + NX + '/lib/zlib/zlib', '-DZ_SOLO',
        '-I' + NX + '/lib/net/lwip/src/include', '-I' + NX + '/lib/net/nforceif/include',
        '-I' + NX + '/lib/sdl/SDL2/include', '-DXBOX']
base = ['-O2', '-std=gnu99', '-include', PRJ + '/nsshim/xbox_compat.h',
        '-DMBEDTLS_USER_CONFIG_FILE="ns_mbedtls_config.h"',
        '-Wno-everything', '-Werror=implicit-function-declaration', '-Werror=int-conversion']

def resolve(s):
    if s.startswith('../nxdk/'):
        p = '/home/claude/nxdk/' + s[len('../nxdk/'):]
        return p if os.path.exists(p) else None
    for root in (PRJ, NS):
        p = os.path.join(root, s)
        if os.path.exists(p): return p
    return None

def compile_one(s):
    p = resolve(s)
    if not p: return (s, 'MISSING')
    obj = OUT + '/obj/' + s.replace('../', '').replace('/', '_').rsplit('.', 1)[0] + '.obj'
    if os.path.exists(obj) and os.path.getmtime(obj) > os.path.getmtime(p) and '--force' not in sys.argv:
        hdr_newer = False
        if not hdr_newer: return (s, None)
    extra = ['-DFT2_BUILD_LIBRARY'] if 'freetype' in s else []
    cmd = ['nxdk-cc'] + base + defs + incs + extra + ['-c', '-o', obj, p]
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=NS)
    if r.returncode:
        errs = [l for l in r.stderr.splitlines() if 'error' in l]
        return (s, '\n'.join(errs[:4]) or r.stderr[:400])
    return (s, None)

with cf.ThreadPoolExecutor(8) as ex:
    res = list(ex.map(compile_one, srcs))
fails = [(s, e) for s, e in res if e]
for s, e in fails:
    print('FAIL', s); print('   ', e.replace('\n', '\n    '))
print('compiled', len(srcs) - len(fails), 'of', len(srcs), 'fails', len(fails))
