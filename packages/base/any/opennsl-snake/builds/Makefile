ifndef TOOLCHAIN
TOOLCHAIN=.
endif

ifndef ARCH
ARCH=amd64
endif

ifdef ONL
include $(ONL)/make/any.mk
else
$(warn Need to define ONL to use this Makefile; trying to fake it)
endif


LIBONLP := $(shell $(ONLPM) --find-file onlp:$(ARCH) libonlp.so)
#LIBOPENNSL := $(shell $(ONLPM) --find-file opennsl:$(ARCH) libopennsl.so)

.PHONY: bdir

# FIXME
OPENNSL_DIR=../../../../opennsl-closed/
OPENNSL_SO=$(OPENNSL_DIR)/output/ONLSwitch/build/libopennsl.so.1
CFLAGS+=-I$(OPENNSL_DIR)/include -std=c99 -Wall
CFLAGS+=-g

GLOBAL_LINK_LIBS += -lpthread $(LIBONLP) $(OPENNSL_SO)
GLOBAL_LINK_LIBS += -Wl,--unresolved-symbols=ignore-in-shared-libs

$(TOOLCHAIN)/bin/opennsl-snake: bdir 
	$(CC) -o $@ $(CFLAGS) opennsl-snake.c $(LDFLAGS) $(GLOBAL_LINK_LIBS)


bdir:
	mkdir -p $(TOOLCHAIN)/bin

clean:
	rm -rf bin
