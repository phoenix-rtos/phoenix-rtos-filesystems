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
	mutexLock(sem->lock);

	if (sem->cnt) sem->cnt--;

	if (sem->wait)
		condSignal(sem->cond);

	mutexUnlock(sem->lock);
}

void down_read(struct rw_semaphore *sem)
{
	mutexLock(sem->lock);

	while (sem->cnt < 0) {
		sem->wait++;
		condWait(sem->cond, sem->lock, 0);
		sem->wait--;
	}

	sem->cnt++;
	mutexUnlock(sem->lock);
}

void up_write(struct rw_semaphore *sem)
{
	mutexLock(sem->lock);
	sem->cnt = 0;

	if (sem->wait)
		condSignal(sem->cond);

	mutexUnlock(sem->lock);
}

void down_write(struct rw_semaphore *sem)
{
	mutexLock(sem->lock);
	while (sem->cnt != 0) {
		sem->wait++;
		condWait(sem->cond, sem->lock, 50000);
		sem->wait--;
	}
	sem->cnt = -1;
	mutexUnlock(sem->lock);
}

void init_rwsem(struct rw_semaphore *sem)
{
	if (sem == NULL)
		return;

	mutexCreate(&sem->lock);
	condCreate(&sem->cond);
	sem->cnt = 0;
	sem->wait = 0;
}
