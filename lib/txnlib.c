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
static int (*glibc_close)(int fd);
static ssize_t (*glibc_read)(int fd, void *buf, size_t count);
static ssize_t (*glibc_write)(int fd, const void *buf, size_t count);
static int (*glibc_remove)(const char *pathname);
static int (*glibc_mkdir)(const char *pathname, mode_t mode);

static int init = 0;
static const char *log_dir = "logs";
static int next_id; // TODO: prevent overflow
static struct txn *cur_txn;
static struct log_node *log_tree;

static int crashed = 0;

// helper methods

void initialize()
{
	// expose glibc_open (TODO: should I initialize/do this differently?)
	glibc_open = dlsym(RTLD_NEXT, "open"); // TODO: what is RTLD_NEXT?
	glibc_close = dlsym(RTLD_NEXT, "close");
	glibc_read = dlsym(RTLD_NEXT, "read");
	glibc_write = dlsym(RTLD_NEXT, "write");
	glibc_remove = dlsym(RTLD_NEXT, "remove");
	glibc_mkdir = dlsym(RTLD_NEXT, "mkdir");

	int err = glibc_mkdir(log_dir, 0777);
	// if (err) {
	// 	printf("making log directory at %s/ failed\n", log_dir);
	// 	return -1;
	// }

	init = 1;
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

// need to free returned pointer in calling code
char *get_path_to(struct log_node *node)
{
	char *path = malloc(4096);
	char copy[4096];
	struct log_node *branch = node;
	while (branch->parent) {
		sprintf(copy, "%s", path);
		sprintf(path, "/%s%s", branch->name, copy);
		branch = branch->parent;
	}
	return path;
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

// set create to non zero to automatically create if not found
// returns child node (created or found)
struct log_node *find_child(struct log_node *parent, const char *name, int create)
{
	if (!parent)
		return NULL;

	int first_open = -1;
	int num_children = sizeof(parent->children) / sizeof(parent->children[0]);
	for (int i = 0; i < num_children; i++) {
		struct log_node *child = parent->children[i];
		if (child) printf("cn: %s\n", child->name);
		if (child) {
			if (strcmp(name, child->name) == 0)
				return child;
		} else {
			if (first_open == -1)
				first_open = i;
		}
	}

	// only initialize basic fields
	if (create) {
		printf("creating: %s\n", name);
		struct log_node *new_node = malloc(sizeof(struct log_node));
		new_node->id = cur_txn->next_node_id++;
		sprintf(new_node->name, "%s", name);
		new_node->parent = parent;
		parent->children[first_open] = new_node;
		return new_node;
	}

	return NULL; // if not found and not created
}

// returns leaf log_node of path
struct log_node *add_to_tree(const char *path)
{
	// let root have empty string as name
	if (!log_tree) {
		log_tree = malloc(sizeof(struct log_node));
		log_tree->is_dir = 1;
	}

	char *rp = realpath_missing(path);
	if (rp) {
		struct log_node *branch = log_tree;
		if (branch->created) // optimization; no need to log created children
			return NULL;
		char *token = strtok(rp, "/");
		while (token != NULL) {
			printf("===========================\n");
			printf("branch: %s\n", branch->name);
			struct log_node *next = find_child(branch, token, 1);
			// printf("next: %s\n", next->name);
			if (next->created) // optmization
				return NULL;

			// TODO: fill in relevant log_node fields
			branch = next;
			token = strtok(NULL, "/");
		}
		return branch;
	} else {
		printf("error getting absolute path for %s\n", path);
	}

	return NULL;
}

// frees node from tree and handles parent's pointer
void remove_from_tree(struct log_node *node)
{
	struct log_node *parent = node->parent;
	int num_children = sizeof(parent->children) / sizeof(parent->children[0]);
	for (int i = 0; i < num_children; i++) {
		if (strcmp(node->name, parent->children[i]->name) == 0) {
			parent->children[i] = NULL;
			break;
		}
	}
	free(node);
}

// return 1 if any node in path was created in transaction
int created_in_txn(const char *path)
{
	char *rp = realpath(path, NULL);
	if (rp) {
		struct log_node *branch = log_tree;
		char *token = strtok(rp, "/");
		while (token != NULL) {
			if (branch->created)
				return 1;

			branch = find_child(branch, token, 0);
			if (!branch)
				return 0;

			token = strtok(NULL, "/");
		}
		return 0;
	} else {
		printf("Error finding realpath for %s\n", path);
		return -1;
	}
}

// API

// return 0 for success; nonzero otherwise
int recover(const char *path)
{
	char *rp = realpath_missing(path);
	if (rp) {
		struct log_node *branch = log_tree;
		char *token = strtok(rp, "/");
		while (branch) {

			char *path = get_path_to(branch);

			// RECOVERY LOGIC
			if (branch->created) {
				// int ret = glibc_remove(path);
				char command[1024];
				sprintf(command, "rm -r %s", path);
				system(command); // TODO: is this okay?
				remove_from_tree(branch);
				break;
			} else if (branch->removed) {
				char move_from[4096];
				sprintf(move_from, "%s/%s", log_dir, branch->backup_loc);
				rename(move_from, path);
				branch->removed = 0;
				// remove_from_tree(branch); // TODO: free later?
				// break;
			}

			branch = find_child(branch, token, 0);
			token = strtok(NULL, "/");
		}
	}
}

int begin_txn(void)
{
	if (!init)
		initialize();

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

	if (!cur_txn->next) {
		// commit everything and then delete log
	}

	void *ended = cur_txn;
	cur_txn = cur_txn->next;
	free(ended);

	return 0;
}

int open(const char *pathname, int flags, ...)
{
	if (!init)
		initialize();

	// TODO: different crash detection
	if (crashed)
		recover(pathname);

	// TODO: do flags dependent logging

	if (cur_txn) {
		struct log_node *opened = add_to_tree(pathname);
		if (opened) {
			opened->created = flags & O_CREAT;
		}
	}

	// shit
	struct log_node *node = log_tree;
	printf("at: %s\n", node->name);
	node = node->children[0];
	printf("at: %s\n", node->name);
	node = node->children[0];
	printf("at: %s\n", node->name);
	node = node->children[0];
	printf("at: %s\n", node->name);
	node = node->children[0];
	printf("at: %s\n", node->name);
	node = node->children[0];
	printf("at: %s\n", node->name);
	node = node->children[0];
	printf("at: %s\n", node->name);
	// if (node->created)
	// 	printf("fuqq\n");
	// else
	// 	printf("fugg\n");

	int ret;
	if (flags & (O_CREAT | O_TMPFILE)) {
		va_list args;
		va_start(args, flags);
		int mode = va_arg(args, int);
		ret = glibc_open(pathname, flags, mode);
	} else {
		ret = glibc_open(pathname, flags);
	}

	// TODO: figure out what happens if crash here

	return ret;
}

int mkdir(const char *pathname, mode_t mode)
{
	if (!init)
		initialize();

	if (cur_txn) {
		struct log_node *dir = add_to_tree(pathname);
		if (dir)
			dir->created = 1;
	}

	return glibc_mkdir(pathname, mode);
}

int remove(const char *pathname)
{
	struct log_node *removed = add_to_tree(pathname);
	if (!removed)
		return glibc_remove(pathname);

	removed->removed = 1;

	char move_to[4096];
	sprintf(removed->backup_loc, "%d-%d-%s", cur_txn->id, removed->id, removed->name);
	sprintf(move_to, "%s/%s", log_dir, removed->backup_loc);

	return rename(pathname, move_to); // TODO: does this return the same as remove()?
}

ssize_t write(int fd, const void *buf, size_t count)
{
	if (!init)
		initialize();

	char *path = get_path_from_fd(fd);

	if (crashed)
		recover(path);

	if (cur_txn) {
		// need method for getting from tree
		printf("adding\n");
		struct log_node *node = add_to_tree(path);
		printf("done adding\n");
		if (!node) {
			// printf("HERE BISH\n");
			return glibc_write(fd, buf, count);
		}

		printf("%s\n", node->name);
		if (node->created) {
			printf("wtffffff\n");
		}

		// TODO: how to figure out how much will be written before write
		off_t pos = lseek(fd, 0, SEEK_CUR);
		ssize_t written = glibc_write(fd, buf, count);
		struct write_log *wl = malloc(sizeof(struct write_log));
		wl->index = pos;
		wl->length = written;

		struct write_log *head = node->writes;
		while (head) {
			if (head->index > wl->index)
				head->index += wl->length;

			if (head->next)
				head = head->next;
			else
				head->next = wl;
		}
		return written;
	} else {
		return glibc_write(fd, buf, count);
	}
}

int close(int fd)
{
	// TODO: log?
	return glibc_close(fd);
}

// for testing
// TODO: also clear transactions
void crash() { crashed = 1; }

void reset()
{
	crashed = 0;
	// mem leak but okay for testing
	cur_txn = NULL;
	log_tree = NULL;
}
