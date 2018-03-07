# My Contributions

The concept of a transactional filesystem is certainly not a novel idea. However, txnlib utilizes its unique position in userspace to provide some optimizations over the vanilla write-head + undo logging implementations.

## Tree log

Write-ahead logging typically records every modification made to underlying storage. However, in the case of a filesystem, there are certain cases in which modifications do not need to be logged. For example, within a transaction, if a user creates a folder `root` and then creates many recursive subdirectories and files, none of the following creations need to be logged to semantically undo the transaction. In the case where this example transaction needs to be rolled back, a simple instruction to delete `root` is sufficient. This optimization is provided through the use of a tree log. Modifications are represented as a tree to mirror the structure of an actual filesystem. txnlib then omits logs that are deemed superfluous.
