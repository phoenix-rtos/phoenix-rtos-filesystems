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


#define LIBEXT2_NAME "ext2"


#define LIBEXT2_TYPE 0x83


int libext2_handler(void *, msg_t *);
#define LIBEXT2_HANDLER libext2_handler


int libext2_mount(id_t *, void **);
#define LIBEXT2_MOUNT libext2_mount


int libext2_unmount(void *);
#define LIBEXT2_UNMOUNT libext2_unmount

#endif /* libext2.h */
