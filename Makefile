CC=gcc
CFLAGS=-Wall -g -o
OBJ=cedit

all: ${OBJ}

${OBJ}:
	${CC} ${CFLAGS} ${OBJ}.o ${OBJ}.c

clean:
	rm -f ${OBJ}.o