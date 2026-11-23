#!/usr/bin/env python3
"""Find calls from the linked program to nxdk functions that are stubs.

    python3 tools/audit_stubs.py <nxdk dir> <main.exe> <nsx.map>

nxdk leaves some libc/libm functions as `assert(0)` stubs, and nxdk's
assert halts the console (cli; hlt). This lists every such function from
the nxdk sources, finds the ones linked into the program (map file) and
reports any call or jump to them in the disassembly. Exit code 1 if any.
"""
import os, re, subprocess, sys

nx, exe, mapf = sys.argv[1:4]
STUB_DIRS = ['lib/pdclib', 'lib/xboxrt', 'lib/winapi']

func_re = re.compile(r'^[A-Za-z_][\w \*]*?\b(\w+)\s*\(([^;{]*)\)\s*\{', re.M)
stubs = set()
for d in STUB_DIRS:
    for root, _, files in os.walk(os.path.join(nx, d)):
        for f in files:
            if not f.endswith('.c'):
                continue
            src = open(os.path.join(root, f), errors='replace').read()
            if 'assert(0)' not in src and 'assert(false)' not in src:
                continue
            for m in func_re.finditer(src):
                name = m.group(1)
                body_start = m.end()
                depth, i = 1, body_start
                while i < len(src) and depth:
                    depth += {'{': 1, '}': -1}.get(src[i], 0)
                    i += 1
                body = src[body_start:i]
                code = re.sub(r'//.*|/\*.*?\*/', '', body, flags=re.S)
                stmts = [s.strip() for s in code.split(';') if s.strip()]
                # a stub: asserts false and does nothing else but return
                if any(s.startswith('assert(0)') or s.startswith('assert(false)') for s in stmts) and \
                   all(s.startswith('assert(') or s.startswith('return') or s.startswith('(void)')
                       or s.startswith('}') for s in stmts):
                    stubs.add(name)

addr = {}
for ln in open(mapf, errors='replace'):
    m = re.match(r'\s*[0-9a-f]{4}:[0-9a-f]{8}\s+_(\w+)\s+([0-9a-f]{8,16})\s', ln)
    if m and m.group(1) in stubs:
        addr[int(m.group(2), 16)] = m.group(1)

if not addr:
    print('no stub function is linked (%d stubs known)' % len(stubs))
    sys.exit(0)

dis = subprocess.run(['llvm-objdump', '-d', '--no-show-raw-insn', exe],
                     capture_output=True, text=True).stdout
hits = {}
for ln in dis.splitlines():
    m = re.search(r'\b(call|jmp)\w*\s+0x([0-9a-f]+)', ln)
    if m:
        a = int(m.group(2), 16)
        if a in addr:
            hits.setdefault(addr[a], []).append(ln.strip())

print('%d stubs known, %d linked: %s' % (len(stubs), len(addr), ', '.join(sorted(addr.values()))))
if hits:
    for name, lines in sorted(hits.items()):
        print('CALLED: %s (%d sites), e.g. %s' % (name, len(lines), lines[0]))
    sys.exit(1)
print('none of them is called')
