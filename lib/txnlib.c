#define _GNU_SOURCE

#include <dlfcn.h>
#include <fcntl.h>
#include <libgen.h>
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
static int next_txn_id = 0;
static int redirect_id = 0;
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
	if (glibc_open == NULL)
		printf("Error wrapping open(): %s\n", dlerror());

	glibc_close = dlsym(RTLD_NEXT, "close");
	if (glibc_close == NULL)
		printf("Error wrapping close(): %s\n", dlerror());

	glibc_mkdir = dlsym(RTLD_NEXT, "mkdir");
	if (glibc_mkdir == NULL)
		printf("Error wrapping mkdir(): %s\n", dlerror());

	glibc_rename = dlsym(RTLD_NEXT, "rename");
	if (glibc_rename == NULL)
		printf("Error wrapping rename(): %s\n", dlerror());

	glibc_remove = dlsym(RTLD_NEXT, "remove");
	if (glibc_remove == NULL)
		printf("Error wrapping remove(): %s\n", dlerror());

	glibc_read = dlsym(RTLD_NEXT, "read");
	if (glibc_read == NULL)
		printf("Error wrapping read(): %s\n", dlerror());

	glibc_write = dlsym(RTLD_NEXT, "write");
	if (glibc_write == NULL)
		printf("Error wrapping write(): %s\n", dlerror());

	glibc_ftruncate = dlsym(RTLD_NEXT, "ftruncate");
	if (glibc_ftruncate == NULL)
		printf("Error wrapping ftruncate(): %s\n", dlerror());

	glibc_fxstat = dlsym(RTLD_NEXT, "__fxstat"); // glibc fstat is weird
	if (glibc_fxstat == NULL)
		printf("Error wrapping fstat(): %s\n", dlerror());

	glibc_lseek = dlsym(RTLD_NEXT, "lseek");
	if (glibc_lseek == NULL)
		printf("Error wrapping lseek(): %s\n", dlerror());

	init = 1;
}

void reset()
{
	cur_txn = NULL;

	for (int i = 0; i < FD_MAX; i++) {
		if (fd_map[i]) {
			free(fd_map[i]);
			fd_map[i] = NULL;
		}
	}

	struct vfile *vf = vfiles;
	while (vf) {
		struct vfile *free_me = vf;
		vf = vf->next;
		free(free_me);
	}
	vfiles = NULL;

	if (keep_log) {
		free(keep_log);
		keep_log = NULL;
	}
}

// transaction is committed if log exists and last line is "commit"
int committed()
{
	int log = glibc_open(redo_log, O_RDONLY);
	if (log == -1)
		return 0;

	char last[8];
	glibc_lseek(log, -7, SEEK_END);
	glibc_read(log, last, 7);
	last[7] = '\0';

	glibc_close(log);

	return strcmp(last, "commit\n") == 0;
}

// returns absolute path even if file doesn't exist (need to free returned pointer)
char *realpath_missing(const char *path)
{
	char *rp = realpath(path, NULL);
	if (rp)
		return rp;

	// if the path is missing, then run process bc realpath() cannot resolve missing paths
	char command[4200]; // ext4 max path length + some more
	snprintf(command, sizeof(command), "realpath -m %s", path); // -m for missing paths

	int size = 4096+2; // 2: 1 for newline + 1 for null term
	rp = calloc(size, 1);

	set_bypass(1);
	FILE *out = popen(command, "r");
	fgets(rp, size, out);
	pclose(out);
	rp[strlen(rp)-1] = '\0'; // trim the newline off
	set_bypass(0);

	return rp;
}

// (need to free returned pointer)
char *get_path_from_fd(int fd)
{
	char *path = calloc(4096+1, 1);
	char link[128];
	snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
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

// fstat is weird, so we have to be weird too
int glibc_fstat(int fd, struct stat *statbuf) { return glibc_fxstat(_STAT_VER, fd, statbuf); }

// ========== fd_map + vfiles ==========

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

// will create node if not yet existing
struct vfile *find_by_path(const char *path, int create)
{
	struct vfile *vf = vfiles;
	while (vf) {
		if (strcmp(path, vf->path) == 0)
			return vf;
		vf = vf->next;
	}

	// if not creating, then return NULL if deleted or doesn't really exist
	if (!create && (find_by_src(path) || access(path, F_OK)))
		return NULL;

	// create and initialize, then add to vfiles
	vf = malloc(sizeof(struct vfile));
	snprintf(vf->path, sizeof(vf->path), "%s", path);
	snprintf(vf->src, sizeof(vf->src), "%s", path);
	snprintf(vf->redirect, sizeof(vf->redirect), "%s/%d.rd", log_dir, redirect_id++);

	int rd = glibc_open(vf->redirect, O_CREAT, 0644);
	if (rd == -1)
		printf("Failed to create redirect file for (%s): %s\n", path, strerror(errno));
	glibc_close(rd);

	if (!create) {
		int err = truncate(vf->redirect, filesize(vf->src));
		if (err)
			printf("Failed to truncate redirect file in find_by_path() for (%s): %s\n", path, strerror(errno));
	}

	vf->writes = NULL;
	vf->next = vfiles;
	vfiles = vf;

	return vf;
}

void merge_range(struct vfile *vf, off_t begin, off_t end)
{
	// find all ranges overlapping with begin and end
	struct range *overlap = malloc(sizeof(struct range));
	overlap->begin = begin;
	overlap->end = end;
	overlap->prev = NULL;
	overlap->next = NULL;

	struct range *writes = vf->writes;
	while (writes) {
		struct range *next = writes->next;
		if (end >= writes->begin && begin <= writes->end) {
			if (writes == vf->writes)
				vf->writes = writes->next;

			if (writes->prev)
				writes->prev->next = writes->next;
			if (writes->next)
				writes->next->prev = writes->prev;
			writes->prev = NULL;
			writes->next = overlap;
			overlap = writes;
		}
		writes = next;
	}

	// find the smallest begin and largest end
	off_t b = end;
	off_t e = begin;
	while (overlap) {
		if (overlap->begin < b)
			b = overlap->begin;
		if (overlap->end > e)
			e = overlap->end;
		struct range *free_me = overlap;
		overlap = overlap->next;
		free(free_me);
	}

	// add back to write ranges for vfile
	struct range *merged = malloc(sizeof(struct range));
	merged->begin = b;
	merged->end = e;
	merged->prev = NULL;
	merged->next = vf->writes;
	if (vf->writes)
		vf->writes->prev = merged;
	vf->writes = merged;
}

int next_fd()
{
	for (int i = 0; i < FD_MAX; i++)
		if (!fd_map[i] && !fcntl(i, F_GETFD)) // also check it is not a valid open fd
			return i;
	return -1;
}

// retrieves entry from fd_map, creates one if doesn't already exist
struct file_desc *get_vfd(int fd)
{
	if (fd < 0)
		return NULL;

	struct file_desc *vfd = fd_map[fd];
	if (!vfd) {
		// if the fd hasn't been seen in the txn, then it must be real (TODO: what about invalid fd?)
		char *path = get_path_from_fd(fd);
		if (strlen(path) == 0)
			return NULL;

		struct vfile *vf = find_by_src(path);
		if (!vf) {
			// if not in vfiles, then it hasn't been touched within the txn
			vf = malloc(sizeof(struct vfile));
			snprintf(vf->path, sizeof(vf->path), "%s", path);
			snprintf(vf->src, sizeof(vf->src), "%s", path);
			snprintf(vf->redirect, sizeof(vf->redirect), "%s/%d.rd", log_dir, redirect_id++);

			int rd = glibc_open(vf->redirect, O_CREAT, 0644);
			if (rd == -1)
				printf("Failed to create redirect file for (%s): %s\n", path, strerror(errno));
			glibc_close(rd);

			int err = truncate(vf->redirect, filesize(vf->src));
			if (err)
				printf("Failed to truncate redirect file in get_vfd() for (%s): %s\n", path, strerror(errno));

			vf->writes = NULL;

			// add to vfiles
			vf->next = vfiles;
			vfiles = vf;
		}

		vfd = malloc(sizeof(struct file_desc));
		vfd->pos = glibc_lseek(fd, 0, SEEK_CUR);
		vfd->file = vf;
		fd_map[fd] = vfd;

		free(path);
	}
	return vfd;
}

// ========== redo log methods ==========

int fsync_dir(char *path)
{
	char *dir = dirname(path);
	int fd = glibc_open(dir, O_DIRECTORY);
	int ret = fsync(fd);
	glibc_close(fd);
	return ret;
}

int redo_mkdir(char *path, mode_t mode)
{
	int ret = glibc_mkdir(path, mode);
	fsync_dir(path);
	return ret;
}

int redo_create(char *path, mode_t mode)
{
	glibc_close(glibc_open(path, O_CREAT, mode));
	fsync_dir(path);
	return 0;
}

int redo_write(char *path, off_t pos, off_t length, const char *datapath)
{
	char buf[length];
	int rd = glibc_open(datapath, O_RDWR);
	glibc_lseek(rd, pos, SEEK_SET);
	glibc_read(rd, buf, length);
	glibc_close(rd);

	int fd = glibc_open(path, O_RDWR);
	glibc_lseek(fd, pos, SEEK_SET);
	ssize_t written = glibc_write(fd, buf, length);
	fsync(fd);
	glibc_close(fd);

	return !(written == length);
}

int redo_remove(char *path)
{
	int ret = glibc_remove(path);
	fsync_dir(path);
	return ret;
}

int redo_rename(char *src, char *dest)
{
	int ret = glibc_rename(src, dest);
	fsync_dir(src);
	fsync_dir(dest);
	return ret;
}

int redo_truncate(char *path, off_t length)
{
	return truncate(path, length);
}

// returns nonzero if failed
int replay_log()
{
	FILE *fp = fopen(redo_log, "r");
	char entry[8500]; // two ext4 max paths + a lil more
	while(fgets(entry, sizeof(entry), fp)) {
		char *op = nexttok(entry);
		if (strcmp(op, "mkdir") == 0) {
			char *path = nexttok(NULL);
			mode_t mode = atoi(nexttok(NULL));
			redo_mkdir(path, mode);
		} else if (strcmp(op, "create") == 0) {
			char *path = nexttok(NULL);
			mode_t mode = atoi(nexttok(NULL));
			redo_create(path, mode);
		} else if (strcmp(op, "write") == 0) {
			char *path = nexttok(NULL);
			off_t pos = atoi(nexttok(NULL));
			off_t length = atoi(nexttok(NULL));
			char *datapath = strtok(nexttok(NULL), "\n");
			redo_write(path, pos, length, datapath);
		} else if (strcmp(op, "remove") == 0) {
			char *path = strtok(nexttok(NULL), "\n");
			redo_remove(path);
		} else if (strcmp(op, "rename") == 0) {
			char *src = nexttok(NULL);
			char *dest = strtok(nexttok(NULL), "\n");
			redo_rename(src, dest);
		} else if (strcmp(op, "truncate") == 0) {
			char *path = nexttok(NULL);
			off_t length = atoi(nexttok(NULL));
			redo_truncate(path, length);
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
	int log = glibc_open(redo_log, O_APPEND | O_RDWR);
	if (log == -1) {
		printf("Error opening log: %s\n", strerror(errno));
		return;
	}

	int written = glibc_write(log, entry, strlen(entry));
	if (written != strlen(entry)) {
		printf("Error writing to log: (actual -> %d) vs (expected -> %zd) %s\n",
			written, strlen(entry), strerror(errno));
		return;
	}

	glibc_close(log);
}

int persist_all_data()
{
	// persist all redirects
	struct vfile *vf = vfiles;
	while (vf) {
		int fd = glibc_open(vf->redirect, O_RDONLY);
		fsync(fd);
		glibc_close(fd);
		vf = vf->next;
	}

	// also need to persist log directory
	int ld = glibc_open(log_dir, O_DIRECTORY);
	fsync(ld);
	glibc_close(ld);

	// commit entry indicates log is complete
	int log = glibc_open(redo_log, O_RDWR);
	fsync(log);
	glibc_close(log);
	write_to_log("commit\n"); // do not need to call fsync() after, it's there or it isn't

	return 0;
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

		int log_fd = glibc_open(redo_log, O_CREAT | O_TRUNC | O_RDWR, 0644);
		if (log_fd == -1) {
			printf("Unable to make log at %s: (%s)\n", redo_log, strerror(errno));
			return -1;
		}
		glibc_close(log_fd);
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
		persist_all_data();
		redo();
		reset();
	}

	return 0;
}

int redo()
{
	if (!committed())
		return 0;

	if (access(bypass, F_OK) == 0)
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
		glibc_close(glibc_open(bypass, O_CREAT, 0644));
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
		struct vfile *vf = find_by_path(rp, (flags & O_CREAT)); // not null if already seen or actually exists in fs

		if (!vf)
			return -1;

		// initialize new file_desc to put in fd_map
		struct file_desc *vfd = malloc(sizeof(struct file_desc));
		vfd->pos = 0;
		if (flags & O_TRUNC)
			truncate(vfd->file->redirect, 0);
		else if (flags & O_APPEND)
			vfd->pos = filesize(vfd->file->redirect);
		vfd->file = vf;

		int ret = next_fd();
		fd_map[ret] = vfd;

		// perform logging
		char entry[5000];
		if (flags & O_CREAT) {
			snprintf(entry, sizeof(entry), "create %s %d\n", rp, mode);
			write_to_log(entry);
		}
		if (flags & O_TRUNC) {
			snprintf(entry, sizeof(entry), "truncate %s 0\n", rp);
			write_to_log(entry);
		}
		free(rp);

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
		struct file_desc* vfd = get_vfd(fd);
		if (!vfd)
			return -1;

		free(vfd);
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
		char *rp = realpath_missing(pathname);
		snprintf(entry, sizeof(entry), "mkdir %s %d\n", rp, mode);
		free(rp);

		write_to_log(entry);

		return 0;
	} else {
		return glibc_mkdir(pathname, mode);
	}
}

int rename(const char *oldpath, const char *newpath)
{
	if (!init)
		initialize();
	redo();

	if (cur_txn) {
		char *old_rp = realpath_missing(oldpath);
		char *new_rp = realpath_missing(newpath);
		struct vfile *old = find_by_path(old_rp, 0);
		struct vfile *new = find_by_path(new_rp, 0);

		// TODO: need to handle logic in rename() man pages
		snprintf(old->path, sizeof(old->path), "%s", new_rp);

		// if newpath exists, mark it as deleted in vfiles
		if (new)
			new->path[0] = '\0';

		// log operation
		char entry[10000];
		snprintf(entry, sizeof(entry), "rename %s %s\n", old_rp, new_rp);
		write_to_log(entry);

		free(old_rp);
		free(new_rp);

		return 0;
	} else {
		return glibc_rename(oldpath, newpath);
	}
}

int remove(const char *pathname)
{
	if (!init)
		initialize();
	redo();

	if (cur_txn) {
		char *rp = realpath_missing(pathname);
		struct vfile *vf = find_by_path(rp, 0);

		if (!vf)
			return -1;

		vf->path[0] = '\0';

		char entry[5000];
		snprintf(entry, sizeof(entry), "remove %s\n", rp);
		write_to_log(entry);

		free(rp);
		return 0;
	} else {
		return glibc_remove(pathname);
	}
}

ssize_t read(int fd, void *buf, size_t count)
{
	if (!init)
		initialize();
	redo();

	if (cur_txn) {
		struct file_desc *vfd = get_vfd(fd);
		if (!vfd)
			return 0;

		// read src data
		int src = glibc_open(vfd->file->src, O_RDONLY);
		glibc_lseek(src, vfd->pos, SEEK_SET);
		ssize_t red = glibc_read(src, buf, count);
		glibc_close(src);
		vfd->pos += red;

		// read redirect data
		off_t begin = vfd->pos - red;
		off_t end = vfd->pos;
		struct range *writes = vfd->file->writes;
		int rd = glibc_open(vfd->file->redirect, O_RDONLY);
		while (writes) {
			if (end >= writes->begin && begin <= writes->end) {
				off_t overlap_begin = (begin > writes->begin) ? begin : writes->begin;
				off_t overlap_end = (end < writes->end && end != overlap_begin) ? end : writes->end;
				glibc_lseek(rd, overlap_begin, SEEK_SET);
				glibc_read(rd, buf + (overlap_begin - begin), overlap_end - overlap_begin);
			}
			writes = writes->next;
		}

		return red;
	} else {
		return glibc_read(fd, buf, count);
	}
}

ssize_t write(int fd, const void *buf, size_t count)
{
	if (!init)
		initialize();
	redo();

	if (cur_txn) {
		// first check fd_map for fd, otherwise, map fd to new file_desc
		struct file_desc *vfd = get_vfd(fd);
		if (!vfd)
			return 0;

		// if file has been removed, don't write
		if (strlen(vfd->file->path) == 0)
			return 0;

		// write data to redirect at offset
		int rd = glibc_open(vfd->file->redirect, O_CREAT | O_RDWR, 0644);
		glibc_lseek(rd, vfd->pos, SEEK_SET);
		ssize_t written = glibc_write(rd, buf, count);
		glibc_close(rd);
		vfd->pos += written;

		// add range to vfile
		merge_range(vfd->file, vfd->pos - written, vfd->pos);

		// record entry in log
		char entry[10000];
		snprintf(entry, sizeof(entry), "write %s %ld %ld %s\n", vfd->file->path, vfd->pos - written, written, vfd->file->redirect);
		write_to_log(entry);

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
		struct file_desc *vfd = get_vfd(fd);

		// if extends past file, include write range
		if (length > filesize(vfd->file->redirect))
			merge_range(vfd->file, filesize(vfd->file->redirect), length);

		// log entry
		char entry[5000];
		snprintf(entry, sizeof(entry), "truncate %s %ld\n", vfd->file->path, length);
		write_to_log(entry);

		return truncate(vfd->file->redirect, length);
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
			statbuf->st_size = filesize(vfd->file->redirect);
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
			vfd->pos = filesize(vfd->file->redirect) + offset;

		return vfd->pos;
	} else {
		return glibc_lseek(fd, offset, whence);
	}
}
