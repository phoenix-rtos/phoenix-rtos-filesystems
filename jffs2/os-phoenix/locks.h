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

#define spin_lock(x) mutexLock(*x)
#define spin_unlock(x) mutexUnlock(*x)

#define mutex_lock(x) mutexLock((x)->h)
#define mutex_unlock(x) mutexUnlock((x)->h)

#define DEFINE_SPINLOCK(x) handle_t x


#endif /* _OS_PHOENIX_LOCKS_H_ */
