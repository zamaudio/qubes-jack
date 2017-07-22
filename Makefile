CC=gcc
BACKEND_VMM=xen
CFLAGS=-Wall -Wextra -Werror -O2 -Wno-maybe-uninitialized -fPIC -g
VCHANCFLAGS=$(shell pkg-config --cflags vchan-$(BACKEND_VMM))
VCHANLIBS=$(shell pkg-config --libs vchan-$(BACKEND_VMM))
JACKLIBS=$(shell pkg-config --libs jack)
JACKCFLAGS=$(shell pkg-config --cflags jack)
LIBS=$(JACKLIBS) $(VCHANLIBS) -lm
CFLAGS+=$(VCHANCFLAGS) $(JACKCFLAGS)

all: qubes-vchan-jack-server qubes-vchan-jack-client
qubes-vchan-jack-server:
	$(CC) $(CFLAGS) qubes-vchan-jack-server.c $(LIBS) -o qubes-vchan-jack-server
qubes-vchan-jack-client:
	$(CC) $(CFLAGS) qubes-vchan-jack-client.c $(LIBS) -o qubes-vchan-jack-client
clean:
	rm -f qubes-vchan-jack-server qubes-vchan-jack-client *.o *~
