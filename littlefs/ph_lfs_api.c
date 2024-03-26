/*
 * Phoenix-RTOS
 *
 * Implementation of Phoenix RTOS filesystem API for littlefs.
 *
 * Copyright 2023 Phoenix Systems
 * Author: Jacek Maksymowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include "ph_lfs_api.h"

#include <time.h>

#include "lfs_bd.h"
#include "lfs_internal.h"

#define LOG_TAG "ph_lfs"
/* clang-format off */
#define LOG_ERROR(str, ...) do { fprintf(stderr, LOG_TAG " error: " str "\n", ##__VA_ARGS__); } while (0)
#define TRACE(str, ...)     do { if (0) fprintf(stderr, LOG_TAG ":%d: " str "\n", __LINE__, ##__VA_ARGS__); } while (0)
#define TRACE_FIXUP(...)    do { if (TRACE_FIXUP_ENABLE) { fprintf(stderr, __VA_ARGS__); } } while (0)
#define TRACE2(...)    		do { if (0) { TRACE(__VA_ARGS__); } } while (0)
/* clang-format on */

#if 0
static int TRACE_FIXUP_ENABLE = 1;
#else
#define TRACE_FIXUP_ENABLE 0
#endif

/* Put file's PhID into d_ino field in struct dirent
 * The field is not big enough (32 bit) so this is meant for debugging only */
#define PHIDS_IN_DIRECTORY_LISTING 0

/* If file exists but has no PhID on disk, write the given PhID to disk during lookup */
#define UPDATE_ON_NO_PHID 1

enum phLfsAttrs {
	PH_LFS_ATTR_ATIME = 0,
	PH_LFS_ATTR_CTIME,
	PH_LFS_ATTR_MTIME,
	PH_LFS_ATTR_UID,
	PH_LFS_ATTR_GID,
	PH_LFS_ATTR_MODE,
};

#define LFS_TYPE_PH_ATTR_LAST   (LFS_TYPE_USERATTR + 0xfb)
#define LFS_TYPE_PH_ATTR_NUM(x) (LFS_TYPE_PH_ATTR_LAST - x)
#define LFS_TYPE_PH_ATTR_ATIME  LFS_TYPE_PH_ATTR_NUM(PH_LFS_ATTR_ATIME)
#define LFS_TYPE_PH_ATTR_CTIME  LFS_TYPE_PH_ATTR_NUM(PH_LFS_ATTR_CTIME)
#define LFS_TYPE_PH_ATTR_MTIME  LFS_TYPE_PH_ATTR_NUM(PH_LFS_ATTR_MTIME)
#define LFS_TYPE_PH_ATTR_UID    LFS_TYPE_PH_ATTR_NUM(PH_LFS_ATTR_UID)
#define LFS_TYPE_PH_ATTR_GID    LFS_TYPE_PH_ATTR_NUM(PH_LFS_ATTR_GID)
#define LFS_TYPE_PH_ATTR_MODE   LFS_TYPE_PH_ATTR_NUM(PH_LFS_ATTR_MODE)

enum extrasType {
	EXTRAS_TYPE_STUB, /* Entry is a stub and extras is NULL */
	EXTRAS_TYPE_FILE, /* Entry is an open file and extras is of type lfs_file_t* */
	EXTRAS_TYPE_DIR,  /* Entry is an open directory and extras is of type lfs_dir_t* */
	EXTRAS_TYPE_OID,  /* Entry is a mountpoint or device file and extras is of type oid_t* */
};

#define PH_LRU_FLAG_ISDIR   (1 << 0) /* Object represents a directory */
#define PH_LRU_FLAG_NOPHID  (1 << 1) /* Object has no PhID stored on disk */
#define PH_LRU_FLAG_DELMARK (1 << 2) /* Object has been deleted from filesystem */
#define PH_LRU_FLAG_CREAT   (1 << 3) /* Object is in progress of being created or moved */

typedef struct ph_lfs_lru {
	struct ph_lfs_lru *prev, *next;
	rbnode_t phIdNode;
	id_t phId;                  /* File's ID for use within Phoenix RTOS */
	lfs_block_t parentBlock[2]; /* Metadata block pair that stores info about this file/directory */
	uint16_t id;                /* ID of this file/directory within the parent metadata block pair */
	uint8_t flags;              /* Bitfield of flags from PH_LRU_FLAG_* */
	uint8_t extrasType;         /* Type of data stored at pointer extras from enum extrasType */
	void *extras;               /* Additional data for this entry */
} ph_lfs_lru_t;


static int ph_lfs_setSimpleAttr(lfs_t *lfs, ph_lfs_lru_t *obj, long long attr, uint16_t attrType, size_t attrSize);


void ph_lfs_bumpLastPhId(lfs_t *lfs, id_t found)
{
	if (found > lfs->lastFileId) {
		lfs->lastFileId = found;
	}
}


static id_t ph_lfs_getNextPhId(lfs_t *lfs)
{
	return ++lfs->lastFileId;
}


static void ph_lfs_rollBackPhId(lfs_t *lfs, id_t unusedId)
{
	LFS_ASSERT(lfs->lastFileId > LFS_ROOT_PHID);
	if (lfs->lastFileId == unusedId) {
		lfs->lastFileId--;
	}
}


static inline bool ph_lfs_objIsDir(const ph_lfs_lru_t *obj)
{
	return (obj->flags & PH_LRU_FLAG_ISDIR) != 0;
}


static inline bool ph_lfs_objHasNoPhID(const ph_lfs_lru_t *obj)
{
	return (obj->flags & PH_LRU_FLAG_NOPHID) != 0;
}


static inline bool ph_lfs_objDelMarked(const ph_lfs_lru_t *obj)
{
	return (obj->flags & PH_LRU_FLAG_DELMARK) != 0;
}


static inline bool ph_lfs_objIsEvictable(const ph_lfs_lru_t *obj)
{
	return ((obj->flags & PH_LRU_FLAG_NOPHID) == 0) && (obj->extrasType == EXTRAS_TYPE_STUB);
}


static int ph_lfs_comparePhID(rbnode_t *n1, rbnode_t *n2)
{
	ph_lfs_lru_t *o1 = lib_treeof(ph_lfs_lru_t, phIdNode, n1);
	ph_lfs_lru_t *o2 = lib_treeof(ph_lfs_lru_t, phIdNode, n2);

	if (o1->phId != o2->phId) {
		return o1->phId > o2->phId ? 1 : -1;
	}

	return 0;
}


int ph_lfs_mount(lfs_t *lfs, const struct lfs_config *cfg, unsigned int port)
{
	lib_rbInit(&lfs->phIdTree, ph_lfs_comparePhID, NULL);
	lfs->phLfsObjects = NULL;
	lfs->nPhLfsObjects = 0;
	lfs->port = port;
	lfs->lastFileId = LFS_ROOT_PHID;
	lfs->initialScan = true;

	int err = lfs_rawmount(lfs, cfg);
	if (err < 0) {
		return err;
	}

	return lfs_fs_forceconsistency(lfs);
}


struct ph_lfs_matchPhId_data {
	lfs_t *lfs;
	const id_t phIdLE; /* Must be little-endian */
};

static int ph_lfs_matchPhId(void *data, lfs_tag_t tag, const void *buffer)
{
	struct ph_lfs_matchPhId_data *find = data;
	lfs_t *lfs = find->lfs;
	const struct lfs_diskoff *disk = buffer;
	if (lfs_tag_size(tag) != ID_SIZE) {
		return LFS_CMP_LT;
	}

	id_t readPhId;
	int err = lfs_bd_read(lfs,
		&lfs->pcache, &lfs->rcache, lfs->cfg->block_size,
		disk->block, disk->off, &readPhId, ID_SIZE);
	if (err != 0) {
		return err;
	}

	/* Returning "less than" whenever the IDs are not equal is intentional
	 * due to a quirk in lfs_dir_fetchmatch */
	return (find->phIdLE == readPhId) ? LFS_CMP_EQ : LFS_CMP_LT;
}


/* Find file by PhID in directory pointed to by pair */
static lfs_stag_t ph_lfs_findById(lfs_t *lfs, lfs_mdir_t *dir, const lfs_block_t *pair, uint16_t id, id_t phId)
{
	dir->tail[0] = pair[0];
	dir->tail[1] = pair[1];
	struct ph_lfs_matchPhId_data matchData = { lfs, ph_lfs_toLE64(phId) };
	while (true) {
		lfs_stag_t tag = lfs_dir_fetchmatch(lfs, dir, dir->tail,
			LFS_MKTAG(LFS_TYPE_PHID_MASK, 0, 0x3ff),
			LFS_MKTAG(LFS_TYPE_PHID_ANY, id, ID_SIZE),
			NULL,
			ph_lfs_matchPhId, &matchData);
		if ((tag != 0) && (lfs_tag_id(tag) != 0x3ff)) {
			/* If this assertion fails, fixup logic may be faulty */
			LFS_ASSERT(lfs_tag_id(tag) == id);
			return tag;
		}

		if (!dir->split) {
			return LFS_ERR_NOENT;
		}

		TRACE("continuing lookup (not found in %x %x)", dir->pair[0], dir->pair[1]);
	}
}


/* Find file by PhID by scanning the whole filesystem (costly) */
static lfs_stag_t ph_lfs_scanForId(lfs_t *lfs, lfs_mdir_t *dir, id_t phId)
{
	int err = 0;
	dir->tail[0] = lfs->root[0];
	dir->tail[1] = lfs->root[1];
	if (phId == LFS_ROOT_PHID) {
		dir->pair[0] = LFS_BLOCK_NULL;
		dir->pair[1] = LFS_BLOCK_NULL;
		return LFS_MKTAG(LFS_TYPE_PHID_DIR, 0x3ff, ID_SIZE);
	}

	TRACE("scanning for ID %" PRId32, (uint32_t)phId);
	lfs_block_t tortoise[2] = { LFS_BLOCK_NULL, LFS_BLOCK_NULL };
	lfs_size_t tortoise_i = 1;
	lfs_size_t tortoise_period = 1;
	struct ph_lfs_matchPhId_data matchData = { lfs, ph_lfs_toLE64(phId) };
	while (!lfs_pair_isnull(dir->tail)) {
		/* detect cycles with Brent's algorithm */
		if (lfs_pair_issync(dir->tail, tortoise)) {
			LFS_WARN("Cycle detected in tail list");
			err = LFS_ERR_CORRUPT;
			return err;
		}

		if (tortoise_i == tortoise_period) {
			tortoise[0] = dir->tail[0];
			tortoise[1] = dir->tail[1];
			tortoise_i = 0;
			tortoise_period *= 2;
		}

		tortoise_i += 1;

		/* fetch next block in tail list */
		lfs_stag_t tag = lfs_dir_fetchmatch(lfs, dir, dir->tail,
			LFS_MKTAG(LFS_TYPE_PHID_MASK, 0, 0x3ff),
			LFS_MKTAG(LFS_TYPE_PHID_ANY, 0, ID_SIZE),
			NULL,
			ph_lfs_matchPhId, &matchData);
		if (tag < 0) {
			if (tag == LFS_ERR_NOENT) {
				continue;
			}

			return tag;
		}

		if (tag > 0 && !lfs_tag_isdelete(tag)) {
			return tag;
		}
	}

	return LFS_ERR_NOENT;
}


static int ph_lfs_allocateLfsStruct(ph_lfs_lru_t *stub, lfs_stag_t tag, const lfs_mdir_t *mdir)
{
	if (lfs_tag_type3(tag) == LFS_TYPE_PHID_DIR) {
		lfs_dir_t *dir = malloc(sizeof(lfs_dir_t));
		if (dir == NULL) {
			return LFS_ERR_NOMEM;
		}

		dir->nextDir = NULL;
		dir->common.m = *mdir;
		dir->common.id = lfs_tag_id(tag);
		dir->refcount = 0;
		stub->extras = dir;
		stub->extrasType = EXTRAS_TYPE_DIR;
		LFS_ASSERT(ph_lfs_objIsDir(stub));
	}
	else if (lfs_tag_type3(tag) == LFS_TYPE_PHID_REG) {
		lfs_file_t *file = malloc(sizeof(lfs_file_t));
		if (file == NULL) {
			return LFS_ERR_NOMEM;
		}

		file->common.m = *mdir;
		file->common.id = lfs_tag_id(tag);
		file->refcount = 0;
		file->cache.buffer = NULL;
		stub->extras = file;
		stub->extrasType = EXTRAS_TYPE_FILE;
		LFS_ASSERT(!ph_lfs_objIsDir(stub));
	}
	else {
		LOG_ERROR("got unrecognized tag %x\n", tag);
		LFS_ASSERT(0);
		return LFS_ERR_INVAL;
	}

	return 0;
}


static lfs_stag_t ph_lfs_fetchObjMdir(lfs_t *lfs, ph_lfs_lru_t *obj, lfs_mdir_t *dir)
{
	if (obj->phId == LFS_ROOT_PHID) {
		return ph_lfs_scanForId(lfs, dir, obj->phId);
	}
	else if (ph_lfs_objHasNoPhID(obj)) {
		int err = lfs_dir_fetch(lfs, dir, obj->parentBlock);
		if (err < 0) {
			return err;
		}

		lfs_stag_t nameTag = lfs_dir_get(lfs, dir, LFS_MKTAG(0x780, 0x3ff, 0),
			LFS_MKTAG(LFS_TYPE_NAME, obj->id, 0), NULL);
		if (nameTag < 0) {
			return nameTag;
		}

		/* Create a fake PhID tag that we "found" */
		uint16_t type = (lfs_tag_type3(nameTag) == LFS_TYPE_DIR) ? LFS_TYPE_PHID_DIR : LFS_TYPE_PHID_REG;
		return LFS_MKTAG(type, lfs_tag_id(nameTag), ID_SIZE);
	}

	return ph_lfs_findById(lfs, dir, obj->parentBlock, obj->id, obj->phId);
}


static void ph_lfs_removeLRU(lfs_t *lfs, ph_lfs_lru_t *obj)
{
	LFS_ASSERT(obj->extrasType == EXTRAS_TYPE_STUB);
	lfs->nPhLfsObjects--;
	LIST_REMOVE(&lfs->phLfsObjects, obj);
	lib_rbRemove(&lfs->phIdTree, &obj->phIdNode);
}


static ph_lfs_lru_t *ph_lfs_getLRU(lfs_t *lfs, id_t phId)
{
	if (phId == LFS_INVALID_PHID) {
		return NULL;
	}

	ph_lfs_lru_t find = { .phId = phId };
	ph_lfs_lru_t *obj = lib_treeof(ph_lfs_lru_t, phIdNode, lib_rbFind(&lfs->phIdTree, &find.phIdNode));
	if (obj != NULL) {
		/* Shift to back */
		LIST_REMOVE(&lfs->phLfsObjects, obj);
		LIST_ADD(&lfs->phLfsObjects, obj);
	}

	return obj;
}


static void ph_lfs_addToLRU(lfs_t *lfs, ph_lfs_lru_t *obj)
{
	lib_rbInsert(&lfs->phIdTree, &obj->phIdNode);
	LIST_ADD(&lfs->phLfsObjects, obj);
	lfs->nPhLfsObjects++;
	if (lfs->nPhLfsObjects > lfs->cfg->ph.maxCachedObjects) {
		/* The newly inserted object is at the end of the list
		 * and also shouldn't be removed because we are about to use it */
		for (ph_lfs_lru_t *i = lfs->phLfsObjects; i != obj; i = i->next) {
			if (ph_lfs_objIsEvictable(i)) {
				TRACE2("evicting obj %" PRId32, (uint32_t)i->phId);
				ph_lfs_removeLRU(lfs, i);
				break;
			}
		}
	}
}


static ph_lfs_lru_t *ph_lfs_createObj(lfs_t *lfs, id_t phId, bool fetch, const lfs_mdir_t *mdir, lfs_stag_t tag, bool noPhID)
{
	ph_lfs_lru_t *obj = malloc(sizeof(ph_lfs_lru_t));
	if (obj == NULL) {
		return NULL;
	}

	obj->extrasType = EXTRAS_TYPE_STUB;
	obj->flags = noPhID ? PH_LRU_FLAG_NOPHID : 0;
	obj->flags |= (lfs_tag_type3(tag) == LFS_TYPE_PHID_DIR) ? PH_LRU_FLAG_ISDIR : 0;
	obj->phId = phId;
	obj->parentBlock[0] = mdir->pair[0];
	obj->parentBlock[1] = mdir->pair[1];
	obj->id = lfs_tag_id(tag);

	if (fetch) {
		int err = ph_lfs_allocateLfsStruct(obj, tag, mdir);
		if (err != 0) {
			free(obj);
			return NULL;
		}
	}
	else {
		obj->extras = NULL;
	}

	ph_lfs_addToLRU(lfs, obj);
	return obj;
}


static ph_lfs_lru_t *ph_lfs_createObjDev(lfs_t *lfs, id_t phId, const lfs_mdir_t *mdir, lfs_stag_t tag, oid_t *dev)
{
	ph_lfs_lru_t *obj = malloc(sizeof(ph_lfs_lru_t));
	if (obj == NULL) {
		return NULL;
	}

	obj->extras = malloc(sizeof(oid_t));
	if (obj->extras == NULL) {
		free(obj);
		return NULL;
	}

	*(oid_t *)obj->extras = *dev;

	obj->extrasType = EXTRAS_TYPE_OID;
	obj->flags = (lfs_tag_type3(tag) == LFS_TYPE_PHID_DIR) ? PH_LRU_FLAG_ISDIR : 0;
	obj->phId = phId;
	obj->parentBlock[0] = mdir->pair[0];
	obj->parentBlock[1] = mdir->pair[1];
	obj->id = lfs_tag_id(tag);

	ph_lfs_addToLRU(lfs, obj);
	return obj;
}


static void lfs_openDirs_remove(lfs_t *lfs, lfs_dir_t *elem)
{
	for (lfs_dir_t **p = &lfs->openDirs; *p; p = &(*p)->nextDir) {
		if (*p == elem) {
			*p = (*p)->nextDir;
			break;
		}
	}
}


static void lfs_openDirs_append(lfs_t *lfs, lfs_dir_t *elem)
{
	elem->nextDir = lfs->openDirs;
	lfs->openDirs = elem;
}


static void ph_lfs_freeExtras(lfs_t *lfs, ph_lfs_lru_t *obj)
{
	if ((obj->extrasType == EXTRAS_TYPE_DIR)) {
		lfs_openDirs_remove(lfs, (lfs_dir_t *)obj->extras);
	}
	else if (obj->extrasType == EXTRAS_TYPE_FILE) {
		lfs_free(((lfs_file_t *)obj->extras)->cache.buffer);
	}

	free(obj->extras);
	obj->extras = NULL;
	obj->extrasType = EXTRAS_TYPE_STUB;
}


static void ph_lfs_deleteObj(lfs_t *lfs, ph_lfs_lru_t *obj, bool closing)
{
	if (obj == NULL) {
		return;
	}

	bool openedFile = (obj->extrasType == EXTRAS_TYPE_FILE) || (obj->extrasType == EXTRAS_TYPE_DIR);
	if (openedFile && !closing) {
		obj->flags |= PH_LRU_FLAG_DELMARK;
	}
	else {
		ph_lfs_freeExtras(lfs, obj);
		ph_lfs_removeLRU(lfs, obj);
		free(obj);
	}
}


static int ph_lfs_getObj(lfs_t *lfs, id_t phId, bool fetch, ph_lfs_lru_t **ret)
{
	ph_lfs_lru_t *obj = ph_lfs_getLRU(lfs, phId);
	if (obj != NULL) {
		*ret = obj;
		if (!fetch || (obj->extrasType != EXTRAS_TYPE_STUB)) {
			return 0;
		}

		lfs_mdir_t mdir;
		lfs_stag_t tag = ph_lfs_fetchObjMdir(lfs, obj, &mdir);
		if (tag < 0) {
			/* This should never happen - either we're looking in the wrong place
			 * or the file has disappeared */
			LOG_ERROR("Cannot expand stub because ID not found");
			return LFS_ERR_NOENT;
		}

		LFS_ASSERT(ph_lfs_objIsDir(obj) == (lfs_tag_type3(tag) == LFS_TYPE_PHID_DIR));
		return ph_lfs_allocateLfsStruct(obj, tag, &mdir);
	}

	lfs_mdir_t mdir;
	lfs_stag_t tag = ph_lfs_scanForId(lfs, &mdir, phId);
	if (tag < 0) {
		TRACE("fetch failed");
		return tag;
	}

	*ret = ph_lfs_createObj(lfs, phId, fetch, &mdir, tag, false);
	return (*ret == NULL) ? LFS_ERR_NOMEM : 0;
}


static lfs_stag_t ph_lfs_getPhID(lfs_t *lfs, const lfs_mdir_t *dir, uint16_t id, id_t *phId)
{
	lfs_stag_t tag = lfs_dir_get(lfs, dir,
		LFS_MKTAG(LFS_TYPE_PHID_MASK, 0x3ff, 0x3ff),
		LFS_MKTAG(LFS_TYPE_PHID_ANY, id, ID_SIZE), phId);
	if (tag == LFS_ERR_NOENT) {
		/* Search LRU cache for matching objects */
		ph_lfs_lru_t *obj = lfs->phLfsObjects;
		if (obj == NULL) {
			return tag;
		}

		do {
			obj = obj->prev;
			if (ph_lfs_objHasNoPhID(obj) && (obj->id == id) && lfs_pair_cmp(obj->parentBlock, dir->pair) == 0) {
				*phId = obj->phId;
				return 0;
			}
		} while (obj != lfs->phLfsObjects);
	}

	if (tag >= 0) {
		*phId = ph_lfs_fromLE64(*phId);
	}

	return tag;
}


static ph_lfs_lru_t *ph_lfs_getLRUByFile(lfs_t *lfs, const lfs_mdir_t *dir, uint16_t id)
{
	id_t phId;
	lfs_stag_t ret = ph_lfs_getPhID(lfs, dir, id, &phId);
	return (ret < 0) ? NULL : ph_lfs_getLRU(lfs, phId);
}


static int ph_lfs_readDirPair(lfs_t *lfs, const lfs_mdir_t *dir, uint16_t id, lfs_block_t pair[2])
{
	lfs_stag_t res = lfs_dir_get(lfs, dir, LFS_MKTAG(0x700, 0x3ff, 0),
		LFS_MKTAG(LFS_TYPE_STRUCT, id, 8), pair);
	if (res < 0) {
		return res;
	}

	lfs_pair_fromle32(pair);
	return 0;
}


static lfs_stag_t ph_lfs_dir_find(lfs_t *lfs, lfs_mdir_t *dir, const char **path, uint16_t *id, ph_lfs_lru_t **lastObj)
{
	const char *name = *path;
	if (id) {
		*id = 0x3ff;
	}

	/* default to root dir */
	lfs_stag_t tag = LFS_MKTAG(LFS_TYPE_DIR, 0x3ff, 0);
	ph_lfs_lru_t *obj = NULL;
	while (true) {
		/* TODO: this code can be simplified if we don't need to handle non-canonical paths */
		/* skip slashes */
		name += strspn(name, "/");
		lfs_size_t namelen = strcspn(name, "/");

		/* skip '.' and root '..' */
		if ((namelen == 1 && memcmp(name, ".", 1) == 0) ||
			(namelen == 2 && memcmp(name, "..", 2) == 0)) {
			name += namelen;
			continue;
		}

		/* skip if matched by '..' in name */
		const char *suffix = name + namelen;
		lfs_size_t sufflen;
		int depth = 1;
		bool nextName = false;
		while (true) {
			suffix += strspn(suffix, "/");
			sufflen = strcspn(suffix, "/");
			if (sufflen == 0) {
				break;
			}

			if (sufflen == 2 && memcmp(suffix, "..", 2) == 0) {
				depth -= 1;
				if (depth == 0) {
					name = suffix + sufflen;
					nextName = true;
					break;
				}
			}
			else {
				depth += 1;
			}

			suffix += sufflen;
		}

		if (nextName) {
			continue;
		}

		if (name[0] == '\0') {
			return tag;
		}

		*path = name;

		if (lfs_tag_type3(tag) != LFS_TYPE_DIR) {
			return LFS_ERR_NOTDIR;
		}

		if ((obj != NULL) && (obj->extrasType == EXTRAS_TYPE_OID)) {
			/* This is a mountpoint, can't traverse into it */
			return LFS_ERR_NOENT;
		}

		/* If not root directory - get the directory's address */
		if (lfs_tag_id(tag) != 0x3ff) {
			int err = ph_lfs_readDirPair(lfs, dir, lfs_tag_id(tag), dir->tail);
			if (err != 0) {
				return err;
			}
		}

		while (true) {
			tag = lfs_dir_fetchmatch(lfs, dir, dir->tail,
				LFS_MKTAG(0x780, 0, 0),
				LFS_MKTAG(LFS_TYPE_NAME, 0, namelen),
				/* If last name, we need to output next available ID */
				(strchr(name, '/') == NULL) ? id : NULL,
				lfs_dir_find_match, &(struct lfs_dir_find_match) { lfs, name, namelen });
			if (tag < 0) {
				return tag;
			}

			if (tag != 0) {
				/* The directory we found may be a mountpoint - need to find its PhID to check */
				obj = ph_lfs_getLRUByFile(lfs, dir, lfs_tag_id(tag));
				if (lastObj != NULL) {
					*lastObj = obj;
				}

				break;
			}

			if (!dir->split) {
				return LFS_ERR_NOENT;
			}
		}

		/* to next name */
		name += namelen;
	}
}


static int ph_lfs_file_rawsync(lfs_t *lfs, lfs_file_t *file)
{
	if ((file->flags & LFS_F_ERRED) != 0) {
		/* it's not safe to do anything if our file errored */
		return 0;
	}

	int err = lfs_file_flush(lfs, file);
	if (err != 0) {
		file->flags |= LFS_F_ERRED;
		return err;
	}

	if (((file->flags & LFS_F_DIRTY) != 0) && !lfs_pair_isnull(file->common.m.pair)) {
		lfs_tag_t tag;
		const void *buffer;
		struct lfs_ctz ctz;
		if ((file->flags & LFS_F_INLINE) != 0) {
			buffer = file->cache.buffer;
			tag = LFS_MKTAG(LFS_TYPE_INLINESTRUCT, file->common.id, file->ctz.size);
		}
		else {
			ctz = file->ctz;
			lfs_ctz_tole32(&ctz);
			buffer = &ctz;
			tag = LFS_MKTAG(LFS_TYPE_CTZSTRUCT, file->common.id, sizeof(ctz));
		}

		ph_lfs_time_t modTimeLE = ph_lfs_toLE64(time(NULL));
		err = lfs_dir_commit(
			lfs,
			&file->common.m,
			LFS_MKATTRS(
				{ tag, buffer },
				{ LFS_MKTAG_IF(lfs->cfg->ph.useMTime != 0, LFS_TYPE_PH_ATTR_MTIME, file->common.id, sizeof(modTimeLE)), &modTimeLE }));
		if (err != 0) {
			file->flags |= LFS_F_ERRED;
			return err;
		}

		file->flags &= ~LFS_F_DIRTY;
	}

	return 0;
}


static int ph_lfs_closeObj(lfs_t *lfs, ph_lfs_lru_t *obj, bool isUnmount)
{
	int err = 0;
	int refcount = 1;
	if (obj->extrasType == EXTRAS_TYPE_FILE) {
		lfs_file_t *file = (lfs_file_t *)obj->extras;
		err = ph_lfs_file_rawsync(lfs, file);
		refcount = --file->refcount;
	}
	else if (obj->extrasType == EXTRAS_TYPE_DIR) {
		lfs_dir_t *dir = (lfs_dir_t *)obj->extras;
		refcount = --dir->refcount;
	}

	if ((refcount == 0) || isUnmount) {
		if (ph_lfs_objDelMarked(obj) || isUnmount) {
			ph_lfs_deleteObj(lfs, obj, true);
		}
		else {
			ph_lfs_freeExtras(lfs, obj);
		}
	}

	return err;
}


int ph_lfs_close(lfs_t *lfs, id_t phId)
{
	ph_lfs_lru_t *obj = ph_lfs_getLRU(lfs, phId);
	if ((obj == NULL) || (obj->extrasType == EXTRAS_TYPE_STUB) || (obj->extrasType == EXTRAS_TYPE_OID)) {
		return LFS_ERR_INVAL;
	}

	return ph_lfs_closeObj(lfs, obj, false);
}


static int ph_lfs_dir_rawopen(lfs_t *lfs, ph_lfs_lru_t *obj)
{
	LFS_ASSERT((obj->extrasType == EXTRAS_TYPE_DIR) && (obj->extras != NULL));
	lfs_dir_t *dir = (lfs_dir_t *)obj->extras;
	int err;
	dir->refcount++;
	if (dir->refcount != 1) {
		return 0;
	}

	lfs_block_t pair[2];
	if (obj->id == 0x3ff) {
		/* handle root dir separately */
		pair[0] = lfs->root[0];
		pair[1] = lfs->root[1];
	}
	else {
		/* get dir pair from parent */
		err = ph_lfs_readDirPair(lfs, &dir->common.m, obj->id, pair);
		if (err != 0) {
			ph_lfs_closeObj(lfs, obj, false);
			return err;
		}
	}

	/* fetch first pair */
	err = lfs_dir_fetch(lfs, &dir->common.m, pair);
	if (err != 0) {
		ph_lfs_closeObj(lfs, obj, false);
		return err;
	}

	/* setup entry */
	dir->head[0] = dir->common.m.pair[0];
	dir->head[1] = dir->common.m.pair[1];
	dir->common.id = 0;
	dir->pos = 0;

	lfs_openDirs_append(lfs, dir);

	return 0;
}


static int ph_lfs_file_rawopen(lfs_t *lfs, ph_lfs_lru_t *obj)
{
	LFS_ASSERT((obj->extrasType == EXTRAS_TYPE_FILE) && (obj->extras != NULL));
	lfs_file_t *file = (lfs_file_t *)obj->extras;
	int err;
	file->refcount++;
	if (file->refcount != 1) {
		return 0;
	}

	file->flags = (lfs->cfg->ph.readOnly != 0) ? LFS_O_RDONLY : LFS_O_RDWR;
	file->pos = 0;
	file->off = 0;
	file->cache.buffer = NULL;

	/* Try to load what's on disk, if it's inlined we'll fix it later */
	lfs_stag_t tag = lfs_dir_get(lfs, &file->common.m, LFS_MKTAG(0x700, 0x3ff, 0),
		LFS_MKTAG(LFS_TYPE_STRUCT, file->common.id, 8), &file->ctz);
	if (tag < 0) {
		TRACE("can't get file struct %d", tag);
		err = tag;
		goto cleanup;
	}

	LFS_ASSERT(lfs_tag_type3(tag) != LFS_TYPE_DIRSTRUCT);
	file->cache.buffer = lfs_malloc(lfs->cfg->cache_size);
	if (file->cache.buffer == NULL) {
		err = LFS_ERR_NOMEM;
		goto cleanup;
	}

	lfs_cache_zero(lfs, &file->cache); /* Zero to avoid information leak */

	if (lfs_tag_type3(tag) == LFS_TYPE_INLINESTRUCT) {
		file->ctz.head = LFS_BLOCK_INLINE;
		file->ctz.size = lfs_tag_size(tag);
		if (file->ctz.size > lfs->cfg->cache_size) {
			lfs->largeInlineOpened = true;
		}

		file->flags |= LFS_F_INLINE;
		file->cache.block = file->ctz.head;
		file->cache.off = 0;
		file->cache.size = lfs->cfg->cache_size;

		if (file->ctz.size > 0) {
			lfs_stag_t res = lfs_dir_get(lfs, &file->common.m,
				LFS_MKTAG(0x700, 0x3ff, 0),
				LFS_MKTAG(LFS_TYPE_STRUCT, file->common.id,
					lfs_min(file->cache.size, 0x3fe)),
				file->cache.buffer);
			if (res < 0) {
				TRACE("can't get struct %d", tag);
				err = res;
				goto cleanup;
			}
		}
	}
	else {
		lfs_ctz_fromle32(&file->ctz);
	}

	return 0;

cleanup:
	file->flags |= LFS_F_ERRED;
	ph_lfs_closeObj(lfs, obj, false);
	return err;
}


static int ph_lfs_openObj(lfs_t *lfs, id_t phId, ph_lfs_lru_t **objOut, bool specificType, enum extrasType expectedType)
{
	ph_lfs_lru_t *obj;
	int err = ph_lfs_getObj(lfs, phId, true, &obj);
	if (err != 0) {
		TRACE("can't get obj %d", err);
		return err;
	}

	if (specificType) {
		if (obj->extrasType != expectedType) {
			if (obj->extrasType == EXTRAS_TYPE_FILE) {
				lfs_file_t *file = (lfs_file_t *)obj->extras;
				if (file->refcount == 0) {
					ph_lfs_freeExtras(lfs, obj);
				}

				return (expectedType == EXTRAS_TYPE_DIR) ? LFS_ERR_NOTDIR : LFS_ERR_INVAL;
			}
			else if (obj->extrasType == EXTRAS_TYPE_DIR) {
				lfs_dir_t *dir = (lfs_dir_t *)obj->extras;
				if (dir->refcount == 0) {
					ph_lfs_freeExtras(lfs, obj);
				}

				return LFS_ERR_ISDIR;
			}
			else {
				TRACE("invalid open %d", obj->extrasType);
				return LFS_ERR_INVAL;
			}
		}
	}

	if (objOut != NULL) {
		*objOut = obj;
	}

	if (obj->extrasType == EXTRAS_TYPE_DIR) {
		return ph_lfs_dir_rawopen(lfs, obj);
	}
	else if (obj->extrasType == EXTRAS_TYPE_FILE) {
		if ((lfs->cfg->ph.useATime != 0) && (lfs->cfg->ph.readOnly == 0)) {
			(void)ph_lfs_setSimpleAttr(lfs, obj, time(NULL), LFS_TYPE_PH_ATTR_ATIME, sizeof(ph_lfs_time_t));
		}

		return ph_lfs_file_rawopen(lfs, obj);
	}
	else {
		TRACE("trying to open a non-openable object (type %d)", obj->extrasType);
		return LFS_ERR_INVAL;
	}
}


int ph_lfs_open(lfs_t *lfs, id_t phId)
{
	return ph_lfs_openObj(lfs, phId, NULL, false, EXTRAS_TYPE_STUB);
}


typedef struct {
	lfs_mdir_t *parent;    /* Pointer to parent directory */
	uint16_t newId;        /* ID of the new file to be created */
	bool isDir;            /* Is the new object a file or directory */
	void *structPtr;       /* Pointer to struct data to be inserted */
	lfs_size_t structSize; /* Size of struct to be inserted */
} ph_lfs_newFileData;


static int ph_lfs_commitPayload(lfs_t *lfs, ph_lfs_newFileData *d, const char *name, size_t nlen, uint16_t mode, oid_t *dev, id_t *result)
{
	LFS_ASSERT((d->structPtr != NULL) || (d->structSize == 0));
	id_t phId = ph_lfs_getNextPhId(lfs);
	lfs_tag_t nameTag = LFS_MKTAG(d->isDir ? LFS_TYPE_DIR : LFS_TYPE_REG, d->newId, nlen);
	lfs_tag_t phIdTag = LFS_MKTAG(d->isDir ? LFS_TYPE_PHID_DIR : LFS_TYPE_PHID_REG, d->newId, ID_SIZE);

	bool dirSoftTail = d->isDir && !d->parent->split;
	lfs_tag_t structTag;
	if (d->isDir) {
		structTag = LFS_MKTAG(LFS_TYPE_DIRSTRUCT, d->newId, d->structSize);
	}
	else {
		structTag = LFS_MKTAG(LFS_TYPE_INLINESTRUCT, d->newId, d->structSize);
	}

	ph_lfs_lru_t *obj;
	if ((dev == NULL) || (!LFS_ISDEV(mode))) {
		/* Add stub to LRU */
		obj = ph_lfs_createObj(lfs, phId, false, d->parent, phIdTag, false);
	}
	else {
		/* Add object to LRU that stores the device oid */
		obj = ph_lfs_createObjDev(lfs, phId, d->parent, phIdTag, dev);
		if (obj == NULL) {
			return LFS_ERR_NOMEM;
		}
	}

	if (obj != NULL) {
		obj->flags |= PH_LRU_FLAG_CREAT;
	}

	ph_lfs_time_t creationTimeLE = ph_lfs_toLE64(time(NULL));
	uint16_t modeLE = ph_lfs_toLE16(mode);
	id_t phIdLE = ph_lfs_toLE64(phId);
	int err = lfs_dir_commit(lfs, d->parent,
		LFS_MKATTRS(
			{ LFS_MKTAG(LFS_TYPE_CREATE, d->newId, 0), NULL },
			{ nameTag, name },
			{ phIdTag, &phIdLE },
			{ LFS_MKTAG(LFS_TYPE_PH_ATTR_MODE, d->newId, sizeof(modeLE)), &modeLE },
			{ LFS_MKTAG_IF(lfs->cfg->ph.useCTime != 0, LFS_TYPE_PH_ATTR_CTIME, d->newId, sizeof(creationTimeLE)), &creationTimeLE },
			{ LFS_MKTAG_IF(lfs->cfg->ph.useMTime != 0, LFS_TYPE_PH_ATTR_MTIME, d->newId, sizeof(creationTimeLE)), &creationTimeLE },
			{ structTag, d->structPtr },
			{ LFS_MKTAG_IF(dirSoftTail, LFS_TYPE_SOFTTAIL, 0x3ff, d->structSize), d->structPtr }));

	if (err == LFS_ERR_NOSPC) {
		/* It may happen that the file name doesn't fit in the metadata blocks, e.g., a 256 byte file name will
		 * not fit in a 128 byte block. A smaller name might fit. */
		err = LFS_ERR_NAMETOOLONG;
	}

	if (err < 0) {
		ph_lfs_deleteObj(lfs, obj, true);
		ph_lfs_rollBackPhId(lfs, phId);
		return err;
	}

	if (obj != NULL) {
		obj->flags &= ~PH_LRU_FLAG_CREAT;
	}

	*result = phId;
	return 0;
}


static int ph_lfs_dir_create(lfs_t *lfs, ph_lfs_lru_t *parentObj, const char *name, size_t nlen, uint16_t mode, oid_t *dev, id_t *result)
{
	LFS_ASSERT(parentObj != NULL && parentObj->extrasType == EXTRAS_TYPE_DIR && parentObj->extras != NULL);
	lfs_dir_t *parentDir = (lfs_dir_t *)parentObj->extras;
	int err;
	struct lfs_mlist cwd;
	uint16_t id;
	cwd.m.tail[0] = parentDir->head[0];
	cwd.m.tail[1] = parentDir->head[1];
	cwd.next = lfs->mlist;
	err = ph_lfs_dir_find(lfs, &cwd.m, &name, &id, NULL);
	if (!(err == LFS_ERR_NOENT && id != 0x3ff)) {
		return (err < 0) ? err : LFS_ERR_EXIST;
	}

	/* Build up new directory */
	lfs_alloc_ack(lfs);
	lfs_mdir_t dir;
	err = lfs_dir_alloc(lfs, &dir);
	if (err != 0) {
		return err;
	}

	/* Find last directory in list of directories stored on disk */
	lfs_mdir_t pred = cwd.m;
	while (pred.split) {
		err = lfs_dir_fetch(lfs, &pred, pred.tail);
		if (err != 0) {
			return err;
		}
	}

	/* Put a pointer to current end of list into our directory */
	lfs_pair_tole32(pred.tail);
	err = lfs_dir_commit(lfs, &dir, LFS_MKATTRS({ LFS_MKTAG(LFS_TYPE_SOFTTAIL, 0x3ff, 8), pred.tail }));
	lfs_pair_fromle32(pred.tail);
	if (err != 0) {
		return err;
	}

	/* current block not end of list? */
	if (cwd.m.split) {
		/* update tails, this creates a desync */
		err = lfs_fs_preporphans(lfs, 1);
		if (err != 0) {
			return err;
		}

		/* it's possible our predecessor has to be relocated, and if
		 * our parent is our predecessor's predecessor, this could have
		 * caused our parent to go out of date */
		cwd.id = 0;
		lfs->mlist = &cwd;

		lfs_pair_tole32(dir.pair);
		err = lfs_dir_commit(lfs, &pred, LFS_MKATTRS({ LFS_MKTAG(LFS_TYPE_SOFTTAIL, 0x3ff, 8), dir.pair }));
		lfs_pair_fromle32(dir.pair);
		if (err != 0) {
			lfs->mlist = cwd.next;
			return err;
		}

		lfs->mlist = cwd.next;
		err = lfs_fs_preporphans(lfs, -1);
		if (err != 0) {
			return err;
		}
	}

	/* now insert into our parent block */
	lfs_pair_tole32(dir.pair);
	ph_lfs_newFileData d = {
		.parent = &cwd.m,
		.newId = id,
		.isDir = true,
		.structPtr = dir.pair,
		.structSize = 8,
	};
	return ph_lfs_commitPayload(lfs, &d, name, nlen, mode, dev, result);
}


static int ph_lfs_file_create(lfs_t *lfs, ph_lfs_lru_t *parentObj, const char *name, size_t nlen, uint16_t mode, oid_t *dev, id_t *result)
{
	LFS_ASSERT(parentObj != NULL && parentObj->extrasType == EXTRAS_TYPE_DIR && parentObj->extras != NULL);
	lfs_dir_t *parentDir = (lfs_dir_t *)parentObj->extras;
	lfs_mdir_t parent = { 0 };
	parent.tail[0] = parentDir->head[0];
	parent.tail[1] = parentDir->head[1];
	uint16_t id;
	lfs_stag_t tag = ph_lfs_dir_find(lfs, &parent, &name, &id, NULL);
	if (!(tag == LFS_ERR_NOENT && id != 0x3ff)) {
		return (tag < 0) ? tag : LFS_ERR_EXIST;
	}

	ph_lfs_newFileData d = {
		.parent = &parent,
		.newId = id,
		.isDir = false,
		.structPtr = NULL,
		.structSize = 0,
	};
	return ph_lfs_commitPayload(lfs, &d, name, nlen, mode, dev, result);
}


int ph_lfs_create(lfs_t *lfs, id_t parentPhId, const char *name, uint16_t mode, oid_t *dev, id_t *result)
{
	lfs_size_t nlen = strlen(name);
	if (nlen > lfs->name_max) {
		return LFS_ERR_NAMETOOLONG;
	}

	ph_lfs_lru_t *parentObj;
	int err = ph_lfs_openObj(lfs, parentPhId, &parentObj, true, EXTRAS_TYPE_DIR);
	if (err < 0) {
		TRACE("parent dir err %d", err);
		return err;
	}

	if (S_ISDIR(mode)) {
		err = ph_lfs_dir_create(lfs, parentObj, name, nlen, mode, dev, result);
	}
	else {
		err = ph_lfs_file_create(lfs, parentObj, name, nlen, mode, dev, result);
	}

	if (err < 0) {
		TRACE("error creating %d", err);
	}

	if (lfs->cfg->ph.useMTime != 0) {
		(void)ph_lfs_setSimpleAttr(lfs, parentObj, time(NULL), LFS_TYPE_PH_ATTR_MTIME, sizeof(ph_lfs_time_t));
	}

	ph_lfs_closeObj(lfs, parentObj, false);
	return err;
}


ssize_t ph_lfs_write(lfs_t *lfs, id_t phId, size_t offs, const void *data, size_t len)
{
	ph_lfs_lru_t *obj;
	int err = ph_lfs_getObj(lfs, phId, false, &obj);
	if (err != 0) {
		return err;
	}

	if (obj->extrasType != EXTRAS_TYPE_FILE) {
		TRACE("invalid %d", obj->extrasType);
		/* Object not open or not a file */
		return LFS_ERR_INVAL;
	}

	lfs_file_t *file = (lfs_file_t *)obj->extras;
	lfs_soff_t seekRes = lfs_file_rawseek(lfs, file, offs, LFS_SEEK_SET);
	if (seekRes < 0) {
		return seekRes;
	}

	LFS_ASSERT((size_t)seekRes == offs);
	return lfs_file_rawwrite(lfs, file, data, len);
}


ssize_t ph_lfs_read(lfs_t *lfs, id_t phId, size_t offs, void *data, size_t len)
{
	ph_lfs_lru_t *obj;
	int err = ph_lfs_getObj(lfs, phId, false, &obj);
	if (err != 0) {
		return err;
	}

	bool wasOpened = false;
	if (obj->extrasType != EXTRAS_TYPE_FILE) {
		/* For some reason OS reads symlinks without opening... */
		long long mode;
		int err = ph_lfs_getattr(lfs, phId, atMode, &mode);
		if (err < 0) {
			return err;
		}

		if (!S_ISLNK(mode)) {
			return LFS_ERR_INVAL;
		}

		err = ph_lfs_openObj(lfs, phId, &obj, true, EXTRAS_TYPE_FILE);
		if (err < 0) {
			return err;
		}

		wasOpened = true;
	}

	lfs_file_t *file = (lfs_file_t *)obj->extras;
	ssize_t res = lfs_file_rawseek(lfs, file, offs, LFS_SEEK_SET);
	if (res >= 0) {
		LFS_ASSERT((size_t)res == offs);
		res = lfs_file_rawread(lfs, file, data, len);
	}

	if (wasOpened) {
		ph_lfs_close(lfs, phId);
	}

	return res;
}


int ph_lfs_sync(lfs_t *lfs, id_t phId)
{
	ph_lfs_lru_t *obj;
	int err = ph_lfs_getObj(lfs, phId, false, &obj);
	if (err != 0) {
		return err;
	}

	if (obj->extrasType != EXTRAS_TYPE_FILE) {
		/* File may not be opened or may be a directory
		 * Either way the object is already synced
		 */
		return 0;
	}

	lfs_file_t *file = (lfs_file_t *)obj->extras;
	return ph_lfs_file_rawsync(lfs, file);
}


int ph_lfs_truncate(lfs_t *lfs, id_t phId, size_t size)
{
	ph_lfs_lru_t *obj;
	int err = ph_lfs_openObj(lfs, phId, &obj, true, EXTRAS_TYPE_FILE);
	if (err < 0) {
		return err;
	}

	err = lfs_file_rawtruncate(lfs, (lfs_file_t *)obj->extras, size);
	ph_lfs_closeObj(lfs, obj, false);
	return err;
}


/* TODO: this function can't support going upwards in the filesystem (lookups like "../something") */
static ssize_t ph_lfs_lookupFromObj(lfs_t *lfs, ph_lfs_lru_t *parentObj, const char *path, id_t *res, oid_t *dev)
{
	LFS_ASSERT(parentObj != NULL);
	if (!ph_lfs_objIsDir(parentObj)) {
		return LFS_ERR_NOTDIR;
	}

	lfs_mdir_t cwd;
	if (parentObj->phId == LFS_ROOT_PHID) {
		cwd.tail[0] = lfs->root[0];
		cwd.tail[1] = lfs->root[1];
	}
	else if (parentObj->extrasType == EXTRAS_TYPE_DIR) {
		TRACE2("dir already open");
		LFS_ASSERT(parentObj->extras != NULL);
		lfs_dir_t *parentDir = parentObj->extras;
		cwd.tail[0] = parentDir->head[0];
		cwd.tail[1] = parentDir->head[1];
	}
	else {
		int err = lfs_dir_fetch(lfs, &cwd, parentObj->parentBlock);
		if (err != 0) {
			return 0;
		}

		err = ph_lfs_readDirPair(lfs, &cwd, parentObj->id, cwd.tail);
		if (err != 0) {
			return 0;
		}
	}

	const char *pathStart = path;
	ph_lfs_lru_t *obj = NULL;
	lfs_stag_t nameTag = ph_lfs_dir_find(lfs, &cwd, &path, NULL, &obj);
	ssize_t lenConsumed = path - pathStart;
	if (nameTag == LFS_ERR_NOENT) {
		if ((obj == NULL) || (obj->extrasType != EXTRAS_TYPE_OID)) {
			return LFS_ERR_NOENT;
		}

		*res = obj->phId;
		if (dev != NULL) {
			*dev = *(oid_t *)obj->extras;
		}

		/* Final slash needs to be "unconsumed" */
		return lenConsumed - 1;
	}
	else if (nameTag < 0) {
		return nameTag;
	}
	else {
		/* If the whole path is resolved, the pointer is not pushed forward to the end */
		lenConsumed += strlen(path);
	}

	uint16_t id = lfs_tag_id(nameTag);
	if (id == 0x3ff) {
		TRACE("lookup stays at parentObj");
		*res = parentObj->phId;
		obj = parentObj;
	}
	else if (obj != NULL) {
		/* We already found the object during lookup */
		*res = obj->phId;
	}
	else {
		lfs_stag_t phIdTag = ph_lfs_getPhID(lfs, &cwd, id, res);
		if (phIdTag == LFS_ERR_NOENT) {
			TRACE("Looked up file without PhID (%s)", pathStart);
			/* Create a fake PhID tag that we "found" */
			uint16_t type = (lfs_tag_type3(nameTag) == LFS_TYPE_DIR) ? LFS_TYPE_PHID_DIR : LFS_TYPE_PHID_REG;
			phIdTag = LFS_MKTAG(type, lfs_tag_id(nameTag), ID_SIZE);
			*res = ph_lfs_getNextPhId(lfs);
			obj = ph_lfs_createObj(lfs, *res, false, &cwd, phIdTag, true);
			/* parentObj may be a stub, so after createObj() it may have been evicted */
			parentObj = NULL;
			if (obj == NULL) {
				ph_lfs_rollBackPhId(lfs, *res);
				return LFS_ERR_NOMEM;
			}

			if ((UPDATE_ON_NO_PHID != 0) && (lfs->cfg->ph.readOnly == 0)) {
				id_t phIdLE = ph_lfs_toLE64(*res);
				int commitErr = lfs_dir_commit(lfs, &cwd, LFS_MKATTRS({ phIdTag, &phIdLE }));
				if (commitErr == 0) {
					obj->flags &= ~PH_LRU_FLAG_NOPHID;
				}
			}
		}
		else if (phIdTag < 0) {
			return phIdTag;
		}
		else {
			TRACE2("lookup res %" PRId32, (uint32_t)*res);
			obj = ph_lfs_getLRU(lfs, *res);
			/* Add stub to LRU if not already in */
			if (obj == NULL) {
				/* If creation here fails it's not a big problem, we can continue */
				obj = ph_lfs_createObj(lfs, *res, false, &cwd, phIdTag, false);
				/* parentObj may be a stub, so after createObj() it may have been evicted */
				parentObj = NULL;
			}
			else {
				LFS_ASSERT((obj->id == lfs_tag_id(phIdTag)) && (lfs_pair_cmp(obj->parentBlock, cwd.pair) == 0));
			}
		}
	}

	if (dev != NULL) {
		if ((obj != NULL) && (obj->extrasType == EXTRAS_TYPE_OID)) {
			*dev = *(oid_t *)obj->extras;
		}
		else {
			dev->id = *res;
			dev->port = lfs->port;
		}
	}

	return lenConsumed;
}


ssize_t ph_lfs_lookup(lfs_t *lfs, id_t parentPhId, const char *path, id_t *res, oid_t *dev)
{
	ph_lfs_lru_t *parentObj;
	int err = ph_lfs_getObj(lfs, parentPhId, false, &parentObj);
	if (err != 0) {
		return err;
	}

	return ph_lfs_lookupFromObj(lfs, parentObj, path, res, dev);
}


static int ph_lfs_getSimpleAttr(lfs_t *lfs, ph_lfs_lru_t *obj, long long *attr, uint16_t attrType, size_t attrSize)
{
	lfs_mdir_t m;
	const lfs_mdir_t *dirPtr = &m;
	uint16_t id;
	if (obj->extrasType == EXTRAS_TYPE_FILE) {
		/* If file is already open we don't need to fetch dir info */
		lfs_file_t *file = (lfs_file_t *)obj->extras;
		dirPtr = &file->common.m;
		id = file->common.id;
	}
	else {
		const lfs_block_t *pair = (obj->phId == LFS_ROOT_PHID) ? lfs->root : obj->parentBlock;
		id = obj->id;
		int err = lfs_dir_fetch(lfs, &m, pair);
		if (err != 0) {
			return err;
		}
	}

	union {
		struct lfs_ctz ctz;  /* If LFS_TYPE_CTZSTRUCT */
		lfs_block_t pair[2]; /* If LFS_TYPE_DIRSTRUCT */
		uint8_t attr[8];     /* If LFS_TYPE_PH_ATTR_* */
	} tagData;

	uint16_t mask = (attrType == LFS_TYPE_STRUCT) ? 0x700 : 0x7ff;
	lfs_stag_t tag;
	do {
		tag = lfs_dir_get(lfs, dirPtr,
			LFS_MKTAG(mask, 0x3ff, 0),
			LFS_MKTAG(attrType, id, sizeof(tagData)),
			&tagData);

		if (tag == LFS_ERR_NOENT) {
			/* File exists but has no such attribute */
			if (attrType == LFS_TYPE_PH_ATTR_ATIME) {
				attrType = LFS_TYPE_PH_ATTR_MTIME;
			}
			else if (attrType == LFS_TYPE_PH_ATTR_MTIME) {
				attrType = LFS_TYPE_PH_ATTR_CTIME;
			}
			else if (attrType == LFS_TYPE_PH_ATTR_MODE) {
				*attr = (ph_lfs_objIsDir(obj) ? S_IFDIR : S_IFREG) | ALLPERMS;
				return 0;
			}
			else {
				*attr = 0;
				return 0;
			}
		}
	} while (tag == LFS_ERR_NOENT);

	if (tag < 0) {
		return tag;
	}

	if (lfs_tag_type3(tag) == LFS_TYPE_CTZSTRUCT) {
		lfs_ctz_fromle32(&tagData.ctz);
		*attr = tagData.ctz.size;
	}
	else if (lfs_tag_type3(tag) == LFS_TYPE_INLINESTRUCT) {
		*attr = lfs_tag_size(tag);
	}
	else if (lfs_tag_type3(tag) == LFS_TYPE_DIRSTRUCT) {
		/* Here we could fetch the directory and measure its size,
		 * but it would slow down directory listing */
		*attr = lfs->cfg->block_size;
	}
	else {
		if (lfs_tag_size(tag) != attrSize) {
			TRACE("invalid attr size");
			return LFS_ERR_INVAL;
		}

		*attr = ph_lfs_attrFromLE(tagData.attr, attrSize);
	}

	return 0;
}


int ph_lfs_getattr(lfs_t *lfs, id_t phId, int type, long long *attr)
{
	ph_lfs_lru_t *obj;
	int err = ph_lfs_getObj(lfs, phId, false, &obj);
	if (err != 0) {
		return err;
	}

	uint16_t attrType;
	size_t attrSize;
	switch (type) {
		case atLinks:
			*attr = 1; /* Hardlinks not possible in LFS */
			return 0;

		case atPollStatus:
			*attr = POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM;
			return 0;

		case atIOBlock:
			*attr = lfs->cfg->block_size;
			return 0;

		case atBlocks: /* Fall-through */
		case atSize:
			if (obj->extrasType == EXTRAS_TYPE_FILE) {
				/* Get cached file size - this is necessary for correct functioning */
				attrType = 0;
				attrSize = 0;
				*attr = lfs_file_rawsize(lfs, obj->extras);
			}
			else {
				attrType = LFS_TYPE_STRUCT;
				attrSize = sizeof(uint32_t);
			}
			break;

		case atType: /* Fall-through */
		case atMode:
			attrType = LFS_TYPE_PH_ATTR_MODE;
			attrSize = sizeof(uint16_t);
			break;

		case atUid:
			attrType = LFS_TYPE_PH_ATTR_UID;
			attrSize = sizeof(uint32_t);
			break;

		case atGid:
			attrType = LFS_TYPE_PH_ATTR_GID;
			attrSize = sizeof(uint32_t);
			break;

		case atCTime:
			attrType = LFS_TYPE_PH_ATTR_CTIME;
			attrSize = sizeof(ph_lfs_time_t);
			break;

		case atMTime:
			attrType = LFS_TYPE_PH_ATTR_MTIME;
			attrSize = sizeof(ph_lfs_time_t);
			break;

		case atATime:
			attrType = LFS_TYPE_PH_ATTR_ATIME;
			attrSize = sizeof(ph_lfs_time_t);
			break;

		default:
			return LFS_ERR_INVAL;
	}

	if (attrType != 0) {
		int ret = ph_lfs_getSimpleAttr(lfs, obj, attr, attrType, attrSize);
		if (ret < 0) {
			return ret;
		}
	}

	if (type == atType) {
		/* We actually read mode, convert it into type */
		uint16_t mode = (uint16_t)*attr;
		if (S_ISDIR(mode)) {
			*attr = otDir;
		}
		else if (S_ISREG(mode)) {
			*attr = otFile;
		}
		else if (LFS_ISDEV(mode)) {
			*attr = otDev;
		}
		else if (S_ISLNK(mode)) {
			*attr = otSymlink;
		}
		else {
			*attr = otUnknown;
		}
	}
	else if (type == atBlocks) {
		uint32_t size = (uint32_t)*attr;
		size += lfs->cfg->block_size - 1;
		size /= lfs->cfg->block_size;
		*attr = size;
	}

	return 0;
}


static int ph_lfs_setSimpleAttr(lfs_t *lfs, ph_lfs_lru_t *obj, long long attr, uint16_t attrType, size_t attrSize)
{
	lfs_mdir_t m;
	const lfs_block_t *pair = (obj->phId == LFS_ROOT_PHID) ? lfs->root : obj->parentBlock;
	int err = lfs_dir_fetch(lfs, &m, pair);
	if (err != 0) {
		TRACE("fetch fail %d", err);
		return err;
	}

	uint8_t writeAttr[8];
	ph_lfs_attrToLE(attr, writeAttr, attrSize);
	return lfs_dir_commit(lfs, &m, LFS_MKATTRS({ LFS_MKTAG(attrType, obj->id, attrSize), writeAttr }));
}


static int ph_lfs_setDev(lfs_t *lfs, id_t phId, void *data, size_t size)
{
	if (size != sizeof(oid_t)) {
		return LFS_ERR_INVAL;
	}

	ph_lfs_lru_t *obj;
	int err = ph_lfs_getObj(lfs, phId, false, &obj);
	if (err != 0) {
		return err;
	}

	oid_t dev;
	memcpy(&dev, data, size);
	if ((dev.port == lfs->port) && (dev.id == phId)) {
		if (obj->extrasType == EXTRAS_TYPE_OID) {
			ph_lfs_freeExtras(lfs, obj);
		}
	}
	else if (obj->extrasType == EXTRAS_TYPE_OID) {
		memcpy(obj->extras, data, size);
	}
	else if (obj->extrasType == EXTRAS_TYPE_STUB) {
		obj->extras = malloc(sizeof(oid_t));
		if (obj->extras == NULL) {
			return LFS_ERR_NOMEM;
		}

		obj->extrasType = EXTRAS_TYPE_OID;
		memcpy(obj->extras, data, size);
	}
	else {
		/* File is already open, we can't invalidate other accesses */
		return LFS_ERR_BUSY;
	}

	return 0;
}


int ph_lfs_setattr(lfs_t *lfs, id_t phId, int type, long long attr, void *data, size_t size)
{
	if (type == atDev) {
		return ph_lfs_setDev(lfs, phId, data, size);
	}

	if (lfs->cfg->ph.readOnly != 0) {
		return LFS_ERR_ROFS;
	}

	if (type == atSize) {
		return ph_lfs_truncate(lfs, phId, attr);
	}

	ph_lfs_lru_t *obj;
	int err = ph_lfs_getObj(lfs, phId, false, &obj);
	if (err != 0) {
		return err;
	}

	int ret = 0;
	uint16_t attrType;
	size_t attrSize;
	/* Simple attributes that are just written to disk */
	switch (type) {
		case atMode: {
			long long currentAttr;
			int ret = ph_lfs_getattr(lfs, phId, atMode, &currentAttr);
			if (ret < 0) {
				return ret;
			}

			attr = (currentAttr & ~ALLPERMS) | (attr & ALLPERMS);
			attrType = LFS_TYPE_PH_ATTR_MODE;
			attrSize = sizeof(uint16_t);
			break;
		}

		case atUid:
			attrType = LFS_TYPE_PH_ATTR_UID;
			attrSize = sizeof(uint32_t);
			break;

		case atGid:
			attrType = LFS_TYPE_PH_ATTR_GID;
			attrSize = sizeof(uint32_t);
			break;

		case atMTime:
			attrType = LFS_TYPE_PH_ATTR_MTIME;
			attrSize = sizeof(ph_lfs_time_t);
			break;

		case atATime:
			attrType = LFS_TYPE_PH_ATTR_ATIME;
			attrSize = sizeof(ph_lfs_time_t);
			break;

		default:
			TRACE("inval %d", type);
			return LFS_ERR_INVAL;
	}

	if ((ret >= 0) && (attrType != 0)) {
		ret = ph_lfs_setSimpleAttr(lfs, obj, attr, attrType, attrSize);
	}

	return ret;
}


static void ph_lfs_dummyDirInfo(struct dirent *info, const char *name)
{
	info->d_ino = LFS_INVALID_PHID;
	info->d_type = DT_DIR;
	strcpy(info->d_name, name);
	info->d_namlen = strlen(name);
}


static int ph_lfs_dir_getinfo(lfs_t *lfs, lfs_mdir_t *dir, uint16_t id, struct dirent *info, size_t maxNameLength)
{
	if (id == 0x3ff) {
		/* special case for root */
		ph_lfs_dummyDirInfo(info, "/");
		return 0;
	}

	const lfs_stag_t tag = lfs_dir_get(lfs, dir, LFS_MKTAG(0x780, 0x3ff, 0),
		LFS_MKTAG(LFS_TYPE_NAME, id, maxNameLength - 1), info->d_name);
	if (tag < 0) {
		return tag;
	}

	info->d_namlen = lfs_min(lfs_tag_size(tag), maxNameLength - 1);
	info->d_name[info->d_namlen] = '\0';
	if (PHIDS_IN_DIRECTORY_LISTING != 0) {
		id_t phId;
		lfs_stag_t phIdTag = ph_lfs_getPhID(lfs, dir, id, &phId);
		info->d_ino = (phIdTag < 0) ? LFS_INVALID_PHID : (ino_t)phId;
	}
	else {
		info->d_ino = LFS_INVALID_PHID;
	}

	uint16_t mode;
	const lfs_stag_t modeTag = lfs_dir_get(lfs, dir, LFS_MKTAG(0x7ff, 0x3ff, 0),
		LFS_MKTAG(LFS_TYPE_PH_ATTR_MODE, id, sizeof(mode)), &mode);

	if (modeTag < 0) {
		/* Can happen with non-Phoenix formatted FS */
		TRACE("mode tag not found");
		info->d_type = (lfs_tag_type3(tag) == LFS_TYPE_DIR) ? DT_DIR : DT_REG;
	}
	else {
		mode = ph_lfs_fromLE16(mode);
		switch (mode & S_IFMT) {
			case S_IFSOCK: /* Fall-through */
			case S_IFLNK:  /* Fall-through */
			case S_IFREG:  /* Fall-through */
			case S_IFBLK:  /* Fall-through */
			case S_IFDIR:  /* Fall-through */
			case S_IFCHR:  /* Fall-through */
			case S_IFIFO:
				info->d_type = (mode & S_IFMT) >> 12;
				break;

			default:
				info->d_type = DT_UNKNOWN;
				break;
		}
	}

	return 0;
}


static int ph_lfs_dirReadFinalize(lfs_dir_t *dir, struct dirent *info)
{
	info->d_reclen = 1;
	dir->pos += 1;
	return 0;
}


static int ph_lfs_dir_rawread(lfs_t *lfs, lfs_dir_t *dir, struct dirent *info, size_t maxNameLength)
{
	if (maxNameLength < 3) {
		return LFS_ERR_NAMETOOLONG;
	}

	/* special offset for '.' and '..' */
	if (dir->pos == 0) {
		ph_lfs_dummyDirInfo(info, ".");
		return ph_lfs_dirReadFinalize(dir, info);
	}
	else if (dir->pos == 1) {
		ph_lfs_dummyDirInfo(info, "..");
		return ph_lfs_dirReadFinalize(dir, info);
	}

	while (true) {
		if (dir->common.id == dir->common.m.count) {
			if (!dir->common.m.split) {
				return LFS_ERR_NOENT;
			}

			int err = lfs_dir_fetch(lfs, &dir->common.m, dir->common.m.tail);
			if (err != 0) {
				return err;
			}

			dir->common.id = 0;
		}

		int err = ph_lfs_dir_getinfo(lfs, &dir->common.m, dir->common.id, info, maxNameLength);
		if ((err != 0) && (err != LFS_ERR_NOENT)) {
			return err;
		}

		dir->common.id += 1;
		if (err != LFS_ERR_NOENT) {
			break;
		}
	}

	return ph_lfs_dirReadFinalize(dir, info);
}


int ph_lfs_readdir(lfs_t *lfs, id_t phId, size_t offs, struct dirent *dent, size_t size)
{
	ph_lfs_lru_t *dirObj;
	int err = ph_lfs_getObj(lfs, phId, false, &dirObj);
	if (err != 0) {
		return err;
	}

	if (dirObj->extrasType != EXTRAS_TYPE_DIR) {
		/* Object not open or not a directory */
		TRACE("invalid %d (id %" PRId32 ")", dirObj->extrasType, (uint32_t)dirObj->phId);
		return LFS_ERR_INVAL;
	}

	lfs_dir_t *dir = (lfs_dir_t *)dirObj->extras;
	if (dir->pos != offs) {
		TRACE("readdir seek necessary");
		int ret = lfs_dir_rawseek(lfs, dir, offs);
		if (ret < 0) {
			return ret;
		}
	}

	size_t maxNameLength = size - sizeof(struct dirent);
	return ph_lfs_dir_rawread(lfs, dir, dent, maxNameLength);
}


static int ph_lfs_dirRemovePrepare(lfs_t *lfs, const lfs_mdir_t *cwd, uint16_t id, struct lfs_mlist *dir)
{
	lfs_block_t pair[2];
	int err = ph_lfs_readDirPair(lfs, cwd, id, pair);
	if (err != 0) {
		return err;
	}

	err = lfs_dir_fetch(lfs, &dir->m, pair);
	if (err != 0) {
		return err;
	}

	if ((dir->m.count > 0) || (dir->m.split)) {
		return LFS_ERR_NOTEMPTY;
	}

	/* mark fs as orphaned */
	err = lfs_fs_preporphans(lfs, 1);
	if (err != 0) {
		return err;
	}

	/* I know it's crazy but yes, dir can be changed by our parent's
	 * commit (if predecessor is child) */
	dir->id = 0;
	lfs->mlist = dir;

	return 0;
}


static int ph_lfs_dirRemoveFinalize(lfs_t *lfs, lfs_mdir_t *cwd, struct lfs_mlist *dir)
{
	/* fix orphan */
	int err = lfs_fs_preporphans(lfs, -1);
	if (err != 0) {
		return err;
	}

	err = lfs_fs_pred(lfs, dir->m.pair, cwd);
	if (err != 0) {
		return err;
	}

	return lfs_dir_drop(lfs, cwd, &dir->m);
}


static int ph_lfs_rawrename(lfs_t *lfs, ph_lfs_lru_t *parentObj, const char *name, ph_lfs_lru_t *sourceObj)
{
	int err;
	LFS_ASSERT(parentObj != NULL && parentObj->extrasType == EXTRAS_TYPE_DIR && parentObj->extras != NULL);
	lfs_dir_t *parentDir = (lfs_dir_t *)parentObj->extras;

	lfs_mdir_t oldcwd;
	const lfs_stag_t oldtag = ph_lfs_fetchObjMdir(lfs, sourceObj, &oldcwd);
	uint16_t oldId = lfs_tag_id(oldtag);
	if ((oldtag < 0) || (oldId == 0x3ff)) {
		return (oldtag < 0) ? (int)oldtag : LFS_ERR_INVAL;
	}

	lfs_mdir_t newcwd;
	uint16_t newid;
	newcwd.tail[0] = parentDir->head[0];
	newcwd.tail[1] = parentDir->head[1];
	const lfs_stag_t prevtag = ph_lfs_dir_find(lfs, &newcwd, &name, &newid, NULL);
	bool lookupFailed = (prevtag < 0) || (lfs_tag_id(prevtag) == 0x3ff);
	bool targetCanBeCreated = (prevtag == LFS_ERR_NOENT) && (newid != 0x3ff);
	if (lookupFailed && !targetCanBeCreated) {
		return (prevtag < 0) ? (int)prevtag : LFS_ERR_INVAL;
	}

	bool samepair = lfs_pair_cmp(oldcwd.pair, newcwd.pair) == 0;
	uint16_t newoldid = oldId;

	struct lfs_mlist prevdir;
	prevdir.next = lfs->mlist;
	lfs_size_t nlen = strlen(name);
	ph_lfs_lru_t *prevObj = NULL;
	if (prevtag != LFS_ERR_NOENT) {
		prevObj = ph_lfs_getLRUByFile(lfs, &newcwd, newid);
	}

	if (prevtag == LFS_ERR_NOENT) {
		if (nlen > lfs->name_max) {
			return LFS_ERR_NAMETOOLONG;
		}

		/* there is a chance we are being renamed in the same
		 * directory/ to an id less than our old id, the global update
		 * to handle this is a bit messy */
		if (samepair && (newid <= newoldid)) {
			newoldid += 1;
		}
	}
	else if (lfs_tag_type3(prevtag) != lfs_tag_type3(oldtag)) {
		return LFS_ERR_ISDIR;
	}
	else if (samepair && newid == newoldid) {
		/* Move to the same file as source - exit with error */
		return LFS_ERR_EXIST;
	}
	else if (lfs_tag_type3(prevtag) == LFS_TYPE_PHID_DIR) {
		err = ph_lfs_dirRemovePrepare(lfs, &newcwd, newid, &prevdir);
		if (err != 0) {
			return err;
		}
	}

	if (!samepair) {
		lfs_fs_prepmove(lfs, newoldid, oldcwd.pair);
	}

	TRACE("move src (%x %x) %d => dest (%x %x) %d (rm %d)",
		sourceObj->parentBlock[0], sourceObj->parentBlock[1], sourceObj->id,
		newcwd.pair[0], newcwd.pair[1], newid, newoldid);

	uint16_t nameTagType = (lfs_tag_type3(oldtag) == LFS_TYPE_PHID_DIR) ? LFS_TYPE_DIR : LFS_TYPE_REG;
	sourceObj->flags |= PH_LRU_FLAG_CREAT;
	/* move over all attributes */
	err = lfs_dir_commit(lfs, &newcwd,
		LFS_MKATTRS(
			{ LFS_MKTAG_IF(prevtag != LFS_ERR_NOENT, LFS_TYPE_DELETE, newid, 0), NULL },
			{ LFS_MKTAG(LFS_TYPE_CREATE, newid, 0), NULL },
			{ LFS_MKTAG(nameTagType, newid, nlen), name },
			{ LFS_MKTAG(LFS_FROM_MOVE, newid, oldId), &oldcwd },
			{ LFS_MKTAG_IF(samepair, LFS_TYPE_DELETE, newoldid, 0), NULL }));
	if (err != 0) {
		lfs->mlist = prevdir.next;
		return err;
	}

	sourceObj->flags &= ~PH_LRU_FLAG_CREAT;
	/* let commit clean up after move (if we're different! otherwise move
	 * logic already fixed it for us) */
	if (!samepair && lfs_gstate_hasmove(&lfs->gstate)) {
		/* remove move operation from gstate and delete old file */
		lfs_fs_prepmove(lfs, 0x3ff, NULL);
		err = lfs_dir_commit(lfs, &oldcwd, LFS_MKATTRS({ LFS_MKTAG(LFS_TYPE_DELETE, newoldid, 0), NULL }));
		if (err != 0) {
			lfs->mlist = prevdir.next;
			return err;
		}
	}

	lfs->mlist = prevdir.next;
	if (prevtag != LFS_ERR_NOENT) {
		if (lfs_tag_type3(prevtag) == LFS_TYPE_DIR) {
			err = ph_lfs_dirRemoveFinalize(lfs, &newcwd, &prevdir);
			if (err != 0) {
				return err;
			}
		}

		ph_lfs_deleteObj(lfs, prevObj, false);
	}

	return 0;
}


int ph_lfs_link(lfs_t *lfs, id_t dir, const char *name, id_t source)
{
	if (source == LFS_ROOT_PHID) {
		return LFS_ERR_INVAL;
	}

	ph_lfs_lru_t *parentObj;
	int err = ph_lfs_openObj(lfs, dir, &parentObj, true, EXTRAS_TYPE_DIR);
	if (err != 0) {
		return err;
	}

	ph_lfs_lru_t *sourceObj;
	err = ph_lfs_getObj(lfs, source, false, &sourceObj);
	if (err != 0) {
		return err;
	}

	err = ph_lfs_rawrename(lfs, parentObj, name, sourceObj);
	if (lfs->cfg->ph.useMTime != 0) {
		(void)ph_lfs_setSimpleAttr(lfs, parentObj, time(NULL), LFS_TYPE_PH_ATTR_MTIME, sizeof(ph_lfs_time_t));
	}

	ph_lfs_closeObj(lfs, parentObj, false);
	return err;
}


static int ph_lfs_removeObject(lfs_t *lfs, ph_lfs_lru_t *obj, lfs_mdir_t *cwd, uint16_t id, bool isDir)
{
	int err;
	struct lfs_mlist dir;
	dir.next = lfs->mlist;
	if (isDir) {
		err = ph_lfs_dirRemovePrepare(lfs, cwd, id, &dir);
		if (err != 0) {
			return err;
		}
	}

	err = lfs_dir_commit(lfs, cwd, LFS_MKATTRS({ LFS_MKTAG(LFS_TYPE_DELETE, id, 0), NULL }));
	lfs->mlist = dir.next;
	if (err != 0) {
		return err;
	}

	ph_lfs_deleteObj(lfs, obj, false);

	if (isDir) {
		err = ph_lfs_dirRemoveFinalize(lfs, cwd, &dir);
		if (err != 0) {
			return err;
		}
	}

	return 0;
}


static int ph_lfs_rawremove(lfs_t *lfs, ph_lfs_lru_t *parentObj, const char *name)
{
	LFS_ASSERT(parentObj != NULL && parentObj->extrasType == EXTRAS_TYPE_DIR && parentObj->extras != NULL);
	lfs_dir_t *parentDir = (lfs_dir_t *)parentObj->extras;
	lfs_mdir_t cwd;
	cwd.tail[0] = parentDir->head[0];
	cwd.tail[1] = parentDir->head[1];
	const lfs_stag_t tag = ph_lfs_dir_find(lfs, &cwd, &name, NULL, NULL);
	const uint16_t id = lfs_tag_id(tag);
	const bool isDir = lfs_tag_type3(tag) == LFS_TYPE_DIR;
	if ((tag < 0) || (id == 0x3ff)) {
		return (tag < 0) ? (int)tag : LFS_ERR_INVAL;
	}

	ph_lfs_lru_t *obj = ph_lfs_getLRUByFile(lfs, &cwd, lfs_tag_id(tag));
	LFS_ASSERT((obj == NULL) || (id == obj->id));
	return ph_lfs_removeObject(lfs, obj, &cwd, id, isDir);
}


int ph_lfs_destroy(lfs_t *lfs, id_t phId)
{
	ph_lfs_lru_t *obj;
	int err = ph_lfs_getObj(lfs, phId, false, &obj);
	if (err != 0) {
		return err;
	}

	lfs_mdir_t cwd;
	err = lfs_dir_fetch(lfs, &cwd, obj->parentBlock);
	if (err != 0) {
		return err;
	}

	/* TODO: modification time should be set on parent directory here,
	 * but finding the parent may require scanning the entire filesystem */
	return ph_lfs_removeObject(lfs, obj, &cwd, obj->id, ph_lfs_objIsDir(obj));
}


int ph_lfs_unlink(lfs_t *lfs, id_t dir, const char *name)
{
	ph_lfs_lru_t *parentObj;
	int err = ph_lfs_openObj(lfs, dir, &parentObj, true, EXTRAS_TYPE_DIR);
	if (err < 0) {
		return err;
	}

	err = ph_lfs_rawremove(lfs, parentObj, name);
	if (lfs->cfg->ph.useMTime != 0) {
		(void)ph_lfs_setSimpleAttr(lfs, parentObj, time(NULL), LFS_TYPE_PH_ATTR_MTIME, sizeof(ph_lfs_time_t));
	}

	ph_lfs_closeObj(lfs, parentObj, false);
	return err;
}


int ph_lfs_statfs(lfs_t *lfs, struct statvfs *st)
{
	st->f_bsize = lfs->cfg->block_size;
	st->f_frsize = lfs->cfg->block_size;
	st->f_blocks = lfs->block_count;
	lfs_ssize_t blocksInUse = lfs_fs_rawsize(lfs);
	if ((blocksInUse < 0) || (blocksInUse > lfs->block_count)) {
		blocksInUse = lfs->block_count;
	}

	st->f_bfree = lfs->block_count - blocksInUse;
	st->f_bavail = st->f_bfree;
	st->f_files = 0;
	st->f_ffree = 0;
	st->f_favail = 0;
	/* TODO: if this is necessary, some sort of random ID should be stored when formatting */
	st->f_fsid = 0x1234;
	st->f_flag = 0;
	st->f_namemax = lfs->name_max;
	return 0;
}


int ph_lfs_unmount(lfs_t *lfs)
{
	while (lfs->phLfsObjects != NULL) {
		(void)ph_lfs_closeObj(lfs, (ph_lfs_lru_t *)lfs->phLfsObjects, true);
	}

	return lfs_rawunmount(lfs);
}


static bool ph_lfs_updateId(uint16_t *id, const struct lfs_mattr *ops, int nOps)
{
	for (int i = 0; i < nOps; i++) {
		if (lfs_tag_type3(ops[i].tag) == LFS_TYPE_DELETE && *id == lfs_tag_id(ops[i].tag)) {
			TRACE_FIXUP("was deleted");
			return true;
		}
		else if (lfs_tag_type3(ops[i].tag) == LFS_TYPE_DELETE && *id > lfs_tag_id(ops[i].tag)) {
			TRACE_FIXUP("d %d -1 ", lfs_tag_id(ops[i].tag));
			*id -= 1;
		}
		else if (lfs_tag_type3(ops[i].tag) == LFS_TYPE_CREATE && *id >= lfs_tag_id(ops[i].tag)) {
			TRACE_FIXUP("c %d +1 ", lfs_tag_id(ops[i].tag));
			*id += 1;
		}
	}

	return false;
}


static int ph_lfs_objectFixupOnCommit(lfs_t *lfs, const lfs_mdir_t *dir, ph_lfs_lru_t *obj, const struct lfs_mattr *ops, int nOps)
{
	bool isOpenFile = obj->extrasType == EXTRAS_TYPE_FILE;
	lfs_file_t *f = isOpenFile ? (lfs_file_t *)obj->extras : NULL;

	TRACE_FIXUP("fixing obj %x %x %d (ph %" PRId32 ") ", obj->parentBlock[0], obj->parentBlock[1], obj->id, (uint32_t)obj->phId);
	bool wasDeleted;
	if ((obj->flags & PH_LRU_FLAG_CREAT) != 0) {
		/* If a file is being created or moved its ID already has the correct value.
		 * If it is being moved we also need to ignore its delete tag. */
		wasDeleted = false;
	}
	else {
		wasDeleted = ph_lfs_updateId(&obj->id, ops, nOps);
	}

	TRACE_FIXUP("\n");
	if (wasDeleted) {
		obj->parentBlock[0] = LFS_BLOCK_NULL;
		obj->parentBlock[1] = LFS_BLOCK_NULL;
		if (f != NULL) {
			f->common.m.pair[0] = LFS_BLOCK_NULL;
			f->common.m.pair[1] = LFS_BLOCK_NULL;
		}

		return 0;
	}
	else {
		obj->parentBlock[0] = dir->pair[0];
		obj->parentBlock[1] = dir->pair[1];
	}

	lfs_mdir_t nextdir;
	int iters = 0;
	while ((obj->id >= dir->count) && dir->split) {
		iters++;
		/* we split and id is on tail now */
		obj->id -= dir->count;
		obj->parentBlock[0] = dir->tail[0];
		obj->parentBlock[1] = dir->tail[1];
		TRACE_FIXUP("next dir: (%x %x) %d\n", obj->parentBlock[0], obj->parentBlock[1], (uint32_t)obj->id);
		int err = lfs_dir_fetch(lfs, &nextdir, dir->tail);
		if (err != 0) {
			return err;
		}

		/* This only has an effect on first iteration */
		dir = &nextdir;
	}

	if (f != NULL) {
		f->common.id = obj->id;
		f->common.m = *dir;
	}

	if (iters > 1) {
		/* In littlefs test suite the loop only ever performs up to one iteration
		 * A possible optimization would be to change the loop into an if statement
		 * The call to lfs_dir_fetch() would also be unnecessary if not isOpenFile */
		TRACE("If this happens often it could be bad for performance");
	}

	return 0;
}


static int ph_lfs_dirFixupOnCommit(lfs_t *lfs, const lfs_mdir_t *dir, lfs_dir_t *obj, const struct lfs_mattr *ops, int nOps)
{
	obj->common.m = *dir;
	uint16_t previousId = obj->common.id;
	bool wasDeleted = ph_lfs_updateId(&obj->common.id, ops, nOps);
	if (wasDeleted) {
		obj->common.m.pair[0] = LFS_BLOCK_NULL;
		obj->common.m.pair[1] = LFS_BLOCK_NULL;
		return 0;
	}

	/* Apply the same change to pos as we did to ID */
	obj->pos += (int)obj->common.id - (int)previousId;

	int iters = 0;
	while ((obj->common.id >= dir->count) && dir->split) {
		iters++;
		/* we split and id is on tail now */
		obj->common.id -= dir->count;
		int err = lfs_dir_fetch(lfs, &obj->common.m, dir->tail);
		if (err != 0) {
			return err;
		}
	}

	if (iters > 1) {
		/* In littlefs test suite the loop only ever performs up to one iteration
		 * A possible optimization would be to change the loop into an if statement
		 * The call to lfs_dir_fetch() would also be unnecessary if not isOpenFile */
		TRACE("If this happens often it could be bad for performance");
	}

	return 0;
}


int ph_lfs_updateOnCommit(lfs_t *lfs, const lfs_block_t oldpair[2], const lfs_mdir_t *dir, const struct lfs_mattr *ops, int nOps)
{
	/* Optimization - shorten table to the last operation that can change file ID */
	int nOpsChanging = 0;
	for (int i = 0; i < nOps; i++) {
		if (lfs_tag_type3(ops[i].tag) == LFS_TYPE_DELETE || lfs_tag_type3(ops[i].tag) == LFS_TYPE_CREATE) {
			nOpsChanging = i + 1;
		}
	}

	if (lfs->phLfsObjects != NULL) {
		ph_lfs_lru_t *obj = lfs->phLfsObjects;
		do {
			obj = obj->next;
			if (lfs_pair_cmp(obj->parentBlock, oldpair) == 0) {
				int err = ph_lfs_objectFixupOnCommit(lfs, dir, obj, ops, nOpsChanging);
				if (err != 0) {
					return err;
				}
			}
		} while (obj != lfs->phLfsObjects);
	}

	for (lfs_dir_t *obj = lfs->openDirs; obj != NULL; obj = obj->nextDir) {
		if (lfs_pair_cmp(obj->common.m.pair, oldpair) == 0) {
			int err = ph_lfs_dirFixupOnCommit(lfs, dir, obj, ops, nOpsChanging);
			if (err != 0) {
				return err;
			}
		}
	}

	return 0;
}


void ph_lfs_updateOnRelocate(lfs_t *lfs, const lfs_block_t oldPair[2], const lfs_block_t newPair[2])
{
	if (lfs->phLfsObjects != NULL) {
		ph_lfs_lru_t *obj = lfs->phLfsObjects;
		do {
			obj = obj->next;
			if (lfs_pair_cmp(obj->parentBlock, oldPair) == 0) {
				TRACE_FIXUP("relocating %" PRId32 "\n", (uint32_t)obj->phId);
				obj->parentBlock[0] = newPair[0];
				obj->parentBlock[1] = newPair[1];
				if (obj->extrasType == EXTRAS_TYPE_FILE) {
					lfs_file_t *f = (lfs_file_t *)obj->extras;
					f->common.m.pair[0] = newPair[0];
					f->common.m.pair[1] = newPair[1];
				}
			}
		} while (obj != lfs->phLfsObjects);
	}

	for (lfs_dir_t *dir = lfs->openDirs; dir != NULL; dir = dir->nextDir) {
		if (lfs_pair_cmp(dir->common.m.pair, oldPair) == 0) {
			dir->common.m.pair[0] = newPair[0];
			dir->common.m.pair[1] = newPair[1];
		}

		if (lfs_pair_cmp(dir->head, oldPair) == 0) {
			dir->head[0] = newPair[0];
			dir->head[1] = newPair[1];
		}
	}
}


static int ph_lfs_evictLargeInline(lfs_t *lfs, lfs_file_t *file)
{
	if (((file->flags & LFS_F_INLINE) == 0) || (file->ctz.size <= lfs->cfg->cache_size)) {
		return 0;
	}

	int err = lfs_file_outline(lfs, file);
	if (err != 0) {
		return err;
	}

	return lfs_file_flush(lfs, file);
}


int ph_lfs_evictInlines(lfs_t *lfs, const lfs_block_t pair[2])
{
	if (!lfs->largeInlineOpened || (lfs->phLfsObjects == NULL)) {
		return 0;
	}

	ph_lfs_lru_t *obj = lfs->phLfsObjects;
	do {
		obj = obj->next;
		if ((obj->extrasType == EXTRAS_TYPE_FILE) && (lfs_pair_cmp(obj->parentBlock, pair) == 0)) {
			lfs_file_t *file = (lfs_file_t *)obj->extras;
			int err = ph_lfs_evictLargeInline(lfs, file);
			if (err != 0) {
				return err;
			}
		}
	} while (obj != lfs->phLfsObjects);

	lfs->largeInlineOpened = false;
	return 0;
}


int ph_lfs_traverseOpenFiles(lfs_t *lfs, int (*cb)(void *data, lfs_block_t block), void *data)
{
	if (lfs->phLfsObjects == NULL) {
		return 0;
	}

	ph_lfs_lru_t *obj = lfs->phLfsObjects;
	do {
		obj = obj->next;
		lfs_file_t *f = (lfs_file_t *)obj->extras;
		if ((obj->extrasType != EXTRAS_TYPE_FILE) || ((f->flags & LFS_F_INLINE) != 0)) {
			continue;
		}

		if ((f->flags & LFS_F_DIRTY) != 0) {
			int err = lfs_ctz_traverse(lfs, &f->cache, &lfs->rcache, f->ctz.head, f->ctz.size, cb, data);
			if (err != 0) {
				return err;
			}
		}

		if ((f->flags & LFS_F_WRITING) != 0) {
			int err = lfs_ctz_traverse(lfs, &f->cache, &lfs->rcache, f->block, f->pos, cb, data);
			if (err != 0) {
				return err;
			}
		}
	} while (obj != lfs->phLfsObjects);

	return 0;
}
