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


#ifndef LIBLFS_CACHESIZE_DEFAULT
/* NOTE: cache size also determines max size of file that can be inlined in directory structure
 * Any file not inlined WILL take at least 1 full block of storage.
 * See `cache_size` in `struct lfs_config`. */
#define LIBLFS_CACHESIZE_DEFAULT 256
#endif

#ifndef LIBLFS_CYCLES_THRESHOLD
/* Threshold number of erase cycles that trigger wear leveling algorithm. See `block_cycles` in `struct lfs_config`. */
#define LIBLFS_CYCLES_THRESHOLD 500
#endif

#ifndef LIBLFS_N_CACHED_OBJECTS
/* Max number of LittleFS file objects cached in memory. Open files/directories are always kept in memory and count towards this limit. */
#define LIBLFS_N_CACHED_OBJECTS 10
#endif

#ifndef LIBLFS_LOOKAHEAD_SIZE
/* Number of blocks to scan ahead when looking for unused blocks. See `lookahead_size` in `struct lfs_config`.*/
#define LIBLFS_LOOKAHEAD_SIZE 16
#endif

#ifndef LIBLFS_FORMAT_ON_MOUNT_FAILURE
/* If set to 1, a failure to mount the filesystem will result in the driver attempting to format the drive.
 * This is intended for testing, in production it could result in unwanted behavior. */
#define LIBLFS_FORMAT_ON_MOUNT_FAILURE 0
#endif

/* Mount mode flags (`mode` argument to `mount` command)*/
#define LIBLFS_READ_ONLY_FLAG (1UL << 0) /* Mount in read-only mode */
#define LIBLFS_USE_CTIME_FLAG (1UL << 1) /* Store created time of files */
#define LIBLFS_USE_MTIME_FLAG (1UL << 2) /* Store modified time of files */
#define LIBLFS_USE_ATIME_FLAG (1UL << 3) /* Store accessed time of files */


enum liblfs_devctlCommand {
	LIBLFS_DEVCTL_FS_GROW = 1, /* Grow filesystem to selected size (shrinking not supported). */
	LIBLFS_DEVCTL_FS_GC = 2,   /* Attempt to proactively find free blocks - may improve performance when writing. */
};

/* Structure for issuing commands through devctl */
typedef struct {
	int command;
	union {
		struct {
			lfs_block_t targetSize; /* Size in blocks. Value of 0 means maximum size of storage device/partition. */
		} fsGrow;
	};
} liblfs_devctl_in_t;


/* Processes filesystem messages */
extern int liblfs_handler(void *fdata, msg_t *msg);


/* Unmounts filesystem (for use with pc-ata driver) */
extern int liblfs_ata_unmount(void *fdata);


/* Mounts filesystem (for use with pc-ata driver) */
extern int liblfs_ata_mount(oid_t *dev, unsigned int sectorsz, ssize_t (*read)(id_t, off_t, char *, size_t), ssize_t (*write)(id_t, off_t, const char *, size_t), void **fdata);


/* Unmount filesystem callback for libstorage */
extern int liblfs_storage_umount(storage_fs_t *fs);


/* Mount filesystem callback for libstorage */
extern int liblfs_storage_mount(storage_t *strg, storage_fs_t *fs, const char *data, unsigned long mode, oid_t *root);


/* Mount filesystem with explicit configuration.
 * `cfg` must remain valid after return.
 * It may be freed by liblfs_rawcfg_unmount, or after that function returns. */
extern int liblfs_rawcfg_mount(void **fs_handle, oid_t *root, const struct lfs_config *cfg);


/* Unmount filesystem.
 * If `freeCfg` != 0, also free the config that was passed when mounting (const struct lfs_config *) */
extern int liblfs_rawcfg_unmount(void *fs_handle, int freeCfg);


#endif
