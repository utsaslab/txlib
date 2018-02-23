#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "txnlib.h"

// test3: remove file then crash

int main(int argc, char **argv)
{
	int fd = open("out/test3.out", O_CREAT | O_RDWR, 0644);
	close(fd);

    int txn0 = begin_txn();

    remove("out/test3.out");

	crash();

	int fd1 = open("out/test3.out", O_RDWR, 0644);
    if (fd1 != -1) {
        write(fd1, "restored successfully\n", 22);
        close(fd1);
    } else {
        int fd2 = open("out/test3.out", O_CREAT | O_RDWR, 0644);
        write(fd2, "restore failed\n", 15);
        close(fd2);
    }

	return 0;
}
