#
# Makefile for Phoenix-RTOS dummyfs
#
# Copyright 2017, 2018 Phoenix Systems
#

LOCAL_PATH := $(call my-dir)

NAME := libdummyfs
SRCS := $(filter-out $(LOCAL_PATH)srv.c, $(wildcard $(LOCAL_PATH)*.c))
HEADERS := $(LOCAL_PATH)dummyfs.h
include $(static-lib.mk)

NAME := dummyfs
SRCS := $(LOCAL_PATH)srv.c
LIBS := libdummyfs
include $(binary.mk)
