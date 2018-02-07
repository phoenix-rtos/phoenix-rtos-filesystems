/*
 * Phoenix-RTOS
 *
 * ext2
 *
 * object.h
 *
 * Copyright 2017 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _OBJECT_H_
#define _OBJECT_H_ /* object.h */


extern ext2_object_t *object_get(unsigned int id);


extern void object_put(ext2_object_t *o);


extern void object_init(void);

#endif /* object.h */
