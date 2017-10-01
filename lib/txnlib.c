#define _GNU_SOURCE

#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "txnlib.h"

static int (*glibc_open)(const char *pathname, int flags, ...);
static int (*glibc_close)(int fd);
static ssize_t (*glibc_read)(int fd, void *buf, size_t count);
static ssize_t (*glibc_write)(int fd, const void *buf, size_t count);

static int next_id; // TODO: prevent overflow
static int next_fd = 3;
static struct txn *cur_txn;

int begin_txn(void)
{
	// expose glibc_open (TODO: should I initialize/do this differently?)
	glibc_open = dlsym(RTLD_NEXT, "open"); // TODO: what is RTLD_NEXT?
	glibc_close = dlsym(RTLD_NEXT, "close");
	glibc_read = dlsym(RTLD_NEXT, "read");
	glibc_write = dlsym(RTLD_NEXT, "write");

	mkdir("logs", 0777);

	// create redo log name
	char logfile[64];
	sprintf(logfile, "logs/txn-%d.log", next_id);

	// log in current transaction
	if (cur_txn) {
		char begin[64];
		sprintf(begin, "begin %d\n", next_id);
		glibc_write(cur_txn->log_fd, begin, strlen(begin));
	}

	struct txn *new_txn = malloc(sizeof(struct txn));
	new_txn->id = next_id++;
	// TODO: make sure you're not overriding other logs and set permissions right
	new_txn->log_fd = glibc_open(logfile, O_CREAT | O_EXCL | O_RDWR, 0644);
	new_txn->next = cur_txn;
	cur_txn = new_txn;

	// mark the entry point of a set of nested transactions
	if (!cur_txn->next) {
		glibc_write(cur_txn->log_fd, "root\n", 5);
	}

	return new_txn->id;
}

int end_txn(int txn_id)
{
	if (txn_id != cur_txn->id) {
		printf("attempting to end non-current transaction (%d vs %d)", txn_id, cur_txn->id);
		return -1;
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

// need to buffer only if O_CREAT flag is set (TODO: huh?)
int open(const char *pathname, int flags, ...)
{
	char entry[64];
	sprintf(entry, "open %d %s %d\n", next_fd, pathname, flags);
	glibc_write(cur_txn->log_fd, entry, strlen(entry));
	return next_fd++;
}

int close(int fd)
{
	char entry[64];
	sprintf(entry, "close %d\n", fd);
	glibc_write(cur_txn->log_fd, entry, strlen(entry));
	return 0;
}

// TODO: keep track of position
ssize_t write(int fd, const void *buf, size_t count)
{
	char data_file[64];
	sprintf(data_file, "logs/txn-data-%d-%d", cur_txn->id, cur_txn->next_buf++);
	int tmp_fd = glibc_open(data_file, O_CREAT | O_EXCL | O_RDWR, 0644); // TODO: later on, let user specify privacy
	glibc_write(tmp_fd, buf, count);
	glibc_close(tmp_fd);

	char entry[64];
	sprintf(entry, "write %d %d %ld\n", fd, cur_txn->next_buf, count);
	glibc_write(cur_txn->log_fd, entry, strlen(entry));

	return 0;
}
