/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Jffs2 FileSystem - system specific information.
 *
 * Copyright 2018 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */


#ifndef _OS_PHOENIX_CRC32_H_
#define _OS_PHOENIX_CRC32_H_

u32 crc32(u32 crc, void *p, size_t len);

#endif /* _OS_PHOENIX_CRC32_H_ */
