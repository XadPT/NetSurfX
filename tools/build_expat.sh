#!/bin/sh
# Compila o expat (XML, usado pelo SVG) e a ligacao expat da libdom com nxdk.
# Requer: eval "$(nxdk/bin/activate -s)"; NSROOT = arvore netsurf (fork)
set -e
P=/home/claude/netsurfx; NSROOT=${NSROOT:-/home/claude/ns2}; PFX=${NSPFX:-/tmp/ns2prefix}
CF="-O2 -std=gnu99 -include $P/nsshim/xbox_compat.h -DXBOX -DXML_STATIC -Werror=implicit-function-declaration"
mkdir -p $P/third_party/expat/obj
for f in xmlparse xmlrole xmltok; do
  nxdk-cc $CF -U_WIN32 -I$P/nsshim -DHAVE_EXPAT_CONFIG_H -I$P/third_party/expat -c $P/third_party/expat/$f.c -o $P/third_party/expat/obj/$f.obj
done
nxdk-lib /out:$P/nslibs/lib/libexpat.lib $P/third_party/expat/obj/*.obj
nxdk-cc $CF -I$P/third_party/expat -I$NSROOT/libdom/include -I$NSROOT/libdom/src -I$NSROOT/libdom/bindings -I$PFX/include -I$P/nsshim -I/home/claude/nxdk/lib/net/lwip/src/include \
  -c $NSROOT/libdom/bindings/xml/expat_xmlparser.c -o $P/nslibs/obj/libdom/bindings_xml_expat_xmlparser.obj
nxdk-lib /out:$P/nslibs/lib/libdom.lib $P/nslibs/obj/libdom/*.obj
# libnsutils: unistd.c (nsu_pread/nsu_pwrite, usados pela cache em disco)
nxdk-cc $CF -I$P/nsshim -I$NSROOT/libnsutils/include -I$NSROOT/libnsutils/src -c $NSROOT/libnsutils/src/unistd.c -o $P/nslibs/obj/libnsutils/src_unistd.obj
nxdk-lib /out:$P/nslibs/lib/libnsutils.lib $P/nslibs/obj/libnsutils/*.obj
