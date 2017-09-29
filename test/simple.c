#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
// #include <sys/stat.h>
// #include <sys/types.h>
#include <unistd.h>

#include "txnlib.h"

int main()
{
	int txn_id = begin_txn();

	int fd = open("message.txt", O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
	write(fd, "hello", 5);
	// void *buf = malloc(16);
	// read(fd, buf, 5);
	// strcat(buf, " goodbye");
	// write(fd, buf, 16);
	close(fd);

	end_txn(txn_id);

	return 0;
}
