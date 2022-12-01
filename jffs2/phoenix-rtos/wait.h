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


struct wait_queue_entry {
};


#define DECLARE_WAITQUEUE(name, tsk)	\
		struct wait_queue_entry name;	\


void add_wait_queue(struct wait_queue_head *wq_head, struct wait_queue_entry *wq_entry);
void remove_wait_queue(struct wait_queue_head *wq_head, struct wait_queue_entry *wq_entry);

void init_waitqueue_head(wait_queue_head_t *wq_head);

void destroy_waitqueue_head(wait_queue_head_t *wq_head);

void wake_up(wait_queue_head_t *wq_head);

void sleep_on_spinunlock(wait_queue_head_t *wq, spinlock_t *s);

/* work queues */
struct work_struct;
typedef void (*work_func_t)(struct work_struct *work);

enum {
	WORK_DEFAULT		= 1,
	WORK_QUEUED			= 2,
	WORK_PENDING		= 4,
	WORK_CANCEL			= 8,
	WORK_WAIT_SYNC		= 16,
	WORK_EXIT			= 32
};

struct work_struct {
	work_func_t func;
	uint8_t state;
	handle_t lock;
	handle_t cond;
	handle_t wait_cond;
};

struct delayed_work {
	struct work_struct work;
	time_t due;
};


struct delayed_work_node {
	struct delayed_work *dw;
	struct delayed_work_node *next;
};

struct workqueue_struct {
	handle_t lock;
	handle_t cond;
	struct delayed_work_node *dwh;
	struct delayed_work_node *dwt;
};


void init_workqueue(struct workqueue_struct *wq);

void delayed_work_starter(void *arg);

static inline void INIT_DELAYED_WORK(struct delayed_work *dwork, work_func_t work_func) 
{
	dwork->work.func = work_func;
	dwork->work.state = WORK_DEFAULT;
	mutexCreate(&dwork->work.lock);
	condCreate(&dwork->work.cond);
	condCreate(&dwork->work.wait_cond);
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

