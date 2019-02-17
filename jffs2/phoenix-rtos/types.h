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

#define pgoff_t unsigned long

typedef u8 bool;

#define true 1
#define false 0

typedef uint32_t __u32;
typedef uint16_t __u16;
typedef uint8_t __u8;

typedef unsigned char 	u_char;
typedef unsigned short 	u_short;
typedef unsigned int	u_int;
typedef unsigned long	u_long;


typedef struct {
	uid_t val;
} kuid_t;


typedef struct {
	gid_t val;
} kgid_t;

typedef struct {
   int counter;
} atomic_t;

typedef unsigned short umode_t;

typedef long loff_t;

typedef int pid_t;

typedef int gfp_t;

typedef unsigned int slab_flags_t;

typedef uint32_t phys_addr_t;

typedef phys_addr_t resource_size_t;

#endif /* _OS_PHOENIX_TYPES_H_ */
