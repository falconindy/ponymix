CFLAGS := -Wall -Wextra -pedantic -O2 -g -D_REENTRANT $(CFLAGS)
LDFLAGS := -lpulse -lm $(LDFLAGS)

pulsemix: pulsemix.o

install: pulsemix
	install -Dm755 pulsemix $(DESTDIR)/usr/bin/pulsemix
	install -Dm644 pulsemix.1 $(DESTDIR)/usr/share/man/man1/pulsemix.1

check: pulsemix
	./runtests ./pulsemix

clean:
	$(RM) pulsemix pulsemix.o
