#define _GNU_SOURCE

#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "txnlib.h"

#include <errno.h>

static int (*glibc_open)(const char *pathname, int flags, ...);
static int (*glibc_close)(int fd);
static ssize_t (*glibc_read)(int fd, void *buf, size_t count);
static ssize_t (*glibc_write)(int fd, const void *buf, size_t count);

static const char *log_dir = "logs";
static int next_id; // TODO: prevent overflow
static struct txn *cur_txn;
static struct log_node *log_tree;

// helper methods

// return 0 on success, nonzero otherwise
int add_to_tree(const char *path)
{
	// let root have empty string as name
	if (!log_tree) {
		log_tree = malloc(sizeof(struct log_node));
		log_tree->is_dir = 1;
	}

	char *ret = realpath(path, NULL);
	if (ret) {
		struct log_node *branch = log_tree;
		int num_children = sizeof(branch->children) / sizeof(branch->children[0]);
		char *token = strtok(ret, "/");
		while (token != NULL) {
			int found = 0;
			int first_open = -1;
			for (int i = 0; i < num_children ; i++) {
				if (branch->children[i]) {
					if (strcmp(token, branch->children[i]->name) == 0) {
						branch = branch->children[i];
						found = 1;
						break;
					}
				} else {
					if (first_open == -1)
						first_open = i;
				}
			}

			if (!found) {
				// printf("adding %s to %s at %d\n", token, branch->name, first_open);
				struct log_node *new_node = malloc(sizeof(struct log_node));
				sprintf(new_node->name, "%s", token);
				// TODO: check if dir
				branch->children[first_open] = new_node;
				branch = new_node;
			}

			token = strtok(NULL, "/");
		}
	} else {
		printf("error getting absolute path for %s\n", path);
		return -1;
	}

	return 0;
}

char *nexttok(char *line)
{
	if (line == NULL)
		return strtok(NULL, " ");
	else
		return strtok(line, " \n");
}

// API

int begin_txn(void)
{
	// expose glibc_open (TODO: should I initialize/do this differently?)
	glibc_open = dlsym(RTLD_NEXT, "open"); // TODO: what is RTLD_NEXT?
	glibc_close = dlsym(RTLD_NEXT, "close");
	glibc_read = dlsym(RTLD_NEXT, "read");
	glibc_write = dlsym(RTLD_NEXT, "write");

	int err = mkdir(log_dir, 0777);
	// if (err) {
	// 	printf("making log directory at %s/ failed\n", log_dir);
	// 	return -1;
	// }

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
	// TODO: do flags dependent logging

	int ret;
	if (flags & (O_CREAT | O_TMPFILE)) {
		va_list args;
		va_start(args, flags);
		int mode = va_arg(args, int);

		ret = glibc_open(pathname, flags, mode);
	} else {
		ret = glibc_open(pathname, flags);
	}

	add_to_tree(pathname);

	return ret;
}

int close(int fd)
{
	// TODO: log?
	return glibc_close(fd);
}

ssize_t write(int fd, const void *buf, size_t count)
{
	// TODO: do the logic :|
	return 0;
}
