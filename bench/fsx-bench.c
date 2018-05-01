#include <stdio.h>
#include <stdlib.h>

int main()
{
	unsigned long mem_writes, mem_truncs;
	unsigned long txn_writes, txn_truncs;
	char output[1024];

	FILE *fp1 = popen("make clean && make && make fsx", "r");
	fgets(output, sizeof(output) - 1, fp1);
	mem_writes = atol(output);
	fgets(output, sizeof(output) - 1, fp1);
	mem_truncs = atol(output);
	pclose(fp1);

	FILE *fp2 = popen("make clean && make && make fsx-txn", "r");
	fgets(output, sizeof(output) - 1, fp2);
	txn_writes = atol(output);
	fgets(output, sizeof(output) - 1, fp2);
	txn_truncs = atol(output);
	pclose(fp2);

	printf("writes:\n");
	printf("  mem -> %2lds %9ldns\n", mem_writes / 1000000000, mem_writes % 1000000000);
	printf("  txn -> %2lds %9ldns (overhead: %4.2fx)\n", txn_writes / 1000000000, txn_writes % 1000000000, (double) txn_writes / mem_writes);
	printf("truncates:\n");
	printf("  mem -> %2lds %9ldns\n", mem_truncs / 1000000000, mem_truncs % 1000000000);
	printf("  txn -> %2lds %9ldns (overhead: %4.2fx)\n", txn_truncs / 1000000000, txn_truncs % 1000000000, (double) txn_truncs / mem_truncs);
}
