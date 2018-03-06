#ifndef TXNLIB_H_
#define TXNLIB_H_

#include <stdint.h>
#include <sys/types.h>

struct txn {
	int id;
	int next_node_id;
	struct txn *next;
};

struct write_log {
	off_t index;
	ssize_t length;
	struct write_log *next;
};

struct log_node {
	int id;
	char name[256];
	int is_dir;

	// undo fields
	int created;
	int removed;
	char backup_loc[4096];
	struct write_log *writes;

	struct log_node *parent;
	struct log_node *children[256]; // TODO: this is a lil arbitrary
};

int recover(const char *path);

int begin_txn(void);
int end_txn(int txn_id);

// fs ops (keep the method signature the exact same as glibc)
// create/remove
int open(const char *pathname, int flags, ... /* mode_t mode */);
int creat(const char *pathname, mode_t mode);
int openat(int dirfd, const char *pathname, int flags, ... /* mode_t mode */);
int mkdir(const char *pathname, mode_t mode);
int remove(const char *pathname); // TODO: wrap unlink(at) and rmdir
// writes
ssize_t write(int fd, const void *buf, size_t count);
// other
ssize_t read(int fd, void *buf, size_t count);
int access(const char *pathname, int mode);
int close(int fd); // keep track close so we do not fail to replicate errors from original program

// for testing
void crash();
void reset();

#endif // TXNLIB_H_
