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


static void __attribute__((unused)) pcache_selfCheck(pcache_t *pcache)
{
	int i, j, b;
	pcache_list_t *l;

	for (i = 0, l = pcache->f.n; (l != &pcache->f) && (i < pcache->max_cnt); i++, l = l->n);
	if (l != &pcache->f)
		fatprint_err("Pcache check detected too long next sequence in free\n");
	for (j = 0, l = pcache->f.p; (l != &pcache->f) && (j < pcache->max_cnt); j++, l = l->p);
	if (l != &pcache->f)
		fatprint_err("Pcache check detected too long prev sequence in free\n");
	if (i != j)
		fatprint_err("Pcache check detected difference between number of next (%d) and prev (%d) in free\n", i ,j);
	for (l = pcache->e.n; (l != &pcache->e) && (i < pcache->max_cnt); i++, l = l->n)
	if (l != &pcache->e)
		fatprint_err("Pcache check detected too long next sequence in empty\n");
	for (l = pcache->e.p; (l != &pcache->e) && (j < pcache->max_cnt); j++, l = l->p);
	if (l != &pcache->e)
		fatprint_err("Pcache check detected too long prev sequence in empty\n");
	if (i != j)
		fatprint_err("Pcache check detected difference between number of next (%d) and prev (%d) in empty + free\n", i ,j);
	i = 0;
	j = 0;
	for (b = 0; b < PCACHE_BUCKETS; b++) {
		for (l = pcache->b[b].n; (l != &(pcache->b[b])) && (i < pcache->max_cnt); i++, l = l->n);
		if (l != &(pcache->b[b]))
			fatprint_err("Pcache check detected too long next sequence in bucket %d\n", b);
		for (l = pcache->b[b].p; (l != &(pcache->b[b])) && (j < pcache->max_cnt); j++, l = l->p);
		if (l != &(pcache->b[b]))
			fatprint_err("Pcache check detected too long prev sequence in bucket %d\n", b);
		if (i != j)
			fatprint_err("Pcache check detected difference between number of next (%d) and prev (%d) in bucket %d\n", i, j, b);
	}
}


int pcache_init(pcache_t *pcache, unsigned size, void *dev, unsigned pagesize)
{
	int i;
	cpage_t *p = NULL;

	if (pagesize == 0) {
		fatprint_err("Page size 0 is not allowed\n");
		return -EINVAL;
	}
	pcache->max_cnt = size / pagesize;
	pcache->pagesize = pagesize;
	if (pcache->max_cnt == 0) {
		fatprint_err("Not enough space to store pages (pagesize (%d) < size (%d))\n", pagesize, size);
		return -EINVAL;
	}
	for (i = 0; i < PCACHE_BUCKETS; i++)
		pcache->b[i].n = pcache->b[i].p = &pcache->b[i];
	pcache->f.n = pcache->f.p = &pcache->f;
	pcache->e.n = pcache->e.p = &pcache->e;

	for (i = 0; i < pcache->max_cnt; i++) {
		p = malloc(sizeof(*p) + pcache->pagesize);
		if (p == NULL)
			break;
		p->f.p = pcache->e.p;
		p->f.n = &(pcache->e);
		pcache->e.p->n = &(p->f);
		pcache->e.p = &(p->f);
	}
	if (p == NULL) {
		fatprint_err("Not enough space to store %d pages\n", pcache->max_cnt);
		while (pcache->e.n != &pcache->e) {
			p = list2page(pcache->e.n, f);
			pcache->e.n = p->f.n;
			free(p);
		}
		return -ENOMEM;
	}

	pcache->cnt = 0;
	pcache->dev = dev;
	mut_init(&pcache->m);
	return EOK;
}


int pcache_resize(pcache_t *pcache, unsigned size, void **dev)
{
	pcache_list_t *l;
	cpage_t *p;

	mut_lock(&pcache->m);
	for (; (pcache->max_cnt > size) && (pcache->e.n != &pcache->e); pcache->max_cnt--) {
		l = pcache->e.p;
		pcache->e.p = l->p;
		pcache->e.p->n = &pcache->e;
		p = list2page(l, f);
		free(p);
	}
	for (; (pcache->max_cnt > size) && (pcache->f.n != &pcache->f); pcache->max_cnt--) {
		l = pcache->f.p;
		pcache->f.p = l->p;
		pcache->f.p->n = &pcache->f;
		p = list2page(l, f);
		p->b.p->n = p->b.n;
		p->b.n->p = p->b.p;
		free(p);
	}
	mut_unlock(&pcache->m);
	if (dev != NULL)
		*dev = pcache->dev;
	if (size == 0)
		mut_kill(&pcache->m);
	return EOK;
}


static cpage_t *_pcache_get(pcache_t *pcache, unsigned long pno)
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


static cpage_t *_pcache_getEmpty(pcache_t *pcache)
{
	pcache_list_t *l;
	cpage_t *p;

	if (pcache->e.n != &(pcache->e)) {
		l = pcache->e.n;
		l->n->p = l->p;
		l->p->n = l->n;
		return list2page(l, f);
	}

	for (l = pcache->f.p; l != &pcache->f; l = l->p) {
		p = list2page(l, f);
		p->used--;
		if (p->used == 0)
			break;
	}
	if (l == &pcache->f)
		return NULL;

	/* remove page from lists */
	p->f.n->p = p->f.p;
	p->f.p->n = p->f.n;
	p->b.n->p = p->b.p;
	p->b.p->n = p->b.n;

	return p;
}


static void pcache_add(pcache_t *pcache, cpage_t *p)
{
	int b = p->no % PCACHE_BUCKETS;

	p->used = 1;
	mut_lock(&pcache->m);

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

	mut_unlock(&pcache->m);
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
		tr = pcache->pagesize - (off % pcache->pagesize);
		if (size < tr)
			tr = size;
		mut_lock(&pcache->m);
		if ((p = _pcache_get(pcache, o)) == NULL) {
			p = _pcache_getEmpty(pcache);
			mut_unlock(&pcache->m);
			if (p == NULL)
				return pcache_devread(pcache->dev, off, size, buff);
			if ((ret = pcache_devread(pcache->dev, o * pcache->pagesize, pcache->pagesize, p->data)) != EOK) {
				/* add page to empty list */
				mut_lock(&pcache->m);
				p->f.p = pcache->e.p;
				p->f.n = &pcache->e;
				pcache->e.p->n = &p->f;
				pcache->e.p = &p->f;
				mut_unlock(&pcache->m);
				return ret;
			}
			p->no = o;
			new = 1;
		}
		memcpy(buff, p->data + (off % pcache->pagesize), tr);
		if (new)
			pcache_add(pcache, p);
		else
			mut_unlock(&pcache->m);
		off += tr;
		size -= tr;
		buff += tr;
	}
	return EOK;
}

