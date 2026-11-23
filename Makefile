# NetSurfX - NetSurf para a Xbox classica (nxdk)
#
# 1) python3 tools/build_libs.py ...   (bibliotecas do NetSurf -> nslibs/lib)
# 2) python3 tools/build_mbedtls.py    (mbedTLS -> nslibs/lib)
# 3) python3 tools/build_ns.py         (nucleo + frontend -> nsbuild/obj)
# 4) make                              (liga tudo e gera default.xbe / NetSurfX.iso)

XBE_TITLE = NetSurfX
GEN_XISO = $(XBE_TITLE).iso
NXDK_DIR ?= $(CURDIR)/../nxdk
NXDK_NET = y
NXDK_SDL = y
# teclado e rato USB (controlador HID do nxdk)
NXDK_USB_ENABLE_HID = y
SRCS = $(CURDIR)/src/xbox_stub.c
include $(NXDK_DIR)/Makefile

NSLIBS = $(CURDIR)/nslibs/lib
NSOBJS := $(sort $(wildcard $(CURDIR)/nsbuild/obj/*.obj))
LDFLAGS += -map:nsx.map -stack:1048576 $(NSOBJS) \
	$(NSLIBS)/libnsfb.lib $(NSLIBS)/libsvgtiny.lib $(NSLIBS)/libexpat.lib $(NSLIBS)/libwebp.lib $(NSLIBS)/libnsbmp.lib $(NSLIBS)/libnsgif.lib \
	$(NSLIBS)/libutf8proc.lib $(NSLIBS)/libnsutils.lib \
	$(NSLIBS)/libdom.lib $(NSLIBS)/libhubbub.lib $(NSLIBS)/libcss.lib \
	$(NSLIBS)/libparserutils.lib $(NSLIBS)/libwapcaplet.lib \
	$(NSLIBS)/libmbedtls.lib $(NSLIBS)/libquickjs.lib \
	$(NXDK_DIR)/lib/libpng.lib $(NXDK_DIR)/lib/libjpeg.lib $(NXDK_DIR)/lib/libzlib.lib \
	$(NXDK_DIR)/lib/libfreetype.lib
