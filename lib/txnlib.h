#ifndef TXNLIB_H_
#define TXNLIB_H_

#include <sys/types.h>

struct txn { // TODO: linux style says make it const but warning from gcc?
	int id;
	int log_fd;
	struct txn *next;
};

int begin_txn(void);
int end_txn(int txn_id);

// keep the method signature the exact same as glibc
int open(const char *pathname, int flags, ... /* mode_t mode */);
int creat(const char *pathname, mode_t mode);
int openat(int dirfd, const char *pathname, int flags, ... /* mode_t mode */);

// keep track close so we do not fail to replicate errors from original program
int close(int fd);

// do not need to wrap read, I think
// ssize_t read(int fd, void *buf, size_t count);

ssize_t write(int fd, const void *buf, size_t count);

int fsync(int fd);
int fdatasync(int fd);

#endif // TXNLIB_H_
