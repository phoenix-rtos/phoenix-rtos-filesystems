/*
 * Phoenix-RTOS
 *
 * Misc. Page cache
 *
 * Device support
 *
 * Copyright 2017 Phoenix Systems
 * Author: Katarzyna Baranowska
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>


#include "pcache.h"
#include "types.h"


struct _list_t {
	struct _list_t *n;
	struct _list_t *p;
};
typedef struct _list_t list_t;


struct _page_t {
	char data[PCACHE_SIZE_PAGE];
	unsigned long no;
	int used;
	list_t b;
	list_t f;
};
typedef struct _page_t page_t;
#define list2page(entry, field) ((page_t *)((void *)entry - __builtin_offsetof(page_t, field)))


typedef struct _pcache_t {
	list_t b[PCACHE_BUCKETS];
	list_t f;
	int cnt;
	int dev;
} pcache_t;


static pcache_t pcache;


void pcache_init(int dev)
{
	int i;

	for (i = 0; i < PCACHE_BUCKETS; i++)
		pcache.b[i].n = pcache.b[i].p = &pcache.b[i];
	pcache.f.n = pcache.f.p = &pcache.f;
	pcache.cnt = 0;
	pcache.dev = dev;
}


static page_t *pcache_get(unsigned long pno)
{
	list_t *l;
	page_t *p;

	for (l = pcache.b[pno % PCACHE_BUCKETS].n; l != &pcache.b[pno % PCACHE_BUCKETS]; l = l->n) {
		p = list2page(l, b);
		if (p->no == pno) {
			p->used++;
			/* move page to the beginning of free list */
			p->f.n->p = p->f.p;
			p->f.p->n = p->f.n;
			p->f.p = pcache.f.p;
			p->f.n = &pcache.f;
			pcache.f.p->n = &p->f;
			pcache.f.p = &p->f;
			return p;
		}
	}
	return NULL;
}


static void pcache_add(page_t *p)
{
	int b = p->no % PCACHE_BUCKETS;
	list_t *l;
	
	p->used = 1;

	/* add page to bucket */
	p->b.p = pcache.b[b].p;
	p->b.n = (&pcache.b[b]);
	pcache.b[b].p->n = &p->b;
	pcache.b[b].p = &p->b;

	/* add page to free list */
	p->f.p = pcache.f.p;
	p->f.n = &pcache.f;
	pcache.f.p->n = &p->f;
	pcache.f.p = &p->f;

	if (pcache.cnt < PCACHE_CNT_MAX) {
		pcache.cnt++;
		return;
	}
	for (l = pcache.f.p; l != &pcache.f; l = l->p) {
		p = list2page(l, f);
		p->used--;
		if (p->used == 0)
			break;
	}
	/* remove page from lists */
	p->f.n->p = p->f.p;
	p->f.p->n = p->f.n;
	p->b.n->p = p->b.p;
	p->b.p->n = p->b.n;

	free(p);
	return;
}


static int pcache_bareread(unsigned long off, unsigned int size, char *buff)
{
// 	printf("pcache bareread (0x%lx-0x%lx) 0x%x\n", off, off + size, size);
	if (lseek(pcache.dev, off, SEEK_SET) < 0)
		return ERR_ARG;

	if (read(pcache.dev, buff, size) != size)
		return ERR_PROTO;
	return ERR_NONE;
}


int pcache_read(unsigned long off, unsigned int size, char *buff)
{
	unsigned long o, tr;
	page_t *p;
	int new, ret;

// 	printf("pcache read (0x%lx-0x%lx) 0x%x\n", off, off + size, size);
	for (o = off / PCACHE_SIZE_PAGE; size > 0; o++) {
// 		printf("pcache read loop (0x%lx-0x%lx) 0x%x o=0x%lx\n", off, off + size, size, o * PCACHE_SIZE_PAGE);
		new = 0;
		if ((p = pcache_get(o)) == NULL) {
// 			printf("No hit\n");
			if ((p = malloc(sizeof(*p))) == NULL)
				return pcache_bareread(off, size, buff);
			if ((ret = pcache_bareread(o * PCACHE_SIZE_PAGE, PCACHE_SIZE_PAGE, p->data)) != ERR_NONE) {
				free(p);
				return ret;
			}
			p->no = o;
			new = 1;
		}
		tr = PCACHE_SIZE_PAGE - (off % PCACHE_SIZE_PAGE);
		if (size < tr) 
			tr = size;
// 		printf("tr is 0x%x\n", tr);
		memcpy(buff, p->data + (off % PCACHE_SIZE_PAGE), tr);
		off += tr;
		size -= tr;
		buff += tr;
		if (new)
			pcache_add(p);
	}
// 	printf("finish\n", tr);
	return ERR_NONE;
}

