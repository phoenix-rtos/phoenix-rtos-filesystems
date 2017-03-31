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


struct _list_t {
	struct _list_t *n;
	struct _list_t *p;
};
typedef struct _list_t list_t;


struct _cpage_t {
	char data[PCACHE_SIZE_PAGE];
	unsigned long no;
	int used;
	list_t b;
	list_t f;
};
typedef struct _cpage_t cpage_t;
#define list2page(entry, field) ((cpage_t *)((void *)entry - __builtin_offsetof(cpage_t, field)))


typedef struct _pcache_t {
	list_t b[PCACHE_BUCKETS];
	list_t f;
	int cnt;
	void *dev;
} pcache_t;


static pcache_t pcache;


void pcache_init(void *dev)
{
	int i;

	for (i = 0; i < PCACHE_BUCKETS; i++)
		pcache.b[i].n = pcache.b[i].p = &pcache.b[i];
	pcache.f.n = pcache.f.p = &pcache.f;
	pcache.cnt = 0;
	pcache.dev = dev;
}


static cpage_t *pcache_get(unsigned long pno)
{
	list_t *l;
	cpage_t *p;

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


static void pcache_add(cpage_t *p)
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


int pcache_read(unsigned long off, unsigned int size, char *buff)
{
	unsigned long o, tr;
	cpage_t *p;
	int new, ret;

	for (o = off / PCACHE_SIZE_PAGE; size > 0; o++) {
		new = 0;
		if ((p = pcache_get(o)) == NULL) {
			if ((p = malloc(sizeof(*p))) == NULL)
				return pcache_devread(pcache.dev, off, size, buff);
			if ((ret = pcache_devread(pcache.dev, o * PCACHE_SIZE_PAGE, PCACHE_SIZE_PAGE, p->data)) != EOK) {
				free(p);
				return ret;
			}
			p->no = o;
			new = 1;
		}
		tr = PCACHE_SIZE_PAGE - (off % PCACHE_SIZE_PAGE);
		if (size < tr)
			tr = size;
		memcpy(buff, p->data + (off % PCACHE_SIZE_PAGE), tr);
		off += tr;
		size -= tr;
		buff += tr;
		if (new)
			pcache_add(p);
	}
	return EOK;
}

