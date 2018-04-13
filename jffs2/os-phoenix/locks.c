/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Jffs2 FileSystem - system specific information.
 *
 * Copyright 2018 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include "../os-phoenix.h"
#include "locks.h"

void up_read(struct rw_semaphore *sem)
{
}

void down_read(struct rw_semaphore *sem)
{
}

void up_write(struct rw_semaphore *sem)
{
}

void down_write(struct rw_semaphore *sem)
{
}

void __init_rwsem(struct rw_semaphore *sem, const char *name,
			 struct lock_class_key *key)
{
}
