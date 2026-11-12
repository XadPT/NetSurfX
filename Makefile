# NetSurfX - fase 1: esqueleto nxdk + SDL2 + rede
XBE_TITLE = NetSurfX
GEN_XISO = $(XBE_TITLE).iso
NXDK_DIR ?= $(CURDIR)/../nxdk
NXDK_NET = y
NXDK_SDL = y
SRCS = $(CURDIR)/src/main.c
CFLAGS += -O2
include $(NXDK_DIR)/Makefile
