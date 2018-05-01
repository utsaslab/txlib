#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "txnlib.h"

// test9: create a file then read from it

int main(int argc, char **argv)
{
	int id = begin_txn();

	int fd = open("out/test9.out", O_CREAT | O_TRUNC | O_RDWR, 0644);
	write(fd, "999999999\n", 10);

	char buf[11];
	lseek(fd, 0, SEEK_SET);
	read(fd, buf, 10);

	end_txn(id);

	ftruncate(fd, 0);
	lseek(fd, 0, SEEK_SET);
	write(fd, buf, 10);
	close(fd);

	return 0;
}
