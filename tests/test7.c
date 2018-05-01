#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "txnlib.h"

// test7: do a lot then rollback

int main(int argc, char **argv)
{
	int fd = open("out/test7.out", O_CREAT | O_TRUNC | O_RDWR, 0644);
	write(fd, "original data\n", 14);
	mkdir("out/test7-dir", 0755);

	int txn0 = begin_txn();

	int fd1 = open("out/test7.out", O_TRUNC);
	remove("out/test7-dir");
	write(fd1, "new txnl data\n", 13);
	rename("out/test7.out", "out/test7-move.out");
	ftruncate(fd1, 1234567890);

	rollback();

	int fd2 = open("out/test7.out", O_RDWR);
	char buf[15];
	read(fd2, buf, 14);
	buf[14] = '\0';

	ftruncate(fd, 0);
	lseek(fd, 0, SEEK_SET);
	if (strcmp(buf, "original data\n") == 0 && mkdir("out/test7-dir", 0755) == -1)
		write(fd, "rollback successful\n", 20);
	else
		write(fd, "rollback failed\n", 16);
	close(fd);

	return 0;
}
