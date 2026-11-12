# NetSurfX - fase 3: HTTP/HTTPS (lwIP + mbedTLS) + libs do NetSurf
XBE_TITLE = NetSurfX
GEN_XISO = $(XBE_TITLE).iso
NXDK_DIR ?= $(CURDIR)/../nxdk
NXDK_NET = y
NXDK_SDL = y
SRCS = $(CURDIR)/src/main.c $(CURDIR)/src/shim.c $(CURDIR)/src/nstest.c \
       $(CURDIR)/src/nsplat.c $(CURDIR)/src/nethttp.c
BUILD_TIME := $(shell date +%s)
CFLAGS += -O2 -I$(CURDIR)/nsshim -I$(CURDIR)/nslibs/include -I$(CURDIR)/mbedtls_cfg \
          -DMBEDTLS_USER_CONFIG_FILE=\"ns_mbedtls_config.h\" -DNS_BUILD_TIME=$(BUILD_TIME)LL
NSLIBS = $(CURDIR)/nslibs/lib
include $(NXDK_DIR)/Makefile
LDFLAGS += $(NSLIBS)/libmbedtls.lib $(NSLIBS)/libdom.lib $(NSLIBS)/libhubbub.lib $(NSLIBS)/libcss.lib $(NSLIBS)/libparserutils.lib $(NSLIBS)/libwapcaplet.lib
