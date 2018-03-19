#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "txnlib.h"

// test10: multiple removes on same filename

int main(int argc, char **argv)
{
        int fd = open("out/test10.out", O_CREAT | O_RDWR, 0644);
        write(fd, "multiple removes undone successfully\n", 37);
        close(fd);

	int txn0 = begin_txn();

        for (int i = 0; i < 10; i++) {
                remove("out/test10.out");
                close(open("out/test10.out", O_CREAT, 0644));
        }

	crash();
        recover();

	return 0;
}
