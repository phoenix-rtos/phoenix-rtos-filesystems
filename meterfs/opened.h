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

#include "files.h"


extern int opened_add(file_t *file, unsigned int *id);


extern int opened_remove(unsigned int id);


extern file_t *opened_find(unsigned int id);


extern int opened_claim(const char *name, unsigned int *id);


extern void opened_init(void);


#endif
