/* 
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Messaging routines
 *
 * Copyright 2012, 2013 Phoenix Systems
 * Author: Jacek Popko
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifdef CONFIG_NET

#include <hal/if.h>
#include "phfs_udp.h"
#include <lwip/netbuf.h>
#include <lwip/api.h>
#include <lwip/netif.h>
#include <main/if.h>


#define PHFS_MAX_TTL	2
#define PHFS_UMOUNT_TIMEOUT	5000000

static int phfs_udp_write(phfs_priv_t *phfs, phfs_msg_t *msg)
{
#ifdef PHFS_UDPENCODE
	u8 buf[2 * sizeof(phfs_msg_t)];
#endif
	struct netbuf *nb;
	u16 k;
	int res;

	
	if ((nb = netbuf_new()) == NULL)
		return -ENOMEM;
#ifdef PHFS_UDPENCODE
	{
		u8 *p = (u8 *)msg, *bufptr = buf;
		
		*bufptr++ = PHFS_MSG_MARK;
		for (k = 0; k < PHFS_MSG_HDRSZ + phfs_msg_getlen(msg); k++)
			if ((p[k] == PHFS_MSG_MARK) || (p[k] == PHFS_MSG_ESC)) {
				*bufptr++ = PHFS_MSG_ESC;
				if (p[k] == PHFS_MSG_MARK)
					*bufptr++ = PHFS_MSG_ESCMARK;
				else
					*bufptr++ = PHFS_MSG_ESCESC;
			}
			else
				*bufptr++ = p[k];

		netbuf_ref(nb, buf, bufptr - buf);
	}
#else
	k = PHFS_MSG_HDRSZ + phfs_msg_getlen(msg);
	netbuf_ref(nb, msg, k);
#endif

	res = netconn_sendto(phfs->conn, nb, &phfs->addr, phfs->port);
	netbuf_delete(nb);

	if (res != EOK)
		return -EIO;
	return k;
}


static int phfs_udp_read(phfs_priv_t *phfs, phfs_msg_t *msg, ktime_t timeout, int *state)
{
	int err, complete = 0;
	unsigned int l = 0;
	u8 *bufptr;
	struct netbuf *nb;
	u16 buflen;
	
	netconn_set_recvtimeout(phfs->conn, timeout / 1000);

	if ((err = netconn_recv(phfs->conn, &nb)) != ERR_OK) {
		*state = PHFS_STATE_DESYN;
		if (err != -EWOULDBLOCK) /* timeout */
			err = -EIO;
		return err;
	}

	netbuf_first(nb);
	do {
#ifdef PHFS_UDPENCODE
		int escfl = 0;
		
		for (netbuf_data(nb, (void **)&bufptr, &buflen); buflen > 0; bufptr++, buflen--)
			if (*state == PHFS_STATE_FRAME) {

				/* Return error if frame is to long */
				if (l == PHFS_MSG_HDRSZ + PHFS_MSG_MAXLEN) {
					*state = PHFS_STATE_DESYN;
					netbuf_delete(nb);
					return -EIO;
				}

				/* Return error if terminator discovered */
				if (*bufptr == PHFS_MSG_MARK) {
					netbuf_delete(nb);
					return -EIO;
				}

				if (!escfl && (*bufptr == PHFS_MSG_ESC)) {
					escfl = 1;
					continue;
				}
				if (escfl) {
					if (*bufptr == PHFS_MSG_ESCMARK)
						*((u8 *)msg + l++) = PHFS_MSG_MARK;
					if (*bufptr == PHFS_MSG_ESCESC)
						*((u8 *)msg + l++) = PHFS_MSG_ESC;
					escfl = 0;
				}
				else
					*((u8 *)msg + l++) = *bufptr;

				/* Frame received */
				if ((l >= PHFS_MSG_HDRSZ) && (l == phfs_msg_getlen(msg) + PHFS_MSG_HDRSZ)) {
					*state = PHFS_STATE_DESYN;
					complete = 1;
					break;
				}
			}
			else
				/* Synchronize */
				if (*bufptr == PHFS_MSG_MARK)
					*state = PHFS_STATE_FRAME;
#else
		netbuf_data(nb, (void **)&bufptr, &buflen);
		memcpy((u8 *)msg + l, bufptr, buflen);
		l += buflen;
#endif
	}
	while (netbuf_next(nb) >= 0 && !complete);

	netbuf_delete(nb);
	
	return l;
}


static int phfs_udp_terminate(phfs_priv_t *phfs)
{
	if (phfs->conn != NULL)
		return netconn_delete(phfs->conn);
	return EOK;
}


int phfs_udp_init(phfs_priv_t *phfs, const phfs_opt_t *opt)
{
	if (opt->transport != PHFS_UDP)
		return -EINVAL;

	if ((phfs->conn = netconn_new(NETCONN_UDP)) == NULL)
		return -ENOMEM;

	if (netconn_bind(phfs->conn, IP_ADDR_ANY, 0) != ERR_OK) {
		phfs_udp_terminate(phfs);
		return -EIO;
	}

	phfs->msg_read = phfs_udp_read;
	phfs->msg_write = phfs_udp_write;
	phfs->terminate = phfs_udp_terminate;
	phfs->addr.addr = opt->ipaddr;
	phfs->port = opt->port;

	return EOK;
}


struct bsd_sockaddr_in
{
	u16	sin_family;	/* Address family		*/
	u16	sin_port;	/* Port number			*/
	u32	sin_addr;	/* Internet address		*/
	/* Pad to size of `struct sockaddr'.	*/
	u8	sin_zero[8];
} __attribute__((packed));


typedef struct phfs_automount_s {
	struct phfs_automount_s *next;
	ip_addr_t addr;
	u16	port;
	u16 ttl;
	char path[28];
	phfs_opt_t opt;
} phfs_automount_t;


static void phfs_lruUmount(phfs_automount_t **head)
{
#if 0	/* TODO: uncomment once FS unmount operation is implemented */
	vnode_t *vnode;
	phfs_automount_t *m, *p;
	
	p = NULL;
	m = *head;
	fs_lookup("/net", &vnode, 1);

	while (m != NULL) {
		if (m->ttl == 0) {
			
			main_printf(ATTR_INFO, "net: %s inactive, unmounting\n", m->path);
			fs_umount(m->path);
			vnode_rmdir(vnode, strrchr(m->path, '/') + 1);

			if (m == *head) {
				*head = m->next;
				vm_kfree(m);
				m = *head;
			}
			else {
				p->next = m->next;
				vm_kfree(m);
				m = p->next;
			}
		}
		else {
			m->ttl--;
			p = m;
			m = m->next;
		}
			
	}
#endif
}


static int phfs_automounthr(void *arg)
{
	phfs_priv_t	phfs;
	phfs_automount_t *mounthead = NULL;
	vnode_t *vnode;
	ktime_t	last_umount, current_time;
	
	fs_lookup("/", &vnode, 1);
	vnode_mkdir(vnode, "net", 0777);
	if (fs_mount("/net", TYPE_DUMMYFS, NULL) != EOK) {
		main_printf(ATTR_ERROR, "phfs: Can't mount /net pseudo fs\n");
		return -EIO;
	}
	fs_lookup("/net", &vnode, 1);

	if ((phfs.conn = netconn_new(NETCONN_UDP)) == NULL)
		return -ENOMEM;

	if (netconn_bind(phfs.conn, IP_ADDR_ANY, PHFS_DEFPORT) != EOK) {
		return -EIO;
	}
	
	last_umount = timesys_getTime();
	for (;;) {
		phfs_msg_t msg;
		int l, state = PHFS_STATE_DESYN;
		phfs_automount_t *p;
		struct bsd_sockaddr_in *sockaddr;
		
		l = phfs_udp_read(&phfs, &msg, PHFS_UMOUNT_TIMEOUT, &state);
		if (l == -EIO) {
			main_printf(ATTR_ERROR, "net: %s(): receive failed\n", __FUNCTION__);
			continue;
		}
		
		if (l == -EWOULDBLOCK) {
			last_umount = timesys_getTime();
			phfs_lruUmount(&mounthead);
			continue;
		}
		
		if (l <= 0 || phfs_msg_gettype(&msg) != PHFS_HELLO) {
			main_printf(ATTR_ERROR, "net: %s(): unknown messaged received\n", __FUNCTION__);
			continue;
		}
	
		sockaddr = (struct bsd_sockaddr_in *)msg.data;
		sockaddr->sin_port = ntohs(sockaddr->sin_port);
		for (p = mounthead; p != NULL; p = p->next)
			if (p->opt.ipaddr == sockaddr->sin_addr && p->opt.port == sockaddr->sin_port) {
				if (p->ttl < PHFS_MAX_TTL)
					p->ttl++;
				break;
			}

		if (p == NULL)
		{
			phfs_opt_t *opt;

			p = vm_kmalloc(sizeof(phfs_automount_t));
			p->next = mounthead;
			mounthead = p;
			p->ttl = PHFS_MAX_TTL;
			opt = &p->opt;
			opt->magic = 0xaa55a55a;
			opt->transport = PHFS_UDP;
			opt->port = sockaddr->sin_port;
			opt->ipaddr = sockaddr->sin_addr;
			main_snprintf(p->path, sizeof(p->path),	"/net/%d.%d.%d.%d:%d",
				((u8 *)&opt->ipaddr)[0], ((u8 *)&opt->ipaddr)[1],
				((u8 *)&opt->ipaddr)[2], ((u8 *)&opt->ipaddr)[3],
				opt->port);

			vnode_mkdir(vnode, strrchr(p->path, '/') + 1, 0777);
			if (fs_mount(p->path, TYPE_PHFS, opt) < 0)
				main_printf(ATTR_ERROR, "net: %s(): failed to mount %s\n", __FUNCTION__, p->path);
			else
				main_printf(ATTR_INFO, "net: %s mounted\n", p->path);
		}
		
		current_time = timesys_getTime();
		if (current_time - (last_umount + PHFS_UMOUNT_TIMEOUT)  < (ktime_t)-1 / 2) {
			
			last_umount = current_time;
			phfs_lruUmount(&mounthead);
		}
	}
	
	return 0;
}


int phfs_connect(u32 ip, u16 port, const char* dirname)
{
    vnode_t *vnode;

    fs_lookup("/", &vnode, 1);
    vnode_mkdir(vnode, "net", 0777);
    if (fs_mount("/net", TYPE_DUMMYFS, NULL) != EOK) {
        main_printf(ATTR_ERROR, "phfs: Can't mount /net pseudo fs\n");
        return -EIO;
    }
    fs_lookup("/net", &vnode, 1);

    vnode_mkdir(vnode, dirname, 0777);

    phfs_opt_t  opt;
    opt.magic = 0xaa55a55a;
    opt.transport = PHFS_UDP;
    opt.port = port;
    opt.ipaddr = ip;

    char full_dirname[64];
    main_snprintf(full_dirname, 64, "/net/%s", dirname);

    if (fs_mount(full_dirname, TYPE_PHFS, &opt) < 0) {
        main_printf(ATTR_ERROR, "net: %s(): failed to mount %s\n", __FUNCTION__, full_dirname);
        return -EIO;
    } else {
        main_printf(ATTR_INFO, "net: %s mounted\n", full_dirname);
        return 0;
    }
}


void phfs_automounter(void)
{
	proc_thread(NULL, phfs_automounthr, NULL, 0, NULL, ttRegular);
}


#endif // #ifdef CONFIG_NET
