CFLAGS = -Wall
LIBDIR = lib
OUTDIR = out
TESTDIR = test

.PHONY: all test clean
.SILENT:

all: txnlib.so

txnlib.so:
	gcc -shared -fPIC $(LIBDIR)/txnlib.h $(LIBDIR)/txnlib.c -o libtxn.so

test:
	rm -rf $(OUTDIR)/; mkdir $(OUTDIR);
	for i in `basename -a $(TESTDIR)/test*.c | grep -Eo "[0-9]{1,4}"`; do \
		(rm -rf logs/*; \
		gcc $(TESTDIR)/test$$i.c -I$(LIBDIR) -L. -ltxn -ldl -o $(OUTDIR)/test$$i && \
		LD_PRELOAD=$(shell pwd)/libtxn.so LD_LIBRARY_PATH=$(shell pwd) ./$(OUTDIR)/test$$i && \
		diff $(TESTDIR)/test$$i.ok $(OUTDIR)/test$$i.out > $(OUTDIR)/test$$i.diff && \
		echo test$$i pass) \
		|| \
		(echo test$$i fail; \
		echo "\n\n:::::::: expected ::::::::"; cat $(TESTDIR)/test$$i.ok; \
		echo "\n\n:::::::: found ::::::::"; cat $(OUTDIR)/test$$i.out) \
	done \

clean:
	rm -rf libtxn.so logs/* $(OUTDIR)/

lint:
	./checkpatch.pl -q --no-tree -f $(LIBDIR)/*
