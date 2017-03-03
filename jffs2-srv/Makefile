#
# Makefile for Phoenix-RTOS kernel (fs/jffs2)
#
# Copyright 2012-2015 Phoenix Systems
#

SRCS = readinode.c nodemgmt.c debug.c malloc.c nodelist.c build.c scan.c compr.c read.c write.c gc.c erase.c os.c jffs2.c flash.c

#ifdef CONFIG_JFFS2_FS_WRITEBUFFER
#	SRCS += wbuf.c
#endif
#ifdef CONFIG_JFFS2_FS_XATTR
#	SRCS += xattr.c xattr_trusted.c xattr_user.c
#endif
#ifdef CONFIG_JFFS2_FS_SECURITY
#	SRCS += security.c
#endif
#ifdef CONFIG_JFFS2_FS_POSIX_ACL
#	SRCS += acl.c
#endif
#ifdef CONFIG_JFFS2_RUBIN
#	SRCS += compr_rubin.c
#endif
#ifdef CONFIG_JFFS2_RTIME
#	SRCS += compr_rtime.c
#endif
#ifdef CONFIG_JFFS2_ZLIB
#	SRCS += compr_zlib.c
#endif
#ifdef CONFIG_JFFS2_LZO
#	JFFS2_SRCS += compr_lzo.c
#endif
#ifdef CONFIG_JFFS2_SUMMARY
#	JFFS2_SRCS += summary.c
#endif


OBJS = $(SRCS:.c=.o)


all: check $(ARCH)


check:
	@(if test -z "$(ARCH)"; then\
		echo "Error: Only main Makefile can be used for compilation!";\
		exit 1;\
	fi;)


.c.o:
	@(echo "  CC $<"; $(CC) -c $(CFLAGS) $<)


-include .depend


$(ARCH): $(OBJS)
	$(SIL)$(AR) $(ARFLAGS) $(ARCH) $(OBJS) 2>/dev/null


depend: check
	$(SIL)$(MKDEP) $(MKDEPFLAGS) $(SRCS) >.depend


clean: check
	@rm -f core *.o $(ARCH)
