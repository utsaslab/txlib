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
#define MAX_OPS 1000
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

void make_noise() {
        int fd = open("/dev/random", O_RDONLY);
        read(fd, noise, NOISE_SIZE);
        close(fd);
}

void append_fs_node(struct fs_node *add, struct fs_node *list)
{
        struct fs_node **last = &list;
        while (*last)
                last = (&(*last)->next);
        *last = add;
}

void diff_recurse(const char *folder, struct fs_node *dir_list, struct fs_node *file_list)
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

                if (ent->d_type == DT_DIR) {
                        append_fs_node(add, dir_list);
                        diff_recurse(add->path, dir_list, file_list);
                } else if (ent->d_type == DT_REG) {
                        append_fs_node(add, file_list);
                } else {
                        printf("UNSUPPORTED FILE TYPE: %d", ent->d_type);
                }
        }
}

// compares 2 folders; return 0 if same, 1 if different
int diff(const char *one, const char *two)
{
        // find all dirs and files within folder one
        struct fs_node *dirs = NULL;
        struct fs_node *files = NULL;
        diff_recurse(one, dirs, files);

        int same = 1;
        // for all dirs and files, match in folder two
        struct fs_node *dit = dirs;
        struct fs_node *fit = files;
        while (dit) {
                char twopath[4096];
                char subpath[4096];
                memcpy(subpath, &dit->path[strlen(one) + 1], strlen(dit->path) - strlen(one));
                sprintf(twopath, "%s/%s", two, subpath);

                DIR *dir = opendir(twopath);
                if (!dir)
                        same = 0;
                closedir(dir);
                dit = dit->next;
        }
        while (fit) {
                char twopath[4096];
                char subpath[4096];
                memcpy(subpath, &fit->path[strlen(one)], strlen(fit->path) - strlen(one));
                sprintf(twopath, "%s%s", two, subpath);

                FILE *f1 = fopen(fit->path, "r");
                FILE *f2 = fopen(twopath, "r");
                char ch1, ch2;

                if (!f1 || !f2)
                        same = 0;

                while ( ((ch1 = fgetc(f1)) != EOF) && ((ch2 = fgetc(f2)) != EOF) ) {
                        if (ch1 != ch2)
                                same = 0;
                }

                fclose(f1);
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

void generate_txn(int num_ops, struct fs_node **dirs, struct fs_node **files, int *next_id)
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
                if (roll < 20)
                        op->op = 0;
                else if (roll < 25)
                        op->op = 1;
                else if (roll < 30)
                        op->op = 2;
                else if (roll < 80)
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
                        close(fd);
                } else if (cur->op == 1) { // mkdir
                        mkdir(my_path, 0755);
                } else if (cur->op == 2) { // remove
                        remove(my_path);
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
        recover();

        // compare txn and before
        if (diff("out/txn", "out/before")) {
                printf("RECOVERY FAILED!!!\n");
                exit(1);
        }

        int id = begin_txn();
        perform_ops("out/txn");
        save_log(1);
        end_txn(id);

        if (diff("out/txn", "out/after")) {
                printf("TRANSACTION FAILED\n");
                exit(1);
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
                                kill_prob -= 0.01;
                                crashes++;
                        } else {
                                done = 1;
                                printf("crashes -> %d\n", crashes);
                        }
                        wait(NULL);
                }
        }
        delete_log();
}

void test(int num_txns)
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
                int num_ops = between(MIN_OPS, MAX_OPS);
                printf(" - txn #%d: num_ops -> %d, ", num_txns - i, num_ops);
                generate_txn(num_ops, &dirs, &files, &next_id);

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
        int num_tests = 10;

        for (int i = 0; i < num_tests; i++) {
                int num_txns = between(MIN_TXNS, MAX_TXNS);
                printf("========== TEST #%d / %d: %d txns ==========\n", i+1, num_tests, num_txns);
                test(num_txns);
        }
}
