/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * dummyfs
 *
 * Copyright 2012, 2016, 2018 Phoenix Systems
 * Copyright 2007 Pawel Pisarczyk
 * Author: Jacek Popko, Katarzyna Baranowska, Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _DUMMYFS_FILE_H_
#define _DUMMYFS_FILE_H_

extern int dummyfs_truncate(dummyfs_object_t *o, size_t size);


extern int dummyfs_read(dummyfs_object_t *o, offs_t offs, char *buff, size_t len);


extern int dummyfs_write(dummyfs_object_t *o, offs_t offs, char *buff, size_t len);

#endif /* _DUMMYFS_FILE_H_ */
