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
	glibc_open = dlsym(RTLD_NEXT, "open");
	glibc_close = dlsym(RTLD_NEXT, "close");
	glibc_read = dlsym(RTLD_NEXT, "read");
	glibc_write = dlsym(RTLD_NEXT, "write");

	// create redo log name
	char log[64];
	strcat(log, "txn-");
	char id[64];
	sprintf(id, "%d", next_id);
	strcat(log, id);
	strcat(log, ".log");

	// log in current transaction
	if (cur_txn) {
		char begin[64];
		strcat(begin, "begin ");
		strcat(begin, id);
		glibc_write(cur_txn->log_fd, begin, strlen(begin));
	}

	struct txn *new_txn = malloc(sizeof(struct txn));
	new_txn->id = next_id++;
	// TODO: make sure you're not overriding other logs and set permissions right
	new_txn->log_fd = glibc_open(log, O_CREAT | O_RDWR | O_TRUNC, 777);
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

	void *ended = cur_txn;
	cur_txn = cur_txn->next;
	free(ended);
	return 0;
}

// need to buffer only if O_CREAT flag is set
int open(const char *pathname, int flags, ...)
{
	glibc_write(cur_txn->log_fd, "open ", 5);
	glibc_write(cur_txn->log_fd, pathname, strlen(pathname));
	glibc_write(cur_txn->log_fd, " ", 1);
	char buf[64];
	sprintf(buf, "%d", flags);
	glibc_write(cur_txn->log_fd, buf, strlen(buf));
	glibc_write(cur_txn->log_fd, "\n", 1);
	return 0;
}

int close(int fd)
{
	write(cur_txn->log_fd, "close ", 6);
	char buf[64];
	sprintf(buf, "%d", fd);
	write(cur_txn->log_fd, buf, strlen(buf));
	write(cur_txn->log_fd, "\n", 1);
	return 0;
}

ssize_t read(int fd, void *buf, size_t count)
{
	return 0;
}

ssize_t write(int fd, const void *buf, size_t count)
{
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
