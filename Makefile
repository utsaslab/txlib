CFLAGS = -Wall

all: txlib.so

txlib.so: txlib.o
	gcc -shared -o libtx.so txlib.o

txlib.o:
	gcc -fPIC -c lib/txlib.c

clean:
	rm -f libtx.so txlib.o
