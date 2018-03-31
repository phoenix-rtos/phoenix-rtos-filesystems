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
#include <fs/if.h>
#include "phfs_msg.h"


static int phfs_serial_safewrite(vnode_buff *vb, u8 *buff, unsigned int len)
{
	int l;
	
	for (l = 0; len;) {
		if ((l = vb_write(vb, (char *)buff, len)) < 0)
			return -EIO;
		buff += l;
		len -= l;
	}
	return 0;
}


static int phfs_serial_write(phfs_priv_t *phfs, phfs_msg_t *msg)
{
	u8 *p = (u8 *)msg;
	u8 cs[2];
	u16 k;
	int res;

	/* Frame start */
	cs[0] = PHFS_MSG_MARK;
	if ((res = phfs_serial_safewrite(&phfs->vb, cs, 1)) < 0)
		return res;

	for (k = 0; k < PHFS_MSG_HDRSZ + phfs_msg_getlen(msg); k++) {
		if ((p[k] == PHFS_MSG_MARK) || (p[k] == PHFS_MSG_ESC)) {
			cs[0] = PHFS_MSG_ESC;
			if (p[k] == PHFS_MSG_MARK)
				cs[1] = PHFS_MSG_ESCMARK;
			else
				cs[1] = PHFS_MSG_ESCESC;
			if ((res = phfs_serial_safewrite(&phfs->vb, cs, 2)) < 0)
				return res;
		}
		else {
			if ((res = phfs_serial_safewrite(&phfs->vb, &p[k], 1)) < 0)
				return res;
		}
	}
	vb_flush(&phfs->vb);
	return k;
}


static int phfs_serial_read(phfs_priv_t *phfs, phfs_msg_t *msg, ktime_t timeout, int *state)
{
	int escfl = 0, err;
	unsigned int l = 0;
	u8 c;
	
	for (;;) {
		
		/* Wait for data */
		if ((err = vb_poll(&phfs->vb, timeout, POLL_READ)) < 0) {
			if (err != -ETIME)
				err = -EIO;
			*state = PHFS_STATE_DESYN;
			return err;
		}

		if (vb_read(&phfs->vb, (char *)&c, 1) < 0) {
			*state = PHFS_STATE_DESYN;
			return -EIO;
		}

		if (*state == PHFS_STATE_FRAME) {
			
			/* Return error if frame is to long */
			if (l == PHFS_MSG_HDRSZ + PHFS_MSG_MAXLEN) {
				*state = PHFS_STATE_DESYN;
				return -EIO;
			}

			/* Return error if terminator discovered */
			if (c == PHFS_MSG_MARK)
				return -EIO;

			if (!escfl && (c == PHFS_MSG_ESC)) {
				escfl = 1;
				continue;
			}
			if (escfl) {
				if (c == PHFS_MSG_ESCMARK)
					c = PHFS_MSG_MARK;
				if (c == PHFS_MSG_ESCESC)
					c = PHFS_MSG_ESC;
				escfl = 0;
			}
			*((u8 *)msg + l++) = c;

			/* Frame received */
			if ((l >= PHFS_MSG_HDRSZ) && (l == phfs_msg_getlen(msg) + PHFS_MSG_HDRSZ)) {
				*state = PHFS_STATE_DESYN;
				break;
			}
		}
		else {
			/* Synchronize */
			if (c == PHFS_MSG_MARK)
				*state = PHFS_STATE_FRAME;
		}
	}

	return l;
}


static int phfs_serial_terminate(phfs_priv_t *phfs)
{
    if (phfs->file != NULL)
        fs_close(phfs->file);

	return EOK;
}


int phfs_serial_init(phfs_priv_t *phfs, const phfs_opt_t *opt)
{
	int ret;

	if (opt->transport != PHFS_SERIAL)
		return -EINVAL;

    vnode_t* vnode;
    if (opt->dev_vnode) {
        vnode = vnode_acq(opt->dev_vnode);
    } else {
        if ((ret = fs_lookup(opt->device, &vnode, 1)) != EOK)
            return ret;
    }

    ret = fs_openv(vnode, O_RDWR, &(phfs->file));
    vnode_put(vnode);
    if (ret != 0)
        return ret;

	vb_init(&phfs->vb, phfs->file);
	phfs->msg_read = phfs_serial_read;
	phfs->msg_write = phfs_serial_write;
	phfs->terminate = phfs_serial_terminate;

	return EOK;
}
