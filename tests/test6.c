#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "txnlib.h"

// test6: reading into ftruncate extension range

int main(int argc, char **argv)
{
        int fd = open("out/test6.out", O_CREAT | O_TRUNC | O_RDWR, 0644);
        write(fd, "666666", 6);

        int txn0 = begin_txn();

        ftruncate(fd, 12);
        lseek(fd, 0, SEEK_SET);
        char buf[12];
        read(fd, buf, 12);

        end_txn(txn0);

        int good = 1;
        for (int i = 0; i < 6; i++)
                if (buf[i] != '6')
                        good = 0;
        for (int i = 6; i < 12; i++)
                if (buf[i] != '\0')
                        good = 0;

        ftruncate(fd, 0);
        lseek(fd, 0, SEEK_SET);
        if (good)
                write(fd, "successful read after ftruncate\n", 32);
        else
                write(fd, "read failed after ftruncate\n", 28);
        close(fd);

        return 0;
}
