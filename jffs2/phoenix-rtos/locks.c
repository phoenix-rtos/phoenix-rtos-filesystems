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


/* Note: current mutex-based RW semaphore implementation has 2 significant drawbacks: */
/* 1. the writers may get starved by the readers */
/* 2. the current writer may inherit priority from the first reader only */
/* To be fixed by RW semaphore implementation in kernel */


void down_read(struct rw_semaphore *sem)
{
	mutexLock(sem->rlock);

	if (++sem->rcnt == 1) {
		mutexLock(sem->wlock);
	}

	mutexUnlock(sem->rlock);
}


void up_read(struct rw_semaphore *sem)
{
	mutexLock(sem->rlock);

	if (--sem->rcnt == 0) {
		mutexUnlock(sem->wlock);
	}

	mutexUnlock(sem->rlock);
}


void down_write(struct rw_semaphore *sem)
{
	mutexLock(sem->wlock);
}


void up_write(struct rw_semaphore *sem)
{
	mutexUnlock(sem->wlock);
}


void exit_rwsem(struct rw_semaphore *sem)
{
	if (sem == NULL) {
		return;
	}

	resourceDestroy(sem->rlock);
	resourceDestroy(sem->wlock);
}


void init_rwsem(struct rw_semaphore *sem)
{
	if (sem == NULL) {
		return;
	}

	mutexCreate(&sem->rlock);
	mutexCreate(&sem->wlock);
	sem->rcnt = 0;
}
