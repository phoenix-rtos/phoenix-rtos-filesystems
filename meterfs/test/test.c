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
#include <stdio.h>

#include "../meterfs.h"


struct {
	oid_t meterfs_oid;
	msg_t msg;
} test_common;


void test_hexdump(char *buff, size_t bufflen)
{
	int i, j;

	for (i = 0; i < bufflen; i += 16) {
		printf("\t");
		for (j = i; j < 16 && i + j < bufflen; ++j)
			printf("0x%02x ", buff[i + j]);
		printf("\n");
	}
}


int test_allocate(const char *name, size_t sectors, size_t filesz, size_t recordsz)
{
	meterfs_i_devctl_t *i = (meterfs_i_devctl_t *)test_common.msg.i.raw;
	meterfs_o_devctl_t *o = (meterfs_o_devctl_t *)test_common.msg.o.raw;
	int err;

	test_common.msg.type = mtDevCtl;
	test_common.msg.i.data = NULL;
	test_common.msg.i.size = 0;
	test_common.msg.o.data = NULL;
	test_common.msg.o.size = 0;
	i->type = meterfs_allocate;
	strncpy(i->allocate.name, name, sizeof(i->allocate.name));
	i->allocate.sectors = sectors;
	i->allocate.filesz = filesz;
	i->allocate.recordsz = recordsz;

	printf("test: Allocating file \"%s\": %u sectors, file size %u, record size %u\n", name, sectors, filesz, recordsz);

	err = msgSend(test_common.meterfs_oid.port, &test_common.msg);

	err = (err < 0) ? err : o->err;

	printf("test: (%s)\n", strerror(err));

	return err;
}


int test_resize(oid_t *oid, size_t filesz, size_t recordsz)
{
	meterfs_i_devctl_t *i = (meterfs_i_devctl_t *)test_common.msg.i.raw;
	meterfs_o_devctl_t *o = (meterfs_o_devctl_t *)test_common.msg.o.raw;
	int err;

	test_common.msg.type = mtDevCtl;
	test_common.msg.i.data = NULL;
	test_common.msg.i.size = 0;
	test_common.msg.o.data = NULL;
	test_common.msg.o.size = 0;
	i->type = meterfs_resize;
	i->resize.oid = *oid;
	i->resize.filesz = filesz;
	i->resize.recordsz = recordsz;

	printf("test: Resizing file #%u: new file size %u, new record size %u\n", oid->id, filesz, recordsz);

	err = msgSend(test_common.meterfs_oid.port, &test_common.msg);

	err = (err < 0) ? err : o->err;

	printf("test: (%s)\n", strerror(err));

	return err;
}


int test_chiperase(void)
{
	meterfs_i_devctl_t *i = (meterfs_i_devctl_t *)test_common.msg.i.raw;
	meterfs_o_devctl_t *o = (meterfs_o_devctl_t *)test_common.msg.o.raw;
	int err;

	test_common.msg.type = mtDevCtl;
	test_common.msg.i.data = NULL;
	test_common.msg.i.size = 0;
	test_common.msg.o.data = NULL;
	test_common.msg.o.size = 0;
	i->type = meterfs_chiperase;

	printf("test: Performing chip erase\n");

	err = msgSend(test_common.meterfs_oid.port, &test_common.msg);

	err = (err < 0) ? err : o->err;

	printf("test: (%s)\n", strerror(err));

	return err;
}


int test_fileinfo(oid_t *oid, struct _info *info)
{
	meterfs_i_devctl_t *i = (meterfs_i_devctl_t *)test_common.msg.i.raw;
	meterfs_o_devctl_t *o = (meterfs_o_devctl_t *)test_common.msg.o.raw;
	int err;

	test_common.msg.type = mtDevCtl;
	test_common.msg.i.data = NULL;
	test_common.msg.i.size = 0;
	test_common.msg.o.data = NULL;
	test_common.msg.o.size = 0;
	i->type = meterfs_info;
	i->oid = *oid;

	err = msgSend(test_common.meterfs_oid.port, &test_common.msg);

	if (info != NULL)
		memcpy(info, &o->info, sizeof(*info));

	printf("test: Got file #%u info: %u sectors, %u record(s), file size %u, record size %u\n",
		oid->id, o->info.sectors, o->info.recordcnt, o->info.filesz, o->info.recordsz);

	err = (err < 0) ? err : o->err;

	printf("test: (%s)\n", strerror(err));

	return err;
}


int test_write(oid_t *oid, void *buff, size_t len)
{
	int err;

	test_common.msg.type = mtWrite;
	test_common.msg.i.io.oid = *oid;
	test_common.msg.i.io.offs = 0;
	test_common.msg.i.io.len = len;
	test_common.msg.i.io.mode = 0;
	test_common.msg.i.data = buff;
	test_common.msg.i.size = len;
	test_common.msg.o.data = NULL;
	test_common.msg.o.size = 0;

	err = msgSend(test_common.meterfs_oid.port, &test_common.msg);

	printf("test: Write to file #%u len %u\n", oid->id, len);
	test_hexdump(buff, len);

	err = (err < 0) ? err : test_common.msg.o.io.err;

	if (err < 0)
		printf("test: (%s)\n", strerror(err));
	else
		printf("test: %d bytes\n", err);

	return err;
}


int test_read(oid_t *oid, offs_t offs, void *buff, size_t len)
{
	int err;

	test_common.msg.type = mtRead;
	test_common.msg.i.io.oid = *oid;
	test_common.msg.i.io.offs = offs;
	test_common.msg.i.io.len = len;
	test_common.msg.i.io.mode = 0;
	test_common.msg.i.data = NULL;
	test_common.msg.i.size = 0;
	test_common.msg.o.data = buff;
	test_common.msg.o.size = len;

	err = msgSend(test_common.meterfs_oid.port, &test_common.msg);

	printf("test: Read from file #%u len %u @offset %u\n", oid->id, len, (size_t)offs);
	test_hexdump(buff, len);

	err = (err < 0) ? err : test_common.msg.o.io.err;

	if (err < 0)
		printf("test: (%s)\n", strerror(err));
	else
		printf("test: %d bytes\n", err);

	return err;
}


int test_open(const char *name, oid_t *oid)
{
	int err;

	printf("test: lookup of file \"%s\" ", name);

	if ((err = lookup(name, NULL, oid)) < 0) {
		printf(" failed (%s)\n", strerror(err));
		return err;
	}

	printf(" found id %u\n", oid->id);

	test_common.msg.type = mtOpen;
	test_common.msg.i.openclose.oid = *oid;
	test_common.msg.i.openclose.flags = 0;
	test_common.msg.i.data = NULL;
	test_common.msg.i.size = 0;
	test_common.msg.o.data = NULL;
	test_common.msg.o.size = 0;

	printf("test: Open\n");

	err = msgSend(test_common.meterfs_oid.port, &test_common.msg);

	err = (err < 0) ? err : test_common.msg.o.io.err;

	printf("test: (%s)\n", strerror(err));

	return err;
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
	test_common.msg.o.size = 0;

	printf("test: Close id %u\n", oid->id);

	err = msgSend(test_common.meterfs_oid.port, &test_common.msg);

	err = (err < 0) ? err : test_common.msg.o.io.err;

	printf("test: (%s)\n", strerror(err));

	return err;
}


int main(void)
{
	oid_t oids[20];
	int opened[20], i;
	char buff[20];

	while (lookup("/", NULL, &test_common.meterfs_oid) < 0)
		usleep(100000);

	printf("test: Started\n");
	test_chiperase();
	test_allocate("test1", 0, 0, 0);
	test_allocate("test2", 0, 2000, 20);
	test_allocate("test3", 1, 2000, 20);
	test_allocate("test4", 2, 20, 200);
	test_allocate("test5", 4, 20, 20);
	test_allocate("test6", 3, 2000000, 20);
	test_allocate("test7", 6, 2000, 20);
	test_allocate("test8", 7, 2000, 20);
	test_allocate("test9", 8, 2000, 20);
	test_allocate("test10", 12, 2000, 20);
	test_allocate("test11", 10, 2000, 20);
	test_allocate("test12", 9, 2000, 20);

	opened[0] = test_open("/test1", &oids[0]);
	opened[1] = test_open("/test2", &oids[1]);
	opened[2] = test_open("/test3", &oids[2]);
	opened[3] = test_open("/test4", &oids[3]);
	opened[4] = test_open("/test5", &oids[4]);
	opened[5] = test_open("/test6", &oids[5]);
	opened[6] = test_open("/test7", &oids[6]);
	opened[7] = test_open("/test8", &oids[7]);
	opened[8] = test_open("/test9", &oids[8]);
	opened[9] = test_open("/test10", &oids[9]);
	opened[10] = test_open("/test11", &oids[10]);
	opened[11] = test_open("/test12", &oids[11]);

	test_fileinfo(&oids[11], NULL);

	for (i = 0; i < 12; ++i) {
		if (opened[i])
			test_close(&oids[i]);
	}

	opened[0] = test_open("/test1", &oids[0]);
	opened[1] = test_open("/test2", &oids[1]);
	opened[2] = test_open("/test3", &oids[2]);
	opened[3] = test_open("/test4", &oids[3]);
	opened[4] = test_open("/test5", &oids[4]);
	opened[5] = test_open("/test6", &oids[5]);
	opened[6] = test_open("/test7", &oids[6]);
	opened[7] = test_open("/test8", &oids[7]);
	opened[8] = test_open("/test9", &oids[8]);
	opened[9] = test_open("/test10", &oids[9]);
	opened[10] = test_open("/test11", &oids[10]);
	opened[11] = test_open("/test12", &oids[11]);

	test_write(&oids[11], "a0000", 5);
	test_write(&oids[11], "a0001", 5);
	test_write(&oids[11], "a0002", 5);
	test_write(&oids[11], "a0003", 5);
	test_write(&oids[11], "a0004", 5);
	test_write(&oids[11], "a0005", 5);
	test_write(&oids[11], "a0006", 5);
	test_write(&oids[11], "a0007", 5);
	test_write(&oids[11], "a0008", 5);
	test_write(&oids[11], "a0009", 5);
	test_write(&oids[11], "a0010", 5);
	test_write(&oids[11], "a0011", 5);
	test_write(&oids[11], "a0012", 5);
	test_write(&oids[11], "a0013", 5);
	test_write(&oids[11], "a0014", 5);
	test_write(&oids[11], "a0015", 5);

	test_fileinfo(&oids[11], NULL);

	for (i = 0; i < 16; ++i)
		test_read(&oids[11], i * 20, buff, 5);

	test_fileinfo(&oids[11], NULL);

	test_resize(&oids[11], 200, 10);

	test_fileinfo(&oids[11], NULL);

	for (i = 0; i < 16; ++i)
		test_read(&oids[11], i * 20, buff, 5);

	test_write(&oids[11], "a0000", 5);
	test_write(&oids[11], "a0001", 5);
	test_write(&oids[11], "a0002", 5);
	test_write(&oids[11], "a0003", 5);
	test_write(&oids[11], "a0004", 5);
	test_write(&oids[11], "a0005", 5);

	test_fileinfo(&oids[11], NULL);

	for (i = 0; i < 16; ++i)
		test_read(&oids[11], i * 10, buff, 5);

	return 0;
}
