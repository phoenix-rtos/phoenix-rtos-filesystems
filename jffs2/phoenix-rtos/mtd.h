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


#ifndef _OS_PHOENIX_MTD_H_
#define _OS_PHOENIX_MTD_H_

#include "fs.h"
#include "dentry.h"

extern struct dentry *mount_mtd(struct file_system_type *fs_type, int flags,
		      const char *dev_name, void *data,
		      int (*fill_super)(struct super_block *, void *, int));


extern void kill_mtd_super(struct super_block *sb);


#endif /* _OS_PHOENIX_MTD_H_ */
