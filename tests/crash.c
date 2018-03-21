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

char noise[50000];

void spam_write()
{
        int times = 100000;
        size_t size = 50000;

        int fd = open("out/spam.txt", O_CREAT | O_RDWR | O_EXCL, 0644);
        int id = begin_txn();
        for (int i = 0; i < times; i++) {
                if (id == -1)
                        id = begin_txn();

                write(fd, noise, size);
                if (i % 500 == 0)
                        printf("%d\n", i);

                int roll = rand() % 100;
                if (roll < 5) {
                        end_txn(id);
                        id = -1;
                }
        }
        end_txn(id);
        close(fd);
}

int main()
{
        srand(time(NULL));

        int num_tests = 100;

        int n = open("/dev/random", O_RDONLY);
        read(n, noise, 50000);
        close(n);

        for (int i = 0; i < num_tests; i++) {
                pid_t child = fork();
                if (child == 0) {
                        spam_write();
                        return 0;
                } else {
                        int sec = rand() % (5 + 1);
                        // int sec = 0;
                        long nsec = rand() % (999999999 + 1);
                        struct timespec ts;
                        ts.tv_sec = sec;
                        ts.tv_nsec = nsec;

                        printf(">>>>> %d days remain (%d s + %ld ns)\n", num_tests-i-1, sec, nsec);
                        nanosleep(&ts, NULL);

                        kill(child, SIGKILL);
                        printf("<<<<< killed\n");

                        // verify valid state
                        int fd0 = open("out/spam.txt", O_CREAT | O_RDWR, 0644); // trigger recovery
                        struct stat st;
                        fstat(fd0, &st);
                        if (st.st_size % 50000 != 0) {
                                printf("ERROR: transactional guarantees not provided! %ld\n", st.st_size);
                                abort();
                        } else {
                                printf("File in valid state! :)\n");
                        }
                        close(fd0);
                        remove("out/spam.txt");

                        wait(NULL);
                }
        }


}
