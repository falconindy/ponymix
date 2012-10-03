CC			= gcc -std=gnu99
CFLAGS		:= -Wall -Wextra -pedantic -O2 -g -D_REENTRANT $(CFLAGS)
LDLIBS		:= -lpulse -lm

PREFIX		?= /usr/local
MANPREFIX	?= $(PREFIX)/share/man

all: ponymix doc

doc: ponymix.1

ponymix.1: README.pod
	pod2man --section=1 --center="Ponymix Manual" --name="PONYMIX" --release="ponymix" $< $@

ponymix: ponymix.o

install: ponymix
	install -Dm755 ponymix $(DESTDIR)$(PREFIX)/bin/ponymix
	install -Dm644 ponymix.1 $(DESTDIR)$(MANPREFIX)/man1/ponymix.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/ponymix
	rm -f $(DESTDIR)$(MANPREFIX)/man1/ponymix.1

check: ponymix
	./runtests ./ponymix

clean:
	$(RM) ponymix ponymix.o ponymix.1
