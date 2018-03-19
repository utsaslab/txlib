#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "txnlib.h"

// test1: deeply nested transaction

int main(int argc, char **argv)
{
	int depth = 1000;
	int ids[depth];

	char id[64];
	int fd = open("out/test1.out", O_CREAT | O_RDWR, 0644);

	for (int i = 0; i < depth; i++)
		ids[i] = begin_txn();

	write(fd, "hello nested transactional world\n", 33);
	write(fd, "goodbye\n", 8);
	close(fd);

	for (int i = depth-1; i >= 0; i--)
		end_txn(ids[i]);

	return 0;
}
