/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * Library
 *
 * Copyright 2019, 2020 Phoenix Systems
 * Author: Kamil Amanowicz, Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <sys/file.h>
#include <sys/stat.h>
#include <sys/threads.h>

#include "ext2.h"
#include "libext2.h"


int libext2_handler(void *fdata, msg_t *msg)
{
	ext2_t *fs = (ext2_t *)fdata;
	ext2_obj_t *obj;
	uint16_t mode;
	oid_t dev;
	unsigned int namelen;

	switch (msg->type) {
	case mtCreate:
		mode = (uint16_t)msg->i.create.mode;
		msg->o.create.oid.port = fs->oid.port;

		switch (msg->i.create.type) {
		case otDir:
			mode |= S_IFDIR;
			break;

		case otFile:
			mode |= S_IFREG;
			break;

		case otDev:
			mode &= 0x1ff;
			mode |= S_IFCHR;
			break;

		case otSymlink:
			mode |= S_IFLNK;
			break;
		}

		/* FIXME: casting namelen to (uint8_t) is not a good way to check for length constraints */
		namelen = strlen(msg->i.data);
		if (ext2_lookup(fs, msg->i.create.dir.id, msg->i.data, namelen, &msg->o.create.oid.id, &dev) > 0) {
			if ((obj = ext2_obj_get(fs, msg->o.create.oid.id)) == NULL) {
				msg->o.create.err = -EINVAL;
				break;
			}

			mutexLock(obj->lock);

			if (S_ISCHR(obj->inode->mode) || S_ISBLK(obj->inode->mode)) {
				if (!dev.port && !dev.id && (S_ISCHR(mode) || S_ISBLK(mode))) {
					memcpy(&obj->dev, &msg->i.create.dev, sizeof(oid_t));
					obj->inode->mode = mode;
					obj->flags |= OFLAG_DIRTY;
					msg->o.create.oid.id = obj->id;
					msg->o.create.err = _ext2_obj_sync(fs, obj);

					mutexUnlock(obj->lock);
					ext2_obj_put(fs, obj);
					break;
				}
				else {
					mutexUnlock(obj->lock);
					ext2_obj_put(fs, obj);

					if ((dev.port == fs->oid.port) && (dev.id == msg->o.create.oid.id)) {
						if (ext2_unlink(fs, msg->i.create.dir.id, msg->i.data, namelen) < 0) {
							msg->o.create.err = -EEXIST;
							break;
						}
					}
					else {
						msg->o.create.err = -EEXIST;
						break;
					}
				}
			}
			else {
				mutexUnlock(obj->lock);
				ext2_obj_put(fs, obj);

				msg->o.create.err = -EEXIST;
				break;
			}
		}

		msg->o.create.err = ext2_create(fs, msg->i.create.dir.id, msg->i.data, namelen, &msg->i.create.dev, mode, &msg->o.create.oid.id);

		if (msg->o.create.err >= 0 && msg->i.create.type == otSymlink) {
			const char *target = msg->i.data + namelen + 1;
			int targetlen = strlen(target);
			int ret;

			/* not writing trailing '\0', readlink() does not append it */
			ret = ext2_write(fs, msg->o.create.oid.id, 0, target, targetlen);
			if (ret < 0) {
				msg->o.create.err = ret;
				ext2_destroy(fs, msg->o.create.oid.id);
				msg->o.create.oid.id = 0;
			}
		}
		break;

	case mtDestroy:
		msg->o.io.err = ext2_destroy(fs, msg->i.destroy.oid.id);
		break;

	case mtLookup:
		msg->o.lookup.fil.port = fs->oid.port;
		msg->o.lookup.err = ext2_lookup(fs, msg->i.lookup.dir.id, msg->i.data, (uint8_t)strlen(msg->i.data), &msg->o.lookup.fil.id, &msg->o.lookup.dev);
		break;

	case mtOpen:
		ext2_open(fs, msg->i.openclose.oid.id);
		break;

	case mtClose:
		ext2_close(fs, msg->i.openclose.oid.id);
		break;

	case mtRead:
		msg->o.io.err = ext2_read(fs, msg->i.io.oid.id, msg->i.io.offs, msg->o.data, msg->o.size);
		break;

	case mtReaddir:
		msg->o.io.err = ext2_read(fs, msg->i.readdir.dir.id, msg->i.readdir.offs, msg->o.data, msg->o.size);
		break;

	case mtWrite:
		msg->o.io.err = ext2_write(fs, msg->i.io.oid.id, msg->i.io.offs, msg->i.data, msg->i.size);
		break;

	case mtTruncate:
		msg->o.io.err = ext2_truncate(fs, msg->i.io.oid.id, msg->i.io.len);
		break;

	case mtDevCtl:
		msg->o.io.err = -EINVAL;
		break;

	case mtGetAttr:
		msg->o.attr.err = ext2_getattr(fs, msg->i.attr.oid.id, msg->i.attr.type, &msg->o.attr.val);
		break;

	case mtSetAttr:
		msg->o.attr.err = ext2_setattr(fs, msg->i.attr.oid.id, msg->i.attr.type, msg->i.attr.val);
		break;

	case mtLink:
		msg->o.io.err = ext2_link(fs, msg->i.ln.dir.id, msg->i.data, (uint8_t)strlen(msg->i.data), msg->i.ln.oid.id);
		break;

	case mtUnlink:
		msg->o.io.err = ext2_unlink(fs, msg->i.ln.dir.id, msg->i.data, (uint8_t)strlen(msg->i.data));
		break;
	}

	return EOK;
}


int libext2_unmount(void *fdata)
{
	ext2_t *fs = (ext2_t *)fdata;

	ext2_objs_destroy(fs);
	ext2_gdt_destroy(fs);
	ext2_sb_destroy(fs);
	free(fs);

	return EOK;
}


int libext2_mount(oid_t *oid, unsigned int sectorsz, dev_read read, dev_write write, void **fdata)
{
	ext2_t *fs;
	int err;

	if ((*fdata = fs = (ext2_t *)malloc(sizeof(ext2_t))) == NULL)
		return -ENOMEM;

	fs->sectorsz = sectorsz;
	fs->read = read;
	fs->write = write;
	memcpy(&fs->oid, oid, sizeof(oid_t));

	if ((err = ext2_sb_init(fs)) < 0) {
		free(fs);
		return err;
	}

	if ((err = ext2_gdt_init(fs)) < 0) {
		ext2_sb_destroy(fs);
		free(fs);
		return err;
	}

	if ((err = ext2_objs_init(fs)) < 0) {
		ext2_gdt_destroy(fs);
		ext2_sb_destroy(fs);
		free(fs);
		return err;
	}

	if ((fs->root = ext2_obj_get(fs, ROOT_INO)) == NULL) {
		ext2_objs_destroy(fs);
		ext2_gdt_destroy(fs);
		ext2_sb_destroy(fs);
		free(fs);
		return -ENOENT;
	}

	return ROOT_INO;
}
