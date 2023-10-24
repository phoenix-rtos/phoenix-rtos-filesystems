/*
 * Phoenix-RTOS
 *
 * FAT filesystem driver
 *
 * libstorage interface header file
 *
 * Copyright 2023 Phoenix Systems
 * Author: Jacek Maksymowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _LIBFAT_H_
#define _LIBFAT_H_

#include <storage/storage.h>


/* Unmount filesystem callback for libstorage */
extern int libfat_umount(storage_fs_t *fs);


/* Mount filesystem callback for libstorage */
extern int libfat_mount(storage_t *strg, storage_fs_t *fs, const char *data, unsigned long mode, oid_t *root);


#endif /* _LIBFAT_H_ */
