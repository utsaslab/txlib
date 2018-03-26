#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "txnlib.h"

// test11: simple mkdir

int main(int argc, char **argv)
{
        int txn0 = begin_txn();

        mkdir("out/test11-dir", 0644);

        crash();
        recover();

        int fd = open("out/test11.out", O_CREAT | O_RDWR, 0644);
        DIR* dir = opendir("out/test11-dir");
        if (dir)
                write(fd, "failed to undo mkdir\n", 21);
        else
                write(fd, "mkdir undone successfully\n", 26);
        close(fd);

	return 0;
}
