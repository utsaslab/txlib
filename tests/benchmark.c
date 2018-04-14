#include <errno.h>
#include <fcntl.h>
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

// durability: 0 -> none, 1 -> fsync, 2 -> txn, 3 -> txn
unsigned long multiwrite(int buf_size, int count, int durability, int overwrite, int length)
{
        if (overwrite) {
                char cmd[1024];
                sprintf(cmd, "dd if=/dev/zero of=%s count=%d bs=%d > /dev/null 2>&1", working_file, GB / 1024, 1024);
                set_bypass(1);
                system(cmd);
                set_bypass(0);
        }

        char buf[buf_size];
        memset(buf, '1', buf_size);
        unsigned long runtime = 0;

        for (int i = 0; i < count; i++) {
                // if (i % 10 == 0) {
                //         printf("%d ", i);
                //         fflush(stdout);
                // }

                struct timeval start, finish;
                if (!overwrite)
                        remove(working_file);

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

        int shrt = 10, lng = 1000;

        printf("  ++++++++++++++++++++++++++\n");
        printf("  +  BENCHMARKING write()  +\n");
        printf("  ++++++++++++++++++++++++++\n");
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
        int buf_size = 128;
        int count = 20;

        printf("> in memory...\n");

        printf("- > append...\n");
        printf("- - > single: "); fflush(stdout);
        mem_ap_single = multiwrite(buf_size, count, 0, 0, 1);
        printf("%02lds %09ldns\n", mem_ap_single / NS, mem_ap_single % NS);
        printf("- - > short:  "); fflush(stdout);
        mem_ap_short = multiwrite(buf_size, count, 0, 0, shrt);
        printf("%02lds %09ldns\n", mem_ap_short / NS, mem_ap_short % NS);
        printf("- - > long:   "); fflush(stdout);
        mem_ap_long = multiwrite(buf_size, count, 0, 0, lng);
        printf("%02lds %09ldns\n", mem_ap_long / NS, mem_ap_long % NS);
        printf("- > overwrite...\n");
        printf("- - > single: "); fflush(stdout);
        mem_ow_single = multiwrite(buf_size, count, 0, 1, 1);
        printf("%02lds %09ldns\n", mem_ow_single / NS, mem_ow_single % NS);
        printf("- - > short:  "); fflush(stdout);
        mem_ow_short = multiwrite(buf_size, count, 0, 1, shrt);
        printf("%02lds %09ldns\n", mem_ow_short / NS, mem_ow_short % NS);
        printf("- - > long:   "); fflush(stdout);
        mem_ow_long = multiwrite(buf_size, count, 0, 1, lng);
        printf("%02lds %09ldns\n", mem_ow_long / NS, mem_ow_long % NS);

        printf("> ending fsync()...\n");

        printf("- > append...\n");
        printf("- - > single: "); fflush(stdout);
        fsync_ap_single = multiwrite(buf_size, count, 1, 0, 1);
        printf("%02lds %09ldns\n", fsync_ap_single / NS, fsync_ap_single % NS);
        printf("- - > short:  "); fflush(stdout);
        fsync_ap_short = multiwrite(buf_size, count, 1, 0, shrt);
        printf("%02lds %09ldns\n", fsync_ap_short / NS, fsync_ap_short % NS);
        printf("- - > long:   "); fflush(stdout);
        fsync_ap_long = multiwrite(buf_size, count, 1, 0, lng);
        printf("%02lds %09ldns\n", fsync_ap_long / NS, fsync_ap_long % NS);
        printf("- > overwrite...\n");
        printf("- - > single: "); fflush(stdout);
        fsync_ow_single = multiwrite(buf_size, count, 1, 1, 1);
        printf("%02lds %09ldns\n", fsync_ow_single / NS, fsync_ow_single % NS);
        printf("- - > short:  "); fflush(stdout);
        fsync_ow_short = multiwrite(buf_size, count, 1, 1, shrt);
        printf("%02lds %09ldns\n", fsync_ow_short / NS, fsync_ow_short % NS);
        printf("- - > long:   "); fflush(stdout);
        fsync_ow_long = multiwrite(buf_size, count, 1, 1, lng);
        printf("%02lds %09ldns\n", fsync_ow_long / NS, fsync_ow_long % NS);

        printf("> transactional...\n");

        printf("- > append...\n");
        printf("- - > single: "); fflush(stdout);
        txn_ap_single = multiwrite(buf_size, count, 2, 0, 1);
        printf("%02lds %09ldns (overhead: mem -> %4.2fx, fsync -> %4.2fx)\n", txn_ap_single / NS, txn_ap_single % NS, (double) txn_ap_single / mem_ap_single, (double) txn_ap_single / fsync_ap_single);
        printf("- - > short:  "); fflush(stdout);
        txn_ap_short = multiwrite(buf_size, count, 2, 0, shrt);
        printf("%02lds %09ldns (overhead: mem -> %4.2fx, fsync -> %4.2fx)\n", txn_ap_short / NS, txn_ap_short % NS, (double) txn_ap_short / mem_ap_short, (double) txn_ap_short / fsync_ap_short);
        printf("- - > long:   "); fflush(stdout);
        txn_ap_long = multiwrite(buf_size, count, 2, 0, lng);
        printf("%02lds %09ldns (overhead: mem -> %4.2fx, fsync -> %4.2fx)\n", txn_ap_long / NS, txn_ap_long % NS, (double) txn_ap_long / mem_ap_long, (double) txn_ap_long / fsync_ap_long);
        printf("- > overwrite...\n");
        printf("- - > single: "); fflush(stdout);
        txn_ow_single = multiwrite(buf_size, count, 2, 1, 1);
        printf("%02lds %09ldns (overhead: mem -> %4.2fx, fsync -> %4.2fx)\n", txn_ow_single / NS, txn_ow_single % NS, (double) txn_ow_single / mem_ow_single, (double) txn_ow_single / fsync_ow_single);
        printf("- - > short:  "); fflush(stdout);
        txn_ow_short = multiwrite(buf_size, count, 2, 1, shrt);
        printf("%02lds %09ldns (overhead: mem -> %4.2fx, fsync -> %4.2fx)\n", txn_ow_short / NS, txn_ow_short % NS, (double) txn_ow_short / mem_ow_short, (double) txn_ow_short / fsync_ow_short);
        printf("- - > long:   "); fflush(stdout);
        txn_ow_long = multiwrite(buf_size, count, 2, 1, lng);
        printf("%02lds %09ldns (overhead: mem -> %4.2fx, fsync -> %4.2fx)\n", txn_ow_long / NS, txn_ow_long % NS, (double) txn_ow_long / mem_ow_long, (double) txn_ow_long / fsync_ow_long);

        printf("============================================\n");
}

// https://stackoverflow.com/questions/2180079/how-can-i-copy-a-file-on-unix-using-c
int cp(const char *to, const char *from)
{
    int fd_to, fd_from;
    char buf[4096];
    ssize_t nread;
    int saved_errno;

    fd_from = open(from, O_RDONLY);
    if (fd_from < 0)
        return -1;

    fd_to = open(to, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd_to < 0)
        goto out_error;

    while (nread = read(fd_from, buf, sizeof buf), nread > 0)
    {
        char *out_ptr = buf;
        ssize_t nwritten;

        do {
            nwritten = write(fd_to, out_ptr, nread);

            if (nwritten >= 0)
            {
                nread -= nwritten;
                out_ptr += nwritten;
            }
            else if (errno != EINTR)
            {
                goto out_error;
            }
        } while (nread > 0);
    }

    if (nread == 0)
    {
        if (close(fd_to) < 0)
        {
            fd_to = -1;
            goto out_error;
        }
        close(fd_from);

        /* Success! */
        return 0;
    }

  out_error:
    saved_errno = errno;

    close(fd_from);
    if (fd_to >= 0)
        close(fd_to);

    errno = saved_errno;
    return -1;
}

unsigned long multiswap(int buf_size, int count, int txn, int filesize, int writes)
{
        char cmd[1024];
        sprintf(cmd, "dd if=/dev/zero of=%s count=%d bs=%d > /dev/null 2>&1", working_file, filesize / 1024, 1024);
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
                        txn_id = begin_txn();
                        fd = open(working_file, O_RDWR);
                } else {
                        cp(working_backup, working_file);
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
        int small = 4 * KB;
        int medium = 4 * MB;
        int large = 1 * GB;

        printf("  ++++++++++++++++++++++++++\n");
        printf("  +  Testing alternatives  +\n");
        printf("  ++++++++++++++++++++++++++\n");
        printf(" - method: swap, txn\n");
        printf(" - filesize: small -> %d, medium -> %d, large -> %d\n", small, medium, large);
        printf("============================================\n");

        unsigned long swap_small, swap_medium, swap_large;
        unsigned long txn_small, txn_medium, txn_large;
        int buf_size = 512;
        int count = 25;

        printf("> swap...\n");

        printf("- > small:  "); fflush(stdout);
        swap_small = multiswap(buf_size, count, 0, small, 2);
        printf("%02lds %09ldns\n", swap_small / NS, swap_small % NS);
        printf("- > medium: "); fflush(stdout);
        swap_medium = multiswap(buf_size, count, 0, medium, 8);
        printf("%02lds %09ldns\n", swap_medium / NS, swap_medium % NS);
        printf("- > large:  "); fflush(stdout);
        swap_large = multiswap(buf_size, count, 0, large, 32);
        printf("%02lds %09ldns\n", swap_large / NS, swap_large % NS);

        printf("> txn...\n");

        printf("- > small:  "); fflush(stdout);
        txn_small = multiswap(buf_size, count, 1, small, 2);
        printf("%02lds %09ldns (overhead: %4.2fx)\n", txn_small / NS, txn_small % NS, (double) txn_small / swap_small);
        printf("- > medium: "); fflush(stdout);
        txn_medium = multiswap(buf_size, count, 1, medium, 8);
        printf("%02lds %09ldns (overhead: %4.2fx)\n", txn_medium / NS, txn_medium % NS, (double) txn_medium / swap_medium);
        printf("- > large:  "); fflush(stdout);
        txn_large = multiswap(buf_size, count, 1, large, 32);
        printf("%02lds %09ldns (overhead: %4.2fx)\n", txn_large / NS, txn_large % NS, (double) txn_large / swap_large);

        printf("============================================\n");
}

int main()
{
        /**
         * 0 -> open
         * 1 -> remove
         * 2 -> write
         * 3 -> swap
         */
        int op = 3;

        if (op == 0)
                openbench();
        else if (op == 1)
                removebench();
        else if (op == 2)
                writebench();
        else if (op == 3)
                swapbench();
        else
                printf("nothing tested\n");
}
