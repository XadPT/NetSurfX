#!/usr/bin/env python3
"""Compila o NetSurfX para Linux (banco de testes) com o mesmo codigo-fonte da Xbox."""
import subprocess, os, sys, glob, concurrent.futures as cf

NSA  = os.environ.get('NSROOT', '/home/claude/ns2')
NS   = NSA + '/netsurf'
PRJ  = '/home/claude/netsurfx'
HT   = '/home/claude/hosttest'
MB   = '/home/claude/deps/mbedtls-3.6.2'
PFX  = os.environ.get('NSPFX', '/tmp/ns2prefix')
OUT  = HT + '/obj2-tsan'
os.makedirs(OUT, exist_ok=True)

SKIP = {'src/xbox_libc.c', 'src/xbox_fastmem.c', 'src/xbox_usbhid.c', 'src/xbox_iconv_gz.c', 'src/xbox_posix.c', 'src/xbox_png.c', 'src/xbox_net.c', 'content/handlers/image/jpeg.c'}

common = ['-O1', '-g', '-fsanitize=thread', '-std=gnu99', '-w', '-Werror=implicit-function-declaration', '-Werror=int-conversion', '-Werror=incompatible-pointer-types', '-I' + HT + '/shim', '-D_GNU_SOURCE',
          '-DMBEDTLS_USER_CONFIG_FILE="ns_mbedtls_config.h"', '-I' + PRJ + '/mbedtls_cfg', '-I' + MB + '/include']

import glob as _g
HDR_MTIME = max([os.path.getmtime(h) for d in ('fe/framebuffer', 'fe/framebuffer/fbtk', 'nsshim', 'src', 'gen')
                 for h in _g.glob(os.path.join('/home/claude/netsurfx', d, '*.h'))] +
                [os.path.getmtime(h) for h in _g.glob('/home/claude/hosttest/shim/*.h')] + [0])

def run(cmd, cwd=None):
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)
    return r.returncode, r.stderr

def build_objs(items, flags, tag):
    def one(src):
        obj = OUT + '/' + tag + '_' + src.replace('/', '_').replace('..', '').rsplit('.', 1)[0] + '.o'
        if os.path.exists(obj) and os.path.getmtime(obj) > max(os.path.getmtime(src), HDR_MTIME) and '--force' not in sys.argv:
            return obj, None
        rc, err = run(['gcc'] + flags + ['-c', '-o', obj, src], cwd=NS)
        return obj, (err if rc else None)
    with cf.ThreadPoolExecutor(16) as ex:
        res = list(ex.map(one, items))
    fails = [(o, e) for o, e in res if e]
    for o, e in fails:
        print('FAIL', o)
        print('\n'.join([l for l in e.splitlines() if 'error' in l][:4]))
    return [o for o, e in res], len(fails)

total_fails = 0

# mbedTLS
mb_srcs = [s for s in glob.glob(MB + '/library/*.c') if os.path.basename(s) not in
           ('net_sockets.c', 'psa_its_file.c', 'timing.c')]
mb_objs, f = build_objs(mb_srcs, common + ['-I' + MB + '/library'], 'mb'); total_fails += f

# libnsfb (sem a superficie "ram", que se registaria antes da nossa)
nsfb_srcs = [NSA + '/libnsfb/src/' + s for s in ('libnsfb.c', 'dump.c', 'cursor.c', 'palette.c', 'plot/api.c',
             'plot/util.c', 'plot/generic.c', 'plot/32bpp-xrgb8888.c', 'plot/32bpp-xbgr8888.c', 'plot/16bpp.c',
             'plot/8bpp.c', 'surface/surface.c', 'surface/ram.c')]
nsfb_objs, f = build_objs(nsfb_srcs, common + ['-I' + NSA + '/libnsfb/include', '-I' + NSA + '/libnsfb/src'], 'nsfb')
total_fails += f

# NetSurf + frontend + codigo NetSurfX
srcs = []
for l in open(PRJ + '/tools/ns_sources.txt'):
    l = l.strip()
    if not l or l in SKIP or 'external/freetype' in l: continue
    p = PRJ + '/' + l if os.path.exists(PRJ + '/' + l) else NS + '/' + l
    srcs.append(p)
nsflags = common + ['-include', HT + '/shim/host_compat.h',
    '-I' + PRJ + '/fe', '-I' + PRJ + '/gen', '-I' + PRJ + '/src', '-I' + NS, '-I' + NS + '/include',
    '-I' + NS + '/frontends', '-I' + NS + '/content/handlers', '-I' + PFX + '/include',
    '-I' + NSA + '/libnsfb/src', '-I' + NSA + '/libnsfb/include',
    '-I/usr/include/freetype2', '-I/usr/include/libpng16',
    '-DWITH_UTF8PROC', '-DWITH_PNG', '-DWITH_BMP', '-DWITH_GIF', '-DWITH_XBOXFETCH', '-DWITH_NS_SVG', '-DWITH_WEBP', '-I/home/claude/netsurfx/third_party/libwebp/src', '-DFB_USE_FREETYPE',
    '-Dnsframebuffer', '-Dsmall', '-DSTMTEXPR=1',
    '-DNETSURF_UA_FORMAT_STRING="Mozilla/5.0 (%s) NetSurf/%d.%d"', '-DNETSURF_HOMEPAGE="about:welcome"',
    '-DNETSURF_LOG_LEVEL=INFO', '-DNETSURF_BUILTIN_LOG_FILTER="(level:WARNING)"',
    '-DNETSURF_BUILTIN_VERBOSE_FILTER="(level:VERBOSE)"',
    '-DNETSURF_FB_RESPATH="D:\\\\res\\\\"', '-DNETSURF_FB_FONTPATH="D:\\\\res\\\\fonts\\\\"',
    '-DNETSURF_FB_FONT_SANS_SERIF="DejaVuSans.ttf"', '-DNETSURF_FB_FONT_SANS_SERIF_BOLD="DejaVuSans-Bold.ttf"',
    '-DNETSURF_FB_FONT_SANS_SERIF_ITALIC="DejaVuSans-Oblique.ttf"',
    '-DNETSURF_FB_FONT_SANS_SERIF_ITALIC_BOLD="DejaVuSans-BoldOblique.ttf"',
    '-DNETSURF_FB_FONT_SERIF="DejaVuSerif.ttf"', '-DNETSURF_FB_FONT_SERIF_BOLD="DejaVuSerif-Bold.ttf"',
    '-DNETSURF_FB_FONT_MONOSPACE="DejaVuSansMono.ttf"', '-DNETSURF_FB_FONT_MONOSPACE_BOLD="DejaVuSansMono-Bold.ttf"',
    '-DNETSURF_FB_FONT_CURSIVE="DejaVuSerif-Italic.ttf"', '-DNETSURF_FB_FONT_FANTASY="DejaVuSans-Bold.ttf"',
    '-D_POSIX_C_SOURCE=200809L', '-D_XOPEN_SOURCE=700', '-D_DEFAULT_SOURCE']
ns_objs, f = build_objs(srcs, nsflags, 'ns'); total_fails += f

plat_objs, f = build_objs([HT + '/host_platform.c'], common + ['-I/usr/include/libpng16'], 'host'); total_fails += f

if total_fails:
    sys.exit('falhas: %d' % total_fails)

libs = [PFX + '/lib/' + l for l in ('libsvgtiny.a', 'libdom.a', 'libhubbub.a', 'libcss.a', 'libparserutils.a', 'libwapcaplet.a',
        'libnsbmp.a', 'libnsgif.a', 'libutf8proc.a', 'libnsutils.a')]
cmd = ['gcc', '-g', '-fsanitize=thread', '-o', HT + '/nsx-tsan'] + ns_objs + nsfb_objs + mb_objs + plat_objs + libs + \
      ['-Wl,--wrap=open', '-Wl,--wrap=realpath', '-lexpat', '/tmp/webpplain/libwebp_nsx.a', '-lfreetype', '-lpng16', '-lz', '-lpthread', '-lm']
rc, err = run(cmd)
if rc:
    print('\n'.join(sorted(set(l for l in err.splitlines() if 'undefined' in l)))[:6000])
    sys.exit('ligacao falhou')
print('OK ->', HT + '/nsx-host')
