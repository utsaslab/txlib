CFLAGS = -Wall
BENCHDIR = bench
LIBDIR = lib
LOGDIR = /var/tmp/txnlib-logs
OUTDIR = out
TESTDIR = tests
BENCHMARKFLAGS =
CRASHFLAGS =
FSXFLAGS = -N 10000

.PHONY: all test clean
.SILENT:

all: txnlib.so

txnlib.so:
	gcc $(CFLAGS) -shared -fPIC $(LIBDIR)/txnlib.h $(LIBDIR)/txnlib.c -o libtxn.so -ldl

test:
	rm -rf $(OUTDIR); mkdir $(OUTDIR) && \
	for i in `basename -a $(TESTDIR)/test*.c | grep -Eo "[0-9]{1,4}"`; do \
		(rm -rf $(LOGDIR)/*; \
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

crash:
	rm -rf $(OUTDIR); mkdir $(OUTDIR) && \
	gcc $(TESTDIR)/crash.c -I$(LIBDIR) -L. -ltxn -o $(OUTDIR)/crash -ldl && \
	LD_PRELOAD=$(shell pwd)/libtxn.so LD_LIBRARY_PATH=$(shell pwd) ./$(OUTDIR)/crash $(CRASHFLAGS) \

benchmark:
	rm -rf $(OUTDIR); mkdir $(OUTDIR) && \
	gcc $(BENCHDIR)/benchmark.c -I$(LIBDIR) -L. -ltxn -o $(OUTDIR)/benchmark -ldl -lm && \
	LD_PRELOAD=$(shell pwd)/libtxn.so LD_LIBRARY_PATH=$(shell pwd) ./$(OUTDIR)/benchmark $(BENCHMARKFLAGS) \

big-writes:
	rm -rf $(OUTDIR); mkdir $(OUTDIR) && \
	gcc $(BENCHDIR)/big-writes.c -I$(LIBDIR) -L. -ltxn -o $(OUTDIR)/big-writes -ldl && \
	LD_PRELOAD=$(shell pwd)/libtxn.so LD_LIBRARY_PATH=$(shell pwd) ./$(OUTDIR)/big-writes \

fsx:
	rm -rf $(OUTDIR); mkdir $(OUTDIR) && \
	gcc ports/fsx-linux.c -I$(LIBDIR) -L. -ltxn -o $(OUTDIR)/fsx -ldl && \
	LD_PRELOAD=$(shell pwd)/libtxn.so LD_LIBRARY_PATH=$(shell pwd) ./$(OUTDIR)/fsx $(OUTDIR)/temp.txt $(FSXFLAGS) \

fsx-txn:
	rm -rf $(OUTDIR); mkdir $(OUTDIR) && \
	gcc ports/fsx-linux-txn.c -I$(LIBDIR) -L. -ltxn -o $(OUTDIR)/fsxt -ldl && \
	LD_PRELOAD=$(shell pwd)/libtxn.so LD_LIBRARY_PATH=$(shell pwd) ./$(OUTDIR)/fsxt $(OUTDIR)/temp.txt $(FSXFLAGS) \

fsx-bench:
	rm -rf $(OUTDIR); mkdir $(OUTDIR) && \
	gcc $(BENCHDIR)/fsx-bench.c -o $(OUTDIR)/fsxb && \
	./$(OUTDIR)/fsxb \

clean:
	rm -rf libtxn.so $(LOGDIR)/ $(OUTDIR)/
