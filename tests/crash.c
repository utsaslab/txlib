/**
 * This test attempts to expose the consistency guarantees txnlib attempts
 * to provide. The 2 relevant "states" in which crashing needs to be tested
 * is before a recovery (in a txn) and during a recovery. Anything else is
 * out of the scope of txnlib.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "txnlib.h"

#define NS 1000000000

#define LAST_OP 5
#define MIN_OPS 100
#define MAX_OPS 999
#define MIN_WRITE_SIZE 4096
#define MAX_WRITE_SIZE 65536
#define MIN_TXNS 10
#define MAX_TXNS 50
#define MAX_TRUNC 65536
#define NOISE_SIZE 1000000

struct operation {
	/**
	 * -1 -> END
	 * 0 -> create
	 * 1 -> mkdirs
	 * 2 -> remove
	 * 3 -> write
	 * 4 -> ftruncate
	 * 5 -> rename
	 */
	int op;

	char path[4096];
	char path2[4096];
	void *data;
	size_t count;

	struct operation *next;
};

struct fs_node {
	char path[4096];
	struct fs_node *prev;
	struct fs_node *next;
};

struct operation *ops = NULL;
char noise[NOISE_SIZE];

// inclusive
int between(int min, int max) { return rand() % (max + 1 - min) + min; }

void make_noise()
{
	int fd = open("/dev/random", O_RDONLY);
	read(fd, noise, NOISE_SIZE);
	close(fd);
}

void append_fs_node(struct fs_node *add, struct fs_node **list)
{
	struct fs_node **last = list;
	while (*last)
		last = (&(*last)->next);
	(*last) = add;
}

void diff_recurse(const char *folder, struct fs_node **dir_list, struct fs_node **file_list)
{
	DIR *cur_dir;
	struct dirent *ent;
	cur_dir = opendir(folder);
	while ((ent = readdir(cur_dir)) != NULL) {
		if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
			continue;

		struct fs_node *add;
		add = malloc(sizeof(struct fs_node));
		sprintf(add->path, "%s/%s", folder, ent->d_name);
		add->next = NULL;

		if (ent->d_type == DT_DIR) {
			append_fs_node(add, dir_list);
			diff_recurse(add->path, dir_list, file_list);
		} else if (ent->d_type == DT_REG) {
			append_fs_node(add, file_list);
		} else {
			printf("UNSUPPORTED FILE TYPE: %d", ent->d_type);
		}
	}
	closedir(cur_dir);
}

// compares 2 folders; return 0 if same, 1 if different
int diff(const char *one, const char *two)
{
	// find all dirs and files within folder one
	struct fs_node *dirs = NULL;
	struct fs_node *files = NULL;
	diff_recurse(one, &dirs, &files);

	int same = 1;
	// for all dirs and files, match in folder two
	struct fs_node *dit = dirs;
	struct fs_node *fit = files;
	while (same && dit) {
		char twopath[4096];
		char subpath[4096];
		memcpy(subpath, &dit->path[strlen(one) + 1], strlen(dit->path) - strlen(one));
		subpath[strlen(dit->path) - strlen(one)] = '\0';
		sprintf(twopath, "%s/%s", two, subpath);

		DIR *dir = opendir(twopath);
		if (!dir) {
			printf("diff: dir mismatch -> %s\n", twopath);
			same = 0;
		} else {
			closedir(dir);
		}
		dit = dit->next;
	}
	while (same && fit) {
		char twopath[4096];
		char subpath[4096];
		memcpy(subpath, &fit->path[strlen(one)], strlen(fit->path) - strlen(one));
		subpath[strlen(fit->path) - strlen(one)] = '\0';
		sprintf(twopath, "%s%s", two, subpath);

		FILE *f1 = fopen(fit->path, "r");
		FILE *f2 = fopen(twopath, "r");
		char ch1, ch2;

		if (!f1 || !f2) {
			if (!f1)
				printf("diff: file mismatch -> %s\n", fit->path);
			if (!f2)
				printf("diff: file mismatch -> %s\n", twopath);
			same = 0;
		} else {
			while ( ((ch1 = fgetc(f1)) != EOF) && ((ch2 = fgetc(f2)) != EOF) )
				if (ch1 != ch2) {
					printf("diff: file contents mismatch -> (%s), (%s)\n", fit->path, twopath);
					same = 0;
				}
		}

		if (f1)
			fclose(f1);
		if (f2)
			fclose(f2);

		fit = fit->next;
	}

	while (dirs) {
		struct fs_node *to_free = dirs;
		dirs = dirs->next;
		free(to_free);
	}
	while (files) {
		struct fs_node *to_free = files;
		files = files->next;
		free(to_free);
	}

	return !same;
}

void append_operation(struct operation *op)
{
	struct operation **last = &ops;
	while (*last)
		last = &((*last)->next);
	*last = op;
}

void generate_txn(int num_ops, struct fs_node **dirs, struct fs_node **files, int *next_id, const char *log)
{
	// cleanup ops
	struct operation *old = ops;
	while (old) {
		struct operation *to_free = old;
		old = old->next;

		if (to_free->data)
			free(to_free->data);
		free(to_free);
	}
	ops = NULL;

	int num_dirs = 0;
	int num_files = 0;
	struct fs_node *temp = *dirs;
	while (temp) {
		num_dirs++;
		temp = temp->next;
	}
	temp = *files;
	while (temp) {
		num_files++;
		temp = temp->next;
	}

	for (int j = 0; j < num_ops; j++) {
		struct operation *op = malloc(sizeof(struct operation));
		op->data = NULL;
		op->next = NULL;

		// set op probs
		int roll = between(0, 100);
		if (roll < 15)
			op->op = 0;
		else if (roll < 17)
			op->op = 1;
		else if (roll < 20)
			op->op = 2;
		else if (roll < 60)
			op->op = 3;
		else if (roll < 85)
			op->op = 4;
		else
			op->op = 5;

		// don't delete last file
		if (op->op == 2 && num_files == 1)
			op->op = 0;

		// randomly select dir + file
		int dir = between(0, num_dirs - 1);
		int file = between(0, num_files - 1);
		struct fs_node *any_dir = *dirs;
		struct fs_node *any_file = *files;
		while (dir--)
			any_dir = any_dir->next;
		while (file--)
			any_file = any_file->next;

		if (op->op == 0) { // create
			struct fs_node *new_file = malloc(sizeof(struct fs_node));
			sprintf(new_file->path, "%s/%d", any_dir->path, (*next_id)++);
			new_file->prev = NULL;
			new_file->next = *files;
			(*files)->prev = new_file;
			(*files) = new_file;
			num_files++;
			sprintf(op->path, "%s", new_file->path);
		} else if (op->op == 1) { // mkdir
			struct fs_node *new_dir = malloc(sizeof(struct fs_node));
			sprintf(new_dir->path, "%s/%d", any_dir->path, (*next_id)++);
			new_dir->prev = NULL;
			new_dir->next = *dirs;
			(*dirs)->prev = new_dir;
			(*dirs) = new_dir;
			num_dirs++;
			sprintf(op->path, "%s", new_dir->path);
		} else if (op->op == 2) { // remove
			// TODO: support removing directories
			if (any_file->next)
				any_file->next->prev = any_file->prev;
			if (any_file->prev)
				any_file->prev->next = any_file->next;
			sprintf(op->path, "%s", any_file->path);

			if (any_file == *files)
				(*files) = (*files)->next;
			free(any_file);

			num_files--;
		} else if (op->op == 3) { // write
			sprintf(op->path, "%s", any_file->path);
			int size = between(MIN_WRITE_SIZE, MAX_WRITE_SIZE);
			op->data = malloc(size);
			op->count = size;

			int rand_pos = between(0, NOISE_SIZE - size);
			memcpy(op->data, noise + rand_pos, size);
		} else if (op->op == 4) { // ftruncate
			sprintf(op->path, "%s", any_file->path);
			op->count = between(0, MAX_TRUNC);
		} else if (op->op == 5) { // rename
			sprintf(op->path, "%s", any_file->path);

			struct fs_node *moved = malloc(sizeof(struct fs_node));
			sprintf(moved->path, "%s-moved", any_file->path);
			moved->prev = NULL;
			moved->next = *files;
			(*files)->prev = moved;
			(*files) = moved;

			sprintf(op->path2, "%s", moved->path);

			if (any_file->next)
				any_file->next->prev = any_file->prev;
			if (any_file->prev)
				any_file->prev->next = any_file->next;
			if (any_file == *files)
				(*files) = (*files)->next;
			free(any_file);
		}

		char tlog[8192];
		sprintf(tlog, "%d %s\n", op->op, op->path);
		int fd = open(log, O_APPEND | O_RDWR);
		write(fd, tlog, strlen(tlog));
		close(fd);

		// printf("gen %d %s\n", op->op, op->path);
		append_operation(op);
	}

	// add end_txn operation
	struct operation *end = malloc(sizeof(struct operation));
	end->op = -1;
	sprintf(end->path, "<END>");
	end->data = NULL;
	end->next = NULL;
	append_operation(end);
}

void perform_ops(const char *folder)
{
	struct operation *cur = ops;
	while (cur) {
		char my_path[8192];
		sprintf(my_path, "%s/%s", folder, cur->path);

		if (cur->op == -1) { // end
			return;
		} else if (cur->op == 0) { // create
			int fd = open(my_path, O_CREAT, 0644);
			if (fd == -1) {
				printf("create(%s) op failed: %s\n", my_path, strerror(errno));
				exit(66);
			}
			close(fd);
		} else if (cur->op == 1) { // mkdir
			mkdir(my_path, 0755);
		} else if (cur->op == 2) { // remove
			int err = remove(my_path);
			if (err) {
				printf("remove(%s) op failed: %s\n", my_path, strerror(errno));
				exit(55);
			}
		} else if (cur->op == 3) { // write
			int fd = open(my_path, O_RDWR);
			write(fd, cur->data, cur->count);
			close(fd);
		} else if (cur->op == 4) { // ftruncate
			int fd = open(my_path, O_RDWR);
			ftruncate(fd, cur->count);
			close(fd);
		} else if (cur->op == 5) {
			char move[8192];
			sprintf(move, "%s/%s", folder, cur->path2);
			rename(my_path, move);
		} else {
			printf("unsupported operation: %d\n", cur->op);
		}
		cur = cur->next;
	}
}

// ========== processes ==========

void phoenix()
{
	// obtain the redo log
	int id = begin_txn();
	perform_ops("out/txn");
	save_log("out/redo-log.save");
	end_txn(id);

	// set things up to redo
	set_bypass(1);
	system("rm -rf out/txn");
	system("cp -r out/before out/txn");
	system("cp out/redo-log.save /var/tmp/txnlib/redo-log");
	set_bypass(0);

	// try to base kill times on actual runtime
	struct timeval start, finish;
	gettimeofday(&start, NULL);
	redo();
	gettimeofday(&finish, NULL);
	uint64_t runtime = 0;
	runtime += 1000000000 * (finish.tv_sec - start.tv_sec);
	runtime += 1000 * (finish.tv_usec - start.tv_usec);

	printf("crashes -> "); fflush(stdout);

	int done = 0;
	double kill_prob = 1000;
	int crashes = 0;
	while (!done) {
		// recover from whatever fs state after kill
		// check match
		// reset folders
		// call redo() and kill in the middle
		redo();
		if (diff("out/txn", "out/after") || diff("out/after", "out/txn")) {
			printf("REDO() FAILED!!!\n");
			exit(88);
		}

		// reset out/txn
		set_bypass(1);
		system("rm -rf out/txn");
		system("cp -r out/before out/txn");
		system("cp out/redo-log.save /var/tmp/txnlib/redo-log");
		set_bypass(0);

		int worker = fork();
		if (worker == 0) {
			redo();
			_exit(0);
		} else {
			int roll = between(0, 1000);
			if (roll < kill_prob) {
				struct timespec ts;
				ts.tv_sec = between(0, runtime / NS);
				ts.tv_nsec = between(0, runtime % NS);
				nanosleep(&ts, NULL);
				kill(worker, SIGKILL);
				kill_prob -= 0.05;
				crashes++;
			} else {
				done = 1;
				printf("%3d\n", crashes);
			}

			int status;
			waitpid(worker, &status, 0);
			int result = WEXITSTATUS(status);
			if (result) {
				printf("worker() error: %d\n", result);
				exit(111);
			}
		}
	}
	delete_log();
}

void test(int num_txns, int c)
{
	int next_id = 0;

	system("rm -rf out/before out/after out/txn");

	// initialize with one folder and one file
	mkdir("out/before", 0755);
	mkdir("out/before/a", 0755);
	close(open("out/before/b.txt", O_CREAT, 0644));
	struct fs_node *dirs = malloc(sizeof(struct fs_node));
	struct fs_node *files = malloc(sizeof(struct fs_node));
	dirs->prev = NULL;
	dirs->next = NULL;
	files->prev = NULL;
	files->next = NULL;
	sprintf(dirs->path, "a");
	sprintf(files->path, "b.txt");

	for (int i = 0; i < num_txns; i++) {
		// logging
		char pls[4096];
		sprintf(pls, "out/gen/%d/txn-%d.log", c, i);
		int fd = open(pls, O_CREAT, 0644);
		close(fd);

		make_noise();

		int num_ops = between(MIN_OPS, MAX_OPS);
		printf(" - txn #%2d: num_ops -> %2d, ", i, num_ops); fflush(stdout);
		generate_txn(num_ops, &dirs, &files, &next_id, pls);

		// initialize after/ and txn/
		system("rm -rf out/after out/txn");
		system("cp -r out/before out/after");
		system("cp -r out/before out/txn");

		perform_ops("out/after");
		phoenix();

		// checkpoint
		system("rm -rf out/before");
		system("mv out/after out/before");
	}
}

int main()
{
	time_t tt = time(NULL);
	printf("seed: %ld\n", tt);
	srand(tt); // comment for determinism

	// tweak
	int num_tests = 100;

	// for logging
	system("rm -rf out/gen");
	system("mkdir out/gen");

	for (int i = 0; i < num_tests; i++) {
		char gen[4096];
		sprintf(gen, "mkdir out/gen/%d", i);
		system(gen);

		int num_txns = between(MIN_TXNS, MAX_TXNS);
		printf("========== TEST #%d: %d txns ==========\n", i, num_txns);
		test(num_txns, i);
	}
	printf("ALL %d TESTS COMPLETED SUCCESSFULLY!!! :)\n", num_tests);
}
