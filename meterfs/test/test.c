/*
 * Phoenix-RTOS
 *
 * Meterfs test
 *
 * Copyright 2018 Phoenix Systems
 * Author: Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <sys/msg.h>
#include <unistd.h>
#include <string.h>

#include "../meterfs.h"


struct {
	oid_t meterfs_oid;
	msg_t msg;
} test_common;


int test_allocate(const char *name, size_t sectors, size_t filesz, size_t recordsz)
{
	meterfs_i_devctl_t *i = (meterfs_i_devctl_t *)&test_common.msg.i.raw;
	meterfs_o_devctl_t *o = (meterfs_o_devctl_t *)&test_common.msg.o.raw;
	int err;

	test_common.msg.type = mtDevCtl;
	test_common.msg.i.data = NULL;
	test_common.msg.i.size = 0;
	test_common.msg.o.data = NULL;
	test_common.msg.i.size = 0;
	i->type = meterfs_allocate;
	strncpy(i->allocate.name, name, sizeof(i->allocate.name));
	i->allocate.sectors = sectors;
	i->allocate.filesz = filesz;
	i->allocate.recordsz = recordsz;

	err = msgSend(test_common.meterfs_oid.port, &test_common.msg);

	return (err < 0) ? err : o->err;
}


int test_resize(oid_t *oid, size_t filesz, size_t recordsz)
{
	meterfs_i_devctl_t *i = (meterfs_i_devctl_t *)&test_common.msg.i.raw;
	meterfs_o_devctl_t *o = (meterfs_o_devctl_t *)&test_common.msg.o.raw;
	int err;

	test_common.msg.type = mtDevCtl;
	test_common.msg.i.data = NULL;
	test_common.msg.i.size = 0;
	test_common.msg.o.data = NULL;
	test_common.msg.i.size = 0;
	i->type = meterfs_resize;
	i->resize.oid = *oid;
	i->resize.filesz = filesz;
	i->resize.recordsz = recordsz;

	err = msgSend(test_common.meterfs_oid.port, &test_common.msg);

	return (err < 0) ? err : o->err;
}


int test_chiperase(void)
{
	meterfs_i_devctl_t *i = (meterfs_i_devctl_t *)&test_common.msg.i.raw;
	meterfs_o_devctl_t *o = (meterfs_o_devctl_t *)&test_common.msg.o.raw;
	int err;

	test_common.msg.type = mtDevCtl;
	test_common.msg.i.data = NULL;
	test_common.msg.i.size = 0;
	test_common.msg.o.data = NULL;
	test_common.msg.i.size = 0;
	i->type = meterfs_chiperase;

	err = msgSend(test_common.meterfs_oid.port, &test_common.msg);

	return (err < 0) ? err : o->err;
}


int test_fileinfo(oid_t *oid, struct _info *info)
{
	meterfs_i_devctl_t *i = (meterfs_i_devctl_t *)&test_common.msg.i.raw;
	meterfs_o_devctl_t *o = (meterfs_o_devctl_t *)&test_common.msg.o.raw;
	int err;

	test_common.msg.type = mtDevCtl;
	test_common.msg.i.data = NULL;
	test_common.msg.i.size = 0;
	test_common.msg.o.data = NULL;
	test_common.msg.i.size = 0;
	i->type = meterfs_info;
	i->oid = *oid;

	err = msgSend(test_common.meterfs_oid.port, &test_common.msg);

	memcpy(info, &o->info, sizeof(*info));

	return (err < 0) ? err : o->err;
}


int test_write(oid_t *oid, void *buff, size_t len)
{
	int err;

	test_common.msg.type = mtWrite;
	test_common.msg.i.io.oid = *oid;
	test_common.msg.i.io.offs = 0;
	test_common.msg.i.io.len = len;
	test_common.msg.i.io.mode = 0;
	test_common.msg.i.data = NULL;
	test_common.msg.i.size = 0;
	test_common.msg.o.data = buff;
	test_common.msg.o.size = len;

	err = msgSend(test_common.meterfs_oid.port, &test_common.msg);

	return (err < 0) ? err : test_common.msg.o.io.err;
}


int test_read(oid_t *oid, offs_t offs, void *buff, size_t len)
{
	int err;

	test_common.msg.type = mtRead;
	test_common.msg.i.io.oid = *oid;
	test_common.msg.i.io.offs = offs;
	test_common.msg.i.io.len = len;
	test_common.msg.i.io.mode = 0;
	test_common.msg.i.data = buff;
	test_common.msg.i.size = len;
	test_common.msg.o.data = NULL;
	test_common.msg.o.size = 0;

	err = msgSend(test_common.meterfs_oid.port, &test_common.msg);

	return (err < 0) ? err : test_common.msg.o.io.err;
}


int test_open(const char *name, oid_t *oid)
{
	int err;

	if ((err = lookup(name, oid)) < 0)
		return err;

	test_common.msg.type = mtOpen;
	test_common.msg.i.openclose.oid = *oid;
	test_common.msg.i.openclose.flags = 0;
	test_common.msg.i.data = NULL;
	test_common.msg.i.size = 0;
	test_common.msg.o.data = NULL;
	test_common.msg.i.size = 0;

	err = msgSend(test_common.meterfs_oid.port, &test_common.msg);

	return (err < 0) ? err : test_common.msg.o.io.err;
}


int test_close(oid_t *oid)
{
	int err;

	test_common.msg.type = mtClose;
	test_common.msg.i.openclose.oid = *oid;
	test_common.msg.i.openclose.flags = 0;
	test_common.msg.i.data = NULL;
	test_common.msg.i.size = 0;
	test_common.msg.o.data = NULL;
	test_common.msg.i.size = 0;

	err = msgSend(test_common.meterfs_oid.port, &test_common.msg);

	return (err < 0) ? err : test_common.msg.o.io.err;
}


int main(void)
{
	while (lookup("/", &test_common.meterfs_oid) < 0)
		usleep(100000);


	return 0;
}
