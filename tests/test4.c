#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "txnlib.h"

// test4: simple remove then crash

int main(int argc, char **argv)
{
        int fd = open("out/test4.out", O_CREAT | O_RDWR, 0644);
        write(fd, "moving\n", 7);
        close(fd);

	int txn0 = begin_txn();

        remove("out/test4.out");

	rollback();

	return 0;
}
