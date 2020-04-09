/*
 * Phoenix-RTOS
 *
 * ATA server
 *
 * Copyright 2019, 2020 Phoenix Systems
 * Author: Kamil Amanowicz, Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _PC_ATASRV_H_
#define _PC_ATASRV_H_

#include <stdint.h>

#include <sys/types.h>


extern ssize_t atasrv_read(id_t *id, offs_t offs, char *buff, size_t len);


extern ssize_t atasrv_write(id_t *id, offs_t offs, const char *buff, size_t len);


#endif
