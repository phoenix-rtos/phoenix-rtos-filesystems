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

#include "../os-phoenix.h"
#include "mtd.h"

int mtd_read(struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen,
			     u_char *buf)
{
	return 0;
}

int mtd_write(struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen,
	      const u_char *buf)
{
	return 0;
}


int mtd_writev(struct mtd_info *mtd, const struct kvec *vecs,
	       unsigned long count, loff_t to, size_t *retlen)
{
	return 0;
}

int mtd_read_oob(struct mtd_info *mtd, loff_t from, struct mtd_oob_ops *ops)
{
	return 0;
}

int mtd_write_oob(struct mtd_info *mtd, loff_t to, struct mtd_oob_ops *ops)
{
	return 0;
}

int mtd_point(struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen,
			      void **virt, resource_size_t *phys)
{
	return 0;
}

int mtd_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	return 0;
}

int mtd_block_markbad(struct mtd_info *mtd, loff_t ofs)
{
	return 0;
}

int mtd_unpoint(struct mtd_info *mtd, loff_t from, size_t len)
{
	return 0;
}

void *mtd_kmalloc_up_to(const struct mtd_info *mtd, size_t *size)
{
	return NULL;
}

int mtd_block_isbad(struct mtd_info *mtd, loff_t ofs)
{
	return 0;
}
