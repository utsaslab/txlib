#include <stdio.h>
#include "../lib/txlib.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main()
{
	// begin transaction
	// create file
	// write to file
	// close file
	// commit transaction
	// int fd = open("hello.txt", O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
	// write(fd, "wowie", 5);
	// close(fd);
	// int tx_id = begin_transaction();
	// int temp = txopen("hello.txt", O_CREAT | O_RDWR);
	// txclose(temp);
	// end_transaction(tx_id);
	void* buf = malloc(4);
	char* word = (char*) buf;
	printf("%s", word);
}
