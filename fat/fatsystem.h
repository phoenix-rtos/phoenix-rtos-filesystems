/*
 * Phoenix-RTOS
 *
 * Misc. utilities - FAT
 *
 * Types and errors
 *
 * Copyright 2012 Phoenix Systems
 * Author: Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _MISC_FATSYSTEM_H_
#define _MISC_FATSYSTEM_H_


#include <stdio.h>
#include <ctype.h>

typedef unsigned int offs_t;

typedef struct _fat_opt_t {
	const char *dev;
	offs_t off;
	unsigned int bufpsz;
	unsigned int bufsz;
} fat_opt_t;


typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;


/* errors */

#define EOK     0
#define ENOENT  1
#define EPROTO  2
#define EINVAL  3
#define ENOMEM  4


typedef void * fat_dev_t;
typedef void * fat_mut_t;


#define fat_printErr(...) printf(__VA_ARGS__)
#define fat_mutUnlock(m)
#define fat_mutLock(m)
#define fat_mutInit(m)
#define fat_mutKill(m)
#define fat_malloc(m) malloc(m)
#define fat_free(m)   free(m)


#define min(a, b) ({__typeof__(a) _a = (a); \
	__typeof__(b) _b = (b); \
	_a < _b ? _a : _b;})


#endif
