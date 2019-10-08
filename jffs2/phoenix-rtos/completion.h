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


#ifndef _OS_PHOENIX_COMPLETION_H_
#define _OS_PHOENIX_COMPLETION_H_

struct completion {
	uint8_t complete;
	handle_t lock;
	handle_t cond;
};


void init_completion(struct completion *comp);


void complete(struct completion *comp);


void wait_for_completion(struct completion *comp);


void complete_and_exit(struct completion *comp, int code);


#endif /* _OS_PHOENIX_COMPLETION_H_ */
