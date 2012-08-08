CC=gcc
CFLAGS=-Wall -Wextra -pedantic -O2 -g -D_REENTRANT
 
LIBS= -lpulse -lm $(LDFLAGS)
HEADERS = pulsemix.h
EXT=.c
SRCS= main.c pulsemix.c
OBJECTS = ${SRCS:${EXT}=.o}

.PHONY: clean
 
%.o: %.c $(HEADERS)
	$(CC) -c -o $@ $< $(CFLAGS)

pulsemix: $(OBJECTS)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)
 
clean:
	rm -f *.o pulsemix

