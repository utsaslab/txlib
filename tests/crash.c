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
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "txnlib.h"

#define LAST_OP 4
#define MIN_OPS 10
#define MAX_OPS 50
#define MAX_WAIT_SEC 1
#define MAX_WAIT_NSEC 999999999
#define MIN_WRITE_SIZE 4096
#define MAX_WRITE_SIZE 65536
#define MIN_TXNS 5
#define MAX_TXNS 50
#define MAX_TRUNC 65536
#define NOISE_SIZE 1000000

struct operation {
        /**
         * -1 -> END
         * 0 -> create
         * 1 -> mkdir
         * 2 -> remove
         * 3 -> write
         * 4 -> ftruncate
         */
        int op;

        char path[4096];
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

// compares 2 folders; return 0 if same, 1 if different
int diff(const char *one, const char *two)
{
        set_bypass(1);

        char cmd[4096];
        sprintf(cmd, "diff -r %s %s", one, two);
        FILE *fp = popen(cmd, "r");
        char output[4096];
        memset(output, '\0', 4096);
        fgets(output, 4096 - 1, fp);
        pclose(fp);

        set_bypass(0);

        return strlen(output);
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
                else if (roll < 70)
                        op->op = 3;
                else
                        op->op = 4;

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
                                printf("create() op failed: %s\n", strerror(errno));
                                exit(66);
                        }
                        close(fd);
                } else if (cur->op == 1) { // mkdir
                        mkdir(my_path, 0755);
                } else if (cur->op == 2) { // remove
                        int err = remove(my_path);
                        if (err) {
                                printf("remove() op failed: %s\n", strerror(errno));
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
                } else {
                        printf("unsupported operation: %d\n", cur->op);
                }
                cur = cur->next;
        }
}

// ========== threads ==========

void work()
{
        // recover, then compare before/ and txn/
        set_bypass(0);
        recover();

        // compare txn and before
        if (diff("out/txn", "out/before")) {
                printf("RECOVERY FAILED!!!\n");
                exit(66);
        }

        int id = begin_txn();
        perform_ops("out/txn");
        save_log(1);
        end_txn(id);

        if (diff("out/txn", "out/after")) {
                printf("TRANSACTION FAILED\n");
                exit(77);
        }
}

// repeatedly kill and revive work()
void phoenix()
{
        int done = 0;
        double kill_prob = 100;
        int crashes = 0;
        while (!done) {
                int worker = fork();
                if (worker == 0) {
                        work();
                        _exit(0);
                } else {
                        int roll = between(0, 100);
                        if (roll < kill_prob) {
                                int sec = between(0, MAX_WAIT_SEC);
                                int nsec = between(0, MAX_WAIT_NSEC);
                                struct timespec ts;
                                ts.tv_sec = sec;
                                ts.tv_nsec = nsec;
                                nanosleep(&ts, NULL);
                                kill(worker, SIGKILL);
                                kill_prob -= 0.05;
                                crashes++;
                        } else {
                                done = 1;
                                printf("crashes -> %d\n", crashes);
                        }
                        
                        int status;
                        waitpid(worker, &status, 0);
                        int result = WEXITSTATUS(status);
                        if (result) {
                                printf("worker() error: %d\n", result);
                                exit(62);
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
                printf(" - txn #%d: num_ops -> %d, ", i, num_ops);
		fflush(stdout);
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
        int num_tests = 50;

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
}
