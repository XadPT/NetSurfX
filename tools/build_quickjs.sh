#!/bin/bash
# NetSurfX: build the QuickJS-ng engine (the version the netsurf-switch
# bindings were written for) as nslibs/lib/libquickjs.lib.
#
#   cd nxdk && eval "$(./bin/activate -s)"
#   bash tools/build_quickjs.sh [quickjs source dir]
#
# The source is cloned into deps/quickjs if no directory is given.
set -euo pipefail
PRJ=$(cd "$(dirname "$0")/.." && pwd)
QJS_TAG=v0.17.0
SRC=${1:-$PRJ/../deps/quickjs}
if [ ! -d "$SRC" ]; then
	git clone -q --depth 1 --branch $QJS_TAG https://github.com/quickjs-ng/quickjs.git "$SRC"
fi
OBJ=$PRJ/nslibs/obj/quickjs
mkdir -p "$OBJ" "$PRJ/nslibs/lib" "$PRJ/nslibs/include/quickjs"

# -include xbox_compat.h: strtod & co. and the libm functions that pdclib
# leaves as assert(0) stubs are redirected to src/xbox_libc.c
CFLAGS="-O2 -std=gnu11 -D_GNU_SOURCE -DCONFIG_VERSION=\"$QJS_TAG\" -DXBOX \
 -I$PRJ/qjsshim -I$PRJ/nsshim -include $PRJ/nsshim/xbox_compat.h \
 -include $PRJ/qjsshim/qjs_xbox.h -Wno-everything \
 -Werror=implicit-function-declaration -Werror=int-conversion"

# quickjs.c with the NetSurfX change (see tools/qjs_nsx_patch.py): the GC
# threshold never goes past the memory limit, so garbage is collected before
# a page runs out of JavaScript memory.
python3 "$PRJ/tools/qjs_nsx_patch.py" "$SRC/quickjs.c" "$OBJ/quickjs_nsx.c"

OBJS=""
for f in quickjs.c libregexp.c libunicode.c dtoa.c; do
	o="$OBJ/${f%.c}.obj"
	src="$SRC/$f"
	[ "$f" = quickjs.c ] && src="$OBJ/quickjs_nsx.c"
	eval nxdk-cc $CFLAGS -I"$SRC" -c "$src" -o "$o"
	OBJS="$OBJS $o"
done
rm -f "$PRJ/nslibs/lib/libquickjs.lib"
llvm-lib -out:"$PRJ/nslibs/lib/libquickjs.lib" $OBJS
cp "$SRC/quickjs.h" "$PRJ/nslibs/include/quickjs/"
echo "libquickjs.lib ($QJS_TAG) ok"
