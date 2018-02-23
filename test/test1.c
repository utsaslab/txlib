#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "txnlib.h"

// test1: simple nested transaction

int main(int argc, char **argv)
{
	int txn0 = begin_txn();
	int txn1 = begin_txn();

	int fd = open("out/test1.out", O_CREAT | O_RDWR, 0644);
	write(fd, "hello nested transactional world\n", 34);
	write(fd, "goodbye\n", 8);
	if (txn0 == txn1)
		write(fd, "transaction ids are the same\n", 29);
	close(fd);

	end_txn(txn1);
	end_txn(txn0);

	return 0;
}
