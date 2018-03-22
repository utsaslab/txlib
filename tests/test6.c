#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "txnlib.h"

// test6: simple write then crash

int main(int argc, char **argv)
{
        char orig[64];
        sprintf(orig, "abcdefghijklkmnopq\n");
        int fd = open("out/test6.out", O_CREAT | O_RDWR, 0644);
        write(fd, orig, strlen(orig));

	int txn0 = begin_txn();

        write(fd, "dirty data\n", 11);

	crash();

        char buf[64];
        int fresh = open("out/test6.out", O_RDWR);
        read(fresh, buf, 30);
        if (strcmp(buf, orig))
                write(fresh, "recovery failed\n", 16);
        close(fresh);
        close(fd);

	return 0;
}
