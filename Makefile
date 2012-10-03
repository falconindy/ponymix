CC = gcc -std=gnu99
CFLAGS := -Wall -Wextra -pedantic -O2 -g -D_REENTRANT $(CFLAGS)
LDLIBS := -lpulse -lm

all: ponymix doc

doc: ponymix.1

ponymix.1: README.pod
	pod2man --section=1 --center="Ponymix Manual" --name="PONYMIX" --release="ponymix" $< $@

ponymix: ponymix.o

install: ponymix
	install -Dm755 ponymix $(DESTDIR)/usr/bin/ponymix
	install -Dm644 ponymix.1 $(DESTDIR)/usr/share/man/man1/ponymix.1

uninstall:
	rm -f $(DESTDIR)/usr/bin/ponymix
	rm -f $(DESTDIR)/usr/share/man/man1/ponymix.1

check: ponymix
	./runtests ./ponymix

clean:
	$(RM) ponymix ponymix.o ponymix.1
