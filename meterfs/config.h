/*
 * Phoenix-RTOS
 *
 * Meterfs configuration
 *
 * Copyright 2018 Phoenix Systems
 * Author: Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _METERFS_CONFIG_H_
#define _METERFS_CONFIG_H_

#include "spi.h"

#ifndef PWEN_PORT
#define PWEN_PORT gpioa
#endif

#ifndef PWEN_PIN
#define PWEN_PIN 4
#endif

#ifndef PWEN_POL
#define PWEN_POL 1
#endif

#ifndef INACTIVE_POL
#define INACTIVE_POL PWEN_POL
#endif

#ifndef CS_PORT
#define CS_PORT gpioe
#endif

#ifndef CS_PIN
#define CS_PIN 12
#endif

#ifndef SCK_PORT
#define SCK_PORT gpioe
#endif

#ifndef SCK_PIN
#define SCK_PIN 13
#endif

#ifndef MISO_PORT
#define MISO_PORT gpioe
#endif

#ifndef MISO_PIN
#define MISO_PIN 14
#endif

#ifndef MOSI_PORT
#define MOSI_PORT gpioe
#endif

#ifndef MOSI_PIN
#define MOSI_PIN 15
#endif

#endif
