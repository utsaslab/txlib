#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "txnlib.h"

#include <errno.h>

// test6: create a lot then remove a little

int main(int argc, char **argv)
{
    // set up deep tree
    mkdir("out/test6-dir", 0744);
    mkdir("out/test6-dir/one", 0744);
    mkdir("out/test6-dir/one/two", 0744);
    mkdir("out/test6-dir/one/two/three", 0744);
    mkdir("out/test6-dir/one/two/three/four", 0744);
    mkdir("out/test6-dir/one/two/three/five", 0744);
    mkdir("out/test6-dir/one/two/six", 0744);
    mkdir("out/test6-dir/one/two/six/seven", 0744);
    int two = open("out/test6-dir/one/two/2.txt", O_CREAT | O_RDWR, 0644);
    int three = open("out/test6-dir/one/two/three/3.txt", O_CREAT | O_RDWR, 0644);
    int four = open("out/test6-dir/one/two/three/four/4.txt", O_CREAT | O_RDWR, 0644);
    int five = open("out/test6-dir/one/two/three/five/5.txt", O_CREAT | O_RDWR, 0644);
    int seven = open("out/test6-dir/one/two/six/seven/7.txt", O_CREAT | O_RDWR, 0644);
    write(two, "22", 2);
    write(three, "333", 3);
    write(four, "4444", 4);
    write(five, "55555", 5);
    write(seven, "7777777", 7);
    close(two);
    close(three);
    close(four);
    close(five);
    close(seven);

    int txn0 = begin_txn();

    mkdir("out/test6-dir/one/two/six/a", 0744);
    mkdir("out/test6-dir/one/two/six/a/b", 0744);
    mkdir("out/test6-dir/one/two/six/a/b/c", 0744);
    int a = open("out/test6-dir/one/two/six/a/a.txt", O_CREAT | O_RDWR, 0644);
    int b = open("out/test6-dir/one/two/six/a/b/b.txt", O_CREAT | O_RDWR, 0644);
    int c = open("out/test6-dir/one/two/six/a/b/c/c.txt", O_CREAT | O_RDWR, 0644);
    write(a, "a\n", 2);
    write(b, "b\n", 2);
    write(c, "c\n", 2);
    close(a);
    close(b);
    close(c);

    remove("out/test6-dir/one/two/six/a/b/c/c.txt");
    remove("out/test6-dir/one/two/six/a/b/c");
    remove("out/test6-dir/one/two/six/a/b/b.txt");
    remove("out/test6-dir/one/two/six/a/b");

    crash();

    int fail0 = open("out/test6-dir/one/two/six/a/a.txt", O_RDWR, 0644);
    int fail1 = open("out/test6-dir/one/two/six/a/b/b.txt", O_RDWR, 0644);
    int fail2 = open("out/test6-dir/one/two/six/a/b/c/c.txt", O_RDWR, 0644);

    int out = open("out/test6.out", O_CREAT | O_RDWR, 0644);
    if (fail0 == -1 && fail1 == -1 && fail2 == -1)
        write(out, "restored successfully\n", 22);
    else
        write(out, "restore failed\n", 15);
    close(out);

	return 0;
}
