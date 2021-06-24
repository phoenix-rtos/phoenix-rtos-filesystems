/*
 * Phoenix-RTOS
 *
 * dummyfs
 *
 * Copyright 2012, 2016, 2018 Phoenix Systems
 * Copyright 2007 Pawel Pisarczyk
 * Author: Jacek Popko, Katarzyna Baranowska, Pawel Pisarczyk, Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/stat.h> /* to set mode for / */
#include <sys/threads.h>
#include <sys/msg.h>
#include <sys/list.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <poll.h>
#include <phoenix/sysinfo.h>

#include "dummyfs.h"
#include "dir.h"
#include "file.h"
#include "object.h"
#include "dev.h"

#define LOG(msg, ...) printf("dummyfs: " msg, ##__VA_ARGS__)

struct _dummyfs_common_t dummyfs_common;

int dummyfs_destroy(oid_t *oid);


static inline int dummyfs_device(oid_t *oid)
{
	return oid->port != dummyfs_common.port;
}


static inline dummyfs_object_t *dummyfs_get(oid_t *oid)
{
	return dummyfs_device(oid) ? dev_find(oid, 0) : object_get(oid->id);
}


int dummyfs_lookup(oid_t *dir, const char *name, oid_t *res, oid_t *dev)
{
	dummyfs_object_t *o, *d;
	int len = 0;
	int err = -ENOENT;

	if (dir == NULL)
		d = object_get(0);
	else if (dummyfs_device(dir))
		return -EINVAL;
	else if ((d = object_get(dir->id)) == NULL)
		return -ENOENT;

	if (!S_ISDIR(d->mode)) {
		object_put(d);
		return -ENOTDIR;
	}

	object_lock(d);
	while (name[len] != '\0') {
		while (name[len] == '/')
			len++;

		err = dir_find(d, name + len, res);

		if (err <= 0)
			break;

		len += err;
		object_unlock(d);
		object_put(d);

		if (dummyfs_device(res))
			break;

		d = object_get(res->id);
		object_lock(d);
	}

	if (err < 0) {
		object_unlock(d);
		object_put(d);
		return err;
	}

	o = dummyfs_get(res);

	if (S_ISCHR(d->mode) || S_ISBLK(d->mode) || S_ISFIFO(d->mode))
		memcpy(dev, &o->dev, sizeof(oid_t));
	else
		memcpy(dev, res, sizeof(oid_t));

	object_put(o);
	object_unlock(d);
	object_put(d);

	return len;
}


int dummyfs_setattr(oid_t *oid, int type, int attr, const void *data, size_t size)
{
	dummyfs_object_t *o;
	int ret = EOK;

	if ((o = dummyfs_get(oid)) == NULL)
		return -ENOENT;

	object_lock(o);
	switch (type) {
		case (atUid):
			o->uid = attr;
			break;

		case (atGid):
			o->gid = attr;
			break;

		case (atMode):
			o->mode = attr;
			break;

		case (atSize):
			object_unlock(o);
			ret = dummyfs_truncate(oid, attr);
			object_lock(o);
			break;

		case (atPort):
			ret = -EINVAL;
			break;

		case (atDev):
			/* TODO: add mouting capabilities */
			ret = -EINVAL;
			break;
	}

	o->mtime = time(NULL);

	object_unlock(o);
	object_put(o);

	return ret;
}


int dummyfs_getattr(oid_t *oid, int type, int *attr)
{
	dummyfs_object_t *o;

	if ((o = dummyfs_get(oid)) == NULL)
		return -ENOENT;

	object_lock(o);
	switch (type) {

		case (atUid):
			*attr = o->uid;
			break;

		case (atGid):
			*attr = o->gid;
			break;

		case (atMode):
			*attr = o->mode;
			break;

		case (atSize):
			*attr = o->size;
			break;

		case (atType):
			if (S_ISDIR(o->mode))
				*attr = otDir;
			else if (S_ISREG(o->mode))
				*attr = otFile;
			else if (S_ISCHR(o->mode) || S_ISBLK(o->mode) || S_ISFIFO(o->mode))
				*attr = otDev;
			else if (S_ISLNK(o->mode))
				*attr = otSymlink;
			else
				*attr = otUnknown;
			break;

		case (atPort):
			*attr = o->oid.port;
			break;

		case (atCTime):
			*attr = o->ctime;
			break;

		case (atMTime):
			*attr = o->mtime;
			break;

		case (atATime):
			*attr = o->atime;
			break;

		case (atLinks):
			*attr = o->nlink;
			break;

		case (atPollStatus):
			// trivial implementation: assume read/write is always possible
			*attr = POLLIN|POLLRDNORM|POLLOUT|POLLWRNORM;
			break;
	}

	object_unlock(o);
	object_put(o);

	return EOK;
}

// allow overriding files by link() to support naive rename() implementation
#define LINK_ALLOW_OVERRIDE 1

int dummyfs_link(oid_t *dir, const char *name, oid_t *oid)
{
	dummyfs_object_t *d, *o, *victim_o = NULL;
	int ret;
	oid_t victim_oid;

	if (name == NULL)
		return -EINVAL;

	if (dummyfs_device(dir))
		return -EINVAL;

	if ((d = object_get(dir->id)) == NULL)
		return -ENOENT;

	if ((o = dummyfs_get(oid)) == NULL) {
		object_put(d);
		return -ENOENT;
	}

	if (!S_ISDIR(d->mode)) {
		object_put(o);
		object_put(d);
		return -EEXIST;
	}

	if (S_ISDIR(o->mode) && o->nlink != 0) {
		object_put(o);
		object_put(d);
		return -EINVAL;
	}

	o->nlink++;

	if (S_ISDIR(o->mode)) {
		object_lock(o);
		dir_add(o, ".", S_IFDIR | 0666, oid);
		dir_add(o, "..", S_IFDIR | 0666, dir);
		o->nlink++;
		object_unlock(o);
		object_lock(d);
		d->nlink++;
		object_unlock(d);
	}

#ifdef LINK_ALLOW_OVERRIDE
	if (dir_find(d, name, &victim_oid) > 0) {
		victim_o = object_get(victim_oid.id);
		if (S_ISDIR(victim_o->mode) // explicitly disallow overwriting directories
				|| victim_oid.id == oid->id) { // linking to self
			object_put(victim_o);
			victim_o = NULL;
		}
		else {
			// object_lock(victim_o); //FIXME: per-object locking
		}
	}
#endif

	object_lock(d);
	if (!victim_o) {
		ret = dir_add(d, name, o->mode, oid);
	}
	else {
		ret = dir_replace(d, name, oid);
		victim_o->nlink--;
		// object_unlock(victim_o); //FIXME: per-object locking
	}

	if (ret != EOK) {
		object_unlock(d);
		object_lock(o);
		o->nlink--;
		if (S_ISDIR(o->mode))
			o->nlink--;
		object_unlock(o);
	}

	d->mtime = d->atime = o->mtime = time(NULL);

	object_unlock(d);
	object_put(o);
	object_put(d);
	object_put(victim_o);

	return ret;
}


int dummyfs_unlink(oid_t *dir, const char *name)
{
	oid_t oid;
	dummyfs_object_t *o, *d;
	int ret;

	if (name == NULL)
		return -EINVAL;

	if (!strcmp(name, ".") || !strcmp(name, ".."))
		return -EINVAL;

	if (dummyfs_device(dir))
		return -EINVAL;

	d = object_get(dir->id);

	if (d == NULL)
		return -EINVAL;

	object_lock(d);

	if (dir_find(d, name, &oid) < 0) {
		object_unlock(d);
		object_put(d);
		return -ENOENT;
	}

	if (oid.id == 0) {
		object_unlock(d);
		object_put(d);
		return -EINVAL;
	}

	o = dummyfs_get(&oid);

	if (o == NULL) {
		object_unlock(d);
		object_put(d);
		return -ENOENT;
	}

	if (S_ISDIR(o->mode) && dir_empty(o) != EOK) {
		object_unlock(d);
		object_put(d);
		object_put(o);
		return -EINVAL;
	}

	ret = dir_remove(d, name);

	if (ret == EOK && S_ISDIR(o->mode))
		d->nlink--;

	d->mtime = d->atime = o->mtime = time(NULL);

	object_unlock(d);
	object_put(d);

	if (ret == EOK) {
		object_lock(o);
		o->nlink--;
		if (S_ISDIR(o->mode))
			o->nlink--;
		object_unlock(o);
	}
	object_put(o);

	return ret;
}


int dummyfs_create(oid_t *dir, const char *name, oid_t *oid, uint32_t mode, oid_t *dev)
{
	dummyfs_object_t *o;
	int ret;

	if (S_ISCHR(mode) || S_ISBLK(mode) || S_ISFIFO(mode))
		o = dev_find(dev, 1);
	else
		o = object_create();

	if (o == NULL)
		return -ENOMEM;

	object_lock(o);
	o->oid.port = dummyfs_common.port;
	o->mode = mode;
	o->atime = o->mtime = o->ctime = time(NULL);

	if (S_ISCHR(mode) || S_ISBLK(mode) || S_ISFIFO(mode))
		memcpy(oid, dev, sizeof(oid_t));
	else
		memcpy(oid, &o->oid, sizeof(oid_t));

	object_unlock(o);

	if ((ret = dummyfs_link(dir, name, &o->oid)) != EOK) {
		object_put(o);
		return ret;
	}

	if (S_ISLNK(mode)) {
		const char* path = name + strlen(name) + 1;
		object_lock(o);
		/* TODO: remove symlink if write failed */
		dummyfs_write_internal(o, 0, path, strlen(path));
		object_unlock(o);
	}

	object_put(o);
	return EOK;
}


int dummyfs_destroy(oid_t *oid)
{
	dummyfs_object_t *o;
	int ret = EOK;

	o = object_get_unlocked(oid->id);

	if (o == NULL)
		return -ENOENT;

	if ((ret = object_remove(o)) == EOK) {
		if (S_ISREG(o->mode)) {
			object_lock(o);
			dummyfs_truncate_internal(o, 0);
			object_unlock(o);
		}
		else if (S_ISDIR(o->mode))
			dir_destroy(o);
		else if (S_ISCHR(o->mode) || S_ISBLK(o->mode) || S_ISFIFO(o->mode))
			dev_destroy(&o->dev);

		else if (o->mode == 0xaBadBabe) {
#ifndef NOMMU
			munmap((void *)((uintptr_t)o->chunks->data & ~0xfff), (o->size + 0xfff) & ~0xfff);
#endif
			free(o->chunks);
		}
		dummyfs_decsz(sizeof(dummyfs_object_t));
		free(o);
	}

	return ret;
}


int dummyfs_readdir(oid_t *dir, offs_t offs, struct dirent *dent, unsigned int size)
{
	dummyfs_object_t *d;
	dummyfs_dirent_t *ei;
	offs_t diroffs = 0;
	int ret = -ENOENT;

	if (dummyfs_device(dir))
		return -EINVAL;

	d = object_get(dir->id);

	if (d == NULL)
		return -ENOENT;

	if (!S_ISDIR(d->mode)) {
		object_put(d);
		return -EINVAL;
	}

	object_lock(d);

	if ((ei = d->entries) == NULL) {
		object_unlock(d);
		object_put(d);
		return -EINVAL;
	}
	dent->d_reclen = 0;
	do {
		if (diroffs >= offs) {
			if ((sizeof(struct dirent) + ei->len + 1) > size) {
				object_unlock(d);
				object_put(d);
				return 	-EINVAL;
			}
			if (ei->deleted) {
				ei = ei->next;
				dent->d_reclen++;
				continue;
			}

			dent->d_ino = ei->oid.id;
			dent->d_reclen++;
			dent->d_namlen = ei->len;
			dent->d_type = ei->type;
			strcpy(dent->d_name, ei->name);

			object_unlock(d);
			object_put(d);
			return 	EOK;
		}
		diroffs++;
		ei = ei->next;
	} while (ei != d->entries);

	d->atime = time(NULL);

	object_unlock(d);
	object_put(d);

	return ret;
}


static int dummyfs_open(oid_t *oid)
{
	dummyfs_object_t *o;

	if ((o = dummyfs_get(oid)) == NULL)
		return -ENOENT;

	object_lock(o);
	o->atime = time(NULL);

	object_unlock(o);
	return EOK;
}


static int dummyfs_close(oid_t *oid)
{
	dummyfs_object_t *o;

	if ((o = dummyfs_get(oid)) == NULL)
		return -ENOENT;

	object_lock(o);
	o->atime = time(NULL);

	object_unlock(o);
	object_put(o);
	object_put(o);
	return EOK;
}


int fetch_modules(void)
{
	oid_t root = {dummyfs_common.port, 0};
	oid_t toid = { 0 };
	oid_t sysoid = { 0 };
	dummyfs_object_t *o;
	void *prog_addr;
	syspageprog_t prog;
	int i, progsz;

	progsz = syspageprog(NULL, -1);
	dummyfs_create(&root, "syspage", &sysoid, S_IFDIR | 0666, NULL);

	for (i = 0; i < progsz; i++) {
		syspageprog(&prog, i);
#ifdef NOMMU
		prog_addr = (void *)prog.addr;
#else
		prog_addr = (void *)mmap(NULL, (prog.size + 0xfff) & ~0xfff, 0x1 | 0x2, 0, OID_PHYSMEM, prog.addr);

		if (!prog_addr)
			continue;
#endif
		dummyfs_create(&sysoid, prog.name, &toid, S_IFREG | 0755, NULL);
		o = object_get(toid.id);

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


int dummyfs_do_mount(const char *path, oid_t *oid)
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

	if ((err = msgSend(toid.port, &msg)) < 0)
		return err;

	return 0;
}


static int dummyfs_mount_sync(const char* mountpt)
{
	oid_t toid;
	int err;
	toid.port = dummyfs_common.port;
	while (lookup("/", NULL, &toid) < 0 || toid.port == dummyfs_common.port)
		usleep(100000);

	toid.id = 0;
	toid.port = dummyfs_common.port;
	if ((err = dummyfs_do_mount(mountpt, &toid))) {
		LOG("failed to mount at %s - error %d\n", mountpt, err);
		return -1;
	}

	return 0;
}

char __attribute__((aligned(8))) mtstack[4096];

void dummyfs_mount_async(void *arg)
{
	char *mountpt = (char *)arg;

	dummyfs_mount_sync(mountpt);
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
	oid_t root = { 0 };
	msg_t msg;
	dummyfs_object_t *o;
	unsigned long rid;
	const char *mountpt = NULL;
	const char *remount_path = NULL;
	int non_fs_namespace = 0;
	int daemonize = 0;
	uint32_t mode;
	int c;

#ifdef TARGET_IMX6ULL
	uint32_t reserved;
#endif

	dummyfs_common.size = 0;

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

		portCreate(&dummyfs_common.port);

		/* Try to mount fs as root */
		if (portRegister(dummyfs_common.port, "/", &root) < 0) {
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
			portCreate(&dummyfs_common.port);
			if (portRegister(dummyfs_common.port, mountpt, &root) < 0) {
				LOG("can't mount as %s\n", mountpt);
				return -1;
			}
			mountpt = NULL;
		}
		else {
			portCreate(&dummyfs_common.port);
		}

	}

	if (mutexCreate(&dummyfs_common.mutex) != EOK) {
		LOG("could not create mutex\n");
		return 2;
	}

	object_init();
	dev_init();

	/* Create root directory */
	o = object_create();

	if (o == NULL)
		return -1;

	o->oid.port = dummyfs_common.port;
	o->mode = S_IFDIR | 0666;

	memcpy(&root, &o->oid, sizeof(oid_t));
	dir_add(o, ".", S_IFDIR | 0666, &root);
	dir_add(o, "..", S_IFDIR | 0666, &root);

	if (!non_fs_namespace && mountpt == NULL) {
		fetch_modules();
		mountpt = remount_path;
	}

	if (daemonize) {
		/* mount synchronously */
		if (!non_fs_namespace && dummyfs_mount_sync(mountpt)) {
			LOG("failed to mount, exiting\n");
			return 1;
		}

		/* init completed - wake parent */
		kill(getppid(), SIGUSR1);
	}
	else if (mountpt != NULL) {
		beginthread(dummyfs_mount_async, 4, &mtstack, sizeof(mtstack), (void*)mountpt);
	}

	/*** MAIN LOOP ***/

	LOG("initialized\n");

	for (;;) {
		if (msgRecv(dummyfs_common.port, &msg, &rid) < 0)
			continue;

		switch (msg.type) {

			case mtOpen:
				msg.o.io.err = dummyfs_open(&msg.i.openclose.oid);
				break;

			case mtClose:
				msg.o.io.err = dummyfs_close(&msg.i.openclose.oid);
				break;

			case mtRead:
				msg.o.io.err = dummyfs_read(&msg.i.io.oid, msg.i.io.offs, msg.o.data, msg.o.size);
				break;

			case mtWrite:
				msg.o.io.err = dummyfs_write(&msg.i.io.oid, msg.i.io.offs, msg.i.data, msg.i.size);
				break;

			case mtTruncate:
				msg.o.io.err = dummyfs_truncate(&msg.i.io.oid, msg.i.io.len);
				break;

			case mtDevCtl:
				msg.o.io.err = -EINVAL;
				break;

			case mtCreate:
				mode = msg.i.create.mode;
				switch (msg.i.create.type) {
				case otDir:
					mode |= S_IFDIR;
					break;

				case otFile:
					mode |= S_IFREG;
					break;

				case otDev:
					if (!(S_ISCHR(mode) || S_ISBLK(mode) || S_ISFIFO(mode))) {
						mode &= 0x1ff;
						mode |= S_IFCHR;
					}
					break;

				case otSymlink:
					mode |= S_IFLNK;
					break;
				}
				msg.o.create.err = dummyfs_create(&msg.i.create.dir, msg.i.data, &msg.o.create.oid, mode, &msg.i.create.dev);
				break;

			case mtDestroy:
				msg.o.io.err = dummyfs_destroy(&msg.i.destroy.oid);
				break;

			case mtSetAttr:
				msg.o.attr.err = dummyfs_setattr(&msg.i.attr.oid, msg.i.attr.type, msg.i.attr.val, msg.i.data, msg.i.size);
				break;

			case mtGetAttr:
				msg.o.attr.err = dummyfs_getattr(&msg.i.attr.oid, msg.i.attr.type, &msg.o.attr.val);
				break;

			case mtLookup:
				msg.o.lookup.err = dummyfs_lookup(&msg.i.lookup.dir, msg.i.data, &msg.o.lookup.fil, &msg.o.lookup.dev);
				break;

			case mtLink:
				msg.o.io.err = dummyfs_link(&msg.i.ln.dir, msg.i.data, &msg.i.ln.oid);
				break;

			case mtUnlink:
				msg.o.io.err = dummyfs_unlink(&msg.i.ln.dir, msg.i.data);
				break;

			case mtReaddir:
				msg.o.io.err = dummyfs_readdir(&msg.i.readdir.dir, msg.i.readdir.offs,
						msg.o.data, msg.o.size);
				break;
		}
		msgRespond(dummyfs_common.port, &msg, rid);
	}

	return EOK;
}
