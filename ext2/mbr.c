/*
 * Phoenix-RTOS
 *
 * ext2
 *
 * mbr.c
 *
 * Copyright 2017 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/msg.h>

#include "pc-ata.h"
#include "mbr.h"


struct mbr_t *read_mbr(void)
{
	int ret = 0;
	struct mbr_t *mbr = malloc(sizeof(struct mbr_t));

	ret = ata_read(0, (char *)mbr, sizeof(struct mbr_t));
	if (ret != sizeof(struct mbr_t)) {
		printf("ext2: invalid mbr size %s\n", "");
		free(mbr);
		return NULL;
	}

	if (mbr->boot_sign != MBR_SIGNATURE) {
		printf("ext2: invalid mbr singature %x\n", mbr->boot_sign);
		free(mbr);
		return NULL;
	}

	return mbr;
}
