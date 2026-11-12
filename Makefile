# NetSurfX - fase 2: libs do NetSurf (libcss, libdom, libhubbub, ...) ligadas
XBE_TITLE = NetSurfX
GEN_XISO = $(XBE_TITLE).iso
NXDK_DIR ?= $(CURDIR)/../nxdk
NXDK_NET = y
NXDK_SDL = y
SRCS = $(CURDIR)/src/main.c $(CURDIR)/src/shim.c $(CURDIR)/src/nstest.c
CFLAGS += -O2 -I$(CURDIR)/nsshim -I$(CURDIR)/nslibs/include
NSLIBS = $(CURDIR)/nslibs/lib
include $(NXDK_DIR)/Makefile
LDFLAGS += $(NSLIBS)/libdom.lib $(NSLIBS)/libhubbub.lib $(NSLIBS)/libcss.lib $(NSLIBS)/libparserutils.lib $(NSLIBS)/libwapcaplet.lib
