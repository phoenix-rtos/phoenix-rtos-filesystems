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


#ifndef _OS_PHOENIX_WAIT_H_
#define _OS_PHOENIX_WAIT_H_


typedef struct wait_queue_head{
	handle_t lock;
	handle_t cond;
	int cnt;
} wait_queue_head_t;

/*
 *  * A single wait-queue entry structure:
 *   */
struct wait_queue_entry {
};


#define DECLARE_WAITQUEUE(name, tsk)	\
		struct wait_queue_entry name;	\


void add_wait_queue(struct wait_queue_head *wq_head, struct wait_queue_entry *wq_entry);
void remove_wait_queue(struct wait_queue_head *wq_head, struct wait_queue_entry *wq_entry);

void init_waitqueue_head(wait_queue_head_t *wq_head);

void wake_up(wait_queue_head_t *wq_head);

// work queues

struct workqueue_struct {
};

static inline void init_workqueue(struct workqueue_struct *wq)
{
}

struct work_struct;
typedef void (*work_func_t)(struct work_struct *work);

enum {
	WORK_DEFAULT,
	WORK_PENDING,
	WORK_CANCEL
};

struct work_struct {
	work_func_t func;
	handle_t lock;
	handle_t cond;
	u8 state;
};

struct delayed_work {
	struct work_struct work;
	unsigned long delay;
};


static inline void INIT_DELAYED_WORK(struct delayed_work *dwork, work_func_t work_func) 
{
	dwork->work.func = work_func;

	mutexCreate(&dwork->work.lock);
	condCreate(&dwork->work.cond);
	dwork->work.state = WORK_DEFAULT;
}


static inline struct delayed_work *to_delayed_work(struct work_struct *work)
{
	return container_of(work, struct delayed_work, work);
}

bool queue_delayed_work(struct workqueue_struct *wq,
				      struct delayed_work *dwork,
				      unsigned long delay);

extern bool cancel_delayed_work_sync(struct delayed_work *dwork);


#endif /* _OS_PHOENIX_WAIT_H_ */

