/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Opened files
 *
 * Copyright 2017 Phoenix Systems
 * Author: Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _METERFS_OPENED_H_
#define _METERFS_OPENED_H_

#include ARCH
#include "files.h"


extern int node_add(file_t *file, const char *name, oid_t *oid);


extern int node_remove(unsigned int id);


extern file_t *node_findFile(unsigned int id);


extern int node_findMount(oid_t *oid, const char *name);


extern int node_claim(const char *name, unsigned int *id);


extern void node_init(void);


#endif
