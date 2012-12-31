CXX = g++ -std=c++11

base_CFLAGS = -Wall -Wextra -pedantic -O2 -g
base_LIBS = -lm

libpulse_CFLAGS = $(shell pkg-config --cflags libpulse)
libpulse_LIBS = $(shell pkg-config --libs libpulse)

CXXFLAGS := $(base_CFLAGS) $(libpulse_CFLAGS) $(CXXFLAGS)
LDLIBS := $(base_LIBS) $(libpulse_LIBS)

all: ponymix

ponymix: ponymix.cc pulse.o
pulse.o: pulse.cc pulse.h

install: ponymix
	install -Dm755 ponymix $(DESTDIR)/usr/bin/ponymix
	install -Dm644 ponymix.1 $(DESTDIR)/usr/share/man/man1/ponymix.1

clean:
	$(RM) ponymix pulse.o
