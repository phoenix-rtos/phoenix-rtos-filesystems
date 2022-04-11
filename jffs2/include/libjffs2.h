/*
 * Phoenix-RTOS
 *
 * jffs2 library
 *
 * Copyright 2019, 2022 Phoenix Systems
 * Author: Jan Sikorski, Hubert Buczynski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _LIBJFFS2_H_
#define _LIBJFFS2_H_

#include <storage/storage.h>


extern int libjffs2_mount(storage_t *storage, storage_fs_t *fs, const char *data, unsigned long mode, oid_t *root);


extern int libjffs2_umount(storage_fs_t *fs);


#endif
