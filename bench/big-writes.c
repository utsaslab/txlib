#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include "txnlib.h"

#define GB 1073741824
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
        char noise[65536];
        int fd = open("/dev/random", O_RDONLY);
        read(fd, noise, sizeof(noise));
        close(fd);

        int id = begin_txn();

        int fd1 = open("out/big-writes.out", O_CREAT | O_TRUNC | O_RDWR, 0644);
        for (int i = 0; i < GB / sizeof(noise); i++)
                write(fd1, noise, sizeof(noise));

        save_log(NULL);
        end_txn(id);

        // set up to redo
        set_bypass(1);
        remove("out/big-writes.out");
        set_bypass(0);

        struct timeval start, finish;
        gettimeofday(&start, NULL);
        redo();
        gettimeofday(&finish, NULL);

	unsigned long runtime = time_passed(start, finish);
        printf("redo() after writing 1 GB: %lds %ldns\n", runtime / 1000000000, runtime % 1000000000);
}
