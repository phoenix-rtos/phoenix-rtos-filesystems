/* 
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Jffs2 FileSystem.
 *
 * Copyright 2014-2015 Phoenix Systems
 * Copyright 2005-2006 Pawel Pisarczyk
 * Author: Katarzyna Baranowska
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _FS_JFFS2_H_
#define _FS_JFFS2_H_


#define JFFS2_COMPR_MODE_MASK       0x01
#define JFFS2_COMPR_MODE_NONE       0x00
#define JFFS2_COMPR_MODE_PRIORITY   0x01
#define JFFS2_COMPR_MODE_SIZE       0x02
#define JFFS2_COMPR_MODE_FAVOURLZO  0x03
#define JFFS2_COMPR_MODE_FORCELZO   0x04
#define JFFS2_COMPR_MODE_FORCEZLIB  0x05
#define JFFS2_COMPR_MODE_DEFAULT    0x06

#define JFFS2_MODE_MASK             0x10
#define JFFS2_MODE_READONLY         0x10
#define JFFS2_MODE_WRITABLE         0x00


typedef struct _jffs2_opt_t {
	dev_t dev;
	offs_t partitionBegin;
	size_t partitionSize;
	unsigned int mode;

	/* The size of the reserved pool. The reserved pool is the JFFS2 flash
	 * space which may only be used by root cannot be used by the other
	 * users. This is implemented simply by means of not allowing the
	 * latter users to write to the file system if the amount if the
	 * available space is less then 'rp_size'. */
	unsigned int rpSize;

} jffs2_opt_t;


/* Function initializes and registers JFFS2 filesystem */
int jffs2_init(void);


/* Function fills jffs2 mount options with default data */
void jffs2_init_opts(jffs2_opt_t *);


#endif
