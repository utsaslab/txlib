#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "txnlib.h"

// test8: attempting invalid transactions

int main(int argc, char **argv)
{
        int fd = open("out/test8.out", O_CREAT | O_RDWR, 0644);
        write(fd, "original data\n", 14);
        mkdir("out/test8-dir", 0755);

        int txn0 = begin_txn();

        // creating file without existing directory
        remove("out/test8-dir");
        int fd1 = open("out/test8-dir/ghost.txt", O_CREAT, 0644);

        // reading/writing to deleted file
        rename("out/test8.out", "out/test8.moved");
        int fd2 = open("out/test8.out", O_RDWR);
        char buf[14];
        read(fd2, buf, 13);
        buf[13] = '\0';
        lseek(fd2, 0, SEEK_SET);
        write(fd2, "should not see this write\n", 26);

        end_txn(txn0);

        int fd3 = open("out/test8.out", O_CREAT | O_RDWR, 0644);

        char buf1[25];
        read(fd3, buf1, 25);
        buf1[24] = '\0';

        lseek(fd3, 0, SEEK_SET);
        if (access("out/test8-dir/ghost.txt", F_OK) &&
            strcmp(buf, "original data") &&
            strcmp(buf1, "should not see this write"))
                write(fd3, "successfully prevented invalid filesystem operations\n", 53);
        else
                write(fd3, "failed to prevent invalid filesystem operations\n", 48);
        close(fd3);

        return 0;
}
