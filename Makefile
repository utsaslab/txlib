CFLAGS = -Wall
LDIR = lib

.PHONY: all test clean

all: txnlib.so

txnlib.so:
	gcc -shared -fPIC $(LDIR)/txnlib.c -o libtxn.so

test:
	gcc test/simple.c -L. -ltxn -ldl -o simple && LD_PRELOAD=$(shell pwd)/libtxn.so ./simple

clean:
	rm -f libtxn.so simple *.log

lint:
	./checkpatch.pl -q --no-tree -f lib/*
