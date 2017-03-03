/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */

#include "nodelist.h"


unsigned int full_name_hash(const char * name, unsigned int len)
{
	unsigned hash = 0;

	while (len--) {
		hash = (hash << 4) | (hash >> 28);
		hash ^= *(name++);
	}
	return hash;
}


static int calculate_inocache_hashsize(uint32_t flash_size)
{
	/*
	 * Pick a inocache hash size based on the size of the medium.
	 * Count how many megabytes we're dealing with, apply a hashsize twice
	 * that size, but rounding down to the usual big powers of 2. And keep
	 * to sensible bounds.
	 */

	int size_mb = flash_size / 1024 / 1024;
	int hashsize = (size_mb * 2) & ~0x3f;

	if (hashsize < INOCACHE_HASHSIZE_MIN)
		return INOCACHE_HASHSIZE_MIN;
	if (hashsize > INOCACHE_HASHSIZE_MAX)
		return INOCACHE_HASHSIZE_MAX;

	return hashsize;
}


int jffs2_flash_setup(struct jffs2_sb_info *c)
{
	int ret = 0;

	if (jffs2_cleanmarker_oob(c)) {
		/* NAND flash... do setup accordingly */
		ret = jffs2_nand_flash_setup(c);
		if (ret)
			return ret;
	}       

	/* and Dataflash */
	if (jffs2_dataflash(c)) {
		ret = jffs2_dataflash_setup(c);
		if (ret)
			return ret;
	}

	/* and Intel "Sibley" flash */
	if (jffs2_nor_wbuf_flash(c)) {
		ret = jffs2_nor_wbuf_flash_setup(c);
		if (ret)
			return ret;
	}

	return ret;
}


void jffs2_flash_cleanup(struct jffs2_sb_info *c)
{

	if (jffs2_cleanmarker_oob(c)) {
		jffs2_nand_flash_cleanup(c);
	}

	/* and DataFlash */
	if (jffs2_dataflash(c)) {
		jffs2_dataflash_cleanup(c);
	}

	/* and Intel "Sibley" flash */
	if (jffs2_nor_wbuf_flash(c)) {
		jffs2_nor_wbuf_flash_cleanup(c);
	}
}


int jffs2_init_sb_info(struct jffs2_sb_info *c, struct jffs2_mount_opts *mo)
{
	size_t blocks;

	c->mount_opts = *mo;
	c->cleanmarker_size = sizeof(struct jffs2_unknown_node);

	blocks = c->flash_size / c->sector_size;

	/*
	 * Size alignment check
	 */
	if ((c->sector_size * blocks) != c->flash_size) {
		c->flash_size = c->sector_size * blocks;
		pr_info("Flash size not aligned to erasesize, reducing to %dKiB\n",
			c->flash_size / 1024);
	}

	if (c->flash_size < 5*c->sector_size) {
		pr_err("Too few erase blocks (%d)\n", c->flash_size / c->sector_size);
		return -EINVAL;
	}

	c->inocache_hashsize = calculate_inocache_hashsize(c->flash_size);
	c->inocache_list = kzalloc(c->inocache_hashsize * sizeof(struct jffs2_inode_cache *), PG_KERNEL);
	if (!c->inocache_list)
		return -ENOMEM;

	return 0;
}


int jffs2_dir_is_empty(struct jffs2_inode_info *d)
{
	struct jffs2_full_dirent *fd;

	for (fd = d->dents; fd; fd = fd->next)
		if (fd->ino)
			return 0;
	return 1;
}


uint32_t jffs2_dir_get_ino(struct jffs2_inode_info *d, const char *name, int namelen)
{
	uint32_t hash = full_name_hash(name, namelen);
	struct jffs2_full_dirent *fd = NULL, *fd_list;
	uint32_t ino = 0;

	/* NB: The 2.2 backport will need to explicitly check for '.' and '..' here */
	for (fd_list = d->dents; fd_list && fd_list->nhash <= hash; fd_list = fd_list->next) {
		if (fd_list->nhash == hash && 
		    (!fd || fd_list->version > fd->version) &&
		    strlen((char *)fd_list->name) == namelen &&
		    !strncmp((char *)fd_list->name, (char *)name, namelen)) {
			fd = fd_list;
		}
	}
	if (fd)
		ino = fd->ino;

	return ino;
}


struct jffs2_inode_info *jffs2_gc_fetch_inode(struct jffs2_sb_info *c,
					      uint32_t inum, int unlinked)
{
	os_inode_t inode;
	struct jffs2_inode_cache *ic;

	if (unlinked) {
		/* The inode has zero nlink but its nodes weren't yet marked
		   obsolete. This has to be because we're still waiting for
		   the final (close() and) iput() to happen.

		   There's a possibility that the final iput() could have
		   happened while we were contemplating. In order to ensure
		   that we don't cause a new read_inode() (which would fail)
		   for the inode in question, we use ilookup() in this case
		   instead of iget().

		   The nlink can't _become_ zero at this point because we're
		   holding the alloc_sem, and jffs2_do_unlink() would also
		   need that while decrementing nlink on any inode.
		*/
		inode = jffs2_ilookup(OFNI_BS_2SFFJ(c), inum);
		if (!inode) {
			jffs2_dbg(1, "ilookup() failed for ino #%u; inode is probably deleted.\n",
				  inum);

			spin_lock(&c->inocache_lock);
			ic = jffs2_get_ino_cache(c, inum);
			if (!ic) {
				jffs2_dbg(1, "Inode cache for ino #%u is gone\n",
					  inum);
				spin_unlock(&c->inocache_lock);
				return NULL;
			}
			if (ic->state != INO_STATE_CHECKEDABSENT) {
				/* Wait for progress. Don't just loop */
				jffs2_dbg(1, "Waiting for ino #%u in state %d\n",
					  ic->ino, ic->state);
				sleep_on_spinunlock(&c->inocache_wq, &c->inocache_lock);
			} else {
				spin_unlock(&c->inocache_lock);
			}

			return NULL;
		}
	} else {
		/* Inode has links to it still; they're not going away because
		   jffs2_do_unlink() would need the alloc_sem and we have it.
		   Just iget() it, and if read_inode() is necessary that's OK.
		*/
		inode = jffs2_iget(OFNI_BS_2SFFJ(c), inum);
		if (IS_ERR(inode))
			return ERR_CAST(inode);
	}

	return JFFS2_INODE_INFO(inode);
}


char *jffs2_gc_fetch_page(struct jffs2_sb_info *c,
				   struct jffs2_inode_info *f,
				   unsigned long offset,
				   unsigned long *priv)
{
	char *ret;

	if((ret = malloc(PAGE_CACHE_SIZE)) == NULL)
		return NULL;
	if(jffs2_read_inode_range(c, f, ret, offset & ~(PAGE_CACHE_SIZE-1), PAGE_CACHE_SIZE) != EOK)
		return NULL;
	return ret;
}

void jffs2_gc_release_page(struct jffs2_sb_info *c,
			   char *ptr,
			   unsigned long *priv)
{
	free(ptr);
}
