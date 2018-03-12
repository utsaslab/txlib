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
static int next_id; // TODO: prevent overflow
static int backup_id;
static const char *log_dir = "logs";
static const char *undo_log = "logs/undo_log";
static struct txn *cur_txn;
static struct file_node *logged;
static struct log_node *tree_log;

/**
 * helper methods
 */

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

void write_to_log(const char *entry)
{
	int log = glibc_open(undo_log, O_RDWR | O_APPEND, 0644);
	int write = glibc_write(log, entry, strlen(entry));
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

// custom implementation of parsing undo logs
char *nexttok(char *line)
{
	if (line == NULL)
		return strtok(NULL, " ");
	else
		return strtok(line, " \n");
}

// for some reason, there is no easy way of doing this
// I tried calling system("tac ..."), but that freezes for some reason
// and there is no easy way in C to do this afaik :|
void generate_reversed_log()
{
	// find number of lines
	int lines = 0;
	FILE *fptr = fopen(undo_log, "r");
	char temp[4096];
	while (fgets(temp, 4096, fptr))
		lines++;

	// create reversed log
	char reversed_log[4096];
	sprintf(reversed_log, "%s/%s", log_dir, "reversed_log");
	int rev = glibc_open(reversed_log, O_CREAT | O_RDWR, 0644);
	for (int i = lines; i > 0; i--) {
		rewind(fptr);
		char entry[4096];
		for (int j = 0; j < i; j++)
			fgets(entry, 4096, fptr);
		glibc_write(rev, entry, strlen(entry));
	}
	close(rev);
}

/**
 * testing
 */

void crash()
{
	cur_txn = NULL;
	logged = NULL;
	tree_log = NULL;
}

/**
 * vanilla log recovery methods
 */

int vanilla_undo_create(const char *path)
{

}

int recover_log()
{
	generate_reversed_log();
	// TODO: process the reversed log
}

/**
 * tree recovery methods
 */

// returns child node (created or found)
struct log_node *find_child(struct log_node *parent, const char *name, int create)
{
	int open_slot = -1;
	int num_children = sizeof(parent->children) / sizeof(parent->children[0]);
	for (int i = 0; i < num_children; i++) {
		struct log_node *child = parent->children[i];
		if (child) {
			if (strcmp(name, child->name) == 0)
				return child;
		} else {
			if (open_slot == -1)
				open_slot = i;
		}
	}

	if (create) {
		struct log_node *new_node = malloc(sizeof(struct log_node));
		sprintf(new_node->name, "%s", name);
		parent->children[open_slot] = new_node;
		return new_node;
	}

	return NULL; // not found and not created
}

struct log_node *add_to_tree(const char *path)
{
	// let root have empty string as name
	if (!tree_log)
		tree_log = malloc(sizeof(struct log_node));

	char *rp = realpath_missing(path);
	struct log_node *branch = tree_log;
	char *sub = strtok(rp, "/");
	while (sub != NULL) {
		branch = find_child(branch, sub, 1);
		if (!branch || branch->created) { // optimization, no need to log children of created
			free(rp);
			return NULL;
		}
		sub = strtok(NULL, "/");
	}
	free(rp);
	return branch;
}

void build_tree()
{
	// build tree first
	FILE *fptr = fopen(undo_log, "r");
	char entry[4096];
	while (fgets(entry, 4096, fptr)) {
		char *op = nexttok(entry);
		if (strcmp("create", op) == 0) {
			char *path = nexttok(entry);
			struct log_node *branch = add_to_tree(path);
			branch->created = 1;
		} else if (strcmp("remove", op) == 0) {
			char *path = nexttok(entry);
			struct log_node *branch = add_to_tree(path);
			branch->removed = 1;
		}
	}
}

// return 0 on success; nonzero otherwise
int recover_tree()
{
	build_tree();
	// TODO: do recovery
}

/**
 * API
 */

int begin_txn(void)
{
	if (!init)
		initialize();

	recover();

	if (!cur_txn) { // beginning transaction
		glibc_mkdir(log_dir, 0777);
		int fd = glibc_open(undo_log, O_CREAT, 0644);
		close(fd);
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
		// remove(undo_log); // TODO: check errors?
	}

	return 0;
}

int recover()
{
	if (cur_txn)
		return 0;

	// check for existence of undo_log
	if (access(undo_log, F_OK) == -1)
		return 0;

	recover_log();
	// recover_tree();
	remove(undo_log);
}

/**
 * glibc wrappers
 */

int open(const char *pathname, int flags, ...)
{
	recover();

	// just route to glibc if not in txn
	if (!cur_txn) {
		if (flags & (O_CREAT | O_TMPFILE)) {
			va_list args;
			va_start(args, flags);
			int mode = va_arg(args, int);
			return glibc_open(pathname, flags, mode);
		} else {
			return glibc_open(pathname, flags);
		}
	}

	// TODO: save metadata if not yet seen
	if (!already_logged(pathname)) {
		add_to_logged(pathname);
	}

	char entry[4096];
	char *rp = realpath_missing(pathname);
	// check if file already exists (don't infer from flags)
	int creating = flags & O_CREAT;
	int exists = (access(pathname, F_OK) == 0);
	if (creating && !exists) {
		sprintf(entry, "create %s\n", rp);
	} else if (!creating && exists) {
		// create metadata file backup
		char metadata_loc[4096];
		sprintf(metadata_loc, "%s/%d.meta", log_dir, backup_id);
		int metadata = glibc_open(metadata_loc, O_CREAT | O_EXCL, 0644);
		close(metadata);

		// copy metadata to backup
		char cmd[4096];
		sprintf(cmd, "touch -r %s %s", pathname, metadata_loc);
		system(cmd);

		// create log entry
		sprintf(entry, "touch %s %d.meta\n", rp, backup_id++); // backup_id is metadata
	}
	write_to_log(entry);

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
	recover();

	if (cur_txn) {
		off_t pos = lseek(fd, 0, SEEK_CUR);
		char *path = get_path_from_fd(fd);
		char entry[4096];
		/**
		 * TODO: need a way to figure out how many bytes will actually
		 *       be written, or log would be inaccurate if count is not returned
		 */
		sprintf(entry, "write %s %ld %ld\n", path, pos, count);
		write_to_log(entry);
	}

	return glibc_write(fd, buf, count);
}
