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

#include <stdlib.h>
#include <string.h>


#include "pcache.h"
#include "types.h"


struct _cpage_t {
	unsigned long no;
	int used;
	pcache_list_t b;
	pcache_list_t f;
	char data[];
};
typedef struct _cpage_t cpage_t;
#define list2page(entry, field) ((cpage_t *)((void *)entry - __builtin_offsetof(cpage_t, field)))


int pcache_init(pcache_t *pcache, unsigned size, void *dev, unsigned pagesize)
{
	int i;

	for (i = 0; i < PCACHE_BUCKETS; i++)
		pcache->b[i].n = pcache->b[i].p = &pcache->b[i];
	pcache->f.n = pcache->f.p = &pcache->f;
	pcache->max_cnt = size;
	pcache->cnt = 0;
	pcache->dev = dev;
	pcache->pagesize = pagesize;

	return EOK;
}


int pcache_resize(pcache_t *pcache, unsigned size, void **dev)
{
	pcache_list_t *l;
	cpage_t *p;

	pcache->max_cnt = size;
	for (; pcache->cnt < size; pcache->cnt--) {
		l = pcache->f.p;
		pcache->f.p = l->p;
		pcache->f.p->n = &pcache->f;
		p = list2page(l, f);
		p->b.p->n = p->b.n;
		p->b.n->p = p->b.p;
		free(p);
	}
	if (dev != NULL)
		*dev = pcache->dev;
	return EOK;
}


static cpage_t *pcache_get(pcache_t *pcache, unsigned long pno)
{
	pcache_list_t *l;
	cpage_t *p;

	for (l = pcache->b[pno % PCACHE_BUCKETS].n; l != &pcache->b[pno % PCACHE_BUCKETS]; l = l->n) {
		p = list2page(l, b);
		if (p->no == pno) {
			p->used++;
			/* move page to the beginning of free list */
			p->f.n->p = p->f.p;
			p->f.p->n = p->f.n;
			p->f.p = pcache->f.p;
			p->f.n = &pcache->f;
			pcache->f.p->n = &p->f;
			pcache->f.p = &p->f;
			return p;
		}
	}
	return NULL;
}


static void pcache_add(pcache_t *pcache, cpage_t *p)
{
	int b = p->no % PCACHE_BUCKETS;
	pcache_list_t *l;
	
	p->used = 1;

	/* add page to bucket */
	p->b.p = pcache->b[b].p;
	p->b.n = (&pcache->b[b]);
	pcache->b[b].p->n = &p->b;
	pcache->b[b].p = &p->b;

	/* add page to free list */
	p->f.p = pcache->f.p;
	p->f.n = &pcache->f;
	pcache->f.p->n = &p->f;
	pcache->f.p = &p->f;

	if (pcache->cnt < pcache->max_cnt) {
		pcache->cnt++;
		return;
	}
	for (l = pcache->f.p; l != &pcache->f; l = l->p) {
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


int pcache_read(pcache_t *pcache, offs_t off, unsigned int size, char *buff)
{
	offs_t o;
	unsigned int tr;
	cpage_t *p;
	int new, ret;

	for (o = off / pcache->pagesize; size > 0; o++) {
		new = 0;
		if ((p = pcache_get(pcache, o)) == NULL) {
			if ((p = malloc(sizeof(*p) + pcache->pagesize)) == NULL)
				return pcache_devread(pcache->dev, off, size, buff);
			if ((ret = pcache_devread(pcache->dev, o * pcache->pagesize, pcache->pagesize, p->data)) != EOK) {
				free(p);
				return ret;
			}
			p->no = o;
			new = 1;
		}
		tr = pcache->pagesize - (off % pcache->pagesize);
		if (size < tr)
			tr = size;
		memcpy(buff, p->data + (off % pcache->pagesize), tr);
		off += tr;
		size -= tr;
		buff += tr;
		if (new)
			pcache_add(pcache, p);
	}
	return EOK;
}

