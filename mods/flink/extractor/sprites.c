/* sprites.c - Flink ANIMs as Lomax ANIM chunks (Sprites), the CELL chunk (Cells) and the tile layers
   (convert_layer).

   A Lomax ANIM: u16 w, h, frameCount, palette (0: the level's, where the Flink colours are), u16 id,
   u16 0, then per frame s16 w, h, 0, 0 and w*h 8-bit pixels padded to 4 bytes. Frames are numbered
   from 1 in NMY records. Banks are numbered in the order they are first asked for (id
   FLINK_ANIM_BASE + n, bank id + 1), which is the order of the ANIM chunks. Besides a world ANIM n
   (SK_ANIM) a bank can be: SK_X2 (ANIM n with every frame repeated twice side by side), SK_MAIN
   (MAIN.WLD's ANIM n), SK_BLANK (one transparent pixel), SK_SCALED (ANIM a's first frame at Flink's
   32 Snake sizes, mirrored half added with b), SK_ROLLED (ANIM a frame b at 16 angles), SK_FILL (a
   w x h frame of one palette-0 colour and a blank second frame), SK_LINE (ANIM a on palette line b),
   SK_GLASS (the magic glass at each fill level) and SK_ADDED (ready-made frames, sprites_add). */
#include "convert.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Flink ANIMs turned into Lomax ANIM chunks: u16 w, h, frameCount, palette (0: the level's,
   where the Flink colours are), u16 id, u16 0, then per frame s16 w, h, 0, 0 and w*h 8-bit pixels
   padded to 4 bytes. Frames are numbered from 1 in NMY records. A key ('x2', n) is ANIM n with every
   frame repeated twice side by side; (WORLD, n) is ANIM n of another world (others[WORLD], e.g.
   MAIN's effects); the key 'blank' is one transparent pixel (for records Lomax needs but Flink
   doesn't draw); ('scaled', n, mirror) is ANIM n's first frame at Flink's 32 sizes (scaled_frames);
   ('fill', w, h, colour) is a w x h frame of one palette-0 colour and a blank
   second frame (the water's tint, see plat_water); ('line', n, L) is ANIM n with its pixels on
   palette line L (a piece whose flags give another line, see scene_record); ('glass',) is the HUD's magic glass at each of
   its fill levels (glass_frames); ('rolled', n, f) is frame f of ANIM n at the 16 angles of a turn
   (rolled_frames). */
static Frame frame_new(int w, int h)
{
    Frame f;
    f.w = w; f.h = h;
    f.px = xalloc((size_t)(w * h != 0 ? w * h : 1));
    return f;
}

static Frames frames_new(int n)
{
    Frames f;
    f.n = n;
    f.f = xalloc((size_t)(n ? n : 1) * sizeof(Frame));
    return f;
}

static Bytes world_anim(const FWorld *w, int n)
{
    if (n < 0 || n >= w->anims.n) die("ANIM %d: the world has %d", n, w->anims.n);
    return w->anims.v[n];
}

/* Flink's Snake sizes: Snake's setup (0x25590) scales its sprites to d2 = 125, 122 .. 32 percent
   (0x1DAD4: w * d2 / 100, nearest pixel), size k = 125 - 3k

   a frame (w, h, pixels) at each of SNAKE_SIZES; mirror: with its mirror image added on the left
   (the head is half a face, Flink draws it twice, 0x254E6) */
/* Flink's Snake sizes: Snake's setup (0x25590) scales its sprites to 125, 122 .. 32 percent
   (0x1DAD4: w * d2 / 100, nearest pixel), size k = 125 - 3k */
static Frames scaled_frames(Frame fr, int mirror)
{
    Frames out = frames_new(32);
    int k, x, y;
    for (k = 0; k < 32; k++) {
        int s = 125 - 3 * k, sw = imax(1, fr.w * s / 100), sh = imax(1, fr.h * s / 100), W = mirror ? 2 * sw : sw;
        out.f[k] = frame_new(W, sh);
        for (y = 0; y < sh; y++)
            for (x = 0; x < sw; x++) {
                u8 v = fr.px[(y * 100 / s) * fr.w + x * 100 / s];
                if (mirror) { out.f[k].px[y * W + sw + x] = v; out.f[k].px[y * W + sw - 1 - x] = v; }
                else out.f[k].px[y * W + x] = v;
            }
    }
    return out;
}

/* Flink's Apple (big) and App2 (0x24724, once a map): ROLL's sprite rotated at 0x1DC56 (the rotator
   0x1D846) d4 = 0x40 at a time, 0x400 a turn, for phases 1-7 of the half turn (0x80 for 8 phases);
   the other half is the same drawn upside down (0x245FE: sprite flags 0x1800).

   a frame (w, h, pixels) at 16 angles, 22.5 degrees apart, turning clockwise as the apple rolls
   right (its phase grows with vx): 0-7 rotated about the frame's middle (nearest pixel, the same
   w x h), 8-15 those turned upside down (h and v flipped) */
/* rolled_frames: a frame at 16 angles, 22.5 degrees apart: 0-7 rotated about the frame's middle
   (nearest pixel, the same w x h), 8-15 those turned upside down. The cosines and sines are the
   exact doubles of cos / sin (2 pi k / 16), so the result is the same on every computer. */
static const double ROLL_CS[7][2] = {
    { 0x1.d906bcf328d46p-1, 0x1.87de2a6aea963p-2 }, { 0x1.6a09e667f3bcdp-1, 0x1.6a09e667f3bcdp-1 },
    { 0x1.87de2a6aea964p-2, 0x1.d906bcf328d46p-1 }, { 0x1.1a62633145c07p-54, 0x1.0000000000000p+0 },
    { -0x1.87de2a6aea962p-2, 0x1.d906bcf328d46p-1 }, { -0x1.6a09e667f3bccp-1, 0x1.6a09e667f3bcdp-1 },
    { -0x1.d906bcf328d46p-1, 0x1.87de2a6aea965p-2 } };

static Frames rolled_frames(Frame fr)
{
    Frames out = frames_new(16);
    int k, x, y, w = fr.w, h = fr.h, i;
    for (k = 0; k < 8; k++) {
        out.f[k] = frame_new(w, h);
        if (!k) { memcpy(out.f[k].px, fr.px, (size_t)(w * h)); continue; }
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                double c = ROLL_CS[k - 1][0], sn = ROLL_CS[k - 1][1];
                volatile double u = x + 0.5 - w / 2.0, v = y + 0.5 - h / 2.0;
                volatile double fx = c * u + sn * v + w / 2.0, fy = -sn * u + c * v + h / 2.0;
                int sx = (int)floor(fx), sy = (int)floor(fy);
                if (sx >= 0 && sx < w && sy >= 0 && sy < h) out.f[k].px[y * w + x] = fr.px[sy * w + sx];
            }
    }
    for (k = 0; k < 8; k++) {
        out.f[8 + k] = frame_new(w, h);
        for (i = 0; i < w * h; i++) out.f[8 + k].px[i] = out.f[k].px[w * h - 1 - i];
    }
    return out;
}

/* glass_frames: the HUD's magic glass (MAIN.WLD's Display BOTT, its one sprite) at each of its fill
   levels: Hud_UpdateMagic (0x34348) fills it from the bottom by OR-ing colour 0xA into its pixels
   under the 32 x 32 1-bit mask at GLASS_MASK (0x3445C); the mask's rows with bits are the levels */
#define GLASS_MASK 0x407C8
#define GLASS_LEVELS 29
static Frames glass_frames(void)
{
    const Rec *r = rec_get(g_main.glass, "BOTT");
    const Piece *pc = NULL;
    const u8 *m = g_flinkPrg + GLASS_MASK - PRG_LOAD;
    Frames all, out;
    Frame fr;
    int i, k, x, y, rows[32], nrows = 0, line;
    if (!r) die("MAIN.WLD: no Display BOTT");
    for (i = 0; i < r->npc; i++)
        if (!(r->pc[i].a & 0x80)) { if (pc) die("Display BOTT: more than one sprite"); pc = &r->pc[i]; }
    if (!pc) die("Display BOTT: no sprite");
    all = flink_anim_frames(g_main.w.anims.v[pc->a]);
    fr = all.f[pc->f];
#define BIT(x, y) ((x) < 32 && (y) < 32 && (m[(y) * 4 + ((x) >> 3)] & (0x80 >> ((x) & 7))))
    for (y = imin(fr.h, 32) - 1; y >= 0; y--) {
        int any = 0;
        for (x = 0; x < 32; x++) if (BIT(x, y)) any = 1;
        if (any) rows[nrows++] = y;
    }
    if (nrows != GLASS_LEVELS) die("the magic glass has %d levels; FLINK.DLL expects %d", nrows, GLASS_LEVELS);
    line = 16 * ((int)be16(g_main.w.anims.v[pc->a].p + 6) & 3);
    out = frames_new(GLASS_LEVELS + 1);
    for (k = 0; k <= GLASS_LEVELS; k++) {
        out.f[k] = frame_new(fr.w, fr.h);
        memcpy(out.f[k].px, fr.px, (size_t)(fr.w * fr.h));
        for (i = 0; i < k; i++)
            for (x = 0; x < fr.w; x++)
                if (BIT(x, rows[i])) {
                    u8 *q = &out.f[k].px[rows[i] * fr.w + x];
                    *q = (u8)(line | (*q & 15) | 0xA);
                }
    }
#undef BIT
    return out;
}

static int key_eq(SKey a, SKey b) { return a.kind == b.kind && a.a == b.a && a.b == b.b && a.c == b.c; }

static int find_key(const Sprites *s, SKey k)
{
    int i;
    for (i = 0; i < s->keys.n; i++)
        if (key_eq(s->keys.v[i], k)) return i;
    return -1;
}

static Frames make_frames(Sprites *s, SKey k)
{
    Frames out, src;
    int i, y;
    switch (k.kind) {
    case SK_BLANK:
        out = frames_new(1); out.f[0] = frame_new(1, 1); return out;
    case SK_FILL:
        out = frames_new(2);
        out.f[0] = frame_new(k.a, k.b);
        memset(out.f[0].px, k.c, (size_t)(k.a * k.b));
        out.f[1] = frame_new(1, 1);
        return out;
    case SK_GLASS:
        return glass_frames();
    case SK_SCALED:
        return scaled_frames(flink_anim_frames(world_anim(s->w, k.a)).f[0], k.b);
    case SK_ROLLED:
        src = flink_anim_frames(world_anim(s->w, k.a));
        if (k.b >= src.n) die("ANIM %d has no frame %d", k.a, k.b);
        return rolled_frames(src.f[k.b]);
    case SK_LINE:
        src = flink_anim_frames(world_anim(s->w, k.a));
        out = frames_new(src.n);
        for (i = 0; i < src.n; i++) {
            int j;
            out.f[i] = frame_new(src.f[i].w, src.f[i].h);
            for (j = 0; j < src.f[i].w * src.f[i].h; j++)
                out.f[i].px[j] = src.f[i].px[j] ? (u8)(k.b * 16 + (src.f[i].px[j] & 15)) : 0;
        }
        return out;
    case SK_MAIN:
        return flink_anim_frames(world_anim(&g_main.w, k.a));
    case SK_X2:
        src = flink_anim_frames(world_anim(s->w, k.a));
        out = frames_new(src.n);
        for (i = 0; i < src.n; i++) {
            int w = src.f[i].w, h = src.f[i].h;
            out.f[i] = frame_new(2 * w, h);
            for (y = 0; y < h; y++) {
                memcpy(out.f[i].px + y * 2 * w, src.f[i].px + y * w, (size_t)w);
                memcpy(out.f[i].px + y * 2 * w + w, src.f[i].px + y * w, (size_t)w);
            }
        }
        return out;
    case SK_ANIM:
        return flink_anim_frames(world_anim(s->w, k.a));
    }
    die("sprite key %d: no frames", k.kind);
    return out;
}

int sprites_bank(Sprites *s, SKey k)
{
    int i = find_key(s, k);
    if (i < 0) {
        Frames f = make_frames(s, k);
        vpush(s->keys, k);
        vpush(s->frames, f);
        i = s->keys.n - 1;
    }
    return FLINK_ANIM_BASE + i + 1;
}

int sprites_add(Sprites *s, SKey k, Frames fr)
{
    if (find_key(s, k) >= 0) die("sprite bank added twice");
    vpush(s->keys, k);
    vpush(s->frames, fr);
    return FLINK_ANIM_BASE + s->keys.n;
}

const Frames *sprites_frames(Sprites *s, SKey k)
{
    sprites_bank(s, k);
    return &s->frames.v[find_key(s, k)];
}

int sprites_width(Sprites *s, SKey k, int f)
{
    const Frames *fr = sprites_frames(s, k);
    return fr->f[imin(f, fr->n - 1)].w;
}

void sprites_centre(Sprites *s, SKey k, int f, int fl, int *cx, int *cy)
{
    const Frames *frs = sprites_frames(s, k);
    const Frame *fr = &frs->f[imin(f, frs->n - 1)];
    int i, x0 = 1 << 30, x1 = -1, y0 = 1 << 30, y1 = -1, x, y;
    for (i = 0; i < fr->w * fr->h; i++)
        if (fr->px[i]) {
            x = i % fr->w; y = i / fr->w;
            x0 = imin(x0, x); x1 = imax(x1, x); y0 = imin(y0, y); y1 = imax(y1, y);
        }
    if (x1 < 0) { x0 = x1 = 0; y0 = y1 = 0; }
    x = (x0 + x1 + 1) / 2;
    y = (y0 + y1 + 1) / 2;
    *cx = fl & 0x08 ? fr->w - x : x;
    *cy = fl & 0x10 ? fr->h - y : y;
}

void sprites_chunks(Sprites *s, Buf *out)
{
    int i, k;
    for (i = 0; i < s->keys.n; i++) {
        const Frames *fr = &s->frames.v[i];
        Buf d = { 0 };
        u8 h[12];
        int mw = 0, mh = 0;
        for (k = 0; k < fr->n; k++) { mw = imax(mw, fr->f[k].w); mh = imax(mh, fr->f[k].h); }
        put_le16(h, (unsigned)mw); put_le16(h + 2, (unsigned)mh); put_le16(h + 4, (unsigned)fr->n);
        put_le16(h + 6, 0); put_le16(h + 8, (unsigned)(FLINK_ANIM_BASE + i)); put_le16(h + 10, 0);
        buf_add(&d, h, 12);
        for (k = 0; k < fr->n; k++) {
            u8 fh[8] = { 0 };
            size_t n = (size_t)(fr->f[k].w * fr->f[k].h);
            put_le16(fh, (unsigned)fr->f[k].w); put_le16(fh + 2, (unsigned)fr->f[k].h);
            buf_add(&d, fh, 8);
            buf_add(&d, fr->f[k].px, n);
            buf_add(&d, NULL, (4 - (n & 3)) & 3);
        }
        chunk(out, "ANIM", d.p, d.n);
        buf_free(&d);
    }
}

/* The output CELL chunk: 8x8 cells of 8-bit pixels, cell 0 blank. Opaque cells (the background
   plane) show the backdrop colour where Flink's are transparent: Lomax doesn't clear the screen
   behind its last plane, the Mega Drive shows the backdrop there. */
/* ---- Cells: the output CELL chunk, 8x8 cells of 8-bit pixels, cell 0 blank. Opaque cells (the
   background plane) show the backdrop colour where Flink's are transparent: Lomax doesn't clear the
   screen behind its last plane, the Mega Drive shows the backdrop there. ---- */
void cells_init(Cells *c, Bytes src)
{
    memset(c, 0, sizeof(*c));
    c->src = src.p;
    c->srcLen = src.n;
    buf_add(&c->data, NULL, 64);
    c->n = 1;
}

static int cells_put(Cells *c, const void *key, int klen, const u8 *px)
{
    int id, i, any = 0;
    if (hmap_get(&c->ids, key, klen, &id)) return id;
    for (i = 0; i < 64; i++) if (px[i]) any = 1;
    id = any ? c->n : 0;
    if (any) { buf_add(&c->data, px, 64); c->n++; }
    hmap_put(&c->ids, key, klen, id);
    return id;
}

int cells_get(Cells *c, int n, int line, int opaque)
{
    int key[4] = { 0, n, line, opaque }, i, id;
    u8 px[64];
    if (hmap_get(&c->ids, key, sizeof(key), &id)) return id;
    memset(px, 0, 64);
    if (n >= 0) {
        for (i = 0; i < 64; i++) {
            size_t b = (size_t)n * 32 + (size_t)(i >> 1);
            int v = b < c->srcLen ? c->src[b] : 0, col = i & 1 ? v & 15 : v >> 4;
            px[i] = col ? (u8)(line * 16 + col) : 0;
        }
    }
    if (opaque) for (i = 0; i < 64; i++) if (!px[i]) px[i] = BACKDROP;
    if (c->remap) for (i = 0; i < 64; i++) px[i] = c->remap[px[i]];   /* (SceneCells: the eyes' colours) */
    return cells_put(c, key, sizeof(key), px);
}

int cells_raw(Cells *c, const u8 *px)
{
    u8 key[68];
    memcpy(key, "raw:", 4);
    memcpy(key + 4, px, 64);
    return cells_put(c, key, sizeof(key), px);
}

/* Flink map -> (TTAB bytes, list of tile ids per cell). keep(word) says whether one 8x8 entry of a
   metatile belongs to this layer. */
/* convert_layer: a Flink map's metatiles -> (TTAB bytes, a tile id per metatile); keep(word, arg)
   says whether an 8x8 entry of a metatile belongs to this layer. Lomax's TTAB entry: 4 x (u16 cell,
   u16 flips 0x2000 / 0x4000), big-endian, entry 0 empty. */
void convert_layer(const FMap *fm, const int *used, Cells *c, int (*keep)(unsigned, int), int keeparg,
                   int opaque, Buf *ttab, IntVec *meta)
{
    int nmeta = (int)(fm->ttab.n / 8), i, q, id;
    HMap tiles = { 0 };
    int ntiles = 1;
    u8 empty[1] = { 0 };
    buf_add(ttab, NULL, 16);
    hmap_put(&tiles, empty, 0, 0);
    for (i = 0; i < nmeta; i++) {
        u8 ent[16];
        int any = 0;
        for (q = 0; q < 4; q++) {
            unsigned w = be16(fm->ttab.p + (size_t)(i * 4 + q) * 2), n = w & 0x7FF, at;
            int cell;
            if (n >= 0x180 && keep(w, keeparg)) {
                if ((int)(n - 0x180) >= 8192) die("a metatile's cell out of range");
                cell = cells_get(c, used[n - 0x180], (int)(w >> 13) & 3, opaque);
            } else cell = cells_get(c, -1, 0, opaque);
            at = (w & 0x800 ? 0x2000u : 0) | (w & 0x1000 ? 0x4000u : 0);
            ent[q * 4] = (u8)(cell >> 8); ent[q * 4 + 1] = (u8)cell;
            ent[q * 4 + 2] = (u8)(at >> 8); ent[q * 4 + 3] = (u8)at;
            if (cell) any = 1;
        }
        if (!hmap_get(&tiles, ent, any ? 16 : 0, &id)) {
            id = ntiles++;
            hmap_put(&tiles, ent, any ? 16 : 0, id);
            buf_add(ttab, ent, 16);
        }
        vpush(*meta, id);
    }
    hmap_free(&tiles);
}
