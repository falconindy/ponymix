CXX := $(CXX) -std=c++11

base_CXXFLAGS = -Wall -Wextra -pedantic -O2 -g
base_LIBS = -lm

libpulse_CXXFLAGS = $(shell pkg-config --cflags libpulse)
libpulse_LIBS = $(shell pkg-config --libs libpulse)

CXXFLAGS := \
	$(base_CXXFLAGS) \
	$(libpulse_CXXFLAGS) \
	$(CXXFLAGS)

LDLIBS := \
	$(base_LIBS) \
	$(libpulse_LIBS)

all: ponymix

ponymix: ponymix.cc pulse.o
pulse.o: pulse.cc pulse.h

install: ponymix
	install -Dm755 ponymix $(DESTDIR)/usr/bin/ponymix
	install -Dm644 ponymix.1 $(DESTDIR)/usr/share/man/man1/ponymix.1
	install -Dm644 bash-completion $(DESTDIR)/usr/share/bash-completion/completions/ponymix
	install -Dm644 zsh-completion $(DESTDIR)/usr/share/zsh/site-functions/_ponymix

clean:
	$(RM) ponymix pulse.o
