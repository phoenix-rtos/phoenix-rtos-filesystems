/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * Library
 *
 * Copyright 2019, 2020 Phoenix Systems
 * Author: Kamil Amanowicz, Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _LIBEXT2_H_
#define _LIBEXT2_H_

#include <stdint.h>

#include <sys/msg.h>
#include <sys/types.h>

#include <storage/storage.h>


#define LIBEXT2_NAME    "ext2"
#define LIBEXT2_TYPE    0x83
#define LIBEXT2_HANDLER libext2_handler
#define LIBEXT2_UNMOUNT libext2_unmount
#define LIBEXT2_MOUNT   libext2_mount


/* Processes filesystem messages */
extern int libext2_handler(void *fdata, msg_t *msg);


/* Unmounts filesystem */
extern int libext2_unmount(void *fdata);


/* Mounts filesystem */
extern int libext2_mount(oid_t *dev, unsigned int sectorsz, ssize_t (*read)(id_t, offs_t, char *, size_t), ssize_t (*write)(id_t, offs_t, const char *, size_t), void **fdata);

/* Unmount filesystem callback for libstorage */
extern int libext2_storage_umount(storage_fs_t *fs);


/* Mount filesystem callback for libstorage */
extern int libext2_storage_mount(storage_t *strg, storage_fs_t *fs, const char *data, unsigned long mode, oid_t *root);


#endif
