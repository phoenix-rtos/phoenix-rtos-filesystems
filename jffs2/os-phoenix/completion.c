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

#include <sys/threads.h>
#include "completion.h"

void init_completion(struct completion *comp)
{
	mutexCreate(&comp->lock);
	condCreate(&comp->cond);
	comp->complete = 0;
}

void complete(struct completion *comp)
{
	mutexLock(comp->lock);
	comp->complete = 1;
	condSignal(comp->cond);
	mutexUnlock(comp->lock);
}

void wait_for_completion(struct completion *comp)
{
	mutexLock(comp->lock);
	while (!comp->complete)
		condWait(comp->cond, comp->lock, 0);
	mutexUnlock(comp->lock);
}

void complete_and_exit(struct completion *comp, int code)
{
	mutexLock(comp->lock);
	comp->complete = 1;
	condSignal(comp->cond);
	mutexUnlock(comp->lock);
	endthread();
}
