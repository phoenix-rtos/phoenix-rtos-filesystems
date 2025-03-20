#ifndef _LIBLFS_CONFIG_H_
#define _LIBLFS_CONFIG_H_

#include <stdbool.h>
#include <stdint.h>

/* Type definitions */
typedef uint32_t lfs_size_t;
typedef uint32_t lfs_off_t;

typedef int32_t lfs_ssize_t;
typedef int32_t lfs_soff_t;

typedef uint32_t lfs_block_t;

/* Maximum name size in bytes, may be redefined to reduce the size of the
 * info struct. Limited to <= 1022. Stored in superblock and must be
 * respected by other littlefs drivers.
 */
#ifndef LFS_NAME_MAX
#define LFS_NAME_MAX 255
#endif

/* Maximum size of a file in bytes, may be redefined to limit to support other
 * drivers. Limited on disk to <= 4294967296. However, above 2147483647 the
 * functions lfs_file_seek, lfs_file_size, and lfs_file_tell will return
 * incorrect values due to using signed integers. Stored in superblock and
 * must be respected by other littlefs drivers.
 */
#ifndef LFS_FILE_MAX
#define LFS_FILE_MAX 2147483647
#endif

/* Maximum size of custom attributes in bytes, may be redefined, but there is
 * no real benefit to using a smaller LFS_ATTR_MAX. Limited to <= 1022.
 */
#ifndef LFS_ATTR_MAX
#define LFS_ATTR_MAX 1022
#endif

/* Configuration provided during initialization of the littlefs */
struct lfs_config {
	/* Opaque user provided context that can be used to pass
	 * information to the block device operations
	 */
	void *context;

	/* Read a region in a block. Negative error codes are propagated
	 * to the user.
	 */
	int (*read)(const struct lfs_config *c, lfs_block_t block,
		lfs_off_t off, void *buffer, lfs_size_t size);

	/* Program a region in a block. The block must have previously
	 * been erased. Negative error codes are propagated to the user.
	 * May return LFS_ERR_CORRUPT if the block should be considered bad.
	 */
	int (*prog)(const struct lfs_config *c, lfs_block_t block,
		lfs_off_t off, const void *buffer, lfs_size_t size);

	/* Erase a block. A block must be erased before being programmed.
	 * The state of an erased block is undefined. Negative error codes
	 * are propagated to the user.
	 * May return LFS_ERR_CORRUPT if the block should be considered bad.
	 */
	int (*erase)(const struct lfs_config *c, lfs_block_t block);

	/* Sync the state of the underlying block device. Negative error codes
	 * are propagated to the user.
	 */
	int (*sync)(const struct lfs_config *c);

#ifdef LFS_THREADSAFE
	/* Lock the underlying block device. Negative error codes
	 * are propagated to the user.
	 */
	int (*lock)(const struct lfs_config *c);

	/* Unlock the underlying block device. Negative error codes
	 * are propagated to the user.
	 */
	int (*unlock)(const struct lfs_config *c);
#endif

	/* Minimum size of a block read in bytes. All read operations will be a
	 * multiple of this value.
	 */
	lfs_size_t read_size;

	/* Minimum size of a block program in bytes. All program operations will be
	 * a multiple of this value.
	 */
	lfs_size_t prog_size;

	/* Size of an erasable block in bytes. This does not impact ram consumption
	 * and may be larger than the physical erase size. However, non-inlined
	 * files take up at minimum one block. Must be a multiple of the read and
	 * program sizes.
	 */
	lfs_size_t block_size;

	/* Number of erasable blocks on the device. */
	lfs_size_t block_count;

	/* Number of erase cycles before littlefs evicts metadata logs and moves
	 * the metadata to another block. Suggested values are in the
	 * range 100-1000, with large values having better performance at the cost
	 * of less consistent wear distribution.
	 *
	 * Set to -1 to disable block-level wear-leveling.
	 */
	int32_t block_cycles;

	/* Size of block caches in bytes. Each cache buffers a portion of a block in
	 * RAM. The littlefs needs a read cache, a program cache, and one additional
	 * cache per file. Larger caches can improve performance by storing more
	 * data and reducing the number of disk accesses. Must be a multiple of the
	 * read and program sizes, and a factor of the block size.
	 */
	lfs_size_t cache_size;

	/* Size of the lookahead buffer in bytes. A larger lookahead buffer
	 * increases the number of blocks found during an allocation pass. The
	 * lookahead buffer is stored as a compact bitmap, so each byte of RAM
	 * can track 8 blocks. Must be a multiple of 8.
	 */
	lfs_size_t lookahead_size;

	/* Optional statically allocated read buffer. Must be cache_size.
	 * By default lfs_malloc is used to allocate this buffer.
	 */
	void *read_buffer;

	/* Optional statically allocated program buffer. Must be cache_size.
	 * By default lfs_malloc is used to allocate this buffer.
	 */
	void *prog_buffer;

	/* Optional statically allocated lookahead buffer. Must be lookahead_size
	 * and aligned to a 32-bit boundary. By default lfs_malloc is used to
	 * allocate this buffer.
	 */
	void *lookahead_buffer;

	/* Optional upper limit on length of file names in bytes. No downside for
	 * larger names except the size of the info struct which is controlled by
	 * the LFS_NAME_MAX define. Defaults to LFS_NAME_MAX when zero. Stored in
	 * superblock and must be respected by other littlefs drivers.
	 */
	lfs_size_t name_max;

	/* Optional upper limit on files in bytes. No downside for larger files
	 * but must be <= LFS_FILE_MAX. Defaults to LFS_FILE_MAX when zero. Stored
	 * in superblock and must be respected by other littlefs drivers.
	 */
	lfs_size_t file_max;

	/* Optional upper limit on custom attributes in bytes. No downside for
	 * larger attributes size but must be <= LFS_ATTR_MAX. Defaults to
	 * LFS_ATTR_MAX when zero.
	 */
	lfs_size_t attr_max;

	/* Optional upper limit on total space given to metadata pairs in bytes. On
	 * devices with large blocks (e.g. 128kB) setting this to a low size (2-8kB)
	 * can help bound the metadata compaction time. Must be <= block_size.
	 * Defaults to block_size when zero.
	 */
	lfs_size_t metadata_max;

#ifdef LFS_MULTIVERSION
	/* On-disk version to use when writing in the form of 16-bit major version
	 * + 16-bit minor version. This limiting metadata to what is supported by
	 * older minor versions. Note that some features will be lost. Defaults to
	 * to the most recent minor version when zero.
	 */
	uint32_t disk_version;
#endif

	struct {
		/* Port for accessing this filesystem */
		unsigned int port;
		uint32_t maxCachedObjects;
		/* Set to 1 to enable storing creation times of files */
		uint8_t useCTime;
		/* Set to 1 to automatically update modification times on files */
		uint8_t useMTime;
		/* Set to 1 to automatically update access times on files */
		uint8_t useATime;
		/* Set to 1 to mount file system in read only mode */
		uint8_t readOnly;
	} ph;
};

#endif /* _LIBLFS_CONFIG_H_ */
