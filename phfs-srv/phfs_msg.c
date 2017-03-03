/* 
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Messaging routines
 *
 * Copyright 2012 Phoenix Systems
 * Copyright 2001, 2005 Pawel Pisarczyk
 *
 * Author: Pawel Pisarczyk, Jacek Popko
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <hal/if.h>
#include <main/if.h>
#include "phfs_msg.h"
#include "phfs_udp.h"
#include "phfs_serial.h"


static u32 phfs_msg_csum(phfs_msg_t *msg)
{
	unsigned int k;
	u32 csum;

	csum = 0;
	for (k = 0; k < PHFS_MSG_HDRSZ + phfs_msg_getlen(msg); k++) {
		if (k >= sizeof(msg->csum))
			csum += (u32)(*((u8 *)msg + k));
	}
	return csum;
}


int phfs_msg_send(phfs_priv_t *phfs, phfs_msg_t *smsg, phfs_msg_t *rmsg)
{
	unsigned int retr;
	int state = PHFS_STATE_DESYN;

	smsg->csum = phfs_msg_csum(smsg);
	for (retr = 0; retr < PHFS_MSG_MAXRETR; retr++) {
		if (phfs->msg_write(phfs, smsg) < 0)
			continue;

		if ((phfs->msg_read(phfs, rmsg, PHFS_MSG_TIMEOUT, &state)) > 0 &&
			 phfs_msg_csum(rmsg) == rmsg->csum)
			return EOK;
	}

	return -EIO;
}


int phfs_msg_init(phfs_priv_t *phfs, const phfs_opt_t *opt)
{
#ifdef CONFIG_NET
	if (opt->transport == PHFS_UDP)
		return phfs_udp_init(phfs, opt);
#endif
	
	if (opt->transport == PHFS_SERIAL)
		return phfs_serial_init(phfs, opt);

	return -EINVAL;
}
