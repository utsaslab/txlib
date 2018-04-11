#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "txnlib.h"

#define NS 1000000000
#define NUM_RUNS 50
#define SHORT 10
#define LONG 1000
#define SMALL 4096 // 4 KB
#define BIG 1073741824 // 1 GB

static const char *working_file = "out/bench.file";
static const char *working_backup = "out/bench.backup";

// nanoseconds
unsigned long time_passed(struct timeval start, struct timeval finish)
{
        long unsigned passed = 0;
        passed += 1000000000 * (finish.tv_sec - start.tv_sec);
        passed += 1000 * (finish.tv_usec - start.tv_usec);
        return passed;
}

// returns average of all runs
unsigned long bench(int buf_size, int count, int txn, int starting_size, int offset)
{
        struct timeval start, finish;
        char buf[buf_size];
        memset(buf, '>', buf_size);

        char cmd[1024];
        sprintf(cmd, "dd if=/dev/zero of=%s count=%d bs=%d > /dev/null 2>&1", working_backup, starting_size / 1024, 1024);
        set_bypass(1);
        system(cmd);
        set_bypass(0);

        unsigned long runtime = 0;
        for (int i = 0; i < NUM_RUNS; i++) {
                // printf("run %d\n", i);
                if (i % 10 == 0) {
                        printf("%d ", i);
                        fflush(stdout);
                }

                if (starting_size) {
                        char copy[1024];
                        sprintf(copy, "cp %s %s", working_backup, working_file);
                        set_bypass(1);
                        system(copy);
                        set_bypass(0);
                } else {
                        remove(working_file);
                }

                gettimeofday(&start, NULL);

                int txn_id = -1;
                if (txn)
                        txn_id = begin_txn();

                int fd = open(working_file, O_CREAT | O_RDWR, 0644);
                lseek(fd, offset, SEEK_SET);
                for (int i = 0; i < count; i++)
                        write(fd, buf, buf_size);
                close(fd);

                if (txn)
                        end_txn(txn_id);

                gettimeofday(&finish, NULL);

                runtime += time_passed(start, finish);
        }
        return runtime / NUM_RUNS;
}

int main()
{
        /**
         * Need to test all configurations of:
         *  - short/long sequence of fs ops
         *  - within/without txn
         *  - changing new/small/big file
         */

        printf("  ++++++++++++++++++++\n");
        printf("  +  CONFIGURATIONS  +\n");
        printf("  ++++++++++++++++++++\n");
        printf(" - fs ops: short -> %d, long -> %d\n", SHORT, LONG);
        printf(" - txn: within, without\n");
        printf(" - file: new -> %d, small -> %d KB, big -> %d GB\n", 0, SMALL / 1024, BIG / (1024*1024*1024));
        printf("============================================\n");

        struct timeval start, finish;
        unsigned long short_none_new, short_none_small, short_none_big;
        unsigned long short_txn_new, short_txn_small, short_txn_big;
        unsigned long long_none_new, long_none_small, long_none_big;
        unsigned long long_txn_new, long_txn_small, long_txn_big;
        int buf_size = 64;

        printf("> short seqs of fs ops...\n");

        printf("- > txnless\n");
        printf("- - > new file:   "); fflush(stdout);
        short_none_new = bench(buf_size, SHORT, 0, 0, 0);
        printf("%lds %ldns\n", short_none_new / NS, short_none_new % NS);
        printf("- - > small file: "); fflush(stdout);
        short_none_small = bench(buf_size, SHORT, 0, SMALL, SMALL - 128);
        printf("%lds %ldns\n", short_none_small / NS, short_none_small % NS);
        printf("- - > big file:   "); fflush(stdout);
        short_none_big = bench(buf_size, SHORT, 0, BIG, BIG * 1/4);
        printf("%lds %ldns\n", short_none_big / NS, short_none_big % NS);

        printf("- > txnl\n");
        printf("- - > new file:   "); fflush(stdout);
        short_txn_new = bench(buf_size, SHORT, 1, 0, 0);
        printf("%lds %ldns %fx\n", short_txn_new / NS, short_txn_new % NS, (double) short_txn_new / short_none_new);
        printf("- - > small file: "); fflush(stdout);
        short_txn_small = bench(buf_size, SHORT, 1, SMALL, SMALL - 128);
        printf("%lds %ldns %fx\n", short_txn_small / NS, short_txn_small % NS, (double) short_txn_small / short_none_small);
        printf("- - > big file:   "); fflush(stdout);
        short_txn_big = bench(buf_size, SHORT, 1, BIG, BIG * 1/4);
        printf("%lds %ldns %fx\n", short_txn_big / NS, short_txn_big % NS, (double) short_txn_big / short_none_big);

        printf("> long seqs of fs ops...\n");

        printf("- > txnless\n");
        printf("- - > new file:   "); fflush(stdout);
        long_none_new = bench(buf_size, LONG, 0, 0, 0);
        printf("%lds %ldns\n", long_none_new / NS, long_none_new % NS);
        printf("- - > small file: "); fflush(stdout);
        long_none_small = bench(buf_size, LONG, 0, SMALL, SMALL - 128);
        printf("%lds %ldns\n", long_none_small / NS, long_none_small % NS);
        printf("- - > big file:   "); fflush(stdout);
        long_none_big = bench(buf_size, LONG, 0, BIG, BIG * 1/4);
        printf("%lds %ldns\n", long_none_big / NS, long_none_big % NS);

        printf("- > txnl\n");
        printf("- - > new file:   "); fflush(stdout);
        long_txn_new = bench(buf_size, LONG, 1, 0, 0);
        printf("%lds %ldns %fx\n", long_txn_new / NS, long_txn_new % NS, (double) long_txn_new / long_none_new);
        printf("- - > small file: "); fflush(stdout);
        long_txn_small = bench(buf_size, LONG, 1, SMALL, SMALL - 128);
        printf("%lds %ldns %fx\n", long_txn_small / NS, long_txn_small % NS, (double) long_txn_small / long_none_small);
        printf("- - > big file:   "); fflush(stdout);
        long_txn_big = bench(buf_size, LONG, 1, BIG, BIG * 1/4);
        printf("%lds %ldns %fx\n", long_txn_big / NS, long_txn_big % NS, (double) long_txn_big / long_none_big);
}
