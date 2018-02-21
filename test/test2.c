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

    if (access("out/test2.out", F_OK)) {
        int out = open("out/test2.out", O_CREAT | O_RDWR, 0644);
        write(out, "does not exist\n", 14);
        close(out);
    }

	return 0;
}
