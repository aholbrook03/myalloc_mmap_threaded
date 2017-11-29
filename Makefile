CC = gcc
CFLAGS = -c -Wall -O2
PROGRAM = bench
OBJECTS = myalloc.o bench.o

%.o: %.c %.h
	${CC} ${CFLAGS} -o $@ $<

bench.o: myalloc.o
	${CC} -Wall -O2 -lpthread -o ${PROGRAM} bench.c myalloc.o
myalloc.o: myalloc.h

.PHONY: clean
clean:
	rm -f ${PROGRAM}
	rm -f *.o

