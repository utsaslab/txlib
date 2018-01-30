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

// kernel methods

struct page_node {
	struct page *page;
	struct page_node *next;
};
struct page_node *held_pages;

void hold_page(struct page *page)
{
	if (page_is_held(page))
		return;

	struct page_node *pn = kmalloc(sizeof(struct page_node), GFP_KERNEL);
	pn->page = page;
	pn->next = held_pages;
	held_pages = pn;
}

bool page_is_held(struct page *page)
{
	struct page_node *temp = held_pages;
	while (temp) {
		if (temp->page == page)
			return true;
		temp = temp->next;
	}
	return false;
}

// syscalls

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
