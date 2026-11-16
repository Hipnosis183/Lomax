/* lomax_wld.c - Lomax's world files (the converter's "Lomax side").

   A Lomax world: chunks (tag, u32 big-endian size, payload; World_LoadWLD), every payload a multiple
   of 4 bytes (File_ReadChunk moves its pointer by size & ~3). Lomax's data is little-endian apart
   from the chunk headers, the HEAD size words, the TTAB and the MAP tile words. */
#include "convert.h"
#include <stdlib.h>
#include <string.h>

LChunks lomax_chunks(const u8 *d, size_t n)
{
    LChunks out = { 0 };
    size_t p = 0;
    while (p + 8 <= n) {
        LChunk c;
        size_t size = be32(d + p + 4);
        memcpy(c.tag, d + p, 4);
        c.tag[4] = 0;
        if (p + 8 + size > n) size = n - p - 8;
        c.p.p = d + p + 8;
        c.p.n = size;
        vpush(out, c);
        p += 8 + size;
        if (!strcmp(c.tag, "END ")) break;
    }
    return out;
}

const LChunk *lchunk(const LChunks *c, const char *tag)
{
    int i;
    for (i = 0; i < c->n; i++)
        if (!strcmp(c->v[i].tag, tag)) return &c->v[i];
    return NULL;
}

static unsigned le16(const u8 *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }

/* ([(name, numParams, set of anim banks)] in NMY order, offset of the closing nameLen 0) */
int lomax_types(const u8 *nmy, size_t n, LTypes *out)
{
    size_t q = 0;
    for (;;) {
        unsigned ln, flags, nanim, k, j;
        LType t;
        if (q + 2 > n) die("Lomax NMY: no end");
        ln = le16(nmy + q);
        if (!ln) break;
        q += 2;
        memset(&t, 0, sizeof(t));
        t.name = xalloc(ln + 1);
        for (k = 0; k < ln && nmy[q + k]; k++) t.name[k] = (char)nmy[q + k];
        q += ln;
        flags = le16(nmy + q);
        nanim = le16(nmy + q + 2);
        q += 6;
        for (k = 0; k < nanim; k++) {
            unsigned ne = le16(nmy + q + 8);
            for (j = 0; j < ne; j++) bank_add(&t.banks, nmy[q + 28 + j * 8]);
            q += 28 + ne * 8;
        }
        t.nparams = (int)(flags >> 4) & 15;
        vpush(*out, t);
    }
    return (int)q;
}

/* Lomax's File_ReadChunk moves its pointer by size & ~3 (and its byte count by size), so every
   payload is padded to a multiple of 4. */
void chunk(Buf *out, const char *tag, const void *data, size_t n)
{
    size_t pad = (4 - (n & 3)) & 3;
    u8 h[8];
    memcpy(h, tag, 4);
    h[4] = (u8)((n + pad) >> 24); h[5] = (u8)((n + pad) >> 16); h[6] = (u8)((n + pad) >> 8); h[7] = (u8)(n + pad);
    buf_add(out, h, 8);
    buf_add(out, data, n);
    buf_add(out, NULL, pad);
}

static void add16(Buf *b, int v) { u8 x[2]; put_le16(x, (unsigned)v & 0xFFFF); buf_add(b, x, 2); }

/* -> (Lomax record bytes, its banks); ents: (bank, frame from 1, dx, dy, flags), 0x80 ending a frame */
/* pack_record: u32 FourCC (as an LE int: the tag's bytes reversed), u32 0x1C, u16 entries, u16 speed
   (0: 0x40), s16 hitbox[4], s16 attack box[4] (the same), 8-byte entries {u8 bank, u8 frame, s16 dx,
   s16 dy, u16 flags} */
LRec pack_record(const char *fourcc, int speed, const int hb[4], const Ent *e, int ne)
{
    LRec r;
    int i, k;
    u8 tag[4];
    memset(&r, 0, sizeof(r));
    for (i = 0; i < 4; i++) tag[i] = (u8)fourcc[3 - i];
    buf_add(&r.d, tag, 4);
    add16(&r.d, 0x1C); add16(&r.d, 0);
    add16(&r.d, ne);
    add16(&r.d, speed ? speed : 0x40);
    for (k = 0; k < 2; k++)
        for (i = 0; i < 4; i++) {
            if (hb[i] < -32768 || hb[i] > 32767) die("record %.4s: hitbox out of range", fourcc);
            add16(&r.d, hb[i]);
        }
    for (i = 0; i < ne; i++) {
        u8 x[2];
        if (e[i].bank < 0 || e[i].bank > 255 || e[i].frame < 0 || e[i].frame > 255) die("record %.4s: entry out of range", fourcc);
        x[0] = (u8)e[i].bank; x[1] = (u8)e[i].frame;
        buf_add(&r.d, x, 2);
        add16(&r.d, e[i].dx); add16(&r.d, e[i].dy); add16(&r.d, e[i].fl);
        bank_add(&r.banks, e[i].bank);
    }
    return r;
}

void free_lrec(LRec *r) { buf_free(&r->d); }

/* A Lomax NMY type: u16 nameLen, the name (NUL-padded to even), u16 flags (params << 4 |
   checkboxes), u16 record count, u16 0, the records. The first four letters pick the behaviour.
   The game copies the name into a 0x20-byte block at +4 and then writes the type's fields from +0x14,
   so names are kept to 15 characters. */
/* lomax_type: u16 nameLen, the name (at most 15 characters, NUL-ended, padded to even), u16 flags
   (params << 4 | checkboxes), u16 record count, u16 0, the records */
void lomax_type(Buf *out, const char *name, int nparams, int nchecks, const LRec *recs, int nrecs)
{
    u8 nb[18];
    size_t ln = strlen(name), i;
    int k;
    if (ln > 15) ln = 15;
    memset(nb, 0, sizeof(nb));
    memcpy(nb, name, ln);
    ln += 1;
    ln += ln & 1;
    add16(out, (int)ln);
    buf_add(out, nb, ln);
    add16(out, nparams << 4 | nchecks);
    add16(out, nrecs);
    add16(out, 0);
    for (k = 0; k < nrecs; k++) buf_add(out, recs[k].d.p, recs[k].d.n);
    (void)i;
}

void build_head(Buf *out, int w8, int h8, const unsigned *pal0, Bytes thead, const u8 *animbits)
{
    u8 *h = xalloc(0x1478);
    int i;
    memcpy(h, thead.p, thead.n < 0x1478 ? thead.n : 0x1478);
    h[0] = (u8)(w8 >> 24); h[1] = (u8)(w8 >> 16); h[2] = (u8)(w8 >> 8); h[3] = (u8)w8;
    h[4] = (u8)(h8 >> 24); h[5] = (u8)(h8 >> 16); h[6] = (u8)(h8 >> 8); h[7] = (u8)h8;
    for (i = 0; i < 256; i++) put_le16(h + 8 + 2 * i, pal0[i]);
    memcpy(h + 0x1458, animbits, 32);
    chunk(out, "HEAD", h, 0x1478);
    free(h);
}

Obj obj(int t, int x, int y, int flags, const int *p, int np, const Place *pl)
{
    Obj o;
    memset(&o, 0, sizeof(o));
    o.t = t; o.x = x; o.y = y; o.flags = flags; o.np = np; o.p = pl;
    if (np > 23) die("too many params");
    if (np) memcpy(o.prm, p, (size_t)np * sizeof(int));
    return o;
}

/* gnmy: '<HHhhH' (size, type, x, y, flags) and the params, then a 0 size */
void gnmy(Buf *out, const Objs *o)
{
    Buf b = { 0 };
    int i, k;
    for (i = 0; i < o->n; i++) {
        const Obj *e = &o->v[i];
        if (e->x < -32768 || e->x > 32767 || e->y < -32768 || e->y > 32767) die("placement out of range");
        add16(&b, 10 + 2 * e->np); add16(&b, e->t); add16(&b, e->x); add16(&b, e->y); add16(&b, e->flags);
        for (k = 0; k < e->np; k++) {
            if (e->prm[k] < 0 || e->prm[k] > 0xFFFF) die("a placement's param %d is out of range", e->prm[k]);
            add16(&b, e->prm[k]);
        }
    }
    add16(&b, 0);
    chunk(out, "GNMY", b.p, b.n);
    buf_free(&b);
}
