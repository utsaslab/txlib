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

        set_bypass(1);
        system("rm -f out/open/*");
        set_bypass(0);

        if (!create)
                for (int i = 0; i < count; i++)
                        close(open(files[i], O_CREAT, 0644));

        int flags = O_RDWR;
        if (create)
                flags |= O_CREAT;
        int first, last;

        {
                gettimeofday(&start, NULL);
                int txn_id = -1;
                if (txn)
                        txn_id = begin_txn();

                first = open(files[0], flags, 0644);
                for (int i = 1; i < count; i++)
                        last = open(files[i], flags, 0644);

                if (txn)
                        end_txn(txn_id);
                gettimeofday(&finish, NULL);
        }

        for (int i = first; i <= last; i++)
                close(i);

        return time_passed(start, finish) / count;
}

void openbench()
{
        /**
         * permutate:
         *  - within/without txn
         *  - creating/existing file
         */

         printf("  +++++++++++++++++++++++++\n");
         printf("  +  BENCHMARKING open()  +\n");
         printf("  +++++++++++++++++++++++++\n");
         printf(" - txn: within, without\n");
         printf(" - file: create, existing\n");
         printf("============================================\n");

         unsigned long none_create, none_existing;
         unsigned long txn_create, txn_existing;
         int count = 1000;

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
        {
                gettimeofday(&start, NULL);
                int txn_id = -1;
                if (txn)
                        txn_id = begin_txn();

                for (int i = 0; i < count; i++)
                        mkdir(dirs[i], 0755);

                if (txn)
                        end_txn(txn_id);

                gettimeofday(&finish, NULL);
        }

        return time_passed(start, finish) / count;
}

void mkdirbench()
{
        /**
         * permutate:
         *  - within/without txn
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

unsigned long multirename(int count, int txn)
{
        mkdir("out/rename", 0755);
        char even[64], odd[64];
        sprintf(even, "out/rename/even.txt");
        sprintf(odd, "out/rename/odd.txt");
        close(open(even, O_CREAT, 0644));

        struct timeval start, finish;
        {
                gettimeofday(&start, NULL);
                int txn_id = -1;
                if (txn)
                        txn_id = begin_txn();

                for (int i = 0; i < count; i++) {
                        if (i % 2 == 0)
                                rename(even, odd);
                        else
                                rename(odd, even);
                }

                if (txn)
                        end_txn(txn_id);

                gettimeofday(&finish, NULL);
        }

        return time_passed(start, finish) / count;
}

void renamebench()
{
        /**
         * permutate:
         *  - within/without txn
         */
        printf("  +++++++++++++++++++++++++++\n");
        printf("  +  BENCHMARKING rename()  +\n");
        printf("  +++++++++++++++++++++++++++\n");
        printf(" - txn: within, without\n");
        printf("============================================\n");

        unsigned long none, txn;
        int count = 10000;

        printf("- > txnless: "); fflush(stdout);
        none = multirename(count, 0);
        printf("%lds %ldns\n", none / NS, none % NS);
        printf("- > txnl:    "); fflush(stdout);
        txn = multirename(count, 1);
        printf("%lds %ldns (overhead: %fx)\n", txn / NS, txn % NS, (double) txn / none);

        printf("============================================\n");
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

                for (int i = 0; i < count; i++)
                        remove(paths[i]);

                if (txn)
                        end_txn(txn_id);
                gettimeofday(&finish, NULL);
        }

        return time_passed(start, finish) / count;
}

void removebench()
{
        /**
         * permutate:
         *  - within/without txn
         *  - file/directory
         */
        printf("  +++++++++++++++++++++++++++\n");
        printf("  +  BENCHMARKING remove()  +\n");
        printf("  +++++++++++++++++++++++++++\n");
        printf(" - txn: within, without\n");
        printf(" - type: file, directory\n");
        printf("============================================\n");

        unsigned long none_file, none_dir;
        unsigned long txn_file, txn_dir;
        int count = 1000;

        printf("- > txnless:\n");
        printf("- - > file: "); fflush(stdout);
        none_file = multiremove(count, 0, 1);
        printf("%lds %ldns\n", none_file / NS, none_file % NS);
        printf("- - > dir:  "); fflush(stdout);
        none_dir = multiremove(count, 0, 0);
        printf("%lds %ldns\n", none_dir / NS, none_dir % NS);

        printf("- > txnl:\n");
        printf("- - > file: "); fflush(stdout);
        txn_file = multiremove(count, 1, 1);
        printf("%lds %ldns (overhead: %fx)\n", txn_file / NS, txn_file % NS, (double) txn_file / none_file);
        printf("- - > dir:  "); fflush(stdout);
        txn_dir = multiremove(count, 1, 0);
        printf("%lds %ldns (overhead: %fx)\n", txn_dir / NS, txn_dir % NS, (double) txn_dir / none_dir);

        printf("============================================\n");
}

unsigned long multiwrite(int buf_size, int count, int txn, int length, int starting_size, int offset)
{
        char cmd[1024];
        sprintf(cmd, "dd if=/dev/zero of=%s count=%d bs=%d > /dev/null 2>&1", working_backup, starting_size / 1024, 1024);
        set_bypass(1);
        system(cmd);
        set_bypass(0);

        if (starting_size) {
                char copy[1024];
                sprintf(copy, "cp %s %s", working_backup, working_file);
                set_bypass(1);
                system(copy);
                set_bypass(0);
        } else {
                remove(working_file);
        }


        char buf[buf_size];
        memset(buf, '>', buf_size);
        struct timeval start, finish;

        int fd = open(working_file, O_CREAT | O_RDWR, 0644);
        {
                gettimeofday(&start, NULL);
                int txn_id = -1;
                if (txn)
                        txn_id = begin_txn();

                lseek(fd, offset, SEEK_SET);
                for (int i = 0; i < count; i++)
                        for (int j = 0; j < length; j++)
                                write(fd, buf, buf_size);

                if (txn)
                        end_txn(txn_id);
                gettimeofday(&finish, NULL);
        }
        close(fd);

        return time_passed(start, finish) / count;
}

void writebench()
{
        /**
         * permutate:
         *  - within/without txn
         *  - short/long sequence of fs ops
         *  - changing new/small/big file
         */

        int shrt = 10, lng = 1000;
        int small = 4096, big = 1073741824;

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
        int count = 50;

        printf("> txnless...\n");

        printf("- > short seqs of writes...\n");
        printf("- - > new file:   "); fflush(stdout);
        none_short_new = multiwrite(buf_size, count, 0, shrt, 0, 0);
        printf("%lds %ldns\n", none_short_new / NS, none_short_new % NS);
        printf("- - > small file: "); fflush(stdout);
        none_short_small = multiwrite(buf_size, count, 0, shrt, small, small - 128);
        printf("%lds %ldns\n", none_short_small / NS, none_short_small % NS);
        printf("- - > big file:   "); fflush(stdout);
        none_short_big = multiwrite(buf_size, count, 0, shrt, big, big * 1/4);
        printf("%lds %ldns\n", none_short_big / NS, none_short_big % NS);

        printf("- > long seqs of writes...\n");
        printf("- - > new file:   "); fflush(stdout);
        none_long_new = multiwrite(buf_size, count, 0, lng, 0, 0);
        printf("%lds %ldns\n", none_long_new / NS, none_long_new % NS);
        printf("- - > small file: "); fflush(stdout);
        none_long_small = multiwrite(buf_size, count, 0, lng, small, small - 128);
        printf("%lds %ldns\n", none_long_small / NS, none_long_small % NS);
        printf("- - > big file:   "); fflush(stdout);
        none_long_big = multiwrite(buf_size, count, 0, lng, big, big * 1/4);
        printf("%lds %ldns\n", none_long_big / NS, none_long_big % NS);

        printf("> txnl...\n");

        printf("- > short seqs of writes...\n");
        printf("- - > new file:   "); fflush(stdout);
        txn_short_new = multiwrite(buf_size, count, 1, shrt, 0, 0);
        printf("%lds %ldns (overhead: %fx)\n", txn_short_new / NS, txn_short_new % NS, (double) txn_short_new / none_short_new);
        printf("- - > small file: "); fflush(stdout);
        txn_short_small = multiwrite(buf_size, count, 1, shrt, small, small - 128);
        printf("%lds %ldns (overhead: %fx)\n", txn_short_small / NS, txn_short_small % NS, (double) txn_short_small / none_short_small);
        printf("- - > big file:   "); fflush(stdout);
        txn_short_big = multiwrite(buf_size, count, 1, shrt, big, big * 1/4);
        printf("%lds %ldns (overhead: %fx)\n", txn_short_big / NS, txn_short_big % NS, (double) txn_short_big / none_short_big);

        printf("- > long seqs of writes...\n");
        printf("- - > new file:   "); fflush(stdout);
        txn_long_new = multiwrite(buf_size, count, 1, lng, 0, 0);
        printf("%lds %ldns (overhead: %fx)\n", txn_long_new / NS, txn_long_new % NS, (double) txn_long_new / none_long_new);
        printf("- - > small file: "); fflush(stdout);
        txn_long_small = multiwrite(buf_size, count, 1, lng, small, small - 128);
        printf("%lds %ldns (overhead: %fx)\n", txn_long_small / NS, txn_long_small % NS, (double) txn_long_small / none_long_small);
        printf("- - > big file:   "); fflush(stdout);
        txn_long_big = multiwrite(buf_size, count, 1, lng, big, big * 1/4);
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
        int op = 0;

        if (op == 0)
                openbench();
        else if (op == 1)
                mkdirbench();
        else if (op == 2)
                renamebench();
        else if (op == 3)
                removebench();
        else if (op == 4)
                writebench();
}
