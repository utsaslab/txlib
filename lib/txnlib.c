#define _GNU_SOURCE

#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "txnlib.h"

#include <unistd.h>
#include <errno.h>

static int (*glibc_open)(const char *pathname, int flags, ...);
static int (*glibc_mkdir)(const char *pathname, mode_t mode);
static int (*glibc_remove)(const char *pathname);
static ssize_t (*glibc_read)(int fd, void *buf, size_t count);
static ssize_t (*glibc_write)(int fd, const void *buf, size_t count);

static int init = 0;
static int crashed = 0;
static int next_id; // TODO: prevent overflow
static const char *log_dir = "logs";
static const char *undo_log = "logs/undo_log";
static struct txn *cur_txn;
static struct file_node *logged;

void initialize()
{
	// expose glibc_open (TODO: should I initialize/do this differently?)
	glibc_open = dlsym(RTLD_NEXT, "open"); // TODO: what is RTLD_NEXT?
	glibc_mkdir = dlsym(RTLD_NEXT, "mkdir");
	glibc_remove = dlsym(RTLD_NEXT, "remove");
	glibc_read = dlsym(RTLD_NEXT, "read");
	glibc_write = dlsym(RTLD_NEXT, "write");

	init = 1;
}

int begin_txn(void)
{
	if (!init)
		initialize();

	if (!cur_txn) { // beginning transaction
		int err = glibc_mkdir(log_dir, 0777);
		if (err) {
			printf("making log directory at %s/ failed\n", log_dir);
			printf("err: %s\n", strerror(errno));
			return -1;
		} else {
			int fd = glibc_open(undo_log, O_CREAT, 0644);
			close(fd);
		}
	}

	struct txn *new_txn = malloc(sizeof(struct txn));
	new_txn->id = next_id++;
	new_txn->next = cur_txn;
	cur_txn = new_txn;

	return cur_txn->id;
}

int end_txn(int txn_id)
{
	if (txn_id != cur_txn->id) {
		printf("attempting to end (%d) but current transaction is (%d)", txn_id, cur_txn->id);
		return -1;
	}

	void *ended = cur_txn;
	cur_txn = cur_txn->next;
	free(ended);

	if (!cur_txn) {
		// commit everything and then delete log
		sync(); // TODO: just flush touched files?
		remove(undo_log); // TODO: check errors?
	}

	return 0;
}

// helper methods

void write_to_log(const char *entry)
{
	int log = glibc_open(undo_log, O_RDWR | O_APPEND, 0644);
	glibc_write(log, entry, strlen(entry));
	fsync(log);
	close(log);
}

// need to free returned pointer after done
char *realpath_missing(const char *path)
{
	char command[4096]; // TODO: arbitrary
	sprintf(command, "realpath -m %s", path);
	FILE *out = popen(command, "r");

	int size = 4096;
	char *rp = malloc(size);
	fgets(rp, size, out);
	rp[strlen(rp)-1] = 0; // trim the newline off
	return rp;
}

char *get_path_from_fd(int fd)
{
	// TODO: check -1 -> error?
	char *path = malloc(4096);
	char link[4096];
	sprintf(link, "/proc/self/fd/%d", fd);
	readlink(link, path, 4096);
	return path;
}

void add_to_logged(const char *name)
{
	char *rp = realpath_missing(name);
	struct file_node *fn = malloc(sizeof(struct file_node));
	sprintf(fn->name, "%s", rp);
	free(rp);
	if (logged)
		fn->next = logged;
	logged = fn;
}

int already_logged(const char *name)
{
	char *rp = realpath_missing(name);
	struct file_node *fn = logged;
	while (fn) {
		if (strcmp(logged->name, rp) == 0) {
			free(rp);
			return 1;
		}
		fn = fn->next;
	}
	free(rp);
	return 0;
}

// for testing

void crash() { crashed = 1; }

void reset()
{
	crashed = 0;
	cur_txn = NULL; // mem leak but okay for testing
}

// glibc wrappers

int open(const char *pathname, int flags, ...)
{
	// TODO: save metadata if not yet seen
	if (!already_logged(pathname)) {
		add_to_logged(pathname);
	}

	// check if file already exists (don't infer from flags)
	if ((flags & O_CREAT) && access(pathname, F_OK) == -1) {
		char entry[4096];
		char *rp = realpath_missing(pathname);
		sprintf(entry, "create %s\n", rp);
		write_to_log(entry);
	}

	if (flags & (O_CREAT | O_TMPFILE)) {
		va_list args;
		va_start(args, flags);
		int mode = va_arg(args, int);
		return glibc_open(pathname, flags, mode);
	} else {
		return glibc_open(pathname, flags);
	}
}

ssize_t write(int fd, const void *buf, size_t count)
{
	off_t pos = lseek(fd, 0, SEEK_CUR);
	char *path = get_path_from_fd(fd);
	char entry[4096];
	/**
	 * TODO: need a way to figure out how many bytes will actually
	 *       be written, or log would be inaccurate if count is not returned
	 */
	sprintf(entry, "write %s %ld %ld", path, pos, count);
	write_to_log(entry);

	return glibc_write(fd, buf, count);
}
