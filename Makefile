CC=gcc
BACKEND_VMM=xen
CFLAGS=-Wall -Wextra -Werror -O2 -Wno-maybe-uninitialized -fPIC -g
VCHANCFLAGS=$(shell pkg-config --cflags vchan-$(BACKEND_VMM))
VCHANLIBS=$(shell pkg-config --libs vchan-$(BACKEND_VMM))
JACKLIBS=$(shell pkg-config --libs jack)
JACKCFLAGS=$(shell pkg-config --cflags jack)
LIBS=$(JACKLIBS) $(VCHANLIBS) -lm
CFLAGS+=$(VCHANCFLAGS) $(JACKCFLAGS)

all: qubes-vchan-jack-passthru
qubes-vchan-jack-passthru:
	$(CC) $(CFLAGS) qubes-vchan-jack-passthru.c $(LIBS) -o qubes-vchan-jack-passthru
clean:
	rm -f qubes-vchan-jack-passthru *.o *~
