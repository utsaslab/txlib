#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "txnlib.h"

// test9: write -> remove

int main(int argc, char **argv)
{
        int fd = open("out/test9.out", O_CREAT | O_RDWR, 0644);
        write(fd, "write to me\n", 12);

	int txn0 = begin_txn();

        write(fd, "blah blah blah\n", 15);
        close(fd);
        remove("out/test8.out");

	crash();
        recover();

	return 0;
}
