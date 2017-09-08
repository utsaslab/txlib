CFLAGS = -Wall
LDIR = lib

.PHONY: all test clean

all: txlib.so

txlib.so: txlib.o
	gcc -shared -o libtx.so txlib.o

txlib.o:
	gcc -fPIC -c lib/txlib.c

test:
	gcc test/simple.c -L. -ltx && ./a.out

clean:
	rm -f libtx.so txlib.o a.out *.log

lint:
	./checkpatch.pl -q --no-tree -f lib/*
