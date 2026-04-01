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


/* NOTE: cache size also determines max size of file that can be inlined in directory structure
 * Any file not inlined WILL take at least 1 full block of storage.
 * See `cache_size` in `struct lfs_config`. */
#define LIBLFS_DEF_CACHESIZE 256

/* Threshold number of erase cycles that trigger wear leveling algorithm. See `block_cycles` in `struct lfs_config`. */
#define LIBLFS_DEF_CYCLES_THRESHOLD 500

/* Threshold after which LFS driver will attempt to evict LFS stub objects from memory.
 * Open files/directories are always kept in memory and count towards this limit. */
#define LIBLFS_DEF_N_CACHED_OBJECTS 10

/* Number of blocks to scan ahead when looking for unused blocks. See `lookahead_size` in `struct lfs_config`.*/
#define LIBLFS_DEF_LOOKAHEAD_SIZE 16

/* If set to 1, "link" filesystem operation works as "rename" - otherwise it returns -ENOSYS.
 * This is a workaround to make it possible to rename files, though with some glitches.
 * TODO: if Phoenix RTOS ever gains support for a proper "rename" operation, fix this. */
#define LIBLFS_DEF_LINK_IS_RENAME 0

/* If set to 1, passing `format` as data to the `mount` command will format the partition with LFS file system.
 * To save some code size, this can be set to 0. */
#define LIBLFS_DEF_MOUNT_FORMAT_OPTION 0

/* If set to 1, a failure to mount the filesystem will result in the driver attempting to format the drive.
 * This is intended for testing, in production it could result in unwanted behavior. */
#define LIBLFS_DEF_FORMAT_ON_MOUNT_FAILURE 0

/* Mount mode flags (`mode` argument to `mount` command)*/
#define LIBLFS_READ_ONLY_FLAG (1UL << 0) /* Mount in read-only mode */
#define LIBLFS_USE_CTIME_FLAG (1UL << 1) /* Automatically update created time of files */
#define LIBLFS_USE_MTIME_FLAG (1UL << 2) /* Automatically update modified time of files */
#define LIBLFS_USE_ATIME_FLAG (1UL << 3) /* Automatically update accessed time of files */


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


/* Processes filesystem message. For use with liblfs_ata_mount or liblfs_rawcfg_mount. */
extern int liblfs_handler(void *fdata, msg_t *msg);


/* Mounts filesystem (for use with pc-ata driver) */
extern int liblfs_ata_mount(oid_t *dev, unsigned int sectorsz, ssize_t (*read)(id_t, off_t, char *, size_t), ssize_t (*write)(id_t, off_t, const char *, size_t), void **fdata);


/* Unmounts filesystem (for use with pc-ata driver) */
extern int liblfs_ata_unmount(void *fdata);


/* Mount filesystem callback for libstorage */
extern int liblfs_storage_mount(storage_t *strg, storage_fs_t *fs, const char *data, unsigned long mode, oid_t *root);


/* Unmount filesystem callback for libstorage */
extern int liblfs_storage_umount(storage_fs_t *fs);


/* Mount filesystem with explicit configuration.
 * `fdata` - pointer to where the handle will be stored
 * `root` - OID of the mounted filesystem.
 * 		`root->port` is an input argument - port for communicating with filesystem.
 * 		`root->id` is an output argument - ID of the root directory of filesystem.
 * `cfg` - file system configuration (incl. block device callbacks). Must remain valid after return.
 * 		It may be freed by liblfs_rawcfg_unmount, or after that function returns.
 * `doFormat` - if != 0 the storage will be formatted before mounting. */
extern int liblfs_rawcfg_mount(void **fdata, oid_t *root, const struct lfs_config *cfg, int doFormat);


/* Unmount filesystem.
 * If `freeCfg` != 0, also free the config that was passed when mounting (const struct lfs_config *) */
extern int liblfs_rawcfg_unmount(void *fdata, int freeCfg);


#endif
