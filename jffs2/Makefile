#
# Makefile for Phoenix-RTOS imx6ull-uart driver
#
# Copyright 2018, 2019 Phoenix Systems
#

CFLAGS += -Wno-pointer-sign -Wno-discarded-qualifiers -Wno-unused-value -Wno-strict-prototypes\
	-DCONFIG_JFFS2_CMODE_NONE -DCONFIG_JFFS2_FS_WRITEBUFFER -DCONFIG_JFFS2_SUMMARY
	
# linux base jffs2 files + writebuffer for nand support + summary for faster mount times
JFFS2_OBJS := compr.o dir.o file.o ioctl.o nodelist.o malloc.o read.o nodemgmt.o \
	readinode.o write.o scan.o gc.o symlink.o build.o erase.o \
	background.o fs.o writev.o super.o debug.o wbuf.o summary.o

# phoenix-rtos specific files
JFFS2_OBJS += phoenix-rtos/fs.o phoenix-rtos/completion.o phoenix-rtos/slab.o \
	phoenix-rtos/dev.o phoenix-rtos/crc32.o phoenix-rtos/rb.o phoenix-rtos/dentry.o \
	phoenix-rtos/wait.o phoenix-rtos/phoenix-rtos.o phoenix-rtos/locks.o jffs2.o \
	phoenix-rtos/object.o

ifeq ($(MTD_EM), 1)
	JFFS2_OBJS += phoenix-rtos/mtd_em.o
else
	JFFS2_OBJS += phoenix-rtos/mtd.o
endif

# rest of additional jffs2 options (not supported)
ifdef $(CONFIG_JFFS2_FS_XATTR)
	JFFS2_OBJS += xattr.o xattr_trusted.o xattr_user.o
endif

ifdef $(CONFIG_JFFS2_FS_SECURITY)
	JFFS2_OBJS += security.o
endif

ifdef $(CONFIG_JFFS2_FS_POSIX_ACL)
	JFFS2_OBJS += acl.o
endif

ifdef $(CONFIG_JFFS2_RUBIN)
	JFFS2_OBJS += compr_rubin.o
endif

ifdef $(CONFIG_JFFS2_RTIME)
	JFFS2_OBJS += compr_rtime.o
endif

ifdef $(CONFIG_JFFS2_ZLIB)
	JFFS2_OBJS += compr_zlib.o
endif

ifdef $(CONFIG_JFFS2_LZO)
	JFFS2_OBJS += compr_lzo.o
endif	
	

$(PREFIX_A)libjffs2.a: $(addprefix $(PREFIX_O)jffs2/, $(JFFS2_OBJS))
	$(ARCH)

#INSTALL_HDRS := libjffs2.h

all: $(PREFIX_A)libjffs2.a
 