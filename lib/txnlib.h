#ifndef TXNLIB_H_
#define TXNLIB_H_

#include <sys/types.h>

struct txn { // TODO: linux style says make it const but warning from gcc?
	int id;
	struct txn *next;
};

struct log_node {
	char name[256];
	int is_dir;

	// undo fields
	int created;
	int removed;

	struct log_node *parent;
	struct log_node *children[256]; // TODO: this is a lil arbitrary
};

int recover(const char *path);

int begin_txn(void);
int end_txn(int txn_id);

// keep the method signature the exact same as glibc
int open(const char *pathname, int flags, ... /* mode_t mode */);
int creat(const char *pathname, mode_t mode);
int openat(int dirfd, const char *pathname, int flags, ... /* mode_t mode */);

// TODO: wrap unlink(at) and rmdir
int remove(const char *pathname);

int access(const char *pathname, int mode);

// keep track close so we do not fail to replicate errors from original program
int close(int fd);

ssize_t read(int fd, void *buf, size_t count);

ssize_t write(int fd, const void *buf, size_t count);

// for testing
void crash() { return; }

#endif // TXNLIB_H_
