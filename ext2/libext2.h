/*
 * Phoenix-RTOS
 *
 * libext2
 *
 *
 * Copyright 2019 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _LIBEXT2_H_
#define _LIBEXT2_H_

#include <stdint.h>

#include <sys/msg.h>
#include <sys/types.h>


extern int libext2_handler(void *, msg_t *);


extern int libext2_mount(oid_t *, ssize_t (*)(id_t, offs_t, char *, size_t), ssize_t (*)(id_t, offs_t, const char *, size_t), void **);


extern int libext2_unmount(void *);


#define LIBEXT2_TYPE    0x83
#define LIBEXT2_NAME    "ext2"
#define LIBEXT2_HANDLER libext2_handler
#define LIBEXT2_MOUNT   libext2_mount
#define LIBEXT2_UNMOUNT libext2_unmount

#endif /* libext2.h */
