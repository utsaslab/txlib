#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "txnlib.h"

// test4: create + remove equally then crash

int main(int argc, char **argv)
{
    // set up deep tree
    mkdir("out/test4-dir", 0744);
    mkdir("out/test4-dir/one", 0744);
    mkdir("out/test4-dir/one/two", 0744);
    mkdir("out/test4-dir/one/two/three", 0744);
    mkdir("out/test4-dir/one/two/three/four", 0744);
    mkdir("out/test4-dir/one/two/three/five", 0744);
    mkdir("out/test4-dir/one/two/six", 0744);
    mkdir("out/test4-dir/one/two/six/seven", 0744);
    int two = open("out/test4-dir/one/two/2.txt", O_CREAT | O_RDWR, 0644);
    int three = open("out/test4-dir/one/two/three/3.txt", O_CREAT | O_RDWR, 0644);
    int four = open("out/test4-dir/one/two/three/four/4.txt", O_CREAT | O_RDWR, 0644);
    int five = open("out/test4-dir/one/two/three/five/5.txt", O_CREAT | O_RDWR, 0644);
    int seven = open("out/test4-dir/one/two/six/seven/7.txt", O_CREAT | O_RDWR, 0644);
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

    mkdir("out/test4-dir/one/two/six/seven/a", 0744);
    int a = open("out/test4-dir/one/two/six/seven/a/a.txt", O_CREAT | O_RDWR, 0644);
    close(a);
    remove("out/test4-dir/one/two/six/seven/a/a.txt");
    remove("out/test4-dir/one/two/six/seven/a");

    crash();

    DIR* dir = opendir("out/test4-dir/one/two/six/seven/a");
    int out = open("out/test4.out", O_CREAT | O_RDWR, 0644);
    if (!dir)
        write(out, "restored properly\n", 18);
    else
        write(out, "restore failed\n", 15);
    close(out);

	// int fd = open("out/test4.out", O_CREAT | O_RDWR, 0644);
	// close(fd);
    //
    // int txn0 = begin_txn();
    //
    // remove("out/test3.out");
    //
	// crash();
    //
	// int fd1 = open("out/test3.out", O_RDWR, 0644);
    // if (fd1 != -1) {
    //     write(fd1, "restored successfully\n", 22);
    //     close(fd1);
    // } else {
    //     int fd2 = open("out/test3.out", O_CREAT | O_RDWR, 0644);
    //     write(fd2, "restore failed\n", 15);
    //     close(fd2);
    // }

	return 0;
}
