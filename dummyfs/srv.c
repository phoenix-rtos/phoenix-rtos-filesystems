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
#include "dir.h"

#define LOG(msg, ...) printf("dummyfs: " msg, ##__VA_ARGS__)

int fetch_modules(dummyfs_t *ctx)
{
	oid_t root = { ctx->port, 0 };
	oid_t toid = { 0 };
	oid_t sysoid = { 0 };
	syspageprog_t prog;
	int i, progsz;

	if ((progsz = syspageprog(NULL, -1)) < 0)
		return -1;

	if (dummyfs_create(ctx, &root, "syspage", &sysoid, 0666, otDir, NULL) != EOK)
		return -1;

	for (i = 0; i < progsz; i++) {
		if (syspageprog(&prog, i) != 0)
			continue;

		dummyfs_createMapped(ctx, &sysoid, prog.name, (void *)prog.addr, prog.size, &toid);
	}

	return EOK;
}


int dummyfs_do_mount(dummyfs_t *ctx, const char *path, oid_t *oid)
{
	struct stat buf;
	oid_t toid;
	msg_t msg = { 0 };
	int err;

	if (lookup(path, &toid, NULL) < EOK) {
		return -ENOENT;
	}

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


static int dummyfs_mount_sync(dummyfs_t *ctx, const char *mountpt)
{
	char *abspath = NULL;
	oid_t toid;
	int err;

	abspath = resolve_path(mountpt, NULL, 1, 0);
	if (abspath == NULL) {
		return -1;
	}

	toid.port = ctx->port;
	while (lookup("/", NULL, &toid) < 0 || toid.port == ctx->port)
		usleep(100000);

	toid.id = 0;
	toid.port = ctx->port;
	if ((err = dummyfs_do_mount(ctx, abspath, &toid))) {
		LOG("failed to mount at %s - error %d\n", abspath, err);
		free(abspath);
		return -1;
	}

	free(abspath);
	return 0;
}

static char __attribute__((aligned(8))) mtstack[4096];

void dummyfs_mount_async(void *arg)
{
	dummyfs_t *ctx = (dummyfs_t *)arg;

	dummyfs_mount_sync(ctx, ctx->mountpt);
	endthread();
}


static void print_usage(const char *progname)
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
	dummyfs_t *ctx;
	oid_t root = { 0 };
	unsigned port;
	msg_t msg;
	msg_rid_t rid;
	const char *mountpt = NULL;
	const char *remount_path = NULL;
	int non_fs_namespace = 0;
	int daemonize = 0;
	int c;


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
			/* PARENT: wait for initialization to finish and then exit */
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

		while (write(1, "", 0) < 0) {
			usleep(500000);
		}

		portCreate(&port);

		/* Try to mount fs as root */
		if (portRegister(port, "/", &root) < 0) {
			LOG("can't mount as rootfs\n");
			return -1;
		}

	}
	else {
		if (non_fs_namespace) {
			while (write(1, "", 0) < 0)
				usleep(1000);
			portCreate(&port);
			if (portRegister(port, mountpt, &root) < 0) {
				LOG("can't mount as %s\n", mountpt);
				return -1;
			}
			mountpt = NULL;
		}
		else {
			portCreate(&port);
		}
	}

	root.port = port;
	if (dummyfs_mount((void **)&ctx, mountpt, 0, &root) != EOK) {
		printf("dummyfs mount failed\n");
		return 1;
	}

	if (!non_fs_namespace && mountpt == NULL) {
		if (fetch_modules(ctx) != EOK) {
			printf("dummyfs: fetch_modules failed\n");
			return 1;
		}
		mountpt = remount_path;
	}

	if (daemonize) {
		/* mount synchronously */
		if (!non_fs_namespace && dummyfs_mount_sync(ctx, mountpt)) {
			LOG("failed to mount, exiting\n");
			return 1;
		}

		/* init completed - wake parent */
		kill(getppid(), SIGUSR1);
	}
	else if (mountpt != NULL) {
		ctx->mountpt = strdup(mountpt);
		if (ctx->mountpt == NULL)
			return 1;
		beginthread(dummyfs_mount_async, 4, &mtstack, sizeof(mtstack), (void *)ctx);
	}

	/*** MAIN LOOP ***/

	LOG("initialized\n");

	for (;;) {
		if (msgRecv(ctx->port, &msg, &rid) < 0)
			continue;

		switch (msg.type) {

			case mtOpen:
				msg.o.io.err = dummyfs_open(ctx, &msg.i.openclose.oid);
				break;

			case mtClose:
				msg.o.io.err = dummyfs_close(ctx, &msg.i.openclose.oid);
				break;

			case mtRead:
				msg.o.io.err = dummyfs_read(ctx, &msg.i.io.oid, msg.i.io.offs, msg.o.data, msg.o.size);
				break;

			case mtWrite:
				msg.o.io.err = dummyfs_write(ctx, &msg.i.io.oid, msg.i.io.offs, msg.i.data, msg.i.size);
				break;

			case mtTruncate:
				msg.o.io.err = dummyfs_truncate(ctx, &msg.i.io.oid, msg.i.io.len);
				break;

			case mtDevCtl:
				msg.o.io.err = -EINVAL;
				break;

			case mtCreate:
				msg.o.create.err = dummyfs_create(ctx, &msg.i.create.dir, msg.i.data, &msg.o.create.oid, msg.i.create.mode, msg.i.create.type, &msg.i.create.dev);
				break;

			case mtDestroy:
				msg.o.io.err = dummyfs_destroy(ctx, &msg.i.destroy.oid);
				break;

			case mtSetAttr:
				msg.o.attr.err = dummyfs_setattr(ctx, &msg.i.attr.oid, msg.i.attr.type, msg.i.attr.val, msg.i.data, msg.i.size);
				break;

			case mtGetAttr:
				msg.o.attr.err = dummyfs_getattr(ctx, &msg.i.attr.oid, msg.i.attr.type, &msg.o.attr.val);
				break;

			case mtLookup:
				msg.o.lookup.err = dummyfs_lookup(ctx, &msg.i.lookup.dir, msg.i.data, &msg.o.lookup.fil, &msg.o.lookup.dev);
				break;

			case mtLink:
				msg.o.io.err = dummyfs_link(ctx, &msg.i.ln.dir, msg.i.data, &msg.i.ln.oid);
				break;

			case mtUnlink:
				msg.o.io.err = dummyfs_unlink(ctx, &msg.i.ln.dir, msg.i.data);
				break;

			case mtReaddir:
				msg.o.io.err = dummyfs_readdir(ctx, &msg.i.readdir.dir, msg.i.readdir.offs,
					msg.o.data, msg.o.size);
				break;

			case mtStat:
				msg.o.io.err = dummyfs_statfs(ctx, msg.o.data, msg.o.size);
				break;
		}
		msgRespond(ctx->port, &msg, rid);
	}

	return EOK;
}
