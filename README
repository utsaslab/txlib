# TXNLIB

The purpose of this library is to provide filesystem-agnostic transactional
features for applications written in C. Currently, transactional filesystems do
exist, but they are filesystem specific (e.g., btfs, ext4, ext2). txnlib
wraps around Unix system calls and leverages the portability of C to allow
cross-platform support.

This library uses [write ahead logging](http://work.tinou.com/2012/09/write-ahead-log.html).

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

txnlib currently supports:

* `open`
* `close`
* `write`

## TODO

* utilize the page cache
* wrap
  * `lseek`
  * `fsync` + `fdatasync`
  * (much more fsho)
