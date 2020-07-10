/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * Library
 *
 * Copyright 2019, 2020 Phoenix Systems
 * Author: Kamil Amanowicz, Lukasz Kosinski
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


/* Mounts filesystem */
extern int libext2_mount(oid_t *dev, unsigned int sectorsz, ssize_t (*read)(id_t, offs_t, char *, size_t), ssize_t (*write)(id_t, offs_t, const char *, size_t), void **data);


/* Unmounts filesystem */
extern int libext2_unmount(void *data);


/* Processes filesystem messages */
extern int libext2_handler(void *data, msg_t *msg);


#define LIBEXT2_TYPE    0x83
#define LIBEXT2_NAME    "ext2"
#define LIBEXT2_MOUNT   libext2_mount
#define LIBEXT2_UNMOUNT libext2_unmount
#define LIBEXT2_HANDLER libext2_handler


#endif
