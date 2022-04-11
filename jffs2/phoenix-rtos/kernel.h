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


#ifndef _OS_PHOENIX_KERNEL_H_
#define _OS_PHOENIX_KERNEL_H_

#include <unistd.h>

#define __init
#define __exit
#define __user

#define likely(x) (x)
#define unlikely(x) (x)

#define PAGE_SHIFT 	12
#define PAGE_SIZE	(1 << PAGE_SHIFT)


#define cond_resched()


struct delayed_call {
};


struct vm_area_struct {
};


struct pipe_inode_info {
};


struct kiocb {
};


struct iov_iter {
};


struct kstat {
};


struct path {
};


struct seq_file
{
};

struct rcu_head
{
};

struct jffs2_sb_info;
struct jffs2_eraseblock;
struct jffs2_inode_info;


#define container_of(ptr, type, member) ({ \
	int _off = (int) &(((type *) 0)->member); \
	(type *)(((void *)ptr) - _off); })

#endif /* KERNEL_H */
