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


#ifndef _OS_PHOENIX_DENTRY_H_
#define _OS_PHOENIX_DENTRY_H_

struct qstr {
	u32 len;
	u32 hash;
	const unsigned char *name;
};

struct dentry {
	struct qstr d_name;
	struct inode *d_inode;
};

static inline struct inode *d_inode(const struct dentry *dentry)
{
	return dentry->d_inode;
}

struct dentry * d_splice_alias(struct inode *inode, struct dentry *dentry)
{
	return NULL;
}

void d_invalidate(struct dentry *dentry)
{
	return;
}

static inline bool d_really_is_positive(const struct dentry *dentry)
{
	return dentry->d_inode != NULL;
}

#endif /* _OS_PHOENIX_DENTRY_H_ */
