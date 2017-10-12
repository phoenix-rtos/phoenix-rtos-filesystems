/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Meterfs
 *
 * Copyright 2017 Phoenix Systems
 * Author: Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <sys/threads.h>
#include <sys/msg.h>
#include "spi.h"
#include "flash.h"


struct {
	unsigned int port;
} meterfs_common;


int main(void)
{
	spi_init();
	flash_init(2 * 1024 * 1024, 4 * 1024);

	/* TODO */
	for (;;)
		usleep(10000 * 1000);
}

