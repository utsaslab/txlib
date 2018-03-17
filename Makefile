CFLAGS = -Wall
LIBDIR = lib
OUTDIR = out
TESTDIR = tests
FSXFLAGS = -N 100000

.PHONY: all test clean
.SILENT:

all: txnlib.so

txnlib.so:
	gcc -shared -fPIC $(LIBDIR)/txnlib.h $(LIBDIR)/txnlib.c -o libtxn.so -ldl

test:
	rm -rf $(OUTDIR)/; mkdir $(OUTDIR);
	for i in `basename -a $(TESTDIR)/test*.c | grep -Eo "[0-9]{1,4}"`; do \
		(rm -rf logs/*; \
		gcc $(TESTDIR)/test$$i.c -I$(LIBDIR) -L. -ltxn -o $(OUTDIR)/test$$i -ldl && \
		LD_PRELOAD=$(shell pwd)/libtxn.so LD_LIBRARY_PATH=$(shell pwd) ./$(OUTDIR)/test$$i && \
		diff $(TESTDIR)/test$$i.ok $(OUTDIR)/test$$i.out > $(OUTDIR)/test$$i.diff && \
		echo test$$i pass) \
		|| \
		(echo =========================================; \
		echo test$$i fail; \
		echo "\n-------- expected --------"; cat $(TESTDIR)/test$$i.ok; \
		echo "\n\n--------- found ---------"; cat $(OUTDIR)/test$$i.out) \
	done \

fsx:
	rm -rf $(OUTDIR); mkdir $(OUTDIR) && \
	gcc ports/fsx-linux.c -I$(LIBDIR) -L. -ltxn -o ports/fsx -ldl && \
	LD_PRELOAD=$(shell pwd)/libtxn.so LD_LIBRARY_PATH=$(shell pwd) ./ports/fsx $(OUTDIR)/temp.txt $(FSXFLAGS) \

clean:
	rm -rf libtxn.so logs/ $(OUTDIR)/
