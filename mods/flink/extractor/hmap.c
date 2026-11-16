/* hmap.c - a hash map from byte strings to ints, keeping insertion order: the converter's output follows
   the order in which cells, tiles and sprite banks are first met. */
#include "convert.h"
#include <stdlib.h>
#include <string.h>

static unsigned hash(const u8 *k, int n)
{
    unsigned h = 2166136261u;
    while (n--) h = (h ^ *k++) * 16777619u;
    return h;
}

static int find(const HMap *m, const void *k, int klen, unsigned h)
{
    unsigned i;
    if (!m->nslot) return -1;
    for (i = h & (unsigned)(m->nslot - 1);; i = (i + 1) & (unsigned)(m->nslot - 1)) {
        int e = m->slot[i] - 1;
        if (e < 0) return -(int)i - 2;
        if (m->len.v[e] == klen && !memcmp(m->keys.p + m->off.v[e], k, (size_t)klen)) return e;
    }
}

int hmap_get(const HMap *m, const void *k, int klen, int *val)
{
    int e = find(m, k, klen, hash(k, klen));
    if (e < 0) return 0;
    *val = m->val.v[e];
    return 1;
}

static void grow(HMap *m)
{
    int i, n = m->nslot ? m->nslot * 2 : 64;
    free(m->slot);
    m->slot = xalloc((size_t)n * sizeof(int));
    m->nslot = n;
    for (i = 0; i < m->len.n; i++) {
        unsigned j = hash(m->keys.p + m->off.v[i], m->len.v[i]) & (unsigned)(n - 1);
        while (m->slot[j]) j = (j + 1) & (unsigned)(n - 1);
        m->slot[j] = i + 1;
    }
}

void hmap_put(HMap *m, const void *k, int klen, int val)
{
    int e;
    if ((m->len.n + 1) * 2 > m->nslot) grow(m);
    e = find(m, k, klen, hash(k, klen));
    if (e >= 0) { m->val.v[e] = val; return; }
    vpush(m->off, m->keys.n);
    buf_add(&m->keys, k, (size_t)klen);
    vpush(m->len, klen);
    vpush(m->val, val);
    m->slot[-e - 2] = m->len.n;
}

void hmap_free(HMap *m)
{
    buf_free(&m->keys);
    free(m->off.v); free(m->len.v); free(m->val.v); free(m->slot);
    memset(m, 0, sizeof(*m));
}
