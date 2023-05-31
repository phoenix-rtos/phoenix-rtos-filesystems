#
# Makefile for Phoenix-RTOS jffs2 port
#
# Copyright 2018, 2019 Phoenix Systems
#

NAME := libjffs2
LOCAL_HEADERS_DIR := include

# disable some warnings for jffs2 code
LOCAL_CFLAGS := -Wno-pointer-sign -Wno-discarded-qualifiers -Wno-unused-value -Wno-strict-prototypes

# enable desired functions: NAND writebuffer and summary nodes
LOCAL_CFLAGS += -DCONFIG_JFFS2_CMODE_NONE -DCONFIG_JFFS2_FS_WRITEBUFFER -DCONFIG_JFFS2_SUMMARY

# linux base jffs2 files + writebuffer for nand support + summary for faster mount times
LOCAL_SRCS := compr.c dir.c file.c ioctl.c nodelist.c malloc.c read.c nodemgmt.c \
	readinode.c write.c scan.c gc.c symlink.c build.c erase.c \
	background.c fs.c writev.c super.c debug.c wbuf.c summary.c

# phoenix-rtos specific files
LOCAL_SRCS += phoenix-rtos/fs.c phoenix-rtos/completion.c phoenix-rtos/slab.c \
	phoenix-rtos/dev.c phoenix-rtos/crc32.c phoenix-rtos/rb.c phoenix-rtos/dentry.c \
	phoenix-rtos/wait.c phoenix-rtos/phoenix-rtos.c phoenix-rtos/locks.c libjffs2.c \
	phoenix-rtos/object.c

ifeq ($(MTD_EM), 1)
  LOCAL_SRCS += phoenix-rtos/mtd_em.c
else
  LOCAL_SRCS += phoenix-rtos/mtd.c
endif

# rest of additional jffs2 options (not supported)
ifeq ($(CONFIG_JFFS2_FS_XATTR), 1)
  LOCAL_SRCS += xattr.c xattr_trusted.c xattr_user.c
endif

ifeq ($(CONFIG_JFFS2_FS_SECURITY), 1)
  LOCAL_SRCS += security.c
endif

ifeq ($(CONFIG_JFFS2_FS_POSIX_ACL), 1)
  LOCAL_SRCS += acl.c
endif

ifeq ($(CONFIG_JFFS2_RUBIN), 1)
  LOCAL_SRCS += compr_rubin.c
endif

ifeq ($(CONFIG_JFFS2_RTIME), 1)
  LOCAL_SRCS += compr_rtime.c
endif

ifeq ($(CONFIG_JFFS2_ZLIB), 1)
  LOCAL_SRCS += compr_zlib.c
endif

ifeq ($(CONFIG_JFFS2_LZO), 1)
  LOCAL_SRCS += compr_lzo.c
endif

include $(static-lib.mk)
