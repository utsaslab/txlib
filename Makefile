CFLAGS = -Wall
LIBDIR = lib
OUTDIR = out
TESTDIR = test

.PHONY: all test clean

all: txnlib.so

txnlib.so:
	gcc -shared -fPIC $(LIBDIR)/txnlib.h $(LIBDIR)/txnlib.c -o libtxn.so

test:
	rm -rf $(OUTDIR)/; mkdir $(OUTDIR);
	for i in `basename -a $(TESTDIR)/test*.c | grep -Eo "[0-9]{1,4}"`; do \
		rm -rf logs/*; \
		gcc $(TESTDIR)/test$$i.c -I$(LIBDIR) -L. -ltxn -ldl -o $(OUTDIR)/test$$i && \
		LD_PRELOAD=$(shell pwd)/libtxn.so ./$(OUTDIR)/test$$i && \
		diff $(TESTDIR)/test$$i.ok $(OUTDIR)/test$$i.out > $(OUTDIR)/test$$i.diff; \
	done \
	# (gcc test/test0.c -I$(LDIR) -L. -ltxn -ldl -o $(OUTDIR)/test0 && LD_PRELOAD=$(shell pwd)/libtxn.so ./$(OUTDIR)/test0 && diff $(TESTDIR)/test0.ok $(OUTDIR)/test0.out > $(OUTDIR)/test0.diff 2>&1 && echo test0 pass) || (echo "\n\n:::::::: expected ::::::::"; cat $(TESTDIR)/test0.ok ; echo "\n\n:::::::: found ::::::::" ; cat $(OUTDIR)/test0.out)

clean:
	rm -rf libtxn.so logs/* $(OUTDIR)/

lint:
	./checkpatch.pl -q --no-tree -f $(LIBDIR)/*
