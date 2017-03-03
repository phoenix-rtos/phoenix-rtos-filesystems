/* 
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Messaging routines
 *
 * Copyright 2012 Phoenix Systems
 * Copyright 2001, 2005-2006 Pawel Pisarczyk
 *
 * Author: Pawel Pisarczyk, Jacek Popko
 * 
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _FS_PHFS_MSG_H_
#define _FS_PHFS_MSG_H_

#include <hal/if.h>
#include "phfs.h"
#include <fs/vnode.h>
#include <fs/vnode_buff.h>

#include <lwip/api.h>

/* Special characters */
#define PHFS_MSG_MARK      0x7e
#define PHFS_MSG_ESC       0x7d
#define PHFS_MSG_ESCMARK   0x5e
#define PHFS_MSG_ESCESC    0x5d


/* Transmission parameters */
#define PHFS_MSG_TIMEOUT  500 * 1000     /* microseconds */
#define PHFS_MSG_MAXRETR  7


/* Message parameters */
#define PHFS_MSG_HDRSZ   2 * 4
#define PHFS_MSG_MAXLEN  512

/* Transmission state */
#define PHFS_STATE_DESYN   0
#define PHFS_STATE_FRAME   1


typedef struct _phfs_msg_t {
	u32 csum;
	u32 type;
	u8  data[PHFS_MSG_MAXLEN];
} phfs_msg_t;


/* Message types */
#define PHFS_ERR    0


#define phfs_msg_settype(m, t)  ((m)->type = ((m)->type & ~0xffff) | (t & 0xffff))
#define phfs_msg_gettype(m)     ((u16)((m)->type & 0xffff))
#define phfs_msg_setlen(m, l)   ((m)->type = ((m)->type & 0xffff) | ((u32)(l) << 16))
#define phfs_msg_getlen(m)      ((u16)((m)->type >> 16))


typedef struct _phfs_priv_t phfs_priv_t;

struct _phfs_priv_t {
	int (*msg_read)(phfs_priv_t *phfs, phfs_msg_t *msg, ktime_t timeout, int *state);
	int (*msg_write)(phfs_priv_t *phfs, phfs_msg_t *msg);
	int (*terminate)(phfs_priv_t *phfs);
	semaphore_t mutex;
	union {
		struct {
            file_t* file;
			vnode_buff vb;
		};
		struct {
			struct netconn *conn;
			ip_addr_t addr;
			u16	port;
		};
	};
};


extern int phfs_msg_send(phfs_priv_t *phfs, phfs_msg_t *smsg, phfs_msg_t *rmsg);
extern int phfs_msg_init(phfs_priv_t *phfs, const phfs_opt_t *opt);

#endif
