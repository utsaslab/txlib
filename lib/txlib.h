#ifndef TXLIB_H_
#define TXLIB_H_

#include <sys/stat.h>
#include <sys/types.h>

struct txn { // TODO: linux style says make it const but warning from gcc?
	int id;
	int log_fd;
	struct txn *next;
};

int begin_transaction(void);
int end_transaction(int tx_id);

int txopen(const char *pathname, int flags);
// int open(const char *pathname, int flags, mode_t mode); // TODO: figure out vargs overloading

int txclose(int fd);

ssize_t txread(int fd, void *buf, size_t count);

ssize_t txwrite(int fd, const void *buf, size_t count);

int txfsync(int fd);

#endif // TXNFS_H_
