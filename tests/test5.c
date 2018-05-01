#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "txnlib.h"

// test5: read() after write() within txn

int main(int argc, char **argv)
{
	int fd = open("out/test5.out", O_CREAT | O_TRUNC | O_RDWR, 0644);
	write(fd, "1234567890", 10);

	int txn0 = begin_txn();

	lseek(fd, 2, SEEK_SET);
	write(fd, "cdefg", 5);
	lseek(fd, 0, SEEK_SET);
	char buf[11];
	read(fd, buf, 10);
	buf[10] = '\0';

	end_txn(txn0);

	ftruncate(fd, 0);
	lseek(fd, 0, SEEK_SET);
	if (strcmp(buf, "12cdefg890") == 0)
		write(fd, "read-your-writes\n", 17);
	else
		write(fd, "no read-your-writes\n", 20);
	close(fd);

	return 0;
}
