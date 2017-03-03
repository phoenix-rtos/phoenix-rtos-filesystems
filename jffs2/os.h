/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */

#ifndef __JFFS2_OS_H__
#define __JFFS2_OS_H__

#define min_t(type, x, y) ({                    \
        type __min1 = (x);                      \
        type __min2 = (y);                      \
        __min1 < __min2 ? __min1: __min2; })
        
#define max_t(type, x, y) ({                    \
        type __max1 = (x);                      \
        type __max2 = (y);                      \
        __max1 > __max2 ? __max1: __max2; })



typedef uint32_t __u32;
typedef uint16_t __u16;
typedef uint8_t __u8;

struct kvec {
	void *iov_base; /* and that should *never* hold a userland pointer */
	size_t iov_len;
};

#define SECTOR_ADDR(x) ( (((unsigned long)(x) / c->sector_size) * c->sector_size) )


#endif
