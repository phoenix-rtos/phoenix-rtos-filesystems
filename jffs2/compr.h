/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright © 2004   Ferenc Havasi <havasi@inf.u-szeged.hu>,
 *		      University of Szeged, Hungary
 * Copyright © 2004-2010 David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */

#ifndef __JFFS2_COMPR_H__
#define __JFFS2_COMPR_H__

#include "jffs2-public.h"
#include "jffs2_fs_i.h"
#include "jffs2_fs_sb.h"
#include "nodelist.h"

#define JFFS2_RUBINMIPS_PRIORITY 10
#define JFFS2_DYNRUBIN_PRIORITY  20
#define JFFS2_LZARI_PRIORITY     30
#define JFFS2_RTIME_PRIORITY     50
#define JFFS2_ZLIB_PRIORITY      60
#define JFFS2_LZO_PRIORITY       80


#define JFFS2_RUBINMIPS_DISABLED /* RUBINs will be used only */
#define JFFS2_DYNRUBIN_DISABLED  /*	   for decompression */

#define FAVOUR_LZO_PERCENT 80

struct jffs2_compressor {
	os_compressor_list_el_t list;
	int priority;			/* used by prirority comr. mode */
	char *name;
	char compr;			/* JFFS2_COMPR_XXX */
	int (*compress)(char *data_in, char *cpage_out,
			uint32_t *srclen, uint32_t *destlen);
	int (*decompress)(char *cdata_in, char *data_out,
			  uint32_t cdatalen, uint32_t datalen);
	int usecount;
	int disabled;		/* if set the compressor won't compress */
	char *compr_buf;	/* used by size compr. mode */
	uint32_t compr_buf_size;	/* used by size compr. mode */
	uint32_t stat_compr_orig_size;
	uint32_t stat_compr_new_size;
	uint32_t stat_compr_blocks;
	uint32_t stat_decompr_blocks;
};

int jffs2_register_compressor(struct jffs2_compressor *comp);
int jffs2_unregister_compressor(struct jffs2_compressor *comp);

int jffs2_compressors_init(void);
int jffs2_compressors_exit(void);

uint16_t jffs2_compress(struct jffs2_sb_info *c, struct jffs2_inode_info *f,
			char *data_in, char **cpage_out,
			uint32_t *datalen, uint32_t *cdatalen);

int jffs2_decompress(struct jffs2_sb_info *c, struct jffs2_inode_info *f,
		     uint16_t comprtype, char *cdata_in,
		     char *data_out, uint32_t cdatalen, uint32_t datalen);

void jffs2_free_comprbuf(char *comprbuf, char *orig);

/* Compressor modules */
/* These functions will be called by jffs2_compressors_init/exit */

#ifdef CONFIG_JFFS2_RUBIN
int jffs2_rubinmips_init(void);
void jffs2_rubinmips_exit(void);
int jffs2_dynrubin_init(void);
void jffs2_dynrubin_exit(void);
#endif
#ifdef CONFIG_JFFS2_RTIME
int jffs2_rtime_init(void);
void jffs2_rtime_exit(void);
#endif
#ifdef CONFIG_JFFS2_ZLIB
int jffs2_zlib_init(void);
void jffs2_zlib_exit(void);
#endif
#ifdef CONFIG_JFFS2_LZO
int jffs2_lzo_init(void);
void jffs2_lzo_exit(void);
#endif

#endif /* __JFFS2_COMPR_H__ */
