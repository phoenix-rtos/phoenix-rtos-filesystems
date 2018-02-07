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
        printf("mbr: invalid mbr size %s\n", "");
        goto error;
    }

    if (mbr->boot_sign != MBR_SIGNATURE) {
        printf("mbr: invalid mbr singature %x\n", mbr->boot_sign);
        goto error;
    }

    return mbr;

error:
    free(mbr);
    return NULL;
}
