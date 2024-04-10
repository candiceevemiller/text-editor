CC=gcc
CFLAGS=-Wall -Wextra -pedantic -g -o
OBJ=cedit

all: ${OBJ}

${OBJ}:
	${CC} ${CFLAGS} ${OBJ} ${OBJ}.c

clean:
	rm -f ${OBJ}