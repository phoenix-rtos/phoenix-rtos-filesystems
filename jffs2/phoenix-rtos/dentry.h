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
	uint32_t len;
	const unsigned char *name;
};


struct dentry {
	struct qstr d_name;
	struct inode *d_inode;
	struct super_block *d_sb;
};


static inline struct inode *d_inode(const struct dentry *dentry)
{
	return dentry->d_inode;
}


struct dentry * d_splice_alias(struct inode *inode, struct dentry *dentry);


void d_invalidate(struct dentry *dentry);


static inline bool d_really_is_positive(const struct dentry *dentry)
{
	return dentry->d_inode != NULL;
}


void d_instantiate(struct dentry *dentry, struct inode *inode);


bool d_is_dir(const struct dentry *dentry);


struct dentry * d_make_root(struct inode *inode);


extern struct dentry *d_obtain_alias(struct inode *);


#endif /* _OS_PHOENIX_DENTRY_H_ */
