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

#ifndef _DUMMYFS_DIR_H_
#define _DUMMYFS_DIR_H_

extern int dir_find(dummyfs_object_t *dir, const char *name, oid_t *res);


extern int dir_add(dummyfs_object_t *dir, const char *name, oid_t *oid);


extern int dir_remove(dummyfs_object_t *dir, const char *name);


extern int dir_empty(dummyfs_object_t *dir);


extern void dir_destroy(dummyfs_object_t *dir);

#endif /* _DUMMYFS_DIR_H_ */
