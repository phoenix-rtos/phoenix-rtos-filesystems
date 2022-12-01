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

struct mutex {
	handle_t h;
};

typedef handle_t spinlock_t;

#define spin_lock_init(x) mutexCreate(x)
#define spin_lock_destroy(x) resourceDestroy(*x)
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
	if (!mutexTry(lock->h)) {
		mutex_unlock(lock);
		return 0;
	}

	return 1;
}


struct rw_semaphore {
	handle_t lock;
};

extern void up_read(struct rw_semaphore *sem);

extern void down_read(struct rw_semaphore *sem);

extern void up_write(struct rw_semaphore *sem);

extern void down_write(struct rw_semaphore *sem);

#define MAX_LOCKDEP_SUBCLASSES		8UL

struct lockdep_subclass_key {
};

struct lock_class_key {
	struct lockdep_subclass_key	subkeys[MAX_LOCKDEP_SUBCLASSES];
};

void init_rwsem(struct rw_semaphore *sem);

void exit_rwsem(struct rw_semaphore *sem);

#define mutex_init(x) mutexCreate(&((x)->h))
#define mutex_destroy(x) resourceDestroy((x)->h)

#define assert_spin_locked(lock) (1)

#define DEFINE_SPINLOCK(x) handle_t x


#endif /* _OS_PHOENIX_LOCKS_H_ */
