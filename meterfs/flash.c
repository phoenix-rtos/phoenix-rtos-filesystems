/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Meterfs - SST25VF016B SPI flash routines
 *
 * Copyright 2017 Phoenix Systems
 * Author: Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */


#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include "spi.h"


struct {
	size_t sectorsz;
	size_t flashsz;
} flash_common;


static void flash_waitBusy(void)
{
	unsigned char status;

	do
		spi_transaction(cmd_rdsr, 0, spi_read, &status, 1);
	while (status & 1);
}


int flash_eraseSector(unsigned int sector)
{
	unsigned int addr;

	addr = sector * flash_common.sectorsz;

	if (addr > flash_common.flashsz)
		return -EINVAL;

	spi_transaction(cmd_wren, 0, 0, NULL, 0);
	spi_transaction(cmd_sector_erase, addr, spi_address, NULL, 0);
	usleep(25000);
	flash_waitBusy();
	spi_transaction(cmd_wrdi, 0, 0, NULL, 0);

	return EOK;
}


int flash_read(unsigned int addr, void *buff, size_t bufflen)
{
	if (addr > flash_common.flashsz || (addr + bufflen) > flash_common.flashsz || !bufflen || buff == NULL)
		return -EINVAL;

	flash_waitBusy();

	spi_transaction(cmd_read, addr, spi_read | spi_address, buff, bufflen);

	return EOK;
}


int flash_write(unsigned int addr, void *buff, size_t bufflen)
{
	size_t i = 0;

	if (addr > flash_common.flashsz || (addr + bufflen) > flash_common.flashsz || !bufflen || buff == NULL)
		return -EINVAL;

	spi_transaction(cmd_wren, 0, 0, NULL, 0);

	if (addr & 1) {
		spi_transaction(cmd_write, addr, spi_address, &((unsigned char *)buff)[0], 1);
		flash_waitBusy();
		spi_transaction(cmd_wrdi, 0, 0, NULL, 0);
		spi_transaction(cmd_wren, 0, 0, NULL, 0);
		++i;
	}

	if (bufflen - i >= 2) {
		spi_transaction(cmd_aai_write, addr + i, spi_address, &((unsigned char *)buff)[i], 2);
		for (i += 2; i < bufflen - 1; i += 2) {
			spi_transaction(cmd_aai_write, 0, 0, &((unsigned char *)buff)[i], 2);
			flash_waitBusy();
		}

		spi_transaction(cmd_wrdi, 0, 0, NULL, 0);
		spi_transaction(cmd_wren, 0, 0, NULL, 0);
	}

	if (i < bufflen) {
		spi_transaction(cmd_write, addr + i, spi_address, &((unsigned char *)buff)[i], 1);
	}

	spi_transaction(cmd_wrdi, 0, 0, NULL, 0);

	return EOK;
}


int flash_regionIsBlank(unsigned int addr, size_t len)
{
	size_t i, step;
	unsigned char tmp[32];

	for (i = 0; i < len; ) {
		step = ((len - i) > sizeof(tmp)) ? sizeof(tmp) : len - i;
		flash_read(addr + i, tmp, step);
		while (step--) {
			if (tmp[i++] != (unsigned char)0xff)
				return 0;
		}
	}

	return 1;
}


void flash_init(size_t flashsz, size_t sectorsz)
{
	unsigned char t;

	flash_common.flashsz = flashsz;
	flash_common.sectorsz = sectorsz;

	/* Remove write protection */
	spi_transaction(cmd_wren, 0, 0, NULL, 0);
	spi_transaction(cmd_ewsr, 0, 0, NULL, 0);
	spi_transaction(cmd_wrsr, 0, 0, &t, 1);
	spi_transaction(cmd_wrdi, 0, 0, NULL, 0);
}
