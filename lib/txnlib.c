#define _GNU_SOURCE

#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <utime.h>
#include "txnlib.h"

#include <unistd.h>
#include <errno.h>

static int (*glibc_open)(const char *pathname, int flags, ...);
static int (*glibc_mkdir)(const char *pathname, mode_t mode);
static int (*glibc_rename)(const char *oldpath, const char *newpath);
static int (*glibc_remove)(const char *pathname);
static ssize_t (*glibc_read)(int fd, void *buf, size_t count);
static ssize_t (*glibc_write)(int fd, const void *buf, size_t count);
static int (*glibc_ftruncate)(int fd, off_t length);

static int init = 0;
static int next_txn_id = 0; // TODO: prevent overflow
static int redirect_id = 0; // TODO: prevent overflow
static const char *log_dir = "/var/tmp/txnlib";
static const char *redo_log = "/var/tmp/txnlib/redo-log";
static const char *bypass = "/var/tmp/txnlib/bypass"; // prevent issues when using system()
static struct txn *cur_txn = NULL;
static int log_fd = -1;
static char *keep_log = NULL;

// ========== helper methods ==========

int committed()
{
	int log_fd = glibc_open(redo_log, O_RDONLY);
	if (log_fd == -1)
		return 0;

	char last[8];
	lseek(log_fd, -7, SEEK_END);
	glibc_read(log_fd, last, 7);
	last[7] = '\0';

	return strcmp(last, "commit\n") == 0;
}

void initialize()
{
	glibc_open = dlsym(RTLD_NEXT, "open");
	glibc_mkdir = dlsym(RTLD_NEXT, "mkdir");
	glibc_rename = dlsym(RTLD_NEXT, "rename");
	glibc_remove = dlsym(RTLD_NEXT, "remove");
	glibc_read = dlsym(RTLD_NEXT, "read");
	glibc_write = dlsym(RTLD_NEXT, "write");
	glibc_ftruncate = dlsym(RTLD_NEXT, "ftruncate");

	init = 1;
}

// returns absolute path even if file doesn't exist (need to free returned pointer)
char *realpath_missing(const char *path)
{
	int size = 4096;
	char *rp = calloc(size, 1);
	char command[4096];
	sprintf(command, "realpath -m %s", path);

	set_bypass(1);
	FILE *out = popen(command, "r");
	fgets(rp, size, out);
	pclose(out);
	rp[strlen(rp)-1] = 0; // trim the newline off
	set_bypass(0);

	return rp;
}

// (need to free returned pointer)
char *get_path_from_fd(int fd)
{
	char *path = calloc(4096, 1);
	char link[128];
	sprintf(link, "/proc/self/fd/%d", fd);
	readlink(link, path, 4096);
	return path;
}

// custom implementation of parsing undo logs
char *nexttok(char *line) { return strtok(line, line ? " \n" : " "); }

off_t filesize(const char *path)
{
	struct stat metadata;
	return (stat(path, &metadata) == 0) ? metadata.st_size : -1;
}

// ========== testing ==========

void crash() { cur_txn = NULL; }

// ========== redo log methods ==========

int redo_mkdir(const char *path, mode_t mode)
{
	return glibc_mkdir(path, mode);
}

// returns nonzero if failed
int replay_log()
{
	FILE *fp = fopen(redo_log, "r");
	char entry[8500]; // two ext4 max paths + a lil more
	memset(entry, '\0', sizeof(entry));
	while(fgets(entry, sizeof(entry), fp)) {
		char *op = nexttok(entry);

		if (strcmp(op, "mkdir") == 0) {
			char *path = nexttok(NULL);
			mode_t mode = atoi(strtok(nexttok(NULL), "\n"));
			redo_mkdir(path, mode);
		}
	}
	fclose(fp);
	return 0;
}

// ========== logging ==========

void write_to_log(const char *entry)
{
	int written = glibc_write(log_fd, entry, strlen(entry));
	if (written != strlen(entry))
		printf("Error writing to log: (actual -> %d) vs (expected -> %zd) %s\n",
			written, strlen(entry), strerror(errno));
}

// ========== API ==========

int begin_txn(void)
{
	if (!init)
		initialize();
	redo();

	if (!cur_txn) { // first transaction
		// clean up workspace
		char cmd[1024];
		sprintf(cmd, "rm -rf %s", log_dir);
		set_bypass(1);
		system(cmd);
		set_bypass(0);

		int err = glibc_mkdir(log_dir, 0777);
		if (err && errno != EEXIST) {
			printf("Unable to make log directory at %s: (%s)\n", log_dir, strerror(errno));
			return -1;
		}

		log_fd = glibc_open(redo_log, O_CREAT | O_TRUNC | O_RDWR, 0644);
		if (log_fd == -1) {
			printf("Unable to make log at %s: (%s)\n", redo_log, strerror(errno));
			return -1;
		}
	}

	struct txn *new_txn = malloc(sizeof(struct txn));
	new_txn->id = next_txn_id++;
	new_txn->next = cur_txn;
	cur_txn = new_txn;

	return cur_txn->id;
}

int end_txn(int txn_id)
{
	// should this be allowed? yes, bc we do not provide isolation
	// TODO: allow later
	if (txn_id != cur_txn->id) {
		printf("attempting to end (%d) but current transaction is (%d)\n", txn_id, cur_txn->id);
		return -1;
	}

	void *ended = cur_txn;
	cur_txn = cur_txn->next;
	free(ended);

	// last transaction
	if (!cur_txn) {
		write_to_log("commit\n");
		fsync(log_fd);
		redo();
	}

	return 0;
}

int redo()
{
	if (!committed())
		return 0;

	int ret = replay_log();
	if (keep_log) {
		int err = glibc_rename(redo_log, keep_log);
		if (err)
			printf("Error saving log to %s\n", strerror(errno));
		free(keep_log);
		keep_log = NULL;
	} else {
		glibc_remove(redo_log);
	}
	return ret;
}

void save_log(const char *dest)
{
	keep_log = calloc(4096, 1);
	if (dest)
		sprintf(keep_log, "%s", dest);
	else
		sprintf(keep_log, "%s", redo_log);
}

void delete_log() { glibc_remove(redo_log); }

void set_bypass(int set)
{
	if (!init)
		initialize();

	if (set) {
		glibc_mkdir(log_dir, 0777);
		close(glibc_open(bypass, O_CREAT, 0644));
	} else {
		glibc_remove(bypass);
	}
}

// ========== glibc wrappers ==========

int mkdir(const char *pathname, mode_t mode)
{
	if (!init)
		initialize();
	redo();

	if (cur_txn) {
		char entry[5000];
		memset(entry, '\0', sizeof(entry));
		char *rp = realpath_missing(pathname);
		snprintf(entry, sizeof(entry), "mkdir %s %d\n", rp, mode);
		free(rp);

		write_to_log(entry);

		return 0;
	} else {
		return glibc_mkdir(pathname, mode);
	}
}
