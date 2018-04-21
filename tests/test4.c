#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "txnlib.h"

// test4: mixing write(), ftruncate(), and lseek()

int main(int argc, char **argv)
{
        int txn0 = begin_txn();

        int fd = open("out/test4.out", O_CREAT | O_RDWR, 0644);
        off_t zero = lseek(fd, 0, SEEK_CUR);

        for (int i = 0; i < 1000; i++)
                write(fd, "4444\n", 5);

        off_t fiveK = lseek(fd, 0, SEEK_CUR);
        ftruncate(fd, 3000);
        off_t threeK = lseek(fd, 0, SEEK_END);
        close(fd);

        end_txn(txn0);

        int fd1 = open("out/test4.out", O_CREAT | O_TRUNC | O_RDWR, 0644);
        if (zero == 0 && fiveK == 5000 && threeK == 3000)
                write(fd1, "transaction successful\n", 23);
        else
                write(fd1, "transaction failed\n", 19);
        close(fd1);

        return 0;
}
