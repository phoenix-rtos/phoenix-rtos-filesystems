/*
 * The little filesystem
 *
 * Copyright (c) 2022, The littlefs authors.
 * Copyright (c) 2017, Arm Limited. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef LFS_H
#define LFS_H
/* clang-format off */

#include <liblfs_config.h>

#include "lfs_util.h"

#ifdef __cplusplus
extern "C"
{
#endif


/// Version info ///

// Software library version
// Major (top-nibble), incremented on backwards incompatible changes
// Minor (bottom-nibble), incremented on feature additions
#define LFS_VERSION 0x00020008
#define LFS_VERSION_MAJOR (0xffff & (LFS_VERSION >> 16))
#define LFS_VERSION_MINOR (0xffff & (LFS_VERSION >>  0))

// Version of On-disk data structures
// Major (top-nibble), incremented on backwards incompatible changes
// Minor (bottom-nibble), incremented on feature additions
#define LFS_DISK_VERSION 0x00020001
#define LFS_DISK_VERSION_MAJOR (0xffff & (LFS_DISK_VERSION >> 16))
#define LFS_DISK_VERSION_MINOR (0xffff & (LFS_DISK_VERSION >>  0))


/// Definitions ///

// Possible error codes, these are negative to allow
// valid positive return values
enum lfs_error {
    LFS_ERR_OK          = 0,    // No error
    LFS_ERR_IO          = -5,   // Error during device operation
    LFS_ERR_CORRUPT     = -84,  // Corrupted
    LFS_ERR_NOENT       = -2,   // No directory entry
    LFS_ERR_EXIST       = -17,  // Entry already exists
    LFS_ERR_NOTDIR      = -20,  // Entry is not a dir
    LFS_ERR_ISDIR       = -21,  // Entry is a dir
    LFS_ERR_NOTEMPTY    = -39,  // Dir is not empty
    LFS_ERR_BADF        = -9,   // Bad file number
    LFS_ERR_FBIG        = -27,  // File too large
    LFS_ERR_INVAL       = -22,  // Invalid parameter
    LFS_ERR_NOSPC       = -28,  // No space left on device
    LFS_ERR_NOMEM       = -12,  // No more memory available
    LFS_ERR_NOATTR      = -61,  // No data/attr available
    LFS_ERR_NAMETOOLONG = -36,  // File name too long
    LFS_ERR_BUSY        = -16,  // Object is in use
    LFS_ERR_ROFS        = -30,  // Read-only filesystem
};

// File types
enum lfs_type {
    // file types
    LFS_TYPE_REG            = 0x001,
    LFS_TYPE_DIR            = 0x002,

    // internally used types
    LFS_TYPE_SPLICE         = 0x400,
    LFS_TYPE_NAME           = 0x000,
    LFS_TYPE_STRUCT         = 0x200,
    LFS_TYPE_USERATTR       = 0x300,
    LFS_TYPE_FROM           = 0x100,
    LFS_TYPE_TAIL           = 0x600,
    LFS_TYPE_GLOBALS        = 0x700,
    LFS_TYPE_CRC            = 0x500,

    // internally used type specializations
    LFS_TYPE_CREATE         = 0x401,
    LFS_TYPE_DELETE         = 0x4ff,
    LFS_TYPE_SUPERBLOCK     = 0x0ff,
    LFS_TYPE_DIRSTRUCT      = 0x200,
    LFS_TYPE_CTZSTRUCT      = 0x202,
    LFS_TYPE_INLINESTRUCT   = 0x201,
    LFS_TYPE_SOFTTAIL       = 0x600,
    LFS_TYPE_HARDTAIL       = 0x601,
    LFS_TYPE_MOVESTATE      = 0x7ff,
    LFS_TYPE_CCRC           = 0x500,
    LFS_TYPE_FCRC           = 0x5ff,

    // internal chip sources
    LFS_FROM_NOOP           = 0x000,
    LFS_FROM_MOVE           = 0x101,
    LFS_FROM_USERATTRS      = 0x102,
};

// File open flags
enum lfs_open_flags {
    // open flags
    LFS_O_RDONLY = 1,         // Open a file as read only
#ifndef LFS_READONLY
    LFS_O_WRONLY = 2,         // Open a file as write only
    LFS_O_RDWR   = 3,         // Open a file as read and write
    LFS_O_CREAT  = 0x0100,    // Create a file if it does not exist
    LFS_O_EXCL   = 0x0200,    // Fail if a file already exists
    LFS_O_TRUNC  = 0x0400,    // Truncate the existing file to zero size
    LFS_O_APPEND = 0x0800,    // Move to end of file on every write
#endif

    // internally used flags
#ifndef LFS_READONLY
    LFS_F_DIRTY   = 0x010000, // File does not match storage
    LFS_F_WRITING = 0x020000, // File has been written since last flush
#endif
    LFS_F_READING = 0x040000, // File has been read since last flush
#ifndef LFS_READONLY
    LFS_F_ERRED   = 0x080000, // An error occurred during write
#endif
    LFS_F_INLINE  = 0x100000, // Currently inlined in directory entry
};

// File seek flags
enum lfs_whence_flags {
    LFS_SEEK_SET = 0,   // Seek relative to an absolute position
    LFS_SEEK_CUR = 1,   // Seek relative to the current file position
    LFS_SEEK_END = 2,   // Seek relative to the end of the file
};

// File info structure
struct lfs_info {
    // Type of the file, either LFS_TYPE_REG or LFS_TYPE_DIR
    uint8_t type;

    // Size of the file, only valid for REG files. Limited to 32-bits.
    lfs_size_t size;

    // Name of the file stored as a null-terminated string. Limited to
    // LFS_NAME_MAX+1, which can be changed by redefining LFS_NAME_MAX to
    // reduce RAM. LFS_NAME_MAX is stored in superblock and must be
    // respected by other littlefs drivers.
    char name[LFS_NAME_MAX+1];
};

// Filesystem info structure
struct lfs_fsinfo {
    // On-disk version.
    uint32_t disk_version;

    // Size of a logical block in bytes.
    lfs_size_t block_size;

    // Number of logical blocks in filesystem.
    lfs_size_t block_count;

    // Upper limit on the length of file names in bytes.
    lfs_size_t name_max;

    // Upper limit on the size of files in bytes.
    lfs_size_t file_max;

    // Upper limit on the size of custom attributes in bytes.
    lfs_size_t attr_max;
};

// Custom attribute structure, used to describe custom attributes
// committed atomically during file writes.
struct lfs_attr {
    // 8-bit type of attribute, provided by user and used to
    // identify the attribute
    uint8_t type;

    // Pointer to buffer containing the attribute
    void *buffer;

    // Size of attribute in bytes, limited to LFS_ATTR_MAX
    lfs_size_t size;
};

// Optional configuration provided during lfs_file_opencfg
struct lfs_file_config {
    // Optional statically allocated file buffer. Must be cache_size.
    // By default lfs_malloc is used to allocate this buffer.
    void *buffer;

    // Optional list of custom attributes related to the file. If the file
    // is opened with read access, these attributes will be read from disk
    // during the open call. If the file is opened with write access, the
    // attributes will be written to disk every file sync or close. This
    // write occurs atomically with update to the file's contents.
    //
    // Custom attributes are uniquely identified by an 8-bit type and limited
    // to LFS_ATTR_MAX bytes. When read, if the stored attribute is smaller
    // than the buffer, it will be padded with zeros. If the stored attribute
    // is larger, then it will be silently truncated. If the attribute is not
    // found, it will be created implicitly.
    struct lfs_attr *attrs;

    // Number of custom attributes in the list
    lfs_size_t attr_count;
};


/// internal littlefs data structures ///
typedef struct lfs_cache {
    lfs_block_t block;
    lfs_off_t off;
    lfs_size_t size;
    uint8_t *buffer;
} lfs_cache_t;

typedef struct lfs_mdir {
    lfs_block_t pair[2];
    uint32_t rev;
    lfs_off_t off;
    uint32_t etag;
    uint16_t count;
    bool erased;
    bool split;
    lfs_block_t tail[2];
} lfs_mdir_t;

typedef struct lfs_mlist {
    struct lfs_mlist *next;
    lfs_mdir_t m;
    uint16_t id;
} lfs_mlist_t;

// littlefs directory type
typedef struct lfs_dir {
    struct lfs_dir *nextDir;
    struct {
        lfs_mdir_t m;
        uint16_t id;
    } common;

    lfs_off_t pos;
    lfs_block_t head[2];
    int refcount;
} lfs_dir_t;

// littlefs file type
typedef struct lfs_file {
    struct {
        lfs_mdir_t m;
        uint16_t id;
    } common;

    struct lfs_ctz {
        lfs_block_t head;
        lfs_size_t size;
    } ctz;

    uint32_t flags;
    lfs_off_t pos;
    lfs_block_t block;
    lfs_off_t off;
    lfs_cache_t cache;
    int refcount;
} lfs_file_t;

typedef struct lfs_superblock {
    uint32_t version;
    lfs_size_t block_size;
    lfs_size_t block_count;
    lfs_size_t name_max;
    lfs_size_t file_max;
    lfs_size_t attr_max;
} lfs_superblock_t;

typedef struct lfs_gstate {
    uint32_t tag;
    lfs_block_t pair[2];
} lfs_gstate_t;

// The littlefs filesystem type
typedef struct lfs {
    lfs_cache_t rcache;
    lfs_cache_t pcache;

    lfs_block_t root[2];
    struct lfs_mlist *mlist;
    uint32_t seed;

    lfs_gstate_t gstate;
    lfs_gstate_t gdisk;
    lfs_gstate_t gdelta;

    struct lfs_free {
        lfs_block_t off;
        lfs_block_t size;
        lfs_block_t i;
        lfs_block_t ack;
        uint32_t *buffer;
    } free;

    const struct lfs_config *cfg;
    lfs_size_t block_count;
    lfs_size_t name_max;
    lfs_size_t file_max;
    lfs_size_t attr_max;

    struct lfs_dir *openDirs; /* List of open directories (lfs_dir_t) */
    void *phLfsObjects;       /* List of objects created by ph_lfs API */
    uint32_t nPhLfsObjects;   /* Number of objects created by ph_lfs API */
    id_t lastFileId;          /* Max valid file ID in the filesystem */
    bool initialScan;         /* Flag that max file ID should be updated when scanning directories */
    bool largeInlineOpened;   /* Flag that an inline file larger than cache has been opened */
    handle_t mutex;
    rbtree_t phIdTree;
    unsigned int port;
} lfs_t;


/// Filesystem functions ///

#ifndef LFS_READONLY
// Format a block device with the littlefs
//
// Requires a littlefs object and config struct. This clobbers the littlefs
// object, and does not leave the filesystem mounted. The config struct must
// be zeroed for defaults and backwards compatibility.
//
// Returns a negative error code on failure.
int lfs_format(lfs_t *lfs, const struct lfs_config *config);
#endif

/// Filesystem-level filesystem operations

// Find on-disk info about the filesystem
//
// Fills out the fsinfo structure based on the filesystem found on-disk.
// Returns a negative error code on failure.
int lfs_fs_stat(lfs_t *lfs, struct lfs_fsinfo *fsinfo);

// Finds the current size of the filesystem
//
// Note: Result is best effort. If files share COW structures, the returned
// size may be larger than the filesystem actually is.
//
// Returns the number of allocated blocks, or a negative error code on failure.
lfs_ssize_t lfs_fs_size(lfs_t *lfs);

// Traverse through all blocks in use by the filesystem
//
// The provided callback will be called with each block address that is
// currently in use by the filesystem. This can be used to determine which
// blocks are in use or how much of the storage is available.
//
// Returns a negative error code on failure.
int lfs_fs_traverse(lfs_t *lfs, int (*cb)(void*, lfs_block_t), void *data);

// Attempt to proactively find free blocks
//
// Calling this function is not required, but may allowing the offloading of
// the expensive block allocation scan to a less time-critical code path.
//
// Note: littlefs currently does not persist any found free blocks to disk.
// This may change in the future.
//
// Returns a negative error code on failure. Finding no free blocks is
// not an error.
int lfs_fs_gc(lfs_t *lfs);

#ifndef LFS_READONLY
// Attempt to make the filesystem consistent and ready for writing
//
// Calling this function is not required, consistency will be implicitly
// enforced on the first operation that writes to the filesystem, but this
// function allows the work to be performed earlier and without other
// filesystem changes.
//
// Returns a negative error code on failure.
int lfs_fs_mkconsistent(lfs_t *lfs);
#endif

#ifndef LFS_READONLY
// Grows the filesystem to a new size, updating the superblock with the new
// block count.
//
// Note: This is irreversible.
//
// Returns a negative error code on failure.
int lfs_fs_grow(lfs_t *lfs, lfs_size_t block_count);
#endif

/* clang-format off */

#define LFS_TYPE_PHID_START 0xfc
#define LFS_TYPE_PHID_MASK (0x700 | LFS_TYPE_PHID_START)
#define LFS_TYPE_PHID_ANY (LFS_TYPE_USERATTR + LFS_TYPE_PHID_START)
#define LFS_TYPE_PHID_REG (LFS_TYPE_USERATTR + LFS_TYPE_PHID_START + 0)
#define LFS_TYPE_PHID_DIR (LFS_TYPE_USERATTR + LFS_TYPE_PHID_START + 1)

#define LFS_INVALID_PHID 0   /* Value to indicate Phoenix ID is invalid */
#define LFS_ROOT_PHID 1      /* Phoenix ID representing root dir */
#define ID_SIZE sizeof(id_t) /* Size of Phoenix IDs stored on disk */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
