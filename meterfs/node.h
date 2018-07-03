/*
 * Phoenix-RTOS
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

#include <arch.h>
#include "files.h"

int node_add(file_t *file, id_t id);


file_t *node_getByName(const char *name, id_t *id);


file_t *node_getById(id_t id);


int node_put(id_t id);


void node_cleanAll(void);


int node_getMaxId(void);


void node_init(void);


#endif
