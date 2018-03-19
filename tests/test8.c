#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "txnlib.h"

// test8: delete -> recreate -> write

int main(int argc, char **argv)
{
        int fd = open("out/test8.out", O_CREAT | O_RDWR, 0644);
        write(fd, "delete me\n", 10);
        close(fd);

	int txn0 = begin_txn();

        remove("out/test8.out");
        int fd1 = open("out/test8.out", O_CREAT | O_RDWR, 0644);
        write(fd1, "I have been recreated\n", 21);
        close(fd1);

	crash();
        recover();

	return 0;
}
