#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "txlib.h"

static int next_id; // TODO: prevent overflow
static struct txn *cur_tx;

int begin_transaction(void)
{
	// log in current transaction
	if (cur_tx) {
		write(cur_tx->log_fd, "begin ", 6);
	}

	// create redo log name
	char buf[64];
	strcat(buf, "txn-");
	char id[64];
	sprintf(id, "%d", next_id);
	strcat(buf, id);
	strcat(buf, ".log");

	struct txn *tx = malloc(sizeof(struct txn));
	tx->id = next_id++;
	tx->log_fd = open(buf, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
	tx->next = cur_tx;
	cur_tx = tx;
	return tx->id;
}

int end_transaction(int tx_id)
{
	if (tx_id != cur_tx->id) {
		printf("attempting to end noncurrent transaction (%d vs %d)", tx_id, cur_tx->id);
		return 1;
	}
	write(cur_tx->log_fd, "commit", 6);
	close(cur_tx->log_fd);
	void *ended = cur_tx;
	cur_tx = cur_tx->next;
	free(ended);
	return 0;
}

int txopen(const char *pathname, int flags)
{
	write(cur_tx->log_fd, "open ", 5);
	write(cur_tx->log_fd, pathname, strlen(pathname));
	write(cur_tx->log_fd, " ", 1);
	char buf[64];
	sprintf(buf, "%d", flags);
	write(cur_tx->log_fd, buf, strlen(buf));
	write(cur_tx->log_fd, "\n", 1);
	return 0;
}

int txclose(int fd)
{
	write(cur_tx->log_fd, "close ", 6);
	char buf[64];
	sprintf(buf, "%d", fd);
	write(cur_tx->log_fd, buf, strlen(buf));
	write(cur_tx->log_fd, "\n", 1);
	return 0;
}

ssize_t txread(int fd, void *buf, size_t count)
{
	return 0;
}

ssize_t txwrite(int fd, const void *buf, size_t count)
{
	return 0;
}

int txfysnc(int fd)
{
	return 0;
}
