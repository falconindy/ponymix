V=1
VDEVEL=$(shell test -d .git && git describe 2>/dev/null)

ifneq "$(VDEVEL)" ""
V=$(VDEVEL)
endif

CXX := $(CXX) -std=c++11

base_CXXFLAGS = -Wall -Wextra -pedantic -O2 -g -DPONYMIX_VERSION=\"$(V)\"
base_LIBS = -lm

libpulse_CXXFLAGS = $(shell pkg-config --cflags libpulse)
libpulse_LIBS = $(shell pkg-config --libs libpulse)

libnotify_CXXFLAGS = $(shell pkg-config --cflags libnotify 2>/dev/null && echo "-DHAVE_NOTIFY")
libnotify_LIBS = $(shell pkg-config --libs libnotify 2>/dev/null)


CXXFLAGS := \
	$(base_CXXFLAGS) \
	$(libnotify_CXXFLAGS) \
	$(libpulse_CXXFLAGS) \
	$(CXXFLAGS)

LDLIBS := \
	$(base_LIBS) \
	$(libnotify_LIBS) \
	$(libpulse_LIBS)

all: ponymix

ponymix: ponymix.cc pulse.o
pulse.o: pulse.cc pulse.h notify.h

install: ponymix
	install -Dm755 ponymix $(DESTDIR)/usr/bin/ponymix
	install -Dm644 ponymix.1 $(DESTDIR)/usr/share/man/man1/ponymix.1
	install -Dm644 bash-completion $(DESTDIR)/usr/share/bash-completion/completions/ponymix
	install -Dm644 zsh-completion $(DESTDIR)/usr/share/zsh/site-functions/_ponymix

clean:
	$(RM) ponymix pulse.o

dist:
	git archive --format=tar --prefix=ponymix-$(V)/ HEAD | gzip -9 > ponymix-$(V).tar.gz
