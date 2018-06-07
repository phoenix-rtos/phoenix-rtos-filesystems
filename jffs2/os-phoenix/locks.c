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

	if (sem->cnt < 0xffffffff) sem->cnt++;

	if (sem->wwait)
		condSignal(sem->wcond);
	else if (sem->rwait)
		condSignal(sem->rcond);

	mutexUnlock(sem->lock);
}

void down_read(struct rw_semaphore *sem)
{
	mutexLock(sem->lock);

	sem->rwait++;
	while (sem->wwait)
		condWait(sem->rcond, sem->lock, 0);

	while (!sem->cnt)
		condWait(sem->rcond, sem->lock, 0);

	sem->rwait--;
	if (sem->cnt) sem->cnt--;

	mutexUnlock(sem->lock);
}

void up_write(struct rw_semaphore *sem)
{
	mutexLock(sem->lock);
	sem->cnt = 0xffffffff;

	if (sem->wwait)
		condSignal(sem->wcond);
	else if (sem->rwait)
		condSignal(sem->rcond);

	mutexUnlock(sem->lock);
}

void down_write(struct rw_semaphore *sem)
{
	mutexLock(sem->lock);

	sem->wwait++;
	while (sem->cnt != 0xffffffff)
		condWait(sem->wcond, sem->lock, 0);

	sem->wwait--;
	sem->cnt = 0;

	mutexUnlock(sem->lock);
}

void init_rwsem(struct rw_semaphore *sem)
{
	if (sem == NULL)
		return;

	mutexCreate(&sem->lock);
	condCreate(&sem->wcond);
	condCreate(&sem->rcond);
	sem->cnt = 0xffffffff;
	sem->rwait = 0;
	sem->wwait = 0;
}
