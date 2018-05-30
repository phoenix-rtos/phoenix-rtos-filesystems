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

#include "../os-phoenix.h"
#include "wait.h"


void wake_up(wait_queue_head_t *wq_head)
{
	mutexLock(wq_head->lock);
	condSignal(wq_head->cond);
	mutexUnlock(wq_head->lock);
}

void init_waitqueue_head(wait_queue_head_t *wq_head) 
{
	mutexCreate(&wq_head->lock);
	condCreate(&wq_head->cond);
	wq_head->cnt = 0;
}


void add_wait_queue(struct wait_queue_head *wq_head, struct wait_queue_entry *wq_entry)
{
	mutexLock(wq_head->lock);
	wq_head->cnt++;
	mutexUnlock(wq_head->lock);
}


void remove_wait_queue(struct wait_queue_head *wq_head, struct wait_queue_entry *wq_entry)
{
	mutexLock(wq_head->lock);
	if (wq_head->cnt) {
		condWait(wq_head->cond, wq_head->lock, 0);
		wq_head->cnt--;
	}
	mutexUnlock(wq_head->lock);
}

void sleep_on_spinunlock(wait_queue_head_t *wq, spinlock_t *s)
{
	DECLARE_WAITQUEUE(__wait, current);
	add_wait_queue((wq), &__wait);	
	spin_unlock(s);
	remove_wait_queue((wq), &__wait);
}


static void delayed_work_starter(void *arg)
{
	struct delayed_work *dwork = (struct delayed_work *)arg;
	usleep(dwork->delay);

	mutexLock(dwork->work.lock);
	if (dwork->work.state == WORK_CANCEL) {
		condSignal(dwork->work.cond);
		mutexUnlock(dwork->work.lock);
		return;
	}
	dwork->work.state = WORK_PENDING;
	mutexUnlock(dwork->work.lock);

	dwork->work.func(&dwork->work);

	mutexLock(dwork->work.lock);
	if(dwork->work.state == WORK_CANCEL)
		condSignal(dwork->work.cond);
	dwork->work.state = WORK_DEFAULT;
	mutexUnlock(dwork->work.lock);
	endthread();
}

static char __attribute__((aligned(8))) dw_stack[4096];

bool queue_delayed_work(struct workqueue_struct *wq,
				      struct delayed_work *dwork,
				      unsigned long delay)
{
	mutexLock(dwork->work.lock);
	if (dwork->work.state == WORK_PENDING) {
		mutexUnlock(dwork->work.lock);
		return 0;
	}

	dwork->delay = delay;
	dwork->work.state = WORK_PENDING;
	mutexUnlock(dwork->work.lock);

	beginthread(delayed_work_starter, 4, &dw_stack, 4096, dwork);
	return 1;
}

bool cancel_delayed_work_sync(struct delayed_work *dwork)
{
	mutexLock(dwork->work.lock);
	if (dwork->work.state != WORK_CANCEL) {
		dwork->work.state = WORK_CANCEL;
		condWait(dwork->work.cond, dwork->work.lock, 0);
	}
	mutexUnlock(dwork->work.lock);
	return 1;
}
