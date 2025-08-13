#
# Makefile for Phoenix-RTOS rofs (ReadOnly FS)
#
# Copyright 2024 Phoenix Systems
#


NAME := librofs
LOCAL_SRCS := rofs.c
LOCAL_HEADERS := rofs.h

include $(static-lib.mk)

NAME := rofs
LOCAL_SRCS := srv.c
LIBS := librofs libtinyaes

include $(binary.mk)
