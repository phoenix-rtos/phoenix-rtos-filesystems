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

#include <sys/time.h>
#include <sys/threads.h>

#include "../phoenix-rtos.h"
#include "wait.h"


void wake_up(wait_queue_head_t *wq_head)
{
	mutexLock(wq_head->lock);
	while(wq_head->cnt > 0) {
		condSignal(wq_head->cond);
		wq_head->cnt--;
	}
	mutexUnlock(wq_head->lock);
}


void init_waitqueue_head(wait_queue_head_t *wq_head)
{
	mutexCreate(&wq_head->lock);
	condCreate(&wq_head->cond);
	wq_head->cnt = 0;
}


void destroy_waitqueue_head(wait_queue_head_t *wq_head)
{
	resourceDestroy(wq_head->cond);
	resourceDestroy(wq_head->lock);
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
	if (wq_head->cnt)
		condWait(wq_head->cond, wq_head->lock, 0);
	mutexUnlock(wq_head->lock);
}

void sleep_on_spinunlock(wait_queue_head_t *wq, spinlock_t *s)
{
	DECLARE_WAITQUEUE(__wait, current);
	add_wait_queue((wq), &__wait);
	spin_unlock(s);
	remove_wait_queue((wq), &__wait);
}


void delayed_work_starter(void *arg)
{
	struct workqueue_struct *wq = (struct workqueue_struct *)arg;
	struct delayed_work *dwork;
	struct delayed_work_node *dwn;
	time_t now, due;

	while (1) {
		mutexLock(wq->lock);
		while (wq->dwh == NULL)
			condWait(wq->cond, wq->lock, 0);
		dwork = wq->dwh->dw;

		/* last element */
		if (wq->dwh->next == NULL) {
			free(wq->dwh);
			wq->dwh = NULL;
			wq->dwt = NULL;
		}
		else {
			dwn = wq->dwh->next;
			free(wq->dwh);
			wq->dwh = dwn;
		}

		due = dwork->due;
		mutexUnlock(wq->lock);

		gettime(&now, NULL);

		if (due > now)
			usleep((unsigned)(due - now));

		mutexLock(dwork->work.lock);
		if (dwork->work.state == WORK_QUEUED) {
			dwork->work.state = WORK_PENDING;
			mutexUnlock(dwork->work.lock);

			dwork->work.func(&dwork->work);

			mutexLock(dwork->work.lock);
			if (dwork->work.state == WORK_PENDING)
				dwork->work.state = WORK_DEFAULT;
			else if (dwork->work.state & (WORK_CANCEL | WORK_WAIT_SYNC))
				condBroadcast(dwork->work.cond);
		}

		mutexUnlock(dwork->work.lock);
	}
}


bool queue_delayed_work(struct workqueue_struct *wq,
				      struct delayed_work *dwork,
				      unsigned long delay)
{
	struct delayed_work_node *dwn;

	mutexLock(dwork->work.lock);
	if (dwork->work.state == WORK_QUEUED) {
		mutexUnlock(dwork->work.lock);
		return 1;
	}

	while (dwork->work.state & (WORK_CANCEL | WORK_WAIT_SYNC)) {
		dwork->work.state = WORK_WAIT_SYNC;
		condWait(dwork->work.wait_cond, dwork->work.lock, 0);
	}

	dwn = malloc(sizeof(struct delayed_work_node));

	if (dwn == NULL) {
		mutexUnlock(dwork->work.lock);
		return 0;
	}

	dwork->work.state = WORK_QUEUED;
	mutexUnlock(dwork->work.lock);

	mutexLock(wq->lock);
	dwn->dw = dwork;
	dwn->next = NULL;

	gettime(&dwork->due, NULL);
	dwork->due += delay;
	if (wq->dwh == NULL)
		wq->dwh = dwn;
	else
		wq->dwt->next = dwn;

	wq->dwt = dwn;
	condSignal(wq->cond);
	mutexUnlock(wq->lock);
	return 1;
}


bool cancel_delayed_work_sync(struct delayed_work *dwork)
{
	int wait_sync = 0;
	mutexLock(dwork->work.lock);
	if (dwork->work.state != WORK_DEFAULT) {

		if (dwork->work.state == WORK_PENDING) {
			dwork->work.state = WORK_CANCEL;
			condWait(dwork->work.cond, dwork->work.lock, 0);
			if (dwork->work.state == WORK_WAIT_SYNC)
				wait_sync = 1;
		}

		dwork->work.state = WORK_DEFAULT;
	}

	if (wait_sync)
		condBroadcast(dwork->work.wait_cond);
	mutexUnlock(dwork->work.lock);

	return 1;
}


void init_workqueue(struct workqueue_struct *wq)
{
	wq->dwh = NULL;
	wq->dwt = NULL;
	mutexCreate(&wq->lock);
	condCreate(&wq->cond);
}
