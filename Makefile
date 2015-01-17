
LIBS   := libusb-1.0

CFLAGS += -Wall -Wextra -pedantic -Wno-unused -std=c1x `pkg-config --cflags $(LIBS)` -Wno-parentheses
LDLIBS += `pkg-config --libs $(LIBS)`

ifeq ($(origin DEBUG), undefined)
	CFLAGS  += -O2
	LDFLAGS += -Wl,-O1
else
	CFLAGS  += -g
	LDFLAGS += -g
endif

.PHONY: all clean debug

all: fxprog test test2 ctl bulk

test2: fx2-jtag.o test2.o arm9-dbg-jtag.o

ctl: ctl.o usb.o
bulk: bulk.o usb.o

%.o: %.c $(wildcard *.h)
	$(COMPILE.c) $< $(OUTPUT_OPTION)

clean:
	$(RM) fxprog test test2 *.o
