#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "txnlib.h"

// test0: simple single-layered transaction with one write then one read

int main(int argc, char **argv)
{
	int txn_id = begin_txn();

	int fd = open("out/test0.out", O_CREAT | O_RDWR, 0644);
	write(fd, "hello transactional world\n", 26);
	write(fd, "goodbye\n", 8);
	close(fd);

	end_txn(txn_id);

	return 0;
}
