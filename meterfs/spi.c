/*
 * Phoenix-RTOS
 *
 * Meterfs - STM32L1x SPI routines
 *
 * Copyright 2017, 2018 Phoenix Systems
 * Author: Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <sys/threads.h>
#include <sys/msg.h>
#include <sys/pwman.h>
#include <sys/interrupt.h>
#include <unistd.h>

#include "spi.h"
#include "flash.h"
#include "config.h"


extern oid_t multidrv;


extern msg_t gmsg;


static void gpio_pinSet(int port, int pin, int state)
{
	multi_i_t *iptr = (multi_i_t *)gmsg.i.raw;

	gmsg.type = mtDevCtl;
	gmsg.i.data = NULL;
	gmsg.i.size = 0;
	gmsg.o.data = NULL;
	gmsg.o.size = 0;

	iptr->type = gpio_set;
	iptr->gpio_set.port = port;
	iptr->gpio_set.mask = 1 << pin;
	iptr->gpio_set.state = !!state << pin;

	msgSend(multidrv.port, &gmsg);
}


static void gpio_pinConfig(int port, char pin, char mode, char af, char ospeed, char otype, char pupd)
{
	multi_i_t *iptr = (multi_i_t *)gmsg.i.raw;

	gmsg.type = mtDevCtl;
	gmsg.i.data = NULL;
	gmsg.i.size = 0;
	gmsg.o.data = NULL;
	gmsg.o.size = 0;

	iptr->type = gpio_def;
	iptr->gpio_def.port = port;
	iptr->gpio_def.pin = pin;
	iptr->gpio_def.mode = mode;
	iptr->gpio_def.af = af;
	iptr->gpio_def.ospeed = ospeed;
	iptr->gpio_def.otype = otype;
	iptr->gpio_def.pupd = pupd;

	msgSend(multidrv.port, &gmsg);
}


void spi_csControl(int state)
{
	gpio_pinSet(CS_PORT, CS_PIN, !state);
}


#if PWENPOL >= 0
void spi_powerCtrl(int state)
{
	static const int activeState = !!PWEN_POL;

	if (state) {
		keepidle(1);
		gpio_pinSet(PWEN_PORT, PWEN_PIN, PWEN_POL);
		gpio_pinSet(CS_PORT, CS_PIN, 1);

#ifdef WP_PORT
		gpio_pinSet(WP_PORT, WP_PIN, 1);
#endif

		gpio_pinConfig(SCK_PORT, SCK_PIN, 2, 5, 1, 0, 0);
		gpio_pinConfig(MISO_PORT, MISO_PIN, 2, 5, 1, 0, 0);
		gpio_pinConfig(MOSI_PORT, MOSI_PIN, 2, 5, 1, 0, 0);

		usleep(10000);
		flash_removeWP();
	}
	else {
		gpio_pinConfig(SCK_PORT, SCK_PIN, 1, 5, 1, 0, 0);
		gpio_pinConfig(MISO_PORT, MISO_PIN, 1, 5, 1, 0, 0);
		gpio_pinConfig(MOSI_PORT, MOSI_PIN, 1, 5, 1, 0, 0);

		gpio_pinSet(PWEN_PORT, PWEN_PIN, !PWEN_POL);
		gpio_pinSet(CS_PORT, CS_PIN, INACTIVE_POL);

	#ifdef WP_PORT
		gpio_pinSet(WP_PORT, WP_PIN, INACTIVE_POL);
	#endif
		keepidle(0);
	}
}
#else
void spi_powerCtrl(int state)
{
	keepidle(state);
}
#endif


void spi_read(unsigned char cmd, unsigned int addr, unsigned char flags, void *buff, size_t bufflen)
{
	multi_i_t *iptr = (multi_i_t *)gmsg.i.raw;

	spi_csControl(1);

	gmsg.type = mtDevCtl;
	gmsg.i.data = NULL;
	gmsg.i.size = 0;

	iptr->type = spi_get;
	iptr->spi_rw.spi = spi1;
	iptr->spi_rw.cmd = cmd;
	iptr->spi_rw.addr = addr;
	iptr->spi_rw.flags = flags;

	gmsg.o.data = buff;
	gmsg.o.size = bufflen;

	msgSend(multidrv.port, &gmsg);
	spi_csControl(0);
}


void spi_write(unsigned char cmd, unsigned int addr, unsigned char flags, const void *buff, size_t bufflen)
{
	multi_i_t *iptr = (multi_i_t *)gmsg.i.raw;

	spi_csControl(1);

	gmsg.type = mtDevCtl;
	gmsg.o.data = NULL;
	gmsg.o.size = 0;

	iptr->type = spi_set;
	iptr->spi_rw.spi = spi1;
	iptr->spi_rw.cmd = cmd;
	iptr->spi_rw.addr = addr;
	iptr->spi_rw.flags = flags;

	gmsg.i.data = (void *)buff;
	gmsg.i.size = bufflen;

	msgSend(multidrv.port, &gmsg);
	spi_csControl(0);
}


void spi_init(void)
{
	gpio_pinConfig(PWEN_PORT, PWEN_PIN, 1, 0, 1, 0, 0);
	gpio_pinConfig(CS_PORT, CS_PIN, 1, 0, 1, 0, 0);

	gpio_pinSet(SCK_PORT, SCK_PIN, INACTIVE_POL);
	gpio_pinSet(MISO_PORT, MISO_PIN, INACTIVE_POL);
	gpio_pinSet(MOSI_PORT, MOSI_PIN, INACTIVE_POL);

#ifdef WP_PORT
	gpio_pinConfig(WP_PORT, WP_PIN, 1, 0, 0, 0, 0);
#endif

	keepidle(1); /* spi_powerCtrl will do keepidle(0) */
	spi_powerCtrl(0);
}
