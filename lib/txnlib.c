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
#include <unistd.h>
#include <utime.h>
#include "txnlib.h"

#include <errno.h>

#define FD_MAX 1024

static int (*glibc_open)(const char *pathname, int flags, ...);
static int (*glibc_close)(int fd);
static int (*glibc_mkdir)(const char *pathname, mode_t mode);
static int (*glibc_rename)(const char *oldpath, const char *newpath);
static int (*glibc_remove)(const char *pathname);
static ssize_t (*glibc_read)(int fd, void *buf, size_t count);
static ssize_t (*glibc_write)(int fd, const void *buf, size_t count);
static int (*glibc_ftruncate)(int fd, off_t length);
static int (*glibc_fxstat)(int vers, int fd, struct stat *statbuf);
static int (*glibc_lseek)(int fd, off_t offset, int whence);

static int init = 0;
static int next_txn_id = 0; // TODO: prevent overflow
static int redirect_id = 0; // TODO: prevent overflow
static int log_fd = -1;
static const char *log_dir = "/var/tmp/txnlib";
static const char *redo_log = "/var/tmp/txnlib/redo-log";
static const char *bypass = "/var/tmp/txnlib/bypass"; // prevent issues when using system()
static struct txn *cur_txn = NULL;
static struct file_desc *fd_map[FD_MAX];
static struct vfile *vfiles = NULL;
static char *keep_log = NULL;

// ========== helper methods ==========

void initialize()
{
	glibc_open = dlsym(RTLD_NEXT, "open");
	glibc_close = dlsym(RTLD_NEXT, "close");
	glibc_mkdir = dlsym(RTLD_NEXT, "mkdir");
	glibc_rename = dlsym(RTLD_NEXT, "rename");
	glibc_remove = dlsym(RTLD_NEXT, "remove");
	glibc_read = dlsym(RTLD_NEXT, "read");
	glibc_write = dlsym(RTLD_NEXT, "write");
	glibc_ftruncate = dlsym(RTLD_NEXT, "ftruncate");
	glibc_fxstat = dlsym(RTLD_NEXT, "__fxstat"); // glibc fstat is weird
	glibc_lseek = dlsym(RTLD_NEXT, "lseek");

	init = 1;
}

int committed()
{
	int log_fd = glibc_open(redo_log, O_RDONLY);
	if (log_fd == -1)
		return 0;

	char last[8];
	glibc_lseek(log_fd, -7, SEEK_END);
	glibc_read(log_fd, last, 7);
	last[7] = '\0';

	glibc_close(log_fd);

	return strcmp(last, "commit\n") == 0;
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

int glibc_fstat(int fd, struct stat *statbuf) { return glibc_fxstat(_STAT_VER, fd, statbuf); }

// ========== fd_map + vfiles ==========

int next_fd()
{
	for (int i = 0; i < FD_MAX; i++)
		if (!fd_map[i] && !fcntl(i, F_GETFD)) // also check it is not a valid open fd
			return i;
	return -1;
}

struct vfile *find_by_path(const char *path)
{
	struct vfile *vf = vfiles;
	while (vf) {
		if (strcmp(path, vf->path) == 0)
			return vf;
		vf = vf->next;
	}
	return NULL;
}

struct vfile *find_by_src(const char *src)
{
	struct vfile *vf = vfiles;
	while (vf) {
		if (strcmp(src, vf->src) == 0)
			return vf;
		vf = vf->next;
	}
	return NULL;
}

// ========== testing ==========

void crash() { cur_txn = NULL; }

// ========== redo log methods ==========

int redo_mkdir(const char *path, mode_t mode)
{
	return glibc_mkdir(path, mode);
}

int redo_create(const char *path)
{
	return glibc_close(glibc_open(path, O_CREAT, 0644));
}

int redo_write(const char *path, off_t pos, off_t length, const char *datapath)
{
	char buf[length];
	int rd = glibc_open(datapath, O_RDWR);
	glibc_lseek(rd, pos, SEEK_SET);
	glibc_read(rd, buf, length);
	glibc_close(rd);

	int fd = glibc_open(path, O_RDWR);
	glibc_lseek(fd, pos, SEEK_SET);
	glibc_write(fd, buf, length);
	glibc_close(fd);

	return 0;
}

int redo_remove(const char *path)
{
	return glibc_remove(path);
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
			mode_t mode = atoi(nexttok(NULL));
			redo_mkdir(path, mode);
		} else if (strcmp(op, "create") == 0) {
			char *path = strtok(nexttok(NULL), "\n");
			redo_create(path);
		} else if (strcmp(op, "write") == 0) {
			char *path = nexttok(NULL);
			off_t pos = atoi(nexttok(NULL));
			off_t length = atoi(nexttok(NULL));
			char *datapath = strtok(nexttok(NULL), "\n");
			redo_write(path, pos, length, datapath);
		} else if (strcmp(op, "remove") == 0) {
			char *path = strtok(nexttok(NULL), "\n");
			redo_remove(path);
		} else if (strcmp(op, "commit") == 0) {
			break;
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
		glibc_close(log_fd);
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
		// glibc_remove(redo_log);
		glibc_rename(redo_log, "/var/tmp/txnlib/redo-log.save");
	}
	return ret;
}

void rollback()
{
	cur_txn = NULL;
	glibc_remove(redo_log);
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

int open(const char *pathname, int flags, ...)
{
	if (!init)
		initialize();
	redo();

	// just get mode so don't need to muddy up method
	int mode = -1;
	if (flags & (O_CREAT | O_TMPFILE)) {
		va_list args;
		va_start(args, flags);
		mode = va_arg(args, int);
	}

	if (cur_txn) {
		char *rp = realpath_missing(pathname);

		// quickly log entry if needed (creating or truncating)
		char entry[5000];
		memset(entry, '\0', sizeof(entry));
		if (flags & O_CREAT) {
			snprintf(entry, sizeof(entry), "create %s\n", rp);
			write_to_log(entry);
		}
		if (flags & O_TRUNC) {
			memset(entry, '\0', sizeof(entry));
			snprintf(entry, sizeof(entry), "truncate %s 0\n", rp);
			write_to_log(entry);
		}

		struct vfile *vf = find_by_path(rp);
		if (!vf) {
			vf = malloc(sizeof(struct vfile));
			snprintf(vf->path, sizeof(vf->path), "%s", rp);
			snprintf(vf->src, sizeof(vf->src), "%s", rp);
			snprintf(vf->redirect, sizeof(vf->redirect), "%s/%d.rd", log_dir, redirect_id++);
			vf->size = filesize(rp);
			vf->next = vfiles;
			vfiles = vf;
		}
		free(rp);

		struct file_desc *vfd = malloc(sizeof(struct file_desc));
		if (flags & O_TRUNC) {
			vfd->pos = 0;
			vf->size = 0;
		} else if (flags & O_APPEND) {
			vfd->pos = vf->size;
		} else {
			vfd->pos = 0;
		}
		vfd->file = vf;

		int ret = next_fd();
		fd_map[ret] = vfd;
		return ret;
	} else {
		return glibc_open(pathname, flags, mode);
	}
}

int close(int fd)
{
	if (!init)
		initialize();
	redo();

	if (cur_txn) {
		free(fd_map[fd]);
		fd_map[fd] = NULL;
		return 0;
	} else {
		return glibc_close(fd);
	}
}

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

int remove(const char *pathname)
{
	if (!init)
		initialize();
	redo();

	if (cur_txn) {
		char entry[5000];
		memset(entry, '\0', sizeof(entry));
		char *rp = realpath_missing(pathname);
		snprintf(entry, sizeof(entry), "remove %s\n", rp);
		write_to_log(entry);
		free(rp);
		return 0;
	} else {
		return glibc_remove(pathname);
	}
}

ssize_t write(int fd, const void *buf, size_t count)
{
	if (!init)
		initialize();
	redo();

	if (cur_txn) {
		// first check fd_map for fd, otherwise, get map fd to new file_desc
		struct file_desc *vfd = fd_map[fd];

		if (!vfd) {
			char *path = get_path_from_fd(fd);

			// could already be in vfiles or not
			struct vfile *vf = find_by_src(path);
			if (!vf) {
				// if not in vfiles, then it hasn't been touched within the txn
				vf = malloc(sizeof(struct vfile));
				snprintf(vf->path, sizeof(vf->path), "%s", path);
				snprintf(vf->src, sizeof(vf->src), "%s", path);
				snprintf(vf->redirect, sizeof(vf->redirect), "%s/%d.rd", log_dir, redirect_id++);
				vf->size = filesize(path);
				vf->next = vfiles;
				vfiles = vf;
			}

			// create a new file_desc and map it
			vfd = malloc(sizeof(struct file_desc));
			vfd->pos = glibc_lseek(fd, 0, SEEK_CUR);
			vfd->file = vf;
			fd_map[fd] = vfd;

			free(path);
		}

		// prepare log entry
		char entry[10000];
		memset(entry, '\0', sizeof(entry));

		// write data to redirect at offset
		int rd = glibc_open(vfd->file->redirect, O_CREAT | O_RDWR, 0644);
		if (rd == -1)
			printf("rd is -1: %s\n", strerror(errno));
		glibc_lseek(rd, vfd->pos, SEEK_SET);
		ssize_t written = glibc_write(rd, buf, count);
		glibc_close(rd);

		snprintf(entry, sizeof(entry), "write %s %ld %ld %s\n", vfd->file->path, vfd->pos, written, vfd->file->redirect);
		write_to_log(entry);

		vfd->pos += written;

		if (vfd->pos > vfd->file->size)
			vfd->file->size = vfd->pos;

		return written;
	} else {
		return glibc_write(fd, buf, count);
	}
}

int ftruncate(int fd, off_t length)
{
	if (!init)
		initialize();
	redo();

	if (cur_txn) {
		struct file_desc *vfd = fd_map[fd];

		if (!vfd) {
			char *path = get_path_from_fd(fd);

			struct vfile *vf = find_by_src(path);
			if (!vf) {
				vf = malloc(sizeof(struct vfile));
				snprintf(vf->path, sizeof(vf->path), "%s", path);
				snprintf(vf->src, sizeof(vf->src), "%s", path);
				snprintf(vf->redirect, sizeof(vf->redirect), "%s/%d.rd", log_dir, redirect_id++);
				vf->size = filesize(path);
				vf->next = vfiles;
				vfiles = vf;
			}

			vfd = malloc(sizeof(struct file_desc));
			vfd->pos = glibc_lseek(fd, 0, SEEK_CUR);
			vfd->file = vf;
			fd_map[fd] = vfd;

			free(path);
		}

		vfd->file->size = length;

		return 0;
	} else {
		return glibc_ftruncate(fd, length);
	}
}

int fstat(int fd, struct stat *statbuf)
{
	if (!init)
		initialize();
	redo();

	if (cur_txn) {
		struct file_desc *vfd = fd_map[fd];
		if (vfd) {
			int ret = stat(vfd->file->src, statbuf);
			statbuf->st_size = vfd->file->size;
			return ret;
		} else {
			return glibc_fstat(fd, statbuf);
		}
	} else {
		return glibc_fstat(fd, statbuf);
	}
}

off_t lseek(int fd, off_t offset, int whence)
{
	if (!init)
		initialize();
	redo();

	if (cur_txn) {
		struct file_desc *vfd = fd_map[fd];

		if (!vfd)
			return glibc_lseek(fd, offset, whence);

		if (whence == SEEK_SET)
			vfd->pos = offset;
		else if (whence == SEEK_CUR)
			vfd->pos += offset;
		else if (whence == SEEK_END)
			vfd->pos = vfd->file->size + offset;

		return vfd->pos;
	} else {
		return glibc_lseek(fd, offset, whence);
	}
}
