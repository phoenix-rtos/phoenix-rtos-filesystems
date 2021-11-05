/*
 * Phoenix-RTOS
 *
 * dummyfs server
 *
 * Copyright 2012, 2016, 2018, 2021 Phoenix Systems
 * Copyright 2007 Pawel Pisarczyk
 * Author: Jacek Popko, Katarzyna Baranowska, Pawel Pisarczyk, Kamil Amanowicz, Maciej Purski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/threads.h>
#include <unistd.h>

#include <phoenix/sysinfo.h>

#include "dummyfs_internal.h"
#include "dummyfs.h"
#include "object.h"
#include "dev.h"
#include "dir.h"

#define LOG(msg, ...) printf("dummyfs: " msg, ##__VA_ARGS__)

int fetch_modules(dummyfs_t *ctx)
{
	oid_t root = {ctx->port, 0};
	oid_t toid = { 0 };
	oid_t sysoid = { 0 };
	dummyfs_object_t *o;
	void *prog_addr;
	syspageprog_t prog;
	int i, progsz;

	progsz = syspageprog(NULL, -1);
	dummyfs_create(ctx, &root, "syspage", &sysoid, 0666, otDir, NULL);

	for (i = 0; i < progsz; i++) {
		syspageprog(&prog, i);
#ifdef NOMMU
		prog_addr = (void *)prog.addr;
#else
		prog_addr = (void *)mmap(NULL, (prog.size + 0xfff) & ~0xfff, 0x1 | 0x2, 0, OID_PHYSMEM, prog.addr);

		if (!prog_addr)
			continue;
#endif
		dummyfs_create(ctx, &sysoid, prog.name, &toid, 0755, otFile, NULL);
		o = object_get(ctx, toid.id);

		if (!o) {
#ifndef NOMMU
			munmap(prog_addr, (prog.size + 0xfff) & ~0xfff);
#endif
			continue;
		}

		o->chunks = malloc(sizeof(dummyfs_chunk_t));
		o->chunks->offs = 0;
		o->chunks->size = prog.size;
		o->chunks->used = prog.size;
		o->chunks->data = (void *)((uintptr_t)prog_addr & ~0xfff) + (prog.addr & 0xfff);
		o->chunks->next = o->chunks;
		o->chunks->prev = o->chunks;
		o->size = prog.size;
		o->mode = 0xaBadBabe;
	}

	return EOK;
}


int dummyfs_do_mount(dummyfs_t *ctx, const char *path, oid_t *oid)
{
	struct stat buf;
	oid_t toid;
	msg_t msg = {0};
	int err;

	if (lookup(path, NULL, &toid) < EOK)
		return -ENOENT;

	if ((err = stat(path, &buf)))
		return err;

	if (!S_ISDIR(buf.st_mode))
		return -ENOTDIR;

	msg.type = mtSetAttr;
	msg.i.attr.oid = toid;
	msg.i.attr.type = atDev;
	msg.i.data = oid;
	msg.i.size = sizeof(oid_t);

	if (((err = msgSend(toid.port, &msg)) < 0) || ((err = msg.o.attr.err) < 0))
		return err;

	return 0;
}


static int dummyfs_mount_sync(dummyfs_t *ctx, const char* mountpt)
{
	oid_t toid;
	int err;
	toid.port = ctx->port;
	while (lookup("/", NULL, &toid) < 0 || toid.port == ctx->port)
		usleep(100000);

	toid.id = 0;
	toid.port = ctx->port;
	if ((err = dummyfs_do_mount(ctx, mountpt, &toid))) {
		LOG("failed to mount at %s - error %d\n", mountpt, err);
		return -1;
	}

	return 0;
}

char __attribute__((aligned(8))) mtstack[4096];

void dummyfs_mount_async(void *arg)
{
	dummyfs_t *ctx = (dummyfs_t *)arg;

	dummyfs_mount_sync(ctx, ctx->mountpt);
	endthread();
}


static void print_usage(const char* progname)
{
	printf("usage: %s [OPTIONS]\n\n"
		"  -m [mountpoint]    Start dummyfs at a given mountopint (the mount will happen asynchronously)\n"
		"  -r [mountpoint]    Remount to a given path after spawning modules\n"
		"  -D                 Daemonize after mounting\n"
		"  -h                 This help message\n",
		progname);
}


static void signal_exit(int sig)
{
	exit(EXIT_SUCCESS);
}


int main(int argc, char **argv)
{
	dummyfs_t ctx = { 0 };
	oid_t root = { 0 };
	msg_t msg;
	dummyfs_object_t *o;
	unsigned long rid;
	const char *mountpt = NULL;
	const char *remount_path = NULL;
	int non_fs_namespace = 0;
	int daemonize = 0;
	int c;

#ifdef TARGET_IMX6ULL
	uint32_t reserved;
#endif

	ctx.size = 0;

	while ((c = getopt(argc, argv, "Dhm:r:N:")) != -1) {
		switch (c) {
			case 'm':
				mountpt = optarg;
				break;
			case 'r':
				remount_path = optarg;
				break;
			case 'h':
				print_usage(argv[0]);
				return 0;
			case 'D':
				daemonize = 1;
				break;
			case 'N':
				non_fs_namespace = 1;
				mountpt = optarg;
				break;
			default:
				print_usage(argv[0]);
				return 1;
		}
	}

	if (daemonize && !mountpt) {
		LOG("can't daemonize without mountpoint, exiting!\n");
		return 1;
	}


	/* Daemonizing first to make all initialization in child process.
	 * Otherwise the port will be destroyed when parent exits. */
	if (daemonize) {
		pid_t pid, sid;

		/* set exit handler */
		signal(SIGUSR1, signal_exit);
		/* Fork off the parent process */
		pid = fork();
		if (pid < 0) {
			LOG("fork failed: [%d] -> %s\n", errno, strerror(errno));
			exit(EXIT_FAILURE);
		}

		if (pid > 0) {
			/* PARENT: wait for initailization to finish and then exit */
			sleep(10);

			LOG("failed to communicate with child\n");
			exit(EXIT_FAILURE);
		}
		/* set default handler back again */
		signal(SIGUSR1, signal_exit);

		/* Create a new SID for the child process */
		sid = setsid();
		if (sid < 0) {
			LOG("setsid failed: [%d] -> %s\n", errno, strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

	if (mountpt == NULL) {

#ifndef TARGET_IMX6ULL
		while (write(1, "", 0) < 0) {
			usleep(500000);
		}
#else
		portCreate(&reserved);
#endif

		portCreate(&ctx.port);

		/* Try to mount fs as root */
		if (portRegister(ctx.port, "/", &root) < 0) {
			LOG("can't mount as rootfs\n");
			return -1;
		}

#ifdef TARGET_IMX6ULL
		portDestroy(reserved);
#endif
	}
	else {

		if (non_fs_namespace) {
			while (write(1, "", 0) < 0)
				usleep(1000);
			portCreate(&ctx.port);
			if (portRegister(ctx.port, mountpt, &root) < 0) {
				LOG("can't mount as %s\n", mountpt);
				return -1;
			}
			mountpt = NULL;
		}
		else {
			portCreate(&ctx.port);
		}

	}

	if (mutexCreate(&ctx.mutex) != EOK) {
		LOG("could not create mutex\n");
		return 2;
	}

	object_init(&ctx);
	dev_init(&ctx);

	/* Create root directory */
	o = object_create(&ctx);

	if (o == NULL)
		return -1;

	o->oid.port = ctx.port;
	o->mode = S_IFDIR | 0666;

	memcpy(&root, &o->oid, sizeof(oid_t));
	dir_add(&ctx, o, ".", S_IFDIR | 0666, &root);
	dir_add(&ctx, o, "..", S_IFDIR | 0666, &root);

	if (!non_fs_namespace && mountpt == NULL) {
		fetch_modules(&ctx);
		mountpt = remount_path;
	}

	if (daemonize) {
		/* mount synchronously */
		if (!non_fs_namespace && dummyfs_mount_sync(&ctx, mountpt)) {
			LOG("failed to mount, exiting\n");
			return 1;
		}

		/* init completed - wake parent */
		kill(getppid(), SIGUSR1);
	}
	else if (mountpt != NULL) {
		ctx.mountpt = strdup(mountpt);
		if (ctx.mountpt == NULL)
			return 1;
		beginthread(dummyfs_mount_async, 4, &mtstack, sizeof(mtstack), (void *)&ctx);
	}

	/*** MAIN LOOP ***/

	LOG("initialized\n");

	for (;;) {
		if (msgRecv(ctx.port, &msg, &rid) < 0)
			continue;

		switch (msg.type) {

			case mtOpen:
				msg.o.io.err = dummyfs_open(&ctx, &msg.i.openclose.oid);
				break;

			case mtClose:
				msg.o.io.err = dummyfs_close(&ctx, &msg.i.openclose.oid);
				break;

			case mtRead:
				msg.o.io.err = dummyfs_read(&ctx, &msg.i.io.oid, msg.i.io.offs, msg.o.data, msg.o.size);
				break;

			case mtWrite:
				msg.o.io.err = dummyfs_write(&ctx, &msg.i.io.oid, msg.i.io.offs, msg.i.data, msg.i.size);
				break;

			case mtTruncate:
				msg.o.io.err = dummyfs_truncate(&ctx, &msg.i.io.oid, msg.i.io.len);
				break;

			case mtDevCtl:
				msg.o.io.err = -EINVAL;
				break;

			case mtCreate:
				msg.o.create.err = dummyfs_create(&ctx, &msg.i.create.dir, msg.i.data, &msg.o.create.oid, msg.i.create.mode, msg.i.create.type, &msg.i.create.dev);
				break;

			case mtDestroy:
				msg.o.io.err = dummyfs_destroy(&ctx, &msg.i.destroy.oid);
				break;

			case mtSetAttr:
				msg.o.attr.err = dummyfs_setattr(&ctx, &msg.i.attr.oid, msg.i.attr.type, msg.i.attr.val, msg.i.data, msg.i.size);
				break;

			case mtGetAttr:
				msg.o.attr.err = dummyfs_getattr(&ctx, &msg.i.attr.oid, msg.i.attr.type, &msg.o.attr.val);
				break;

			case mtLookup:
				msg.o.lookup.err = dummyfs_lookup(&ctx, &msg.i.lookup.dir, msg.i.data, &msg.o.lookup.fil, &msg.o.lookup.dev);
				break;

			case mtLink:
				msg.o.io.err = dummyfs_link(&ctx, &msg.i.ln.dir, msg.i.data, &msg.i.ln.oid);
				break;

			case mtUnlink:
				msg.o.io.err = dummyfs_unlink(&ctx, &msg.i.ln.dir, msg.i.data);
				break;

			case mtReaddir:
				msg.o.io.err = dummyfs_readdir(&ctx, &msg.i.readdir.dir, msg.i.readdir.offs,
						msg.o.data, msg.o.size);
				break;
		}
		msgRespond(ctx.port, &msg, rid);
	}

	return EOK;
}
