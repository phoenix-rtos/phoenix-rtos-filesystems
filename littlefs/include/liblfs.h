/*
 * Phoenix-RTOS
 *
 * LittleFS library header
 *
 * Copyright 2019, 2020, 2024 Phoenix Systems
 * Author: Kamil Amanowicz, Lukasz Kosinski, Jacek Maksymowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _LIBLFS_H_
#define _LIBLFS_H_

#include <stdint.h>

#include <sys/msg.h>
#include <sys/types.h>

#include <storage/storage.h>

#include <liblfs_config.h>

enum liblfs_devctlCommand {
	LIBLFS_DEVCTL_FS_GROW = 1,
	LIBLFS_DEVCTL_FS_GC = 2,
};

typedef struct {
	int command;
	union {
		struct {
			lfs_block_t targetSize;
		} fsGrow;
	};
} liblfs_devctl_in_t;

typedef struct {
	int err;
} liblfs_devctl_out_t;

/* Mount mode settings */
#define LIBLFS_BLOCK_SIZE_LOG_MASK (0x1f)
#define LIBLFS_USE_CTIME_FLAG      (1 << 5)
#define LIBLFS_USE_MTIME_FLAG      (1 << 6)
#define LIBLFS_USE_ATIME_FLAG      (1 << 7)
#define LIBLFS_READ_ONLY_FLAG      (1 << 8)


/* Processes filesystem messages */
extern int liblfs_handler(void *fdata, msg_t *msg);


/* Unmounts filesystem */
extern int liblfs_unmount(void *fdata);


/* Mounts filesystem */
extern int liblfs_mount(oid_t *dev, unsigned int sectorsz, ssize_t (*read)(id_t, off_t, char *, size_t), ssize_t (*write)(id_t, off_t, const char *, size_t), void **fdata);


/* Unmount filesystem callback for libstorage */
extern int liblfs_storage_umount(storage_fs_t *fs);


/* Mount filesystem callback for libstorage */
extern int liblfs_storage_mount(storage_t *strg, storage_fs_t *fs, const char *data, unsigned long mode, oid_t *root);


/* Set configuration according to mode parameter */
extern int liblfs_setConfig(struct lfs_config *cfg, size_t storageSize, unsigned long mode);

/* Mount filesystem with explicit configuration. cfg must remain valid after return,
 * it can only be freed after call to liblfs_rawcfg_unmount */
extern int liblfs_rawcfg_mount(void **fs_handle, oid_t *root, const struct lfs_config *cfg);

extern int liblfs_rawcfg_unmount(void *fs_handle, int freeCfg);


#endif
