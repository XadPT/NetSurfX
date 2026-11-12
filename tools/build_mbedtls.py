import subprocess, os, sys, glob
SRC = '/home/claude/deps/mbedtls-3.6.2'
OUT = '/home/claude/netsurfx/nslibs'
CFG = '/home/claude/netsurfx/mbedtls_cfg'
skip = {'net_sockets.c','threading.c','psa_its_file.c','aesni.c','padlock.c','timing.c','aesce.c','sha256_armv8.c' }
os.makedirs(OUT + '/obj/mbedtls', exist_ok=True); os.makedirs(OUT + '/lib', exist_ok=True)
flags = ['-O2', '-std=gnu99', f'-I{SRC}/include', f'-I{SRC}/library', f'-I{SRC}/tf-psa-crypto/include', f'-I{CFG}',
         '-DMBEDTLS_USER_CONFIG_FILE="ns_mbedtls_config.h"', '-I/home/claude/netsurfx/nsshim',
         '-include', '/home/claude/netsurfx/nsshim/strings.h', '-Wno-everything']
objs, fails = [], 0
for src in sorted(glob.glob(SRC + '/library/*.c')):
    n = os.path.basename(src)
    if n in skip: continue
    obj = f'{OUT}/obj/mbedtls/{n[:-2]}.obj'
    extra = ['-U_MSC_VER'] if n == 'x509_crt.c' else []
    p = subprocess.run(['nxdk-cc'] + flags + extra + ['-c', '-o', obj, src], capture_output=True, text=True)
    if p.returncode:
        fails += 1; print('FAIL', n); print('\n'.join([l for l in p.stderr.splitlines() if 'error' in l][:2]))
    else: objs.append(obj)
if objs:
    p = subprocess.run(['nxdk-lib', f'/out:{OUT}/lib/libmbedtls.lib'] + objs, capture_output=True, text=True)
    print(len(objs), 'objs ->', 'OK' if p.returncode == 0 else p.stderr[:300])
print('fails', fails)
