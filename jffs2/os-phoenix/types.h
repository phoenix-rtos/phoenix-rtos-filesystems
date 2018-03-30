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


#ifndef _OS_PHOENIX_TYPES_H_
#define _OS_PHOENIX_TYPES_H_

#include <sys/types.h>

typedef u8 bool;

typedef uint32_t __u32;
typedef uint16_t __u16;
typedef uint8_t __u8;

typedef struct {
   int counter;
} atomic_t;

typedef unsigned short umode_t;

typedef long loff_t;

#endif /* _OS_PHOENIX_TYPES_H_ */
