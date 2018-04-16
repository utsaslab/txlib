#ifndef TXNLIB_H_
#define TXNLIB_H_

#include <stdint.h>
#include <sys/types.h>

struct txn {
	int id;
	int next_node_id;
	struct txn *next;
};

struct file_node {
	char name[256];
	struct file_node *next;
};

// txnlib API
int begin_txn(void);
int end_txn(int txn_id);
int recover();
// int rollback();
void save_log(const char *dest);
void delete_log();
void set_bypass(int set);

// for testing
void crash();

/**
 * glibc filesystem calls (keep same method signature)
 */

// create
int open(const char *pathname, int flags, ... /* mode_t mode */);
// int creat(const char *pathname, mode_t mode);
// int openat(int dirfd, const char *pathname, int flags, ... /* mode_t mode */);
int mkdir(const char *pathname, mode_t mode);
int rename(const char *oldpath, const char *newpath);

// remove
int remove(const char *pathname);
// int unlink(const char *pathname);
// int rmdir(const char *pathname);
// int unlinkat(int dirfd, const char *pathname, int flags);

// within file
ssize_t write(int fd, const void *buf, size_t count);
int ftruncate(int fd, off_t length);
// int access(const char *pathname, int mode);

#endif // TXNLIB_H_
