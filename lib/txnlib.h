#ifndef TXNLIB_H_
#define TXNLIB_H_

#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>

struct txn {
	int id;
	struct txn *next;
};

struct file_desc {
	int rd_fd;
	struct vfile *file;
};

struct vfile {
	char path[4096+1]; // NULL string if removed
	char src[4096+1]; // for reading
	char redirect[4096+1]; // size of redirect should change with transaction
	struct range *writes;

	struct vfile *next;
};

struct range {
	off_t begin;
	off_t end;

	struct range *prev;
	struct range *next;
};

struct path {
	char name[4096+1];
	struct path *next;
};

// txnlib API
int begin_txn(void);
int end_txn(int txn_id);
int redo(void);
void rollback(void);

void save_log(const char *dest);
void delete_log(void);
void set_bypass(int set);

/**
 * glibc filesystem calls (keep same method signature)
 */

// around file
int open(const char *pathname, int flags, ... /* mode_t mode */);
int close(int fd);
int mkdir(const char *pathname, mode_t mode);
int rename(const char *oldpath, const char *newpath);
int remove(const char *pathname);

// within file
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int ftruncate(int fd, off_t length);
// int fallocate(int fd, int mode, off_t offset, off_t len);
int fstat(int fd, struct stat *statbuf);
off_t lseek(int fd, off_t offset, int whence);
// int access(const char *pathname, int mode);

#endif // TXNLIB_H_
