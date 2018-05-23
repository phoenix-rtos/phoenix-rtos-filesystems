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


#ifndef _OS_PHOENIX_LOCKS_H_
#define _OS_PHOENIX_LOCKS_H_

#include "types.h"

struct mutex {
	handle_t h;
};

typedef handle_t spinlock_t;

#define spin_lock_init(x) mutexCreate(x)
#define spin_lock(x) mutexLock(*x)
#define spin_unlock(x) mutexUnlock(*x)

#define mutex_lock(x) mutexLock((x)->h)
#define mutex_unlock(x) mutexUnlock((x)->h)

static inline int mutex_lock_interruptible(struct mutex *lock)
{
	mutex_lock(lock);
	return 0;
}

static inline bool mutex_is_locked(struct mutex *lock)
{
	return 1;
}


struct rw_semaphore {
	handle_t	lock;
	handle_t	cond;
	int			cnt;
	int			wait;
};

extern void up_read(struct rw_semaphore *sem);

extern void down_read(struct rw_semaphore *sem);

extern void up_write(struct rw_semaphore *sem);

extern void down_write(struct rw_semaphore *sem);

#define MAX_LOCKDEP_SUBCLASSES		8UL

/*
 * Lock-classes are keyed via unique addresses, by embedding the
 * lockclass-key into the kernel (or module) .data section. (For
 * static locks we use the lock address itself as the key.)
 */
struct lockdep_subclass_key {
	char __one_byte;
} __attribute__ ((__packed__));

struct lock_class_key {
	struct lockdep_subclass_key	subkeys[MAX_LOCKDEP_SUBCLASSES];
};


extern void __init_rwsem(struct rw_semaphore *sem, const char *name,
			 struct lock_class_key *key);

#define init_rwsem(sem)						\
do {								\
	static struct lock_class_key __key;			\
								\
	__init_rwsem((sem), #sem, &__key);			\
} while (0)

#define mutex_init(x) mutexCreate(&((x)->h))

#define assert_spin_locked(lock) (1)

#define DEFINE_SPINLOCK(x) handle_t x


#endif /* _OS_PHOENIX_LOCKS_H_ */
