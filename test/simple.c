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

	int fd = open("message.txt", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	write(fd, "hello ", 6);
	write(fd, "goodbye\n", 8);
	write(fd, "abcdefghijklmn\n", 15);
	close(fd);

	end_txn(txn_id);

	return 0;
}
