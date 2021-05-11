/*
 * Phoenix-RTOS
 *
 * Opened files
 *
 * Copyright 2017, 2018, 2020 Phoenix Systems
 * Author: Aleksander Kaminski, Hubert Buczynski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _METERFS_OPENED_H_
#define _METERFS_OPENED_H_

#include <sys/rb.h>
#include "files.h"

int node_add(file_t *file, id_t id, rbtree_t *tree);


file_t *node_getByName(const char *name, id_t *id, rbtree_t *tree);


file_t *node_getById(id_t id, rbtree_t *tree);


void node_cleanAll(rbtree_t *tree);


int node_getMaxId(rbtree_t *tree);


void node_init(rbtree_t *tree);


#endif
