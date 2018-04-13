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
	struct list_head head;
} wait_queue_head_t;

struct wait_queue_entry;

typedef int (*wait_queue_func_t)(struct wait_queue_entry *wq_entry, unsigned mode, int flags, void *key);

int default_wake_function(struct wait_queue_entry *wq_entry, unsigned mode, int flags, void *key);

/* wait_queue_entry::flags */
#define WQ_FLAG_EXCLUSIVE		0x01
#define WQ_FLAG_WOKEN			0x02
#define WQ_FLAG_BOOKMARK		0x04

/*
 *  * A single wait-queue entry structure:
 *   */
struct wait_queue_entry {
	unsigned int			flags;
	void					*private;
	wait_queue_func_t		func;
	struct list_head		entry;
};

struct work_struct {
//	atomic_long_t data;
//	struct list_head entry;
//	work_func_t func;
};

struct delayed_work {
	struct work_struct work;
};

#define INIT_DELAYED_WORK(__work, __func) ({do { } while (0); })

static inline struct delayed_work *to_delayed_work(struct work_struct *work)
{
	return container_of(work, struct delayed_work, work);
}

#define __WAITQUEUE_INITIALIZER(name, tsk) {									\
		.private		= tsk,													\
		.func			= default_wake_function,								\
		.entry			= { NULL, NULL } }

#define DECLARE_WAITQUEUE(name, tsk)											\
		struct wait_queue_entry name = __WAITQUEUE_INITIALIZER(name, tsk)


void add_wait_queue(struct wait_queue_head *wq_head, struct wait_queue_entry *wq_entry);
void remove_wait_queue(struct wait_queue_head *wq_head, struct wait_queue_entry *wq_entry);

struct workqueue_struct {
	int todo;
};


static inline bool queue_delayed_work(struct workqueue_struct *wq,
				      struct delayed_work *dwork,
				      unsigned long delay)
{
	return 0;
}


#endif /* _OS_PHOENIX_WAIT_H_ */

