#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "txnlib.h"

// test7: one write one create then crash

int main(int argc, char **argv)
{
        int fd = open("out/test7.out", O_CREAT | O_RDWR, 0644);
        write(fd, "1234567890\n", 11);
        close(fd);

	int txn0 = begin_txn();

	int fd1 = open("out/test7-1.out", O_CREAT | O_RDWR, 0644);
        write(fd1, "this should be deleted later\n", 29);
	close(fd1);
        int fd2 = open("out/test7.out", O_RDWR);
        write(fd2, "failure\n", 8);
        close(fd2);

	rollback();

	return 0;
}
