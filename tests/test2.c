#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "txnlib.h"

// test2: create file then crash

int main(int argc, char **argv)
{
	int txn0 = begin_txn();

	int fd = open("out/test2.out", O_CREAT | O_RDWR, 0644);
	close(fd);

	crash();

	int fd1 = open("out/test2.out", O_RDWR, 0644);
	// if (fd1 == -1) {
	// 	int fd2 = open("out/test2.out", O_CREAT | O_RDWR, 0644);
	// 	write(fd2, "this did not exist\n", 19);
	// 	close(fd2);
	// } else {
	// 	write(fd1, "this file should not exist\n", 27);
	// 	close(fd1);
	// }

	return 0;
}
