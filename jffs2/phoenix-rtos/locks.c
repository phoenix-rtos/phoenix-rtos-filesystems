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

#include "../phoenix-rtos.h"
#include "locks.h"

/*
 * Note: temporary RW semaphore implementation as mutex
 * It doesn't allow for multiple concurrent readers but enables priority inheritance
 * Fixes issues with both readers and writers being starved due to priority inversion
 * To be replaced by RW semaphore implementation in kernel
 */

void down_read(struct rw_semaphore *sem)
{
	mutexLock(sem->lock);
}


void up_read(struct rw_semaphore *sem)
{
	mutexUnlock(sem->lock);
}


void down_write(struct rw_semaphore *sem)
{
	mutexLock(sem->lock);
}


void up_write(struct rw_semaphore *sem)
{
	mutexUnlock(sem->lock);
}


void init_rwsem(struct rw_semaphore *sem)
{
	if (sem == NULL) {
		return;
	}

	mutexCreate(&sem->lock);
}


void exit_rwsem(struct rw_semaphore *sem)
{
	if (sem == NULL) {
		return;
	}

	resourceDestroy(sem->lock);
}
