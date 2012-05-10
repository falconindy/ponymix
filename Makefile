
CFLAGS := -Wall -Wextra -pedantic -O2 $(CFLAGS)
LDFLAGS := -lpulse -lm $(LDFLAGS)

pulsemix: pulsemix.o

clean:
	$(RM) pulsemix pulsemix.o
