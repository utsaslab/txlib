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

// test5: create a little then remove a lot

int main(int argc, char **argv)
{
    // set up deep tree
    mkdir("out/test5-dir", 0744);
    mkdir("out/test5-dir/one", 0744);
    mkdir("out/test5-dir/one/two", 0744);
    mkdir("out/test5-dir/one/two/three", 0744);
    mkdir("out/test5-dir/one/two/three/four", 0744);
    mkdir("out/test5-dir/one/two/three/five", 0744);
    mkdir("out/test5-dir/one/two/six", 0744);
    mkdir("out/test5-dir/one/two/six/seven", 0744);
    int two = open("out/test5-dir/one/two/2.txt", O_CREAT | O_RDWR, 0644);
    int three = open("out/test5-dir/one/two/three/3.txt", O_CREAT | O_RDWR, 0644);
    int four = open("out/test5-dir/one/two/three/four/4.txt", O_CREAT | O_RDWR, 0644);
    int five = open("out/test5-dir/one/two/three/five/5.txt", O_CREAT | O_RDWR, 0644);
    int seven = open("out/test5-dir/one/two/six/seven/7.txt", O_CREAT | O_RDWR, 0644);
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

    mkdir("out/test5-dir/one/two/three/five/a", 0744);
    int a = open("out/test5-dir/one/two/three/five/a/a.txt", O_CREAT | O_RDWR, 0644);
    write(a, "a\n", 2);
    close(a);
    remove("out/test5-dir/one/two/three/four/4.txt");
    remove("out/test5-dir/one/two/three/four");
    remove("out/test5-dir/one/two/three/four/five/a/a.txt");
    remove("out/test5-dir/one/two/three/five/a");
    remove("out/test5-dir/one/two/three/five/five.txt");
    remove("out/test5-dir/one/two/three/five");
    remove("out/test5-dir/one/two/three");

    crash();

    // check that a/a.txt does not exist
    // check 4.txt
    // check 5.txt
    int fourfours = open("out/test5-dir/one/two/three/four/4.txt", O_RDWR, 0644);
    int fivefives = open("out/test5-dir/one/two/three/five/5.txt", O_RDWR, 0644);
    int ghost = open("out/test5-dir/one/two/three/five/a/a.txt", O_RDWR, 0644);

    int out = open("out/test5.out", O_CREAT | O_RDWR, 0644);
    if (fourfours != -1 && fivefives != -1 && ghost == -1)
        write(out, "restored successfully\n", 22);
    else
        write(out, "restore failed\n", 15);
    close(out);

	return 0;
}
