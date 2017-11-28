#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/sched/xacct.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/uio.h>
#include <linux/fsnotify.h>
#include <linux/security.h>
#include <linux/export.h>
#include <linux/syscalls.h>
#include <linux/pagemap.h>
#include <linux/splice.h>
#include <linux/compat.h>
#include <linux/mount.h>
#include <linux/fs.h>
#include "internal.h"

#include <linux/uaccess.h>
#include <asm/unistd.h>

// TODO: use SYSCALL_DEFINE<N>()

asmlinkage long sys_hold(unsigned int fd)
{
	printk("hold called");
	struct fd f = fdget(fd);
	if (f.file) {
		f.file->hold = true;
	}
	return 0;
}

// TODO: need to release all pages associated to file
asmlinkage long sys_release(unsigned int fd)
{
	printk("release called");
	struct fd f = fdget(fd);
	if (f.file) {
		f.file->hold = false;
	}
	return 0;
}

asmlinkage long sys_check_hold(unsigned int fd)
{
	printk("check_hold called");
	struct fd f = fdget(fd);
	if (f.file) {
		if (f.file->hold)
			printk("file %d is held", fd);
		else
			printk("file %d is not held", fd);
	} else {
		printk("file undefined");
	}
	return 0;
}
