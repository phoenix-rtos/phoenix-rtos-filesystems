/*
 * Phoenix-RTOS
 *
 * Meterfs - SST25VF016B SPI flash routines
 *
 * Copyright 2017, 2018 Phoenix Systems
 * Author: Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/pwman.h>
#include "spi.h"


void (*flash_write)(unsigned int, void *, size_t);


static const unsigned char chips[3][3] = { { 0xbf, 0x25, 0x41 }, { 0x1f, 0x47, 0x01 }, { 0xc2, 0x20, 0x16 } };


void _flash_waitBusy(void)
{
	unsigned char status;
	unsigned int sleep = 1000;

	spi_read(cmd_rdsr, 0, 0, &status, 1);

	while (status & 1) {
		usleep(sleep);
		if (sleep < 100000)
			sleep <<= 1;

		spi_read(cmd_rdsr, 0, 0, &status, 1);
	}
}


void flash_chipErase(void)
{
	keepidle(1);
	spi_powerCtrl(1);
	spi_write(cmd_wren, 0, 0, NULL, 0);
	spi_write(cmd_chip_erase, 0, 0, NULL, 0);
	_flash_waitBusy();
	spi_write(cmd_wrdi, 0, 0, NULL, 0);
	spi_powerCtrl(0);
	keepidle(0);
}


void flash_eraseSector(unsigned int addr)
{
	keepidle(1);
	spi_powerCtrl(1);
	spi_write(cmd_wren, 0, 0, NULL, 0);
	spi_write(cmd_sector_erase, addr, spi_address, NULL, 0);
	_flash_waitBusy();
	spi_write(cmd_wrdi, 0, 0, NULL, 0);
	spi_powerCtrl(0);
	keepidle(0);
}


void flash_read(unsigned int addr, void *buff, size_t bufflen)
{
	keepidle(1);
	spi_powerCtrl(1);

	_flash_waitBusy();

	spi_read(cmd_read, addr, spi_address, buff, bufflen);

	spi_powerCtrl(0);
	keepidle(0);
}


void flash_writeSafe(unsigned int addr, void *buff, size_t bufflen)
{
	size_t i;

	keepidle(1);
	spi_powerCtrl(1);

	for (i = 0; i < bufflen; ++i) {
		spi_write(cmd_wren, 0, 0, NULL, 0);
		spi_write(cmd_write, addr + i, spi_address, buff + i, 1);
		_flash_waitBusy();
	}

	spi_powerCtrl(0);
	keepidle(0);
}


void flash_writeAAI(unsigned int addr, void *buff, size_t bufflen)
{
	size_t i = 0;

	keepidle(1);
	spi_powerCtrl(1);

	if (addr & 1) {
		spi_write(cmd_wren, 0, 0, NULL, 0);
		spi_write(cmd_write, addr, spi_address, buff, 1);
		_flash_waitBusy();
		++i;
	}

	if (bufflen - i >= 2) {
		spi_write(cmd_wren, 0, 0, NULL, 0);
		spi_write(cmd_aai_write, addr + i, spi_address, buff + i, 2);
		_flash_waitBusy();
		for (i += 2; i < bufflen - 1; i += 2) {
			spi_write(cmd_aai_write, 0, 0, buff + i, 2);
			_flash_waitBusy();
		}

		spi_write(cmd_wrdi, 0, 0, NULL, 0);
	}

	if (i < bufflen) {
		spi_write(cmd_wren, 0, 0, NULL, 0);
		spi_write(cmd_write, addr + i, spi_address, buff + i, 1);
	}

	_flash_waitBusy();
	spi_powerCtrl(0);
	keepidle(0);
}


void flash_writePage(unsigned int addr, void *buff, size_t bufflen)
{
	unsigned int chunk;

	keepidle(1);
	spi_powerCtrl(1);

	while (bufflen) {
		if ((chunk = 0x100 - (addr & 0xff)) > bufflen)
			chunk = bufflen;
		spi_write(cmd_wren, 0, 0, NULL, 0);
		spi_write(cmd_write, addr, spi_address, buff, chunk);

		bufflen -= chunk;
		addr += chunk;
		buff += chunk;
	}

	_flash_waitBusy();
	spi_powerCtrl(0);
	keepidle(0);
}


void flash_detect(size_t *flashsz, size_t *sectorsz)
{
	unsigned char jedec[3];

	keepidle(1);
	spi_powerCtrl(1);

	spi_read(cmd_jedecid, 0, 0, jedec, 3);

	spi_powerCtrl(0);
	keepidle(0);

	printf("meterfs: JEDEC ID 0x%02x 0x%02x 0x%02x\n", jedec[0], jedec[1], jedec[2]);

	if (memcmp(jedec, chips[0], 3) == 0) {
		printf("meterfs: Detected SST25VF016B\n");
		flash_write = flash_writeAAI;
		(*flashsz) = 2 * 1024 * 1024;
		(*sectorsz) = 4 * 1024;
	}
	else if (memcmp(jedec, chips[1], 3) == 0) {
		printf("meterfs: Detected AT25DF321A\n");
		flash_write = flash_writePage;
		(*flashsz) = 4 * 1024 * 1024;
		(*sectorsz) = 4 * 1024;
	}
	else if (memcmp(jedec, chips[2], 3) == 0) {
		printf("meterfs: Detected MX25L3206E\n");
		flash_write = flash_writePage;
		(*flashsz) = 4 * 1024 * 1024;
		(*sectorsz) = 4 * 1024;
	}
	else {
		printf("meterfs: Unknown flash memory\n");
		flash_write = flash_writeSafe;
		(*flashsz) = 2 * 1024 * 1024;
		(*sectorsz) = 4 * 1024;
	}

	printf("meterfs: Capacity %u KiB, sector %u\n", (*flashsz) / 1024, *sectorsz);
}


void flash_init(size_t *flashsz, size_t *sectorsz)
{
	/* Detect flash chip, write method, size and sector size */
	flash_detect(flashsz, sectorsz);
}
