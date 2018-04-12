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

unsigned long multiopen(int count, int txn, int create)
{
        struct timeval start, finish;

        mkdir("out/open", 0755);
        char *files[count]; // TODO: free later
        for (int i = 0; i < count; i++) {
                char *f = malloc(32);
                sprintf(f, "out/open/%d.txt", i);
                files[i] = f;
        }

        int flags = O_RDWR;
        if (create)
                flags |= O_CREAT;

        unsigned long runtime = 0;
        for (int r = 0; r < NUM_RUNS; r++) {
                if (r % 10 == 0) {
                        printf("%d ", r);
                        fflush(stdout);
                }

                set_bypass(1);
                system("rm -f out/open/*");
                set_bypass(0);

                if (!create)
                        for (int i = 0; i < count; i++)
                                close(open(files[i], O_CREAT, 0644));

                gettimeofday(&start, NULL);

                int txn_id = -1;
                if (txn)
                        txn_id = begin_txn();

                int first = open(files[0], flags, 0644);
                int last = -1;
                for (int i = 1; i < count; i++)
                        last = open(files[i], flags, 0644);

                if (txn)
                        end_txn(txn_id);

                gettimeofday(&finish, NULL);

                for (int i = first; i <= last; i++)
                        close(i);

                runtime += time_passed(start, finish);
        }
        return runtime / NUM_RUNS;
}

void openbench()
{
        /**
         * permutate:
         *  - within, without
         *  - create, existing
         */

         printf("  +++++++++++++++++++++++++\n");
         printf("  +  BENCHMARKING open()  +\n");
         printf("  +++++++++++++++++++++++++\n");
         printf(" - txn: within, without\n");
         printf(" - file: create, existing\n");
         printf("============================================\n");

         unsigned long none_create, none_existing;
         unsigned long txn_create, txn_existing;
         int count = 100;

         printf("- > txnless\n");
         printf("- - > creating file: "); fflush(stdout);
         none_create = multiopen(count, 0, 1);
         printf("%lds %ldns\n", none_create / NS, none_create % NS);
         printf("- - > existing file: "); fflush(stdout);
         none_existing = multiopen(count, 0, 0);
         printf("%lds %ldns\n", none_existing / NS, none_existing % NS);

         printf("- > txnl\n");
         printf("- - > creating file: "); fflush(stdout);
         txn_create = multiopen(count, 1, 1);
         printf("%lds %ldns (overhead: %fx)\n", txn_create / NS, txn_create % NS, (double) txn_create / none_create);
         printf("- - > existing file: "); fflush(stdout);
         txn_existing = multiopen(count, 1, 0);
         printf("%lds %ldns (overhead: %fx)\n", txn_existing / NS, txn_existing % NS, (double) txn_existing / none_existing);

         printf("============================================\n");
}

unsigned long multimkdir(int count, int txn)
{
        set_bypass(1);
        system("rm -rf out/mkdir");
        set_bypass(0);

        mkdir("out/mkdir", 0755);
        char *dirs[count]; // TODO: free later
        for (int i = 0; i < count; i++) {
                char *d = malloc(32);
                sprintf(d, "out/mkdir/%d", i);
                dirs[i] = d;
        }

        struct timeval start, finish;
        unsigned long runtime = 0;

        gettimeofday(&start, NULL);

        int txn_id = -1;
        if (txn)
                txn_id = begin_txn();

        for (int i = 0; i < count; i++)
                mkdir(dirs[i], 0755);

        if (txn)
                end_txn(txn_id);

        gettimeofday(&finish, NULL);
        runtime = time_passed(start, finish);

        return runtime / count;
}

void mkdirbench()
{
        /**
         * permutate:
         *  - within, without txn
         */
        printf("  ++++++++++++++++++++++++++\n");
        printf("  +  BENCHMARKING mkdir()  +\n");
        printf("  ++++++++++++++++++++++++++\n");
        printf(" - txn: within, without\n");
        printf("============================================\n");

        unsigned long none, txn;
        int count = 1000;

        printf("- > txnless: "); fflush(stdout);
        none = multimkdir(count, 0);
        printf("%lds %ldns\n", none / NS, none % NS);
        printf("- > txnl:    "); fflush(stdout);
        txn = multimkdir(count, 1);
        printf("%lds %ldns (overhead: %fx)\n", txn / NS, txn % NS, (double) txn / none);

        printf("============================================\n");
}

// returns average of all runs
unsigned long multiwrite(int buf_size, int txn, int count, int starting_size, int offset)
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

void writebench(int shrt, int lng, int small, int big)
{
        /**
         * permutate:
         *  - within/without txn
         *  - short/long sequence of fs ops
         *  - changing new/small/big file
         */

        printf("  ++++++++++++++++++++++++++\n");
        printf("  +  BENCHMARKING write()  +\n");
        printf("  ++++++++++++++++++++++++++\n");
        printf(" - txn: within, without\n");
        printf(" - fs ops: short -> %d, long -> %d\n", shrt, lng);
        printf(" - file: new -> %d, small -> %d KB, big -> %d GB\n", 0, small / 1024, big / (1024*1024*1024));
        printf("============================================\n");

        unsigned long none_short_new, none_short_small, none_short_big;
        unsigned long none_long_new, none_long_small, none_long_big;
        unsigned long txn_short_new, txn_short_small, txn_short_big;
        unsigned long txn_long_new, txn_long_small, txn_long_big;
        int buf_size = 64;

        printf("> txnless...\n");

        printf("- > short seqs of writes...\n");
        printf("- - > new file:   "); fflush(stdout);
        none_short_new = multiwrite(buf_size, 0, shrt, 0, 0);
        printf("%lds %ldns\n", none_short_new / NS, none_short_new % NS);
        printf("- - > small file: "); fflush(stdout);
        none_short_small = multiwrite(buf_size, 0, shrt, small, small - 128);
        printf("%lds %ldns\n", none_short_small / NS, none_short_small % NS);
        printf("- - > big file:   "); fflush(stdout);
        none_short_big = multiwrite(buf_size, 0, shrt, big, big * 1/4);
        printf("%lds %ldns\n", none_short_big / NS, none_short_big % NS);

        printf("- > long seqs of writes...\n");
        printf("- - > new file:   "); fflush(stdout);
        none_long_new = multiwrite(buf_size, 0, lng, 0, 0);
        printf("%lds %ldns\n", none_long_new / NS, none_long_new % NS);
        printf("- - > small file: "); fflush(stdout);
        none_long_small = multiwrite(buf_size, 0, lng, small, small - 128);
        printf("%lds %ldns\n", none_long_small / NS, none_long_small % NS);
        printf("- - > big file:   "); fflush(stdout);
        none_long_big = multiwrite(buf_size, 0, lng, big, big * 1/4);
        printf("%lds %ldns\n", none_long_big / NS, none_long_big % NS);

        printf("> txnl...\n");

        printf("- > short seqs of writes...\n");
        printf("- - > new file:   "); fflush(stdout);
        txn_short_new = multiwrite(buf_size, 1, shrt, 0, 0);
        printf("%lds %ldns (overhead: %fx)\n", txn_short_new / NS, txn_short_new % NS, (double) txn_short_new / none_short_new);
        printf("- - > small file: "); fflush(stdout);
        txn_short_small = multiwrite(buf_size, 1, shrt, small, small - 128);
        printf("%lds %ldns (overhead: %fx)\n", txn_short_small / NS, txn_short_small % NS, (double) txn_short_small / none_short_small);
        printf("- - > big file:   "); fflush(stdout);
        txn_short_big = multiwrite(buf_size, 1, shrt, big, big * 1/4);
        printf("%lds %ldns (overhead: %fx)\n", txn_short_big / NS, txn_short_big % NS, (double) txn_short_big / none_short_big);

        printf("- > long seqs of writes...\n");
        printf("- - > new file:   "); fflush(stdout);
        txn_long_new = multiwrite(buf_size, 1, lng, 0, 0);
        printf("%lds %ldns (overhead: %fx)\n", txn_long_new / NS, txn_long_new % NS, (double) txn_long_new / none_long_new);
        printf("- - > small file: "); fflush(stdout);
        txn_long_small = multiwrite(buf_size, 1, lng, small, small - 128);
        printf("%lds %ldns (overhead: %fx)\n", txn_long_small / NS, txn_long_small % NS, (double) txn_long_small / none_long_small);
        printf("- - > big file:   "); fflush(stdout);
        txn_long_big = multiwrite(buf_size, 1, lng, big, big * 1/4);
        printf("%lds %ldns (overhead: %fx)\n", txn_long_big / NS, txn_long_big % NS, (double) txn_long_big / none_long_big);

        printf("============================================\n");
}

int main()
{
        /**
         * 0 -> open
         * 1 -> mkdir
         * 2 -> rename
         * 3 -> remove
         * 4 -> write
         */
        int op = 1;

        if (op == 0)
                openbench();
        else if (op == 1)
                mkdirbench();
        else if (op == 2)
                renamebench();
        else if (op == 3)
                removebench();
        else if (op == 4)
                writebench(SHORT, LONG, SMALL, BIG);
}
