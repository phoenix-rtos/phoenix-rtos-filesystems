/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Jffs2 FileSystem - system specific information.
 *
 * Copyright 2018 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */


#ifndef _OS_PHOENIX_H_
#define _OS_PHOENIX_H_

#include <stdlib.h>
#include <sys/threads.h>
#include <sys/rb.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include "os-phoenix/completion.h"
#include "os-phoenix/dev.h"
#include "os-phoenix/rb.h"
#include "os-phoenix/types.h"


struct mutex {
	handle_t h;
};


struct dir_context {
	int todo;
};

struct dentry {
	int todo;
};

#define __init
#define __user

struct file {
	int todo;
};


struct vm_area_struct {
	int todo;
};


struct pipe_inode_info {
	int todo;
};


struct kiocb {
	int todo;
};


struct iov_iter {
	int todo;
};


struct inode {
	struct timespec i_mtime;
	int todo;
};


ssize_t generic_read_dir(struct file *filp, char __user *buf, size_t siz, loff_t *ppos)
{
		return -EISDIR;
}

struct file_operations {
	loff_t (*llseek) (struct file *, loff_t, int);
	ssize_t (*read) (struct file *, char __user *, size_t, loff_t *);
	ssize_t (*read_iter) (struct kiocb *, struct iov_iter *);
	ssize_t (*write_iter) (struct kiocb *, struct iov_iter *);
	int (*iterate_shared) (struct file *, struct dir_context *);
	long (*unlocked_ioctl) (struct file *, unsigned int, unsigned long);
	int (*mmap) (struct file *, struct vm_area_struct *);
	int (*open) (struct inode *, struct file *);
	int (*fsync) (struct file *, loff_t, loff_t, int datasync);
	ssize_t (*splice_read)(struct file *, loff_t *, struct pipe_inode_info *, size_t, unsigned int);
};


struct list_head {
	struct list_head *next, *prev;
};

#define LIST_POISON1 (void *)0x100
#define LIST_POISON2 (void *)0x200

#define WRITE_ONCE(x, val) x = (val)

#include "linux/list.h"

#define container_of(ptr, type, member) ({ \
	int _off = (int) &(((type *) 0)->member); \
	(type *)(((void *)ptr) - _off); })

typedef struct {
	handle_t lock;
	struct list_head task_list;
} wait_queue_head_t;

typedef handle_t spinlock_t;


struct qstr {
	u32 len;
	u32 hash;
	const unsigned char *name;
};


inline void *kmalloc(int len, int flag)
{
	return malloc(len);
}


inline void kfree(void *ptr)
{
	free(ptr);
}

#define spin_lock(x) mutexLock(*x)
#define spin_unlock(x) mutexUnlock(*x)

#define DEFINE_SPINLOCK(x) handle_t x

#define pr_info(...) printf(__VA_ARGS__)
#define pr_debug(...) printf(__VA_ARGS__)
#define pr_warn(...) printf(__VA_ARGS__)
#define pr_err(...) printf(__VA_ARGS__)

#define GFP_KERNEL 0

#endif /* _OS_PHOENIX_H_ */
