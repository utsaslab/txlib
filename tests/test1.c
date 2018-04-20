#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "txnlib.h"

// test1: simple single-layered transaction

int main(int argc, char **argv)
{
        int txn0 = begin_txn();

        int fd = open("out/test1.out", O_CREAT | O_RDWR, 0644);
        write(fd, "hello transactional world\n", 26);
        close(fd);

        end_txn(txn0);

        return 0;
}
