# txnlib

The purpose of this library is to provide filesystem-agnostic transactional
features for applications written in C. Currently, transactional filesystems do
exist, but they are filesystem specific (e.g., btfs, ext4, ext2). txnlib
wraps around glibc system calls and leverages the portability of C to allow
cross-platform support.

This library uses
[write-ahead](http://work.tinou.com/2012/09/write-ahead-log.html) and
[undo](http://mlwiki.org/index.php/Undo_Logging) logging.

## Usage

One of the main focuses for txnlib is ease of use. The ideal use case for
txnlib is to make an existing block of code to be transactional simply by
throwing beginning and ending transactional calls around the targeted area.
Because txnlib wraps around glibc system calls for file I/O, there should be
minimal edits made to the existing code when making this conversion. Adding
transactions (properly) should not affect the correctness, compilation, or
logic of the existing code.

Below is a simple example.

```c
#include <txnlib.h>

int txn_id = begin_txn();

int fd = open("bottom.txt", O_CREAT | O_RDWR, 0644);
write(fd, "hello transactional world\n", 26);
write(fd, "goodbye\n", 8);
close(fd);

end_txn(txn_id);
```

## Testing

The repo already includes some useful tests in assessing the correctness and
performance of txnlib. To run a particular test, enter `make clean && make &&
make <target>`. The targets are listed in the Makefile and described below.

* `test` - Runs all tests within the `tests/` directory.
* `crash` - Processes transactions while repeatedly spawning and killing the
	recovery process. Reveals potential race conditions or implementation
	errors through stress.
* `benchmark` - Contains 3 different benchmarks. One for `remove()`, one for
	`write()`, and one for comparing to the "vim" method of saving files.
	`benchmark.c` must be modified to run the different tests.
* `big-writes` - Measures how `redo()` runtimes scale with the amount of data
	written within a transaction.
* `fsx-bench` - Benchmarks the overhead of wrapping a transaction around an
	`fsx` workload.
* `sqlite-bench` - Compares the runtime difference between ?equivalent?
	workloads for txnlib and SQLite.

## Support

txnlib currently wraps:

* `open`
* `close`
* `mkdir`
* `rename`
* `remove`
* `read`
* `write`
* `ftruncate`
* `fstat`
* `lseek`

## TODO

* wrap more glibc functions
  * `creat` + `openat`
  * `renameat` + `renameat2`
  * `access` + `faccessat`
  * `chmod`
  * `truncate`
  * `fallocate`
* support memory mapping files
* combine all logging into one file for optimization (fewer calls to `fsync()`)
* support multi-threaded usage

## Design and Implementation

As stated above, an important goal for txnlib is ease of use. Applications should
be able to easily include transactions into the original codebase. To accomplish
this, txnlib wraps around glibc filesystem calls, like `open`, `mkdir`, `write`,
`rename`, etc. so that developers only need to make minimal changes. For each of
these calls, before the corresponding glibc function is invoked, txnlib performs
write-ahead logging. When a transaction is committed, all recorded logs are
deleted as they are no longer needed. In the case of crash and recovery, txnlib
checks for the existence of logs as an indication to whether a crash had
occurred. If a crash is detected, txnlib performs a full recovery of the
filesystem.

## Notes

Implementations for undo logging and unoptimized redo logging exist in other
branches. Also, we managed to port vim in 2 lines!
