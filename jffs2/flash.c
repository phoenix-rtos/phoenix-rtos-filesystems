/* 
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Jffs2 FileSystem - functions for communication Jffs2 FileSystem with flash
 *
 * Copyright 2014-2015 Phoenix Systems
 * Author: Katarzyna Baranowska
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <fs/jffs2/nodelist.h>
#include <dev/storage/flash/mtd_if.h>


int jffs2_flash_direct_read(struct jffs2_sb_info *c, uint32_t ofs, uint32_t len, size_t *retlen, char *buf)
{
	int ret;
	os_priv_data *osPriv = c->os_priv;

	mtd_lock(MINOR(osPriv->dev));
	ret = _mtd_read(MINOR(osPriv->dev), ofs + osPriv->partitionBegin, (char *) buf, len, retlen);

	if (ret < 0)
		*retlen = 0;
	else
		*retlen = len;

	mtd_unlock(MINOR(osPriv->dev));
	return ret;
}


int jffs2_flash_direct_write(struct jffs2_sb_info *c, uint32_t ofs, uint32_t len, size_t *retlen, char *buf)
{
	int ret;
	os_priv_data *osPriv = c->os_priv;

	mtd_lock(MINOR(osPriv->dev));
	ret = _mtd_programWithWait(MINOR(osPriv->dev), ofs + osPriv->partitionBegin, (char *) buf, len, retlen);
	if (ret < 0)
		*retlen = 0;
	else
		*retlen = len;
	mtd_unlock(MINOR(osPriv->dev));
	return ret;
}


int jffs2_flash_direct_writev(struct jffs2_sb_info *c, struct kvec *vecs, uint32_t cnt, uint32_t flash_ofs, size_t *retlen)
{
	int vec, ret = EOK;
	os_priv_data *osPriv = c->os_priv;
	size_t tmpRetLen = 0;

	mtd_lock(MINOR(osPriv->dev));
	*retlen = 0;
	for (vec = 0; (vec < cnt) && (ret == EOK); vec++) {
		ret = _mtd_programWithWait(MINOR(osPriv->dev), flash_ofs + osPriv->partitionBegin, vecs[vec].iov_base, vecs[vec].iov_len, &tmpRetLen);
		*retlen += tmpRetLen;
		flash_ofs += vecs[vec].iov_len;
	}
	mtd_unlock(MINOR(osPriv->dev));
	return ret;
}


int jffs2_flash_erase(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb)
{
	int ret;
	os_priv_data *osPriv = c->os_priv;

	mtd_lock(MINOR(osPriv->dev));
	ret = _mtd_eraseUniformWithWait(MINOR(osPriv->dev), jeb->offset + osPriv->partitionBegin);
	mtd_unlock(MINOR(osPriv->dev));
	return ret;
}
