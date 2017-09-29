#define _GNU_SOURCE

#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "txnlib.h"

static int (*glibc_open)(const char *pathname, int flags, ...);
static int (*glibc_close)(int fd);
static ssize_t (*glibc_read)(int fd, void *buf, size_t count);
static ssize_t (*glibc_write)(int fd, const void *buf, size_t count);

static int next_id; // TODO: prevent overflow
static struct txn *cur_txn;

int begin_txn(void)
{
	// expose glibc_open (TODO: should I initialize/do this differently?)
	glibc_open = dlsym(RTLD_NEXT, "open"); // TODO: what is RTLD_NEXT?
	glibc_close = dlsym(RTLD_NEXT, "close");
	glibc_read = dlsym(RTLD_NEXT, "read");
	glibc_write = dlsym(RTLD_NEXT, "write");

	// create redo log name
	char logfile[64];
	sprintf(logfile, "txn-%d.log", next_id);

	// log in current transaction
	if (cur_txn) {
		char begin[64];
		sprintf(begin, "begin %d\n", next_id);
		glibc_write(cur_txn->log_fd, begin, strlen(begin));
	}

	struct txn *new_txn = malloc(sizeof(struct txn));
	new_txn->id = next_id++;
	// TODO: make sure you're not overriding other logs and set permissions right
	new_txn->log_fd = glibc_open(logfile, O_CREAT | O_RDWR | O_TRUNC, 777);
	new_txn->next = cur_txn;
	cur_txn = new_txn;

	return new_txn->id;
}

int end_txn(int txn_id)
{
	if (txn_id != cur_txn->id) {
		printf("attempting to end non-current transaction (%d vs %d)", txn_id, cur_txn->id);
		return 1;
	}
	glibc_write(cur_txn->log_fd, "commit\n", 7);
	glibc_close(cur_txn->log_fd);

	// if possible, record in parent log
	if (cur_txn->next) {
		char entry[64];
		sprintf(entry, "end %d\n", cur_txn->id);
		glibc_write(cur_txn->next->log_fd, entry, strlen(entry));
	}

	void *ended = cur_txn;
	cur_txn = cur_txn->next;
	free(ended);

	return 0;
}

// need to buffer only if O_CREAT flag is set
int open(const char *pathname, int flags, ...)
{
	char entry[64];
	sprintf(entry, "open %s %d\n", pathname, flags);
	glibc_write(cur_txn->log_fd, entry, strlen(entry));
	return 0;
}

int close(int fd)
{
	char entry[64];
	sprintf(entry, "close %d\n", fd); // TODO: need to look up mapping
	glibc_write(cur_txn->log_fd, entry, strlen(entry));
	return 0;
}

// TODO: keep track of position
ssize_t write(int fd, const void *buf, size_t count)
{
	char entry[64];
	sprintf(entry, "write")
	return 0;
}

int fsync(int fd)
{
	return 0;
}

int fdatasync(int fd)
{
	return 0;
}
