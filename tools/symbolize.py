#!/usr/bin/env python3
"""Turn the addresses in NSX-CRASH.LOG / NSX-HANG.LOG into function names.

    python3 tools/symbolize.py nsx.map NSX-CRASH.LOG
    python3 tools/symbolize.py nsx.map 0005a1c3 00061f20

Uses the lld-link map of the same build (public and static symbols).
"""
import bisect, re, sys


def load_map(path):
    syms = []
    line_re = re.compile(r'^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8,16})\s+(?:f\s+)?(?:i\s+)?(\S+)?')
    with open(path, errors='replace') as f:
        for ln in f:
            m = line_re.match(ln)
            if not m:
                continue
            addr = int(m.group(2), 16)
            if addr == 0:
                continue
            syms.append((addr, m.group(1), m.group(3) or ''))
    syms.sort()
    return syms


def lookup(syms, keys, a):
    i = bisect.bisect_right(keys, a) - 1
    if i < 0:
        return '?'
    addr, name, obj = syms[i]
    if a - addr > 0x10000:
        return '? (no nearby symbol)'
    return '%s+0x%x  [%s]' % (name.lstrip('_'), a - addr, obj)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    syms = load_map(sys.argv[1])
    keys = [s[0] for s in syms]
    args = sys.argv[2:]
    if len(args) == 1 and not re.fullmatch(r'[0-9a-fA-F]{8}', args[0]):
        text = open(args[0], errors='replace').read()
        for ln in text.splitlines():
            print(ln)
            for h in re.findall(r'\b([0-9a-fA-F]{8})\b', ln):
                a = int(h, 16)
                if 0x11000 <= a < 0x4000000:
                    print('        %s  %s' % (h, lookup(syms, keys, a)))
    else:
        for h in args:
            print('%s  %s' % (h, lookup(syms, keys, int(h, 16))))


if __name__ == '__main__':
    main()
