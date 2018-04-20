#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "txnlib.h"

// test5: simple metadata test

int main(int argc, char **argv)
{
        /**
         * Relevant metadata includes:
         *   mode
         *   user + group
         *   access/modify/change time
         */
        int fd = open("out/test5.out", O_CREAT | O_RDWR, 0777);
        struct stat old;
        fstat(fd, &old);
        time_t before = old.st_mtim.tv_sec;
        close(fd);

        sleep(1); // ensure that times are different enough

	int txn0 = begin_txn();

        int fd1 = open("out/test5.out", O_RDWR);
        write(fd1, ".", 1);
        close(fd1);
        
        rollback();

        struct stat new;
        int fd2 = open("out/test5.out", O_RDWR);
        fstat(fd2, &new);
        time_t after = new.st_mtim.tv_sec;

        if (before == after)
                write(fd2, "metadata restored\n", 18);
        else
                write(fd2, "metadata failure\n", 17);
        close(fd2);

	return 0;
}
