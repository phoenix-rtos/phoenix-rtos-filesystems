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

#define __init
#define __user

struct delayed_call {
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


struct super_block {
	int todo;
};


struct iattr {
	int todo;
};

struct kstat {
	int todo;
};

struct path {
	int todo;
};

#include "os-phoenix/completion.h"
#include "os-phoenix/dev.h"
#include "os-phoenix/rb.h"
#include "os-phoenix/locks.h"
#include "os-phoenix/types.h"
#include "os-phoenix/dentry.h"
#include "os-phoenix/fs.h"


#define JFFS2_INODE_INFO(i) (container_of(i, struct jffs2_inode_info, vfs_inode))

#define ITIME(sec) ((struct timespec){sec, 0})
#define I_SEC(tv) ((tv).tv_sec)

static inline void *ERR_PTR(long error)
{
	return (void *) error;
}

static inline bool IS_ERR(const void *ptr)
{
	return (unsigned long)ptr;
}


ssize_t generic_read_dir(struct file *filp, char __user *buf, size_t siz, loff_t *ppos)
{
		return -EISDIR;
}

loff_t generic_file_llseek(struct file *file, loff_t offset, int whence)
{
		return -EINVAL;
}


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



inline void *kmalloc(int len, int flag)
{
	return malloc(len);
}


inline void kfree(void *ptr)
{
	free(ptr);
}

#define pr_notice(...) printf(__VA_ARGS__)
#define pr_info(...) printf(__VA_ARGS__)
#define pr_debug(...) printf(__VA_ARGS__)
#define pr_warn(...) printf(__VA_ARGS__)
#define pr_err(...) printf(__VA_ARGS__)

#define GFP_KERNEL 0

extern const struct inode_operations jffs2_file_inode_operations;
extern const struct inode_operations jffs2_symlink_inode_operations;
extern const struct address_space_operations jffs2_file_address_operations;

int jffs2_fsync(struct file *, loff_t, loff_t, int);

long jffs2_ioctl(struct file *, unsigned int, unsigned long);

int jffs2_setattr (struct dentry *, struct iattr *);

struct inode *jffs2_iget(struct super_block *, unsigned long);

struct inode *jffs2_new_inode (struct inode *dir_i, umode_t mode,
			       struct jffs2_raw_inode *ri);

unsigned int full_name_hash(void *salt, const char * name, unsigned int len)
{
	unsigned hash = 0;
	unsigned long c;

	while (len--) {
		c = *name++;
		hash = (hash + (c << 4) + (c >> 4)) * 11;
	}
	return hash;
}

void *kmemdup(const void *src, size_t len, unsigned gfp)
{
	return NULL;
}

#endif /* _OS_PHOENIX_H_ */
