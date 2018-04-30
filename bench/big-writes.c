#include <fcntl.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "txnlib.h"

#define WRITE_SIZE 8192

// nanoseconds
unsigned long time_passed(struct timeval start, struct timeval finish)
{
        long unsigned passed = 0;
        passed += 1000000000 * (finish.tv_sec - start.tv_sec);
        passed += 1000 * (finish.tv_usec - start.tv_usec);
        return passed;
}

int main()
{
        // make random noise
        char noise[WRITE_SIZE];
        int fd = open("/dev/random", O_RDONLY);
        read(fd, noise, sizeof(noise));
        close(fd);

        // testing parameters
        int start_pow = 15;
        int range = 20;
        unsigned long filesizes[range];
        for (int i = 0; i < range; i++)
                filesizes[i] = (unsigned long) pow(2, start_pow + i);

        printf("  ++++++++++++++++++++++++++++++++++\n");
        printf("  +  Benchmarking redo() runtimes  +\n");
        printf("  ++++++++++++++++++++++++++++++++++\n");
        printf(" - filesize (in bytes): multiples of 2 from %ld to %ld\n", filesizes[0], filesizes[range-1]);
        printf("============================================\n");

        for (int i = 0; i < range; i++) {
                printf("> 2^%2d: ", i + start_pow); fflush(stdout);

                // generate and save redo log
                int id = begin_txn();
                int fd1 = open("out/big-writes.out", O_CREAT | O_TRUNC | O_RDWR, 0644);
                int idk = filesizes[i] / WRITE_SIZE;
                for (int i = 0; i < idk; i++)
                        write(fd1, noise, sizeof(noise));
                save_log(NULL);
                end_txn(id);

                // reset for redo()
                set_bypass(1);
		ftruncate(fd1, 0); // to be safe
		fsync(fd1);
                remove("out/big-writes.out");
                close(fd1);
                int dir = open("out", O_DIRECTORY);
                fsync(dir);
                close(dir);
                set_bypass(0);

                // time redo()
                struct timeval start, finish;
                gettimeofday(&start, NULL);
                redo();
                gettimeofday(&finish, NULL);

                unsigned long runtime = time_passed(start, finish);
                printf("%2lds %9ldns\n", runtime / 1000000000, runtime % 1000000000);
        }
}
