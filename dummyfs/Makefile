#
# Makefile for Phoenix-RTOS dummyfs
#
# Copyright 2017, 2018 Phoenix Systems
#

LOCAL_PATH := $(call my-dir)
DUMMYFS_SRCS := $(wildcard $(LOCAL_PATH)*.c)

NAME := dummyfs
SRCS := $(DUMMYFS_SRCS)
include $(binary.mk)

NAME := libdummyfs
SRCS := $(filter-out $(LOCAL_PATH)srv.c, $(DUMMYFS_SRCS))
HEADERS := $(LOCAL_PATH)dummyfs.h
include $(static-lib.mk)
