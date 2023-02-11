/*
 * Phoenix-RTOS
 *
 * Meterfs MTD device adapter
 *
 * Copyright 2023 Phoenix Systems
 * Author: Hubert Badocha
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _METERFS_MTD_H_
#define _METERFS_MTD_H_

#include <storage/storage.h>


int meterfs_mount(storage_t *storage, storage_fs_t *fs, const char *data, unsigned long mode, oid_t *root);


int meterfs_umount(storage_fs_t *fs);

#endif
