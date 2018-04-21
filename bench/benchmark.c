#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "txnlib.h"

#define KB 1024
#define MB 1048576
#define GB 1073741824
#define NS 1000000000

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

unsigned long multiremove(int count, int txn, int file)
{
        mkdir("out/remove", 0755);
        char *paths[count];
        for (int i = 0; i < count; i++) {
                char *path = malloc(1024); // TODO: free later
                sprintf(path, "out/remove/%d", i);
                paths[i] = path;

                if (file)
                        close(open(path, O_CREAT, 0644));
                else
                        mkdir(path, 0755);
        }

        struct timeval start, finish;
        {
                gettimeofday(&start, NULL);
                int txn_id = -1;
                if (txn)
                        txn_id = begin_txn();

                for (int i = 0; i < count; i++) {
                        // printf("i: %d\n", i);
                        remove(paths[i]);
                }

                if (txn)
                        end_txn(txn_id);
                gettimeofday(&finish, NULL);
        }

        for (int i = 0; i < count; i++)
                free(paths[i]);

        return time_passed(start, finish) / count;
}

// keep this as benchmark for super simple endpoint
void removebench()
{
        /**
         * permutate:
         *  - within/without txn
         *  - file/directory
         */
        int count = 100000;

        printf("  +++++++++++++++++++++++++++\n");
        printf("  +  BENCHMARKING remove()  +\n");
        printf("  +++++++++++++++++++++++++++\n");
        printf(" - count: %d\n", count);
        printf(" - txn: within, without\n");
        printf(" - type: file, directory\n");
        printf("============================================\n");

        unsigned long none_file, none_dir;
        unsigned long txn_file, txn_dir;

        printf("- > txnless:\n");
        printf("- - > file: "); fflush(stdout);
        none_file = multiremove(count, 0, 1);
        printf("%2lds %9ldns\n", none_file / NS, none_file % NS);
        printf("- - > dir:  "); fflush(stdout);
        none_dir = multiremove(count, 0, 0);
        printf("%2lds %9ldns\n", none_dir / NS, none_dir % NS);

        printf("- > txnl:\n");
        printf("- - > file: "); fflush(stdout);
        txn_file = multiremove(count, 1, 1);
        printf("%2lds %9ldns (overhead: %7.2fx)\n", txn_file / NS, txn_file % NS, (double) txn_file / none_file);
        printf("- - > dir:  "); fflush(stdout);
        txn_dir = multiremove(count, 1, 0);
        printf("%2lds %9ldns (overhead: %7.2fx)\n", txn_dir / NS, txn_dir % NS, (double) txn_dir / none_dir);

        printf("============================================\n");
}

// durability: 0 -> none, 1 -> fsync, 2 -> txn
unsigned long multiwrite(int buf_size, int count, int durability, int overwrite, int length)
{
        if (overwrite) {
                char cmd[1024];
                sprintf(cmd, "dd if=/dev/zero of=%s count=%d bs=%d > /dev/null 2>&1", working_file, GB / 1024, 1024);
                set_bypass(1);
                system(cmd);
                set_bypass(0);
        }

        unsigned long runtime = 0;
        for (int i = 0; i < count; i++) {
                struct timeval start, finish;
                if (!overwrite)
                        remove(working_file);

                // change the buffer each time just in case
                char buf[buf_size];
                memset(buf, i+'0', buf_size);

                int fd = open(working_file, O_CREAT | O_RDWR, 0644);
                {
                        gettimeofday(&start, NULL);
                        int txn_id = -1;
                        if (durability == 2)
                                txn_id = begin_txn();

                        for (int j = 0; j < length; j++)
                                write(fd, buf, buf_size);

                        if (durability == 1)
                                fsync(fd);

                        if (durability == 2)
                                end_txn(txn_id);
                        gettimeofday(&finish, NULL);

                }
                close(fd);
                runtime += time_passed(start, finish);
        }

        return runtime / count;
}

void writebench()
{
        /**
         * permutate:
         *  - memory/fsync/txn writes
         *  - overwrite/append to file
         *  - single/short/long sequence of fs ops
         */

        int shrt = 10, lng = 100;
        int buf_size = 128;
        int count = 10000;

        printf("  ++++++++++++++++++++++++++\n");
        printf("  +  BENCHMARKING write()  +\n");
        printf("  ++++++++++++++++++++++++++\n");
        printf(" - count: %d\n", count);
        printf(" - fs ops: memory, fsync, txn\n");
        printf(" - file: append, overwrite\n");
        printf(" - length: single -> %d, short -> %d, long -> %d\n", 1, shrt, lng);
        printf("============================================\n");

        unsigned long mem_ap_single, mem_ap_short, mem_ap_long;
        unsigned long mem_ow_single, mem_ow_short, mem_ow_long;
        unsigned long fsync_ap_single, fsync_ap_short, fsync_ap_long;
        unsigned long fsync_ow_single, fsync_ow_short, fsync_ow_long;
        unsigned long txn_ap_single, txn_ap_short, txn_ap_long;
        unsigned long txn_ow_single, txn_ow_short, txn_ow_long;

        printf("> in memory...\n");

        printf("- > append...\n");
        printf("- - > single: "); fflush(stdout);
        mem_ap_single = multiwrite(buf_size, count, 0, 0, 1);
        printf("%2lds %9ldns\n", mem_ap_single / NS, mem_ap_single % NS);
        printf("- - > short:  "); fflush(stdout);
        mem_ap_short = multiwrite(buf_size, count, 0, 0, shrt);
        printf("%2lds %9ldns\n", mem_ap_short / NS, mem_ap_short % NS);
        printf("- - > long:   "); fflush(stdout);
        mem_ap_long = multiwrite(buf_size, count, 0, 0, lng);
        printf("%2lds %9ldns\n", mem_ap_long / NS, mem_ap_long % NS);
        printf("- > overwrite...\n");
        printf("- - > single: "); fflush(stdout);
        mem_ow_single = multiwrite(buf_size, count, 0, 1, 1);
        printf("%2lds %9ldns\n", mem_ow_single / NS, mem_ow_single % NS);
        printf("- - > short:  "); fflush(stdout);
        mem_ow_short = multiwrite(buf_size, count, 0, 1, shrt);
        printf("%2lds %9ldns\n", mem_ow_short / NS, mem_ow_short % NS);
        printf("- - > long:   "); fflush(stdout);
        mem_ow_long = multiwrite(buf_size, count, 0, 1, lng);
        printf("%2lds %9ldns\n", mem_ow_long / NS, mem_ow_long % NS);

        printf("> ending fsync()...\n");

        printf("- > append...\n");
        printf("- - > single: "); fflush(stdout);
        fsync_ap_single = multiwrite(buf_size, count, 1, 0, 1);
        printf("%2lds %9ldns\n", fsync_ap_single / NS, fsync_ap_single % NS);
        printf("- - > short:  "); fflush(stdout);
        fsync_ap_short = multiwrite(buf_size, count, 1, 0, shrt);
        printf("%2lds %9ldns\n", fsync_ap_short / NS, fsync_ap_short % NS);
        printf("- - > long:   "); fflush(stdout);
        fsync_ap_long = multiwrite(buf_size, count, 1, 0, lng);
        printf("%2lds %9ldns\n", fsync_ap_long / NS, fsync_ap_long % NS);
        printf("- > overwrite...\n");
        printf("- - > single: "); fflush(stdout);
        fsync_ow_single = multiwrite(buf_size, count, 1, 1, 1);
        printf("%2lds %9ldns\n", fsync_ow_single / NS, fsync_ow_single % NS);
        printf("- - > short:  "); fflush(stdout);
        fsync_ow_short = multiwrite(buf_size, count, 1, 1, shrt);
        printf("%2lds %9ldns\n", fsync_ow_short / NS, fsync_ow_short % NS);
        printf("- - > long:   "); fflush(stdout);
        fsync_ow_long = multiwrite(buf_size, count, 1, 1, lng);
        printf("%2lds %9ldns\n", fsync_ow_long / NS, fsync_ow_long % NS);

        printf("> transactional...\n");

        printf("- > append...\n");
        printf("- - > single: "); fflush(stdout);
        txn_ap_single = multiwrite(buf_size, count, 2, 0, 1);
        printf("%2lds %9ldns (overhead: mem -> %7.2fx, fsync -> %7.2fx)\n", txn_ap_single / NS, txn_ap_single % NS, (double) txn_ap_single / mem_ap_single, (double) txn_ap_single / fsync_ap_single);
        printf("- - > short:  "); fflush(stdout);
        txn_ap_short = multiwrite(buf_size, count, 2, 0, shrt);
        printf("%2lds %9ldns (overhead: mem -> %7.2fx, fsync -> %7.2fx)\n", txn_ap_short / NS, txn_ap_short % NS, (double) txn_ap_short / mem_ap_short, (double) txn_ap_short / fsync_ap_short);
        printf("- - > long:   "); fflush(stdout);
        txn_ap_long = multiwrite(buf_size, count / 10, 2, 0, lng);
        printf("%2lds %9ldns (overhead: mem -> %7.2fx, fsync -> %7.2fx)\n", txn_ap_long / NS, txn_ap_long % NS, (double) txn_ap_long / mem_ap_long, (double) txn_ap_long / fsync_ap_long);
        printf("- > overwrite...\n");
        printf("- - > single: "); fflush(stdout);
        txn_ow_single = multiwrite(buf_size, count, 2, 1, 1);
        printf("%2lds %9ldns (overhead: mem -> %7.2fx, fsync -> %7.2fx)\n", txn_ow_single / NS, txn_ow_single % NS, (double) txn_ow_single / mem_ow_single, (double) txn_ow_single / fsync_ow_single);
        printf("- - > short:  "); fflush(stdout);
        txn_ow_short = multiwrite(buf_size, count, 2, 1, shrt);
        printf("%2lds %9ldns (overhead: mem -> %7.2fx, fsync -> %7.2fx)\n", txn_ow_short / NS, txn_ow_short % NS, (double) txn_ow_short / mem_ow_short, (double) txn_ow_short / fsync_ow_short);
        printf("- - > long:   "); fflush(stdout);
        txn_ow_long = multiwrite(buf_size, count / 10, 2, 1, lng);
        printf("%2lds %9ldns (overhead: mem -> %7.2fx, fsync -> %7.2fx)\n", txn_ow_long / NS, txn_ow_long % NS, (double) txn_ow_long / mem_ow_long, (double) txn_ow_long / fsync_ow_long);

        printf("============================================\n");
}

void copy(const char *dest, const char *src)
{
	FILE *d = fopen(dest, "w");
	FILE *s = fopen(src, "r");
	char c;

	while ( (c = fgetc(s)) != EOF )
		fputc(c, d);

	fclose(d);
	fclose(s);
}

unsigned long multiswap(int buf_size, int count, int txn, unsigned long filesize, int writes)
{
        char cmd[1024];
        sprintf(cmd, "dd if=/dev/zero of=%s count=%ld bs=%d > /dev/null 2>&1", working_file, filesize / 1024, 1024);
        set_bypass(1);
        system(cmd);
        set_bypass(0);

        char buf[buf_size];
        memset(buf, '1', buf_size);
        unsigned long runtime = 0;

        for (int i = 0; i < count; i++) {
                struct timeval start, finish;

                gettimeofday(&start, NULL);
                int fd = -1, txn_id = -1;
                if (txn) {
                        fd = open(working_file, O_RDWR); // TODO: issue with close(fd) after end_txn()
                        txn_id = begin_txn();
                } else {
                        copy(working_backup, working_file);
                        fd = open(working_backup, O_RDWR);
                }

                for (int j = 0; j < writes; j++) {
                        lseek(fd, j*(filesize / writes), SEEK_SET);
                        write(fd, buf, buf_size);
                }

                if (txn) {
                        end_txn(txn_id);
                } else {
                        fsync(fd);
                        rename(working_backup, working_file);
                }
                close(fd);
                gettimeofday(&finish, NULL);
                runtime += time_passed(start, finish);
        }
        return runtime / count;
}

void swapbench()
{
        // compare to copy-write-then-rename method
        int count = 50;
        int max_buf_size = 4 * KB;
        int range = 25; // max 2^30 filesize (start at 6)
        unsigned long filesizes[range];
        for (int i = 0; i < range; i++)
                filesizes[i] = (unsigned long) pow(2, 6+i);

        printf("  ++++++++++++++++++++++++++\n");
        printf("  +  Testing alternatives  +\n");
        printf("  ++++++++++++++++++++++++++\n");
        printf(" - count: %d\n", count);
        printf(" - method: swap, txn\n");
        printf(" - filesize: multiples of 2 from %ld to %ld\n", filesizes[0], filesizes[range-1]);
        printf("============================================\n");

        unsigned long swap_times[range];
        unsigned long txn_times[range];

        printf("> swap...\n");

        for (int i = 0; i < range; i++) {
                printf("- > 2^%2d: ", i+6); fflush(stdout);
                int buf_size = filesizes[i] / 16;
                if (buf_size > max_buf_size)
                        buf_size = max_buf_size;
                swap_times[i] = multiswap(buf_size, count, 0, filesizes[i], 16);
                printf("%2lds %9ldns\n", swap_times[i] / NS, swap_times[i] % NS);
        }

        printf("> txn...\n");

        for (int i = 0; i < range; i++) {
                printf("- > 2^%2d: ", i+6); fflush(stdout);
                int buf_size = filesizes[i] / 16;
                if (buf_size > max_buf_size)
                        buf_size = max_buf_size;
                txn_times[i] = multiswap(buf_size, count, 1, filesizes[i], 8);
                printf("%2lds %9ldns (overhead: %6.3fx)\n", txn_times[i] / NS, txn_times[i] % NS, (double) txn_times[i] / swap_times[i]);
        }
}

int main()
{
        /**
         * 0 -> remove
         * 1 -> write
         * 2 -> swap
         */
        int op = 2;

        if (op == 0)
                removebench();
        else if (op == 1)
                writebench();
        else if (op == 2)
                swapbench();
        else
                printf("nothing tested\n");
}
