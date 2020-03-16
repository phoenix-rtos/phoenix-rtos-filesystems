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
#include <fcntl.h>

#include "dummyfs.h"
#include "dir.h"
#include "file.h"
#include "object.h"

//#define LOG(msg, ...)

#define LOG(msg, ...) do { \
	char buf[128]; \
	sprintf(buf, __FILE__ ":%d - " msg "\n", __LINE__, ##__VA_ARGS__ ); \
	debug(buf); \
} while (0)


struct dummyfs_common dummyfs_common;


int dummyfs_destroy(id_t *id);


int dummyfs_lookup(id_t *id, const char *name, const size_t len, id_t *resId, mode_t *mode)
{
	dummyfs_object_t *d;
	int err = -ENOENT;

	if ((d = object_get(id)) == NULL)
		return -ENOENT;

	if (!S_ISDIR(d->mode)) {
		object_put(d);
		return -EINVAL;
	}

	object_lock(d);
	err = dir_findId(d, name, len, resId, mode);

	object_unlock(d);
	object_put(d);

	return err;
}


int dummyfs_setattr(id_t *id, int type, const void *data, size_t size)
{
	dummyfs_object_t *o;
	int ret = EOK;

	if ((o = object_get(id)) == NULL)
		return -ENOENT;

	object_lock(o);
	switch (type) {
		case (atUid):
			o->uid = *(int *)data;
			break;

		case (atGid):
			o->gid = *(int *)data;
			break;

		case (atMode):
			o->mode = *(int *)data;
			break;

		case (atSize):
			object_unlock(o);
			ret = dummyfs_truncate(id, *(int *)data);
			object_lock(o);
			break;

		case (atPort):
			ret = -EINVAL;
			break;

		case (atDev):
			ret = -EINVAL;
			break;
		case atMount:
			if (OBJ_IS_MOUNT(o) || OBJ_IS_MNTPOINT(o)) {
				ret = -EBUSY;
				break;
			}
			OBJ_SET_MOUNT(o);
			memcpy(&o->mnt, data, sizeof(oid_t));
			break;
		case atMountPoint:
			if (OBJ_IS_MOUNT(o) || OBJ_IS_MNTPOINT(o)) {
				ret = -EBUSY;
				break;
			}
			OBJ_SET_MNTPOINT(o);
			memcpy(&o->mnt, data, sizeof(oid_t));
			break;
	}

	if (!ret)
		o->mtime = time(NULL);

	object_unlock(o);
	object_put(o);

	return ret;
}


ssize_t dummyfs_getattr(id_t *id, int type, void *attr, size_t maxlen)
{
	dummyfs_object_t *o;
	struct stat *stat;
	ssize_t retval = 0;

	if ((o = object_get(id)) == NULL)
		return -ENOENT;

	object_lock(o);
	switch (type) {

		case atSize:
			*(int *)attr = o->size;
			retval = sizeof(int);
			break;

		case atStatStruct:
			stat = (struct stat *)attr;
			//stat->st_dev = o->port;
			stat->st_ino = o->id;
			stat->st_mode = o->mode;
			stat->st_nlink = o->nlink;
			stat->st_uid = o->uid;
			stat->st_gid = o->gid;
			stat->st_size = o->size;
			stat->st_atime = o->atime;
			stat->st_mtime = o->mtime;
			stat->st_ctime = o->ctime;
			if (S_ISCHR(o->mode) || S_ISBLK(o->mode))
				stat->st_rdev = o->dev.port;
			retval = sizeof(struct stat);
			break;
		case atEvents:
			*(int *)attr = POLLIN | POLLOUT;
			retval = sizeof(int);
			break;
		case atMount:
		case atMountPoint:
			*(oid_t *)attr = o->mnt;
			break;
	}

	object_unlock(o);
	object_put(o);

	return retval;
}

// allow overriding files by link() to support naive rename() implementation
#define LINK_ALLOW_OVERRIDE 1

int dummyfs_link(id_t *dirId, const char *name, const size_t len, id_t *id)
{
	dummyfs_object_t *d, *o, *victim_o = NULL;
	int ret;
	id_t victim_id;

	if (name == NULL)
		return -EINVAL;

	if ((d = object_get(dirId)) == NULL)
		return -ENOENT;

	if ((o = object_get(id)) == NULL) {
		object_put(o);
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
		dir_add(o, ".", 1, o);
		dir_add(o, "..", 2, d);
		o->nlink++;
		object_unlock(o);
		object_lock(d);
		d->nlink++;
		object_unlock(d);
	}

#ifdef LINK_ALLOW_OVERRIDE
	if (dir_findId(d, name, len, &victim_id, NULL) > 0) {
		victim_o = object_get(&victim_id);
		if (S_ISDIR(victim_o->mode) // explicitly disallow overwriting directories
				|| victim_id == *id) { // linking to self
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
		ret = dir_add(d, name, len, o);
	}
	else {
		ret = dir_replace(d, name, len, o);
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


int dummyfs_unlink(id_t *id, const char *name, const size_t len)
{
	id_t resId;
	dummyfs_object_t *o, *d;
	int ret;

	if (name == NULL)
		return -EINVAL;

	if (!strcmp(name, ".") || !strcmp(name, ".."))
		return -EINVAL;

	d = object_get(id);

	if (d == NULL)
		return -EINVAL;

	object_lock(d);

	if (dir_findId(d, name, len, &resId, NULL) < 0) {
		object_unlock(d);
		object_put(d);
		return -ENOENT;
	}

	if (resId == 0) {
		object_unlock(d);
		object_put(d);
		return -EINVAL;
	}

	o = object_get(&resId);

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

	ret = dir_remove(d, name, len);

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


int dummyfs_create(id_t *dirId, const char *name, const size_t len, id_t *resId, int mode, oid_t *dev)
{
	dummyfs_object_t *o;
	int ret;

	o = object_create();

	if (o == NULL)
		return -ENOMEM;

	object_lock(o);
	o->mode = mode;
	o->atime = o->mtime = o->ctime = time(NULL);

	if ((S_ISCHR(o->mode) || S_ISBLK(o->mode)) && dev != NULL)
		memcpy(&o->dev, dev, sizeof(oid_t));

	*resId = o->id;

	object_unlock(o);

	if ((ret = dummyfs_link(dirId, name, len, &o->id)) != EOK) {
		object_put(o);
		return ret;
	}

	if (S_ISLNK(mode)) {
		const char* path = name + strlen(name) + 1;
		object_lock(o);
		// TODO: remove symlink if write failed
		dummyfs_write_internal(o, 0, path, strlen(path) + 1, &ret);
		object_unlock(o);
	}

	object_put(o);
	return ret;
}


int dummyfs_destroy(id_t *id)
{
	dummyfs_object_t *o;
	int ret = EOK;

	o = object_get_unlocked((int)*id);

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

		dummyfs_decsz(sizeof(dummyfs_object_t));
		free(o);
	}

	return ret;
}


int dummyfs_readdir(id_t *id, offs_t offs, struct dirent *dent, unsigned int size)
{
	dummyfs_object_t *d;
	dummyfs_dirent_t *ei;
	offs_t diroffs = 0;
	int ret = -ENOENT;
	d = object_get(id);

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
			if ((sizeof(struct dirent) + ei->len) > size) {
				object_unlock(d);
				object_put(d);
				return 	-EINVAL;
			}
			if (ei->deleted) {
				ei = ei->next;
				dent->d_reclen++;
				continue;
			}

			dent->d_ino = ei->o->id;
			dent->d_reclen++;
			dent->d_namlen = ei->len;
			dent->d_type = (S_IFMT & ei->o->mode);
			memcpy(&(dent->d_name[0]), ei->name, ei->len);

			if (OBJ_IS_MNTPOINT(d) && !strcmp(ei->name, ".."))
				dent->d_ino = d->mnt.id;

			object_unlock(d);
			object_put(d);
			return dent->d_reclen;
		}
		diroffs++;
		ei = ei->next;
	} while (ei != d->entries);

	d->atime = time(NULL);

	object_unlock(d);
	object_put(d);

	return ret;
}


static int dummyfs_open(id_t *id)
{
	dummyfs_object_t *o;

	if ((o = object_get(id)) == NULL)
		return -ENOENT;

	object_lock(o);
	o->atime = time(NULL);

	object_unlock(o);
	return EOK;
}


static int dummyfs_close(id_t *id)
{
	dummyfs_object_t *o;

	if ((o = object_get(id)) == NULL)
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
	id_t progId = 0;
	id_t sysId = { 0 };
	void *prog_addr;
	syspageprog_t prog;
	int i, progsz, ret;

	progsz = syspageprog(NULL, -1);
	dummyfs_create(&dummyfs_common.rootId, "syspage", strlen("syspage"), &sysId, S_IFDIR | DEFFILEMODE, NULL);

	for (i = 0; i < progsz; i++) {

		syspageprog(&prog, i);
#ifdef NOMMU
		prog_addr = (void *)prog.addr;
#else
		prog_addr = (void *)mmap(NULL, (prog.size + 0xfff) & ~0xfff, PROT_READ | PROT_WRITE, MAP_NONE, FD_PHYSMEM, prog.addr);
#endif

		dummyfs_create(&sysId, prog.name, strlen(prog.name), &progId, S_IFREG | DEFFILEMODE, NULL);
		dummyfs_write(&progId, 0, prog_addr, prog.size, &ret);

		munmap(prog_addr, (prog.size + 0xfff) & ~0xfff);
	}
	return EOK;
}


int dummyfs_do_mount(const char *path, oid_t *oid)
{
/*	TBD
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
*/
	return 0;
}


static int dummyfs_mount_sync(const char* mountpt)
{
	/*	TBD
	oid_t toid;
	int err;
	toid.port = dummyfs_common.portfd;
	while (lookup("/", NULL, &toid) < 0 || toid.port == dummyfs_common.port)
		usleep(100000);

	toid.id = 0;
	toid.port = dummyfs_common.port;
	if ((err = dummyfs_do_mount(mountpt, &toid))) {
		LOG("failed to mount at %s - error %d\n", mountpt, err);
		return -1;
	}
	*/
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
		"WARNING: OPTIONS ARE NOT SUPPORTED IN THIS VERSION\n"
		"  -m [mountpoint]    Start dummyfs at a given mountopint (the mount will happen asynchronously)\n"
		"  -r [mountpoint]    Remount to a given path after spawning modules\n"
		"  -D                 Daemonize after mounting\n"
		"  -h                 This help message\n",
		progname);
}

#define PORT_DESCRIPTOR 3
#define DUMMYFS_ID_MAX 0x000000003fffffff

int main(int argc, char **argv)
{
	msg_t msg;
	dummyfs_object_t *o;
	unsigned int rid;
	int c, err;
	pid_t pid, sid;

	LOG("Started");
	dummyfs_common.size = 0;

	while ((c = getopt(argc, argv, "Dhm:r:N:")) != -1) {
		switch (c) {
			case 'm':
			case 'r':
			case 'h':
			case 'D':
			case 'N':
			default:
				print_usage(argv[0]);
				return 1;
		}
	}

	dummyfs_common.portfd = PORT_DESCRIPTOR;

	if (mutexCreate(&dummyfs_common.mutex) != EOK) {
		LOG("could not create mutex\n");
		return 2;
	}

	object_init();

	/* Create root directory */
	o = object_create();

	if (o == NULL)
		return -1;

	o->mode = S_IFDIR | DEFFILEMODE;

	dummyfs_common.rootId = o->id;
	dir_add(o, ".", 1, o);
	dir_add(o, "..", 2, o);

	fetch_modules();
	/* Daemonizing */

	/* Fork off the parent process */
	pid = fork();
	if (pid < 0) {
		LOG("fork failed: [%d] -> %s\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (pid > 0)
		return DUMMYFS_ROOT_ID;

	/* Create a new SID for the child process */
	sid = setsid();
	if (sid < 0) {
		LOG("setsid failed: [%d] -> %s\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/*** MAIN LOOP ***/

	LOG("Initialized");
	for (;;) {

		if (msgRecv(dummyfs_common.portfd, &msg, &rid) < 0)
			continue;

		if (msg.object & ~DUMMYFS_ID_MAX) {
			msgRespond(dummyfs_common.portfd, -EBADF, &msg, rid);
			continue;
		}

		switch (msg.type) {
			case mtLookup:

				err = dummyfs_lookup(&msg.object, msg.i.data, msg.i.size, &msg.o.lookup.id, &msg.o.lookup.mode);
				if ((err == -ENOENT) && (msg.i.lookup.flags & O_CREAT)) {
					err = dummyfs_create(&msg.object, msg.i.data, msg.i.size, &msg.o.lookup.id, msg.i.lookup.mode, &msg.i.lookup.dev);
					if (!err)
						msg.o.lookup.mode = msg.i.lookup.mode;
				}

				if (!err)
					dummyfs_open(&msg.o.lookup.id);
				break;

			case mtRead:
				msg.o.io = dummyfs_read(&msg.object, msg.i.io.offs, msg.o.data, msg.o.size, &err);
				break;

			case mtWrite:
				msg.o.io = dummyfs_write(&msg.object, msg.i.io.offs, msg.i.data, msg.i.size, &err);
				break;

			case mtSetAttr:
				err = dummyfs_setattr(&msg.object, msg.i.attr, msg.i.data, msg.i.size);
				break;

			case mtGetAttr:
				err = dummyfs_getattr(&msg.object, msg.i.attr, msg.o.data, msg.o.size);
				break;

			case mtLink:
				err = dummyfs_link(&msg.object, msg.i.data, msg.i.size, &msg.i.link.id);
				break;

			case mtUnlink:
				err = dummyfs_unlink(&msg.object, msg.i.data, msg.i.size);
				break;

			case mtOpen:
				err = dummyfs_open(&msg.object);
				msg.o.open = msg.object;
				break;

			case mtClose:
				err = dummyfs_close(&msg.object);
				break;
		}
		msgRespond(dummyfs_common.portfd, err, &msg, rid);
	}

	return EOK;
}
