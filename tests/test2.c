#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "txnlib.h"

// test2: attempting to end incorrect transaction

int main(int argc, char **argv)
{
	int one   = begin_txn();
	int two   = begin_txn();
	int three = begin_txn();
	int four  = begin_txn();

	int good = 0;
	int bad = 0;

	good = good | end_txn(four);
	good = good | end_txn(three);
	bad  = bad  | end_txn(one);
	good = good | end_txn(two);
	good = good | end_txn(one);

	int fd = open("out/test2.out", O_CREAT | O_RDWR, 0644);
	if (good == 0 && bad)
		write(fd, "transactions ended correctly\n", 29);
	else
		write(fd, "transactions ended incorrectly\n", 31);
	close(fd);
}
