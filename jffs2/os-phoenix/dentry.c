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

#include "../os-phoenix.h"
#include "dentry.h"

struct dentry * d_splice_alias(struct inode *inode, struct dentry *dentry)
{
	return NULL;
}

void d_invalidate(struct dentry *dentry)
{
	return;
}

void d_instantiate(struct dentry *dentry, struct inode *inode)
{
}

struct dentry * d_make_root(struct inode *inode)
{
	return NULL;
}

