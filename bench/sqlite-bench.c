#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include "txnlib.h"

#define NUM_WRITES 1024 * 1024
#define WRITE_SIZE 16

// nanoseconds
unsigned long time_passed(struct timeval start, struct timeval finish)
{
	long unsigned passed = 0;
	passed += 1000000000 * (finish.tv_sec - start.tv_sec);
	passed += 1000 * (finish.tv_usec - start.tv_usec);
	return passed;
}

void setup_sqlite()
{
	int fd = open("out/setup.sql", O_CREAT | O_TRUNC | O_RDWR, 0644);
	write(fd, "DROP TABLE main.reals;\nCREATE TABLE main.reals( n REAL );\n", 59);
	close(fd);
	set_bypass(1);
	system("sqlite3 out/nums.db < out/setup.sql 2>/dev/null");
	set_bypass(0);
}

void make_sql_txn(int count)
{
	int fd = open("out/txn.sql", O_CREAT | O_TRUNC | O_RDWR, 0644);
	write(fd, "BEGIN;\n", 7);
	while (count--) {
		char entry[64];
		snprintf(entry, sizeof(entry), "INSERT INTO reals VALUES (%d.%d);\n", count+1, count+1);
		write(fd, entry, strlen(entry));
	}
	write(fd, "COMMIT;\n", 8);
	close(fd);
}

int main()
{
	struct timeval start, finish;
	unsigned long txnlib, sqlite;
	printf("count: %d\n", NUM_WRITES);

	// sqlite
	setup_sqlite();
	make_sql_txn(NUM_WRITES);

	gettimeofday(&start, NULL);
	system("sqlite3 out/nums.db < out/txn.sql");
	gettimeofday(&finish, NULL);

	sqlite = time_passed(start, finish);
	printf("sqlite transaction: %2lds %9ldns\n", sqlite / 1000000000, sqlite % 1000000000);

	// txnlib
	char *reals[NUM_WRITES / 2];
	for (int i = 0; i < sizeof(reals) / sizeof(reals[0]); i++) {
		reals[i] = malloc(WRITE_SIZE+1);
		snprintf(reals[i], WRITE_SIZE, "%07d\n", i);
	}
	int fd = open("out/write-8mb.out", O_CREAT | O_TRUNC | O_RDWR, 0644);

	gettimeofday(&start, NULL);
	int id = begin_txn();
	for (int i = 0; i < sizeof(reals) / sizeof(reals[0]) / 2; i++) {
		write(fd, reals[i], WRITE_SIZE);
		// write(fd, reals[i], 8);
	}
	end_txn(id);
	gettimeofday(&finish, NULL);

	close(fd);
	txnlib = time_passed(start, finish);
	printf("txnlib transaction: %2lds %9ldns\n", txnlib / 1000000000, txnlib % 1000000000);

	printf("txnlib is %4.2fx slower than sqlite\n", (double) txnlib / sqlite);
}
