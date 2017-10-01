CFLAGS = -Wall
LDIR = lib

.PHONY: all test clean

all: txnlib.so

txnlib.so:
	gcc -shared -fPIC $(LDIR)/txnlib.h $(LDIR)/txnlib.c -o libtxn.so

test:
	gcc test/simple.c -I$(LDIR) -L. -ltxn -ldl -o simple && LD_PRELOAD=$(shell pwd)/libtxn.so ./simple

clean:
	rm -rf libtxn.so simple logs

lint:
	./checkpatch.pl -q --no-tree -f $(LDIR)/*
