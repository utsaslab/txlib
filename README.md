# TXNLIB

The purpose of this library is to provide filesystem-agnostic transactional
features for applications written in C. Currently, transactional filesystems do
exist, but they are filesystem specific (e.g., btfs, ext4, ext2). txnlib
wraps around Unix system calls and leverages the portability of C to allow
cross-platform support.

This library uses [write-ahead](http://work.tinou.com/2012/09/write-ahead-log.html) and undo logging.

## Usage

One of the main focuses for txnlib is ease of use. The ideal use case for
txnlib is to convert an existing block of code to be transactional simply by
throwing beginning and ending transactional calls around the targeted area.
Because txnlib wraps around Unix system calls for file I/O, there should be
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

## Support

txnlib currently wraps:

* `open`
* `mkdir`
* `remove`
* `write`
* `close`

## TODO

* wrap
  * `creat` + `openat`
  * `read`
  * `close`
  * `rename` + `renameat` + `renameat2`
  * `access` + `faccessat`
  * `truncate` + `ftruncate`
  * `ftell` + `fseek` + `lseek`
  * (much mo fsho)

## Design and Implementation

As said above, an important goal for txnlib is ease of use. Applications should be able to easily include transactions into the original codebase. To accomplish this, txnlib wraps around `glibc` filesystem calls, like `open`, `read`, `write`, `close`, etc. so that developers only need to make minimal changes. For each of these calls, before the corresponding `glibc` function is invoked, txnlib performs write-ahead logging. When a transaction is committed, all recorded logs are deleted as there are no longer needed. In the case of crash and recovery, txnlib checks for the existence of logs as an indication to whether a crash had occurred. If a crash is detected, txnlib performs a full recovery of the filesystem. (Note: an on-demand, lazy recovery protocol was considered, but due to some edge cases regarding filesystem complications, this idea has been ruled out.)
