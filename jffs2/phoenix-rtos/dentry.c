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

#include "../phoenix-rtos.h"
#include "dentry.h"


struct dentry * d_splice_alias(struct inode *inode, struct dentry *dentry)
{
	if (inode == NULL)
		return NULL;

	if (!IS_ERR(inode)) {
		dentry->d_inode = inode;
		return dentry;
	}

	return NULL;
}


void d_invalidate(struct dentry *dentry)
{
	dentry->d_inode = NULL;
	dentry->d_sb = NULL;
}


void d_instantiate(struct dentry *dentry, struct inode *inode)
{
	dentry->d_inode = inode;
	dentry->d_sb = inode->i_sb;
}


struct dentry * d_make_root(struct inode *inode)
{
	struct dentry *res = NULL;

	if (inode) {
		res = malloc(sizeof(struct dentry));
		memset(res, 0, sizeof(struct dentry));
		res->d_inode = inode;
		res->d_sb = inode->i_sb;
		inode->i_count = 1;
	}

	return res;
}


bool d_is_dir(const struct dentry *dentry)
{
	return S_ISDIR(d_inode(dentry)->i_mode);
}

/* not used */
struct dentry *d_obtain_alias(struct inode *inode)
{
	return NULL;
}
