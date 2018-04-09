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
static int keep_log = 0;
static int next_id = 0;; // TODO: prevent overflow
static int backup_id = 0;
static const char *log_dir = "logs";
static const char *undo_log = "logs/undo_log";
static const char *reversed_log = "logs/reversed_log";
static const char *bypass = "logs/bypass"; // prevent issues when using system()
static struct txn *cur_txn = NULL;
static struct file_node *logged = NULL;

// ========== helper methods ==========

int crashed() { return !cur_txn && (access(undo_log, F_OK) == 0); }

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

void write_to_log(const char *entry)
{
	int fd = glibc_open(undo_log, O_RDWR | O_APPEND);
	if (fd == -1) {
		printf("Error opening log: %s\n", strerror(errno));
		exit(1);
	}

	int written = glibc_write(fd, entry, strlen(entry));
	if (written != strlen(entry))
		printf("Error writing to log: (actual -> %d) vs (expected -> %zd) %s\n",
			written, strlen(entry), strerror(errno));

	int syn = fsync(fd);
	if (syn)
		printf("Error flushing log: %s\n", strerror(errno));

	int err = close(fd);
	if (err)
		printf("Error closing log: %s\n", strerror(errno));
}

// returns absolute path even if file doesn't exist
// (need to free returned pointer)
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
char *nexttok(char *line) { return strtok(line, line ? " \n" : " "); }

// for some reason, there is no easy way of doing this
// I tried calling system("tac ..."), but that freezes for some reason
// and there is no easy way in C to do this afaik :|
void generate_reversed_log()
{
	set_bypass(1);
	// find number of lines
	int num_lines = 0;
	FILE *fptr = fopen(undo_log, "r");
	char temp[4096];
	while (fgets(temp, 4096, fptr))
		num_lines++;

	// iterate backwards
	int rev = glibc_open(reversed_log, O_CREAT | O_RDWR | O_TRUNC, 0644);
	for (int i = num_lines; i > 0; i--) {
		rewind(fptr);
		char entry[4096];
		for (int j = 0; j < i; j++)
			fgets(entry, 4096, fptr);
		glibc_write(rev, entry, strlen(entry));
	}
	fclose(fptr);
	close(rev);
	set_bypass(0);
}

off_t filesize(const char *path)
{
	struct stat metadata;
	if (stat(path, &metadata) == 0)
		return metadata.st_size;
	else
		return -1;
}

void copy(const char *dest, const char *src)
{
	FILE *d = fopen(dest, "w");
	FILE *s = fopen(src, "r");
	char c;

	while ( (c = fgetc(s)) != EOF )
		fputc(c, d);

	fclose(d);
	fclose(s);
}

// ========== testing ==========

void crash()
{
	close(glibc_open("logs/crashed", O_CREAT, 0644));
	cur_txn = NULL;
	logged = NULL;
}

// ========== vanilla log recovery methods ==========

int undo_create(const char *path)
{
	return glibc_remove(path);
}

int undo_remove(const char *path, const char *backup)
{
	char cp[4096];
	sprintf(cp, "%s.copy", backup);
	copy(cp, backup);
	return glibc_rename(cp, path);
}

int undo_rename(const char *old, const char *new)
{
	return glibc_rename(new, old);
}

int undo_write(const char *path, int pos, int range, int prev_size, const char *backup)
{
	char *saved = malloc(range);
	int bup = glibc_open(backup, O_RDWR);
	ssize_t bupped = glibc_read(bup, saved, range);
	close(bup);

	int dirty = glibc_open(path, O_RDWR);
	if (dirty == -1) // okay if file doesn't exist bc may have crashed during recovery
		return -1;
	lseek(dirty, pos, SEEK_SET);
	ssize_t restored = glibc_write(dirty, saved, range);
	glibc_ftruncate(dirty, prev_size);
	close(dirty);
	free(saved);

	// this is okay bc still idempotent
	if (filesize(path) != prev_size || restored != range) {
		// printf("Error in undo_write. (expected: %d, backup: %zd, restored %zd)\n",
		// 	range, bupped, restored);
		return -1;
	}

	return 0;
}

int undo_touch(const char *path, const char *metadata)
{
	// mode -> user -> group -> atim -> mtim -> ctim
	FILE *md = fopen(metadata, "r");
	int size = 4096;
	char buf[size];
	fgets(buf, size, md);
	int mode = atoi(nexttok(buf));
	fgets(buf, size, md);
	int user = atoi(nexttok(buf));
	fgets(buf, size, md);
	int group = atoi(nexttok(buf));
	fgets(buf, size, md);
	int atim_sec = atoi(nexttok(buf));
	int atim_usec = atoi(nexttok(NULL)) % 1000;
	fgets(buf, size, md);
	int mtim_sec = atoi(nexttok(buf));
	int mtim_usec = atoi(nexttok(NULL)) % 1000;

	int err = chmod(path, mode);
	if (err) {
		// printf("Error restoring mode in undo_touch(). (%s)\n", strerror(errno));
		return -1;
	}
	err = chown(path, user, group);
	if (err) {
		printf("Error chowning in undo_touch(). (%s)\n", strerror(errno));
		return -1;
	}

	struct timeval tvs[2];
	struct timeval access;
	struct timeval modify;
	access.tv_sec = atim_sec;
	access.tv_usec = atim_usec;
	modify.tv_sec = mtim_sec;
	modify.tv_usec = mtim_usec;
	tvs[0] = access;
	tvs[1] = modify;

	err = utimes(path, tvs);
	if (err) {
		printf("Error restoring utimes in undo_touch(). (%s)\n", strerror(errno));
		return -1;
	}

	return 0;
}

int recover_log()
{
	generate_reversed_log();
	FILE *fptr = fopen(reversed_log, "r");
	int length = 4096;
	char entry[length];
	while (fgets(entry, length, fptr)) {
		char *op = nexttok(entry);
		if (strcmp("create", op) == 0) {
			char *path = strtok(nexttok(NULL), "\n"); // trim newline
			undo_create(path);
		} else if (strcmp("remove", op) == 0) {
			char *path = nexttok(NULL);
			char *backup = strtok(nexttok(NULL), "\n"); // newline
			undo_remove(path, backup);
		} else if (strcmp("rename", op) == 0) {
			char *old = nexttok(NULL);
			char *new = strtok(nexttok(NULL), "\n");
			undo_rename(old, new);
		} else if (strcmp("write", op) == 0) {
			char *path = nexttok(NULL);
			int pos = atoi(nexttok(NULL));
			int range = atoi(nexttok(NULL));
			int size = atoi(nexttok(NULL));
			char *backup = strtok(nexttok(NULL), "\n"); // trim newline
			undo_write(path, pos, range, size, backup);
		} else if (strcmp("touch", op) == 0) {
			char *path = nexttok(NULL);
			char *metadata = strtok(nexttok(NULL), "\n");
			undo_touch(path, metadata);
		} else if (!op) { // TODO: why is op sometimes null?
			printf("Operation is null in recover_log().\n");
			break;
		}
	}
	fclose(fptr);
	return 0;
}

// ========== API ==========

int begin_txn(void)
{
	if (!init)
		initialize();

	recover();

	if (!cur_txn) { // beginning transaction
		set_bypass(1);
		system("rm -rf logs");
		set_bypass(0);

		int err = glibc_mkdir(log_dir, 0777);
		if (err && errno != EEXIST) {
			printf("Unable to make log directory at %s: (%s)\n", log_dir, strerror(errno));
			return -1;
		}

		int fd = glibc_open(undo_log, O_CREAT | O_EXCL, 0644);
		if (fd == -1) {
			printf("Unable to make undo log at %s: (%s)\n", undo_log, strerror(errno));
			return -1;
		}
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
	// TODO: should this be allowed?
	if (txn_id != cur_txn->id) {
		printf("attempting to end (%d) but current transaction is (%d)\n", txn_id, cur_txn->id);
		return -1;
	}

	void *ended = cur_txn;
	cur_txn = cur_txn->next;
	free(ended);

	if (!cur_txn) {
		// commit everything and then delete log
		sync();
		if (!keep_log) {
			glibc_remove(undo_log);
			keep_log = 0;
		}
	}

	return 0;
}

int recover()
{
	if (!crashed())
		return 0;

	if ((access(bypass, F_OK) == 0))
		return 0;

	if (cur_txn) {
		printf("Crash detected within transaction. Undefined state.\n");
		return -1;
	}

	// check for existence of undo_log
	if (access(undo_log, F_OK) == -1) {
		printf("Crash detected but cannot find undo log at %s\n", undo_log);
		return -1;
	}

	recover_log();
	glibc_rename("logs/undo_log", "logs/recovered_undo_log"); // existence of undo log indicates crash

	return 0;
}

void save_log(int keep) { keep_log = keep; }

int delete_log() { return glibc_remove(undo_log); }

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
		sprintf(metadata_loc, "%s/%d.meta", log_dir, backup_id++);

		struct stat md;
		stat(pathname, &md);
		char relevant[4096];
		sprintf(relevant, "%d\n%d\n%d\n%ld %ld\n%ld %ld\n", md.st_mode, md.st_uid, md.st_gid, md.st_atim.tv_sec, md.st_atim.tv_nsec, md.st_mtim.tv_sec, md.st_mtim.tv_nsec);

		int metadata = glibc_open(metadata_loc, O_CREAT | O_RDWR | O_TRUNC, 0644);
		glibc_write(metadata, relevant, strlen(relevant));
		close(metadata);

		// create log entry
		sprintf(entry, "touch %s %s\n", rp, metadata_loc);
	} else {
		printf("undefined open() state: %s creating -> %d, exists -> %d\n", pathname, creating, exists);
		// exit(1);
	}
	free(rp);
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

int mkdir(const char *pathname, mode_t mode)
{
	if (!init)
		initialize();

	recover();

	if (cur_txn) {
		char entry[4096];
		char *rp = realpath_missing(pathname);
		sprintf(entry, "create %s\n", rp);
		write_to_log(entry);
		free(rp);
	}

	return glibc_mkdir(pathname, mode);
}

int rename(const char *oldpath, const char *newpath)
{
	if (!init)
		initialize();

	recover();

	if (cur_txn) {
		char entry[4096];
		char *old_rp = realpath_missing(oldpath);
		char *new_rp = realpath_missing(newpath);
		sprintf(entry, "rename %s %s\n", old_rp, new_rp);
		write_to_log(entry);
		free(old_rp);
		free(new_rp);
	}

	return glibc_rename(oldpath, newpath);
}

int remove(const char *pathname)
{
	if (!init)
		initialize();

	recover();

	if (!cur_txn)
		return glibc_remove(pathname);

	char entry[4096];
	char backup[4096];
	char *rp = realpath_missing(pathname);
	sprintf(backup, "%s/%d", log_dir, backup_id++);
	sprintf(entry, "remove %s %s\n", rp, backup);
	write_to_log(entry);

	free(rp);

	return glibc_rename(pathname, backup);
}

ssize_t write(int fd, const void *buf, size_t count)
{
	if (!init)
		initialize();

	recover();

	if (cur_txn) {
		off_t pos = lseek(fd, 0, SEEK_CUR);
		char *path = get_path_from_fd(fd);
		char entry[4096];

		off_t fsize = filesize(path);
		int zeros = 0; // lseek past filesize fills with zeros
		if (fsize < pos) {
			zeros = pos - fsize;
			pos = fsize;
		}

		// backup data that will be overwritten
		char *bup = malloc(count);
		int read_fd = glibc_open(path, O_RDWR);
		lseek(read_fd, pos, SEEK_SET);
		int bup_size = glibc_read(read_fd, bup, count);
		close(read_fd);

		char backup_loc[4096];
		sprintf(backup_loc, "%s/%d.data", log_dir, backup_id++);
		int backup_data = glibc_open(backup_loc, O_CREAT | O_RDWR, 0644);
		glibc_write(backup_data, bup, bup_size);
		fsync(backup_data);
		close(backup_data);

		/**
		 * TODO: need a way to figure out how many bytes will actually
		 *       be written, or log would be inaccurate if count is not returned
		 */
		sprintf(entry, "write %s %ld %ld %ld %s\n", path, pos, count + zeros, fsize, backup_loc);
		write_to_log(entry);
		free(path);
	}

	return glibc_write(fd, buf, count);
}

int ftruncate(int fd, off_t length)
{
	if (!init)
		initialize();

	recover();

	if (!cur_txn)
		return glibc_ftruncate(fd, length);

	char *path = get_path_from_fd(fd);
	off_t size = filesize(path);

	struct stat st;
	stat(path, &st);

	// TODO: reset file offset
	if (length > size) {
		off_t extend = length - size;
		char *zeros = calloc(extend, 1);

		int fd1 = glibc_open(path, O_RDWR, st.st_mode);
		lseek(fd1, 0, SEEK_END);
		write(fd1, zeros, extend); // TODO: verify written
		close(fd1);

		free(zeros);
		free(path);

		return 0;
	} else {
		char *trunk = malloc(length);
		lseek(fd, 0, SEEK_SET);
		glibc_read(fd, trunk, length);

		remove(path);
		int fd1 = open(path, O_CREAT | O_RDWR, st.st_mode);
		write(fd1, trunk, length);
		dup2(fd1, fd);
		close(fd1);

		free(trunk);
		free(path);
		return 0;
	}
}
