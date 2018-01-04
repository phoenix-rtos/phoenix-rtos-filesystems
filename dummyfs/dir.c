/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * dummyfs - directory operations
 *
 * Copyright 2018 Phoenix Systems
 * Author: Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <sys/msg.h>

#include "dummyfs.h"


int dir_find(void)
{

	/* Iterate over all entries to find the matching one */
	do {
		if (!strcmp(e->name, (char *)name)) {
			memcpy(res, &e->oid, sizeof(oid_t));
			mutexUnlock(dummyfs_common.mutex);
			dummyfs_put(o);
			return EOK;
		}

		e = e->next;
	} while (e != dir->entries);

	return EOK;
}


