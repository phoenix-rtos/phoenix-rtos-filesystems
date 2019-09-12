/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Jffs2 FileSystem - system specific information.
 *
 * Copyright 2018 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include "../phoenix-rtos.h"

#include "mtd.h"


#define MTD_PAGE_SIZE 4096
#define MTD_BLOCK_SIZE (64 * MTD_PAGE_SIZE)

#define ECC_BITFLIP_THRESHOLD 4

static int mtd_read_err(int ret, flashdrv_meta_t *meta, int status)
{
	int max_bitflip = 0;
	int status_blk0 = (ret >> 8) & 0xff;
	int i;
	/* check current read status. if it is EBADMSG we have nothing to do here since
	 * read is marked as uncorrectable */
	if (status == -EBADMSG)
		return status;

	/* block 0 is erased... */
	if (status_blk0 == 0xff) {
		/* but we still need to count errors */
		if (ret & 0xc) {
			for (i = 0; i < sizeof(meta->errors)/sizeof(meta->errors[0]); i++)
				max_bitflip = max(max_bitflip, (int)meta->errors[i]);
		}
		return max_bitflip > ECC_BITFLIP_THRESHOLD ? -EBADMSG : status;
	}
	/* uncorrectable errors */
	else if (status_blk0 == 0xfe || (ret & 0x4)) {
		return -EBADMSG;
	}
	/* correctable errors */
	else if ((status_blk0 & 0x7) || (ret & 0x8)) {
		/* count them and return maximum */
		if (status_blk0 & 0x7)
			max_bitflip = max(max_bitflip, status_blk0);

		for (i = 0; i < sizeof(meta->errors)/sizeof(meta->errors[0]); i++)
			max_bitflip = max(max_bitflip, (int)meta->errors[i]);

		return max_bitflip >= ECC_BITFLIP_THRESHOLD ? -EUCLEAN : status;
	}
	/* no error */
	return status;
}


int mtd_read(struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen,
			     u_char *buf)
{
	int ret, err = 0;
	*retlen = 0;

	if (!len)
		return 0;

	mutexLock(mtd->lock);
	if (from > mtd->size || len < 0 || from + len > mtd->size) {
		printf("mtd_read: invalid read offset: 0x%llx - 0x%llx max offset: 0x%llx", from, from + len, mtd->size);
		BUG();
	}

	if (from % mtd->writesize) {
		ret = flashdrv_read(mtd->dma, (from / mtd->writesize) + mtd->start, mtd->data_buf, mtd->meta_buf);
		err = mtd_read_err(ret, mtd->meta_buf, err);


		*retlen += mtd->writesize - (from % mtd->writesize);
		if (*retlen > len)
			*retlen = len;

		memcpy(buf, mtd->data_buf + (from % mtd->writesize), *retlen);
		len -= *retlen;
		from += *retlen;
	}

	if (!len) {
		mutexUnlock(mtd->lock);
		return err;
	}

	while (len >= mtd->writesize) {
		ret = flashdrv_read(mtd->dma, (from / mtd->writesize) + mtd->start, mtd->data_buf, mtd->meta_buf);
		err = mtd_read_err(ret, mtd->meta_buf, err);

		memcpy(buf + *retlen, mtd->data_buf, mtd->writesize);
		len -= mtd->writesize;
		*retlen += mtd->writesize;
		from += mtd->writesize;
	}

	if (len > 0) {
		ret = flashdrv_read(mtd->dma, (from / mtd->writesize) + mtd->start, mtd->data_buf, mtd->meta_buf);
		err = mtd_read_err(ret, mtd->meta_buf, err);

		memcpy(buf + *retlen, mtd->data_buf, len);
		*retlen += len;
	}
	mutexUnlock(mtd->lock);

	if (err == -EBADMSG)
		pr_err("mtd_read 0x%llx - 0x%llx uncorrectable flash error\n", from, from + len);

	return err;
}


int mtd_write(struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen,
		const u_char *buf)
{
	int ret, err = 0;
	flashdrv_meta_t *meta = mtd->meta_buf;
	*retlen = 0;

	if (!len)
		return 0;

	mutexLock(mtd->lock);
	if (to > mtd->size || len < 0 || to + len > mtd->size) {
		printf("mtd_write: invalid write offset: 0x%llx-0x%llx max offset: 0x%llx", to, to + len, mtd->size);
		BUG();
	}

	while (len) {
		memset(meta->errors, 0, sizeof(meta->errors));
		ret = flashdrv_read(mtd->dma, (to / mtd->writesize) + mtd->start, NULL, mtd->meta_buf);
		err = mtd_read_err(ret, mtd->meta_buf, err);
		if (err == -EBADMSG) {
			mutexUnlock(mtd->lock);
			return err;
		}

		memcpy(mtd->data_buf, buf + *retlen, mtd->writesize);

		if ((ret = flashdrv_write(mtd->dma, (to / mtd->writesize) + mtd->start, mtd->data_buf, mtd->meta_buf))) {
			mutexUnlock(mtd->lock);
			printf("mtd_write: Flash write error 0x%x\n to 0x%llx", ret, to);
			return -1;
		}

		len -= mtd->writesize;
		*retlen += mtd->writesize;
		to += mtd->writesize;
	}

	mutexUnlock(mtd->lock);
	return 0;
}


int mtd_writev(struct mtd_info *mtd, const struct kvec *vecs,
		unsigned long count, loff_t to, size_t *retlen)
{
	unsigned long i;
	size_t writelen = 0;
	int ret = 0;

	*retlen = 0;

	for (i = 0; i < count; i++) {
		ret = mtd_write(mtd, to, vecs[i].iov_len, &writelen, vecs[i].iov_base);
		*retlen = writelen;

		if (ret || writelen != vecs[i].iov_len)
			return ret;
		to += writelen;
	}

	return ret;
}


int mtd_read_oob(struct mtd_info *mtd, loff_t from, struct mtd_oob_ops *ops)
{

	int ret = 0, err = 0;
	flashdrv_meta_t *meta = mtd->meta_buf;

	while (ops->oobretlen < ops->ooblen) {
		memset(meta->errors, 0, sizeof(meta->errors));
		ret = flashdrv_read(mtd->dma, (from / mtd->writesize) + mtd->start, NULL, mtd->meta_buf);
		err = mtd_read_err(ret, mtd->meta_buf, err);

		memcpy(ops->oobbuf + ops->oobretlen, mtd->meta_buf, ops->ooblen > mtd->oobsize ? mtd->oobsize : ops->ooblen);
		ops->oobretlen += ops->ooblen > mtd->oobsize ? mtd->oobsize : ops->ooblen;
		from += mtd->writesize;
	}

	if (err == -EBADMSG)
		pr_err("mtd_read_oob: 0x%llx uncorrectable flash error\n", from);

	return err;
}


int mtd_write_oob(struct mtd_info *mtd, loff_t to, struct mtd_oob_ops *ops)
{

	int ret;

	if (ops->ooblen > mtd->oobsize) {
		printf("mtd_write_oob: Oob flash write len larger than oob size %d > %d\n", ops->ooblen, mtd->oobsize);
		return -1;
	}

	mutexLock(mtd->lock);

	memset(mtd->meta_buf, 0xff, sizeof(flashdrv_meta_t));
	memcpy(mtd->meta_buf, ops->oobbuf, ops->ooblen);

	if ((ret = flashdrv_write(mtd->dma, (to / mtd->writesize) + mtd->start, NULL, mtd->meta_buf))) {
		printf("mtd_write_oob: Oob flash write error 0x%x\n", ret);
		return -1;
	}
	mutexUnlock(mtd->lock);

	ops->oobretlen = ops->ooblen;
	return 0;
}


/* not supported in nand */
int mtd_point(struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen,
		void **virt, resource_size_t *phys)
{
	return -EOPNOTSUPP;
}


int mtd_unpoint(struct mtd_info *mtd, loff_t from, size_t len)
{
	return -EOPNOTSUPP;
}


int mtd_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	int ret;

	if (instr->len != mtd->erasesize || instr->addr % mtd->erasesize) {
		printf("mtd_erase: Invalid flash erase parametrs\n");
		return -1;
	}

	mutexLock(mtd->lock);
	if ((ret = flashdrv_erase(mtd->dma, (u32)(instr->addr / mtd->writesize) + mtd->start))) {
		printf("mtd_erase: Flash erase error 0x%d\n", ret);
		return -1;
	}

	mutexUnlock(mtd->lock);

	instr->state = MTD_ERASE_DONE;
	instr->callback(instr);

	return 0;
}


int mtd_block_markbad(struct mtd_info *mtd, loff_t ofs)
{
	mutexLock(mtd->lock);

	memset(mtd->data_buf, 0xff, mtd->writesize);
	memset(mtd->data_buf, 0, 2);

	if (flashdrv_writeraw(mtd->dma, (ofs / mtd->writesize) + mtd->start, mtd->data_buf, mtd->writesize))
		return -1;
	mutexUnlock(mtd->lock);

	return 0;
}


void *mtd_kmalloc_up_to(const struct mtd_info *mtd, size_t *size)
{
	void *ret;

	if (*size < mtd->writesize)
		*size = mtd->writesize;

	while (*size > mtd->writesize) {
		ret = malloc(*size);

		if (ret != NULL)
			return ret;

		*size >>= 1;
		*size += mtd->writesize - 1;
		*size &= mtd->writesize - 1;
	}

	return malloc((size_t)size);
}


int mtd_block_isbad(struct mtd_info *mtd, loff_t ofs)
{

	int ret = 0;

	mutexLock(mtd->lock);

	if (flashdrv_readraw(mtd->dma, (ofs / mtd->writesize) + mtd->start, mtd->data_buf, mtd->writesize)) {
		mutexUnlock(mtd->lock);
		return -1;
	}

	if(!((char *)mtd->data_buf)[0])
		ret = 1;

	mutexUnlock(mtd->lock);

	return ret;
}


struct dentry *mount_mtd(struct file_system_type *fs_type, int flags,
		const char *dev_name, void *data,
		int (*fill_super)(struct super_block *, void *, int))
{
	struct mtd_info *mtd;
	flashdrv_dma_t *dma;
	jffs2_partition_t *p = (jffs2_partition_t *)data;

	mtd = malloc(sizeof(struct mtd_info));

	mtd->data_buf = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_UNCACHED, OID_NULL, 0);
	mtd->meta_buf = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_UNCACHED, OID_NULL, 0);

	if (mtd->data_buf == NULL || mtd->meta_buf == NULL) {
		free(mtd);
		printf("jffs2: Failed to map flash read/write buffers\n");
		return NULL;
	}

	dma = flashdrv_dmanew();

	if (dma == NULL)
		return NULL;

	mtd->name = "micron";
	mtd->dma = dma;
	mtd->type = MTD_NANDFLASH;
	mtd->erasesize = MTD_BLOCK_SIZE;
	mtd->writesize = MTD_PAGE_SIZE;
	mtd->flags = MTD_WRITEABLE;
	mtd->size = MTD_BLOCK_SIZE * p->size;
	mtd->oobsize = 16;
	mtd->oobavail = 16;
	mtd->start = p->start * 64;

	mutexCreate(&mtd->lock);

	struct super_block *sb = malloc(sizeof(struct super_block));
	sb->s_mtd = mtd;
	sb->s_part = p;
	sb->s_flags = p->flags;
	p->sb = sb;

	fill_super(sb, NULL, 0);

	return sb->s_root;
}


void kill_mtd_super(struct super_block *sb)
{
}
