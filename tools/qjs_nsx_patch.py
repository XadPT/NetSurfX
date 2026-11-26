#!/usr/bin/env python3
"""NetSurfX change to QuickJS-ng (v0.17.0) quickjs.c: copy IN to OUT with it.

After every automatic GC, QuickJS sets the next GC point to 1.5 times the
memory in use. With a memory limit (NetSurfX: 6 MB per window) that point
soon lies beyond the limit: at 4.5 MB in use the next GC would come at
6.75 MB, so the page fails with "out of memory" while megabytes of garbage
(from pages already left) wait for a GC that never comes. The next GC point
is now kept halfway between the memory in use and the limit.
"""
import sys

src, dst = sys.argv[1], sys.argv[2]
s = open(src, encoding='utf-8').read()
old = '''        JS_RunGC(rt);
        rt->malloc_gc_threshold = rt->malloc_state.malloc_size +
            (rt->malloc_state.malloc_size >> 1);
    }'''
new = '''        JS_RunGC(rt);
        rt->malloc_gc_threshold = rt->malloc_state.malloc_size +
            (rt->malloc_state.malloc_size >> 1);
        /* NSX gccap: never past the memory limit */
        if (rt->malloc_state.malloc_limit != 0 &&
            rt->malloc_gc_threshold > rt->malloc_state.malloc_limit) {
            size_t used = rt->malloc_state.malloc_size;
            size_t lim = rt->malloc_state.malloc_limit;
            rt->malloc_gc_threshold = used < lim ? used + (lim - used) / 2 : lim;
        }
    }'''
if s.count(old) != 1:
    sys.exit('qjs_nsx_patch: js_trigger_gc not found (QuickJS version changed?)')
open(dst, 'w', encoding='utf-8').write(s.replace(old, new))
