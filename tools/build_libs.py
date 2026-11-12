import subprocess, shlex, os, sys, glob
ROOT = '/home/claude/ns-src/netsurf-all-3.11'
OUT = '/home/claude/netsurfx/nslibs'
PFX = '/tmp/nsprefix'
libs = sys.argv[1:]
os.makedirs(OUT + '/lib', exist_ok=True)
env = dict(os.environ)
fails = 0
for lib in libs:
    d = f'{ROOT}/{lib}'
    os.makedirs(f'{OUT}/obj/{lib}', exist_ok=True)
    r = subprocess.run(['make', '-Bn', 'V=1', f'PREFIX={PFX}',
        f'NSSHARED={PFX}/share/netsurf-buildsystem', 'COMPONENT_TYPE=lib-static'],
        cwd=d, capture_output=True, text=True)
    objs = []
    for line in r.stdout.splitlines():
        if not line.startswith('cc ') or ' -c ' not in line: continue
        toks = shlex.split(line)
        src = toks[toks.index('-c') + 1]
        flags = []
        i = 1
        while i < len(toks):
            t = toks[i]
            if t in ('-o',): i += 2; continue
            if t == '-c': i += 2; continue
            if t.startswith(('-MMD','-MP','-W','-std','-Werror')) : i += 1; continue
            flags.append(t); i += 1
        if 'expat_xmlparser' in src: continue
        if lib == 'libnsfb' and src.startswith('src/surface/') and not (src.endswith('surface.c') or src.endswith('ram.c')): continue
        obj = f'{OUT}/obj/{lib}/' + src.replace('/', '_').replace('.c', '.obj')
        cmd = ['nxdk-cc'] + flags + ['-std=gnu99', '-include', '/home/claude/netsurfx/nsshim/xbox_compat.h', '-DXBOX', '-I/home/claude/nxdk/lib/net/lwip/src/include', '-I/home/claude/nxdk/lib/net/nforceif/include', f'-I{PFX}/include', '-I/home/claude/netsurfx/nsshim', '-include', '/home/claude/netsurfx/nsshim/strings.h', '-DWITHOUT_ICONV_FILTER', '-O2', '-Werror=implicit-function-declaration', '-Werror=int-conversion', '-c', '-o', obj, f'{d}/{src}']
        p = subprocess.run(cmd, cwd=d, capture_output=True, text=True)
        if p.returncode:
            fails += 1
            print('FAIL', lib, src); print('\n'.join([l for l in p.stderr.splitlines() if 'error' in l][:2]))
        else:
            objs.append(obj)
    if objs:
        p = subprocess.run(['nxdk-lib', f'/out:{OUT}/lib/{lib}.lib'] + objs, capture_output=True, text=True)
        print(lib, len(objs), 'objs ->', 'OK' if p.returncode == 0 else p.stderr[:300])
print('fails', fails)
