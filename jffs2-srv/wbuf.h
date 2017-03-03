/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */

#ifndef _JFFS2_FS_WBUF_H_
#define _JFFS2_FS_WBUF_H_

#ifndef CONFIG_JFFS2_FS_WRITEBUFFER

#ifdef CONFIG_JFFS2_SUMMARY
#define jffs2_can_mark_obsolete(c) (0)
#else
#define jffs2_can_mark_obsolete(c) (1)
#endif

#define jffs2_is_writebuffered(c) (0)
#define jffs2_cleanmarker_oob(c) (0)
#define jffs2_write_nand_cleanmarker(c,jeb) (-EIO)

#define jffs2_flash_write(c, ofs, len, retlen, buf) jffs2_flash_direct_write(c, ofs, len, retlen, buf)
#define jffs2_flash_read(c, ofs, len, retlen, buf) jffs2_flash_direct_read(c, ofs, len, retlen, buf)
#define jffs2_flush_wbuf_pad(c) ({ do{} while(0); (void)(c), 0; })
#define jffs2_flush_wbuf_gc(c, i) ({ do{} while(0); (void)(c), (void) i, 0; })
#define jffs2_write_nand_badblock(c,jeb,bad_offset) (1)
#define jffs2_nand_flash_setup(c) (0)
#define jffs2_nand_flash_cleanup(c) do {} while(0)
#define jffs2_wbuf_dirty(c) (0)
#define jffs2_flash_writev(a,b,c,d,e,f) jffs2_flash_direct_writev(a,b,c,d,e)
#define jffs2_wbuf_timeout NULL
#define jffs2_wbuf_process NULL
#define jffs2_dataflash(c) (0)
#define jffs2_dataflash_setup(c) (0)
#define jffs2_dataflash_cleanup(c) do {} while (0)
#define jffs2_nor_wbuf_flash(c) (0)
#define jffs2_nor_wbuf_flash_setup(c) (0)
#define jffs2_nor_wbuf_flash_cleanup(c) do {} while (0)
#define jffs2_ubivol(c) (0)
#define jffs2_ubivol_setup(c) (0)
#define jffs2_ubivol_cleanup(c) do {} while (0)
#define jffs2_dirty_trigger(c) do {} while (0)

#else /* NAND and/or ECC'd NOR support present */

#define jffs2_is_writebuffered(c) (c->wbuf != NULL)

#ifdef CONFIG_JFFS2_SUMMARY
#define jffs2_can_mark_obsolete(c) (0)
#else
#define jffs2_can_mark_obsolete(c) (c->mtd->flags & (MTD_BIT_WRITEABLE))
#endif

#define jffs2_cleanmarker_oob(c) (c->mtd->type == MTD_NANDFLASH)

#define jffs2_wbuf_dirty(c) (!!(c)->wbuf_len)

/* wbuf.c */
int jffs2_flash_writev(struct jffs2_sb_info *c, const struct kvec *vecs, unsigned long count, loff_t to, size_t *retlen, uint32_t ino);
int jffs2_flash_write(struct jffs2_sb_info *c, loff_t ofs, size_t len, size_t *retlen, const u_char *buf);
int jffs2_flash_read(struct jffs2_sb_info *c, loff_t ofs, size_t len, size_t *retlen, u_char *buf);
int jffs2_check_oob_empty(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,int mode);
int jffs2_check_nand_cleanmarker(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb);
int jffs2_write_nand_cleanmarker(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb);
int jffs2_write_nand_badblock(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb, uint32_t bad_offset);
void jffs2_wbuf_timeout(unsigned long data);
void jffs2_wbuf_process(void *data);
int jffs2_flush_wbuf_gc(struct jffs2_sb_info *c, uint32_t ino);
int jffs2_flush_wbuf_pad(struct jffs2_sb_info *c);
int jffs2_nand_flash_setup(struct jffs2_sb_info *c);
void jffs2_nand_flash_cleanup(struct jffs2_sb_info *c);

#define jffs2_dataflash(c) (c->mtd->type == MTD_DATAFLASH)
int jffs2_dataflash_setup(struct jffs2_sb_info *c);
void jffs2_dataflash_cleanup(struct jffs2_sb_info *c);
#define jffs2_ubivol(c) (c->mtd->type == MTD_UBIVOLUME)
int jffs2_ubivol_setup(struct jffs2_sb_info *c);
void jffs2_ubivol_cleanup(struct jffs2_sb_info *c);

#define jffs2_nor_wbuf_flash(c) (c->mtd->type == MTD_NORFLASH && ! (c->mtd->flags & MTD_BIT_WRITEABLE))
int jffs2_nor_wbuf_flash_setup(struct jffs2_sb_info *c);
void jffs2_nor_wbuf_flash_cleanup(struct jffs2_sb_info *c);
void jffs2_dirty_trigger(struct jffs2_sb_info *c);

#endif /* WRITEBUFFER */

#endif
