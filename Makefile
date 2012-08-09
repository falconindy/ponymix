OUT=pulsemix
SRC=${wildcard *.c}
OBJ=${SRC:.c=.o}

PREFIX  ?= /usr/local
CFLAGS  := -std=gnu99 -Wall -Wextra -pedantic -O2 -D_REENTRANT ${CFLAGS}
LDFLAGS := -lpulse -lm ${LDFLAGS}

${OUT}: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

install: ${OUT}
	install -D -m755 ${OUT} ${DESTDIR}${PREFIX}/bin/${OUT}

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/${OUT}

clean:
	${RM} ${OUT} ${OBJ}

.PHONY: clean install uninstall
