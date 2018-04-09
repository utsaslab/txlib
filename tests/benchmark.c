#include <fcntl.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "txnlib.h"

// nanoseconds
long unsigned time_passed(struct timeval start, struct timeval finish)
{
        long unsigned passed = 0;
        passed += 1000000000 * (finish.tv_sec - start.tv_sec);
        passed += 1000 * (finish.tv_usec - start.tv_usec);
        return passed;
}

void workload()
{
        int fd = open("out/bench.file", O_CREAT | O_TRUNC | O_RDWR, 0644);
        for (int i = 0; i < 10000; i++) {
                if (i % 1000 == 0) {
                        printf("%d ", i);
                        fflush(stdout);
                }
                write(fd, "wow", 3);
        }
        close(fd);
}

int main()
{
        struct timeval start, finish;

        printf("beginning transactionless workload... ");
        fflush(stdout);
        gettimeofday(&start, NULL);
        workload();
        gettimeofday(&finish, NULL);
        long unsigned no_txn = time_passed(start, finish);
        printf("done\n");

        printf("beginning transactional workload...   ");
        fflush(stdout);
        gettimeofday(&start, NULL);
        int id = begin_txn();
        workload();
        end_txn(id);
        gettimeofday(&finish, NULL);
        long unsigned txn = time_passed(start, finish);
        printf("done\n");

        double overhead = txn / no_txn;

        printf("  +++++++++++++\n");
        printf("  +  RESULTS  +\n");
        printf("  +++++++++++++\n");
        printf("> without txn: %lds %ldns\n", no_txn / 1000000000, no_txn);
        printf("> within txn: %lds %ldns\n", txn / 1000000000, txn);
        printf("> overhead: %fx\n", overhead);
}
