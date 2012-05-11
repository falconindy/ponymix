CFLAGS := -Wall -Wextra -pedantic -O2 -g -D_REENTRANT $(CFLAGS)
LDFLAGS := -lpulse -lm $(LDFLAGS)

pulsemix: pulsemix.o

install: pulsemix
	mkdir -p $(DESTDIR)/usr/bin
	install -m755 pulsemix $(DESTDIR)/usr/bin/pulsemix

clean:
	$(RM) pulsemix pulsemix.o
