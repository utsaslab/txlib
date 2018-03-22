#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "txnlib.h"

#define ATOM 50000

char noise[ATOM];

void spam_write()
{
        int fd = open("out/spam.txt", O_CREAT | O_RDWR | O_EXCL, 0644);
        int id = begin_txn();

        while (1) {
                if (id == -1)
                        id = begin_txn();

                write(fd, noise, ATOM);

                int roll = rand() % 100;
                if (roll < 5) {
                        end_txn(id);
                        id = -1;
                }
        }
}

int main()
{
        srand(time(NULL));

        int num_tests = 10000;

        int n = open("/dev/random", O_RDONLY);
        read(n, noise, ATOM);
        close(n);

        for (int i = 0; i < num_tests; i++) {
                pid_t child = fork();
                if (child == 0) {
                        spam_write();
                        return 0;
                } else {
                        int sec = rand() % (0 + 1);
                        long nsec = rand() % (999999999 + 1);
                        struct timespec ts;
                        ts.tv_sec = sec;
                        ts.tv_nsec = nsec;

                        printf("--- test #%d (%d s + %09ld ns) ... ", i, sec, nsec);

                        nanosleep(&ts, NULL);
                        kill(child, SIGKILL);
                        wait(NULL);

                        // verify valid state
                        int fd0 = open("out/spam.txt", O_CREAT | O_RDWR, 0644); // trigger recovery
                        struct stat st;
                        fstat(fd0, &st);
                        if (st.st_size % ATOM != 0) {
                                printf("\nERROR: transactional guarantees not provided! %ld\n", st.st_size);
                                abort();
                        } else {
                                printf("✓✓✓ File in valid state! ✓✓✓\n");
                        }
                        close(fd0);
                        remove("out/spam.txt");
                }
        }


}
