CC = gcc -std=gnu99
CFLAGS := -Wall -Wextra -pedantic -O2 -g -D_REENTRANT $(CFLAGS)
LDFLAGS := -lpulse -lm $(LDFLAGS)

ponymix: ponymix.o

install: ponymix
	install -Dm755 ponymix $(DESTDIR)/usr/bin/ponymix
	install -Dm644 ponymix.1 $(DESTDIR)/usr/share/man/man1/ponymix.1

check: ponymix
	./runtests ./ponymix

clean:
	$(RM) ponymix ponymix.o
