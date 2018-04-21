#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "txnlib.h"

// test0: simple mkdir test

int is_real_dir(const char *path)
{
        struct stat st;
        stat(path, &st);
        return S_ISDIR(st.st_mode) && access(path, F_OK) == 0;
}

int main(int argc, char **argv)
{
        mkdir("out/test3-dir", 0755);

        int txn0 = begin_txn();

        mkdir("out/test3-dir/a", 0755);
        mkdir("out/test3-dir/a/b/c", 0755);
        mkdir("out/test3-dir/a/b", 0755);
        mkdir("out/test3-dir/a/d", 0755);
        mkdir("out/test3-dir/a/b/e", 0755);

        end_txn(txn0);

        int fd = open("out/test3.out", O_CREAT | O_TRUNC | O_RDWR, 0644);
        if (is_real_dir("out/test3-dir/a") &&
            is_real_dir("out/test3-dir/a/b") &&
            !is_real_dir("out/test3-dir/a/b/c") &&
            is_real_dir("out/test3-dir/a/d") &&
            is_real_dir("out/test3-dir/a/b/e"))
                write(fd, "mkdirs successful :)\n", 21);
        else
                write(fd, "mkdirs failed\n", 14);
        close(fd);

        return 0;
}
