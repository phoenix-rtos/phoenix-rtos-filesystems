/*
 * Phoenix-RTOS
 *
 * jffs2 interface
 *
 * Copyright 2019 Phoenix Systems
 * Author: Jan Sikorski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _LIBJFFS2_H_
#define _LIBJFFS2_H_

#include <sys/msg.h>


extern int jffs2lib_message_handler(void *partition, msg_t *msg);


extern void *jffs2lib_create_partition(size_t start, size_t end, unsigned mode, unsigned port, long *rootid);


extern int jffs2lib_mount_partition(void *partition);


extern int jffs2lib_umount_partition(void *partition, unsigned port);


#endif
