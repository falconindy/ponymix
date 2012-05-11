CFLAGS := -Wall -Wextra -pedantic -O2 $(CFLAGS)
LDFLAGS := -lpulse -lm $(LDFLAGS)

pulsemix: pulsemix.o

install: pulsemix
	mkdir $(DESTDIR)/usr/bin
	install -m755 pulsemix $(DESTDIR)/usr/bin/pulsemix

clean:
	$(RM) pulsemix pulsemix.o
