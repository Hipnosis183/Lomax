/* flink_wld.c - Flink's world files (the converter's "Flink side").

   A Flink world is a run of chunks (tag, u32 big-endian size, payload, padded to an even size) up to
   its END: CELL (4bpp tiles), ANIM (sprites), NMY (a type: name, editor flags, animation records),
   then per map HEAD, TTAB, MAP and GNMY (the placements). ANIM, HEAD, TTAB, MAP and CELL payloads
   are XPK/NUKE-packed (xpk.c). Flink's decompilation.md 3 has the format. */
#include "convert.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

const u8 *g_flinkPrg;
DWORD g_flinkPrgSize;

static Bytes up(const u8 *p, size_t n)
{
    Bytes b;
    if (n >= 4 && !memcmp(p, "XPKF", 4)) {
        size_t len;
        b.p = xpk_unpack(p, n, &len);
        b.n = len;
    } else {
        b.p = p;
        b.n = n;
    }
    return b;
}

/* a type's records, and in box2 each record's second box (the record's bytes 8..11: left, top,
   right, bottom as the hitbox), which some thinks test against the player: the City dummy's lance
   tip (0x1A41C reads it at record + 0x10)

   An NMY chunk's animation records (after name and editor flags): {tag: (speed, hitbox
   (left, top, right, bottom), [(anim, frame, dx, dy, flags)])}. Piece flags: 0x80 last piece of a
   frame, 0x08/0x10 h/v flip; anim or frame with bit 7 set = not a sprite (attach point). */
/* flink_records: an NMY chunk's animation records (after name and editor flags) */
static Recs flink_records(const u8 *p, size_t n)
{
    VEC(Rec) out = { 0 };
    size_t q = 0;
    Recs rs;
    while (q + 20 <= n) {
        unsigned len = be32(p + q + 4), i;
        const u8 *r = p + q + 8;
        Rec rec;
        memset(&rec, 0, sizeof(rec));
        memcpy(rec.tag, p + q, 4);
        rec.npc = (int)be16(r);
        rec.speed = (int)be16(r + 2);
        for (i = 0; i < 4; i++) { rec.hb[i] = (signed char)r[4 + i]; rec.box2[i] = (signed char)r[8 + i]; }
        rec.pc = xalloc((size_t)(rec.npc ? rec.npc : 1) * sizeof(Piece));
        for (i = 0; i < (unsigned)rec.npc; i++) {
            const u8 *e = r + 12 + i * 5;
            if (12 + i * 5 + 5 > len || q + 8 + 12 + i * 5 + 5 > n) die("Flink world: a record runs past its chunk");
            rec.pc[i].a = e[0]; rec.pc[i].f = e[1];
            rec.pc[i].dx = (signed char)e[2]; rec.pc[i].dy = (signed char)e[3]; rec.pc[i].fl = e[4];
        }
        vpush(out, rec);
        q += 8 + len;
    }
    rs.r = out.v;
    rs.n = out.n;
    return rs;
}

const Rec *rec_get(const Recs *rs, const char *tag)
{
    int i;
    if (!rs) return NULL;
    for (i = rs->n - 1; i >= 0; i--)
        if (!memcmp(rs->r[i].tag, tag, 4)) return &rs->r[i];
    return NULL;
}

int recs_has(const Recs *rs, const char *tag) { return rec_get(rs, tag) != NULL; }

Recs *world_recs(FWorld *w, int slot)
{
    return slot >= 0 && slot < w->recs.n ? &w->recs.v[slot] : NULL;
}

/* CELL data, ANIMs, NMY names and records, and the maps (head, ttab, map, gnmy) of the file's
   first world (data: the world's bytes instead of a file, as the title's in FLINK.PRG). */
void flink_world(FWorld *w, const u8 *d, size_t n)
{
    size_t o = 0;
    Bytes ttab = { 0 };
    memset(w, 0, sizeof(*w));
    while (o + 8 <= n) {
        char tag[5];
        size_t size = be32(d + o + 4);
        const u8 *p = d + o + 8;
        memcpy(tag, d + o, 4);
        tag[4] = 0;
        if (o + 8 + size > n) size = n - o - 8;
        if (!strcmp(tag, "CELL")) w->cells = up(p, size);
        else if (!strcmp(tag, "ANIM")) { Bytes b = up(p, size); vpush(w->anims, b); }
        else if (!strcmp(tag, "NMY ")) {
            Recs empty = { 0 };
            if (size <= 4) {
                char *s = xalloc(1);
                vpush(w->nmy, s);
                vpush(w->recs, empty);
            } else {
                unsigned ln = be16(p), k;
                char *s = xalloc(ln + 1);
                for (k = 0; k < ln && p[2 + k]; k++) s[k] = (char)p[2 + k];
                vpush(w->nmy, s);
                Recs r = flink_records(p + 4 + ln, size - 4 - ln);
                vpush(w->recs, r);
            }
        } else if (!strcmp(tag, "HEAD")) {
            FMap m;
            memset(&m, 0, sizeof(m));
            m.head = up(p, size);
            m.ttab = ttab;
            vpush(w->maps, m);
        } else if (!strcmp(tag, "TTAB")) {
            ttab = up(p, size);
            if (w->maps.n) w->maps.v[w->maps.n - 1].ttab = ttab;
        } else if (!strcmp(tag, "MAP ")) {
            if (w->maps.n) w->maps.v[w->maps.n - 1].map = up(p, size);
        } else if (!strcmp(tag, "GNMY")) {
            if (w->maps.n) { w->maps.v[w->maps.n - 1].gnmy.p = p; w->maps.v[w->maps.n - 1].gnmy.n = size; }
        }
        o += 8 + size + (size & 1);
        if (!strcmp(tag, "END ")) break;
    }
}

/* Flink's worlds don't agree on the case of some type names (MOUNTAIN's 'Ingredient holder', VILLAGE's
   'Deadly ground', FOREST's 'Scroll Point', ...: Flink finds a type by its slot, not its name), so
   flink_placements gives each placement the spelling the converter uses, matched without case: a key
   of ENEMIES, PLATFORMS, MAIN_ENEMIES or MAP_AREAS, or one of NMY_NAMES, the other names it compares
   a placement's name with. A new name compared with a placement's name goes in NMY_NAMES (CANON) unless it is such a key.

   a type name -> the converter's spelling of it (the name itself if it has none) */
/* Flink's worlds don't agree on the case of some type names: each placement gets the converter's
   spelling, matched without case (NMY_NAMES, the PLATFORMS, ENEMIES, MAIN_ENEMIES and
   MAP_AREAS keys, in that order: the first spelling of a name wins). */
static const char *const CANON[] = {
    /* NMY_NAMES */
    "App2", "Apple (big)", "Ball (swinging)", "Boom", "Bos2", "Boss", "Bridge", "Chest", "Deadly ground", "Drop Ball",
    "Exit point", "Falling apple", "Burst of lava", "Butterflies", "Fountain", "Generator", "Great balls of fire",
    "Ground type", "Knol spiky", "Lava", "Liane", "Protuberance", "Rolling stone", "Map ", "Monkey", "Music", "Priority",
    "Revive point", "Rope slide", "Scroll point", "Sinkplatform", "Snake", "Spike", "Stahlkugel",
    "Stone", "Trap platform", "Tree", "Trunk", "Warp", "Water",
    /* PLATFORMS */
    "Normalplatform", "Mega platform", "Meg2 platform", "Hand", "Sinkplatform", "Tra2 platform", "Tra2 Platform",
    "Trap platform", "Leaveplatform", "Swingplatform", "Swing platform", "Trunk", "Bouncy platform", "Jumppad",
    "Seesaw", "Ball (swinging)", "Exploding platform", "Wolky platform", "Floating platform", "Wall platform", "Rock",
    "Wagon", "Steer platform", "Water", "Throw platform", "Plant",
    /* ENEMIES */
    "Boss", "Forest Dummy", "Ingredient Holder", "Thief", "Visje", "Firemonster", "Statue", "Raketen wurm", "Puge",
    "City dummy", "Mountain dummy", "Yeti", "Buzzard", "Cyclin' Dude", "Chopper Bike", "Little Dude", "Intree dummy",
    "Worm", "Razorback", "Meta dummy", "Storm Cloud", "Blitz dude", "Circling suckers", "Donkey monkey", "Bos1",
    "Bos3 dude",
    /* MAIN_ENEMIES */
    "Ingredient Holder", "Thief",
    /* MAP_AREAS */
    "Shrink area", "Kill area", "Final area",
};

const char *nmy_name(const char *name)
{
    size_t i;
    for (i = 0; i < sizeof(CANON) / sizeof(CANON[0]); i++)
        if (!_stricmp(CANON[i], name)) return CANON[i];
    return name;
}

/* A map's GNMY entries: name, NMY slot, x, y, checkboxes (+0x0B), word params, and for the links
   (generator_links) the entry's offset in the list (off), its spawn flags (sf, +0x0A: bit 5 never
   spawned by proximity, bit 7 linked) and, with bit 7, the linked entry's offset (link: the entry's
   last word, which is not a param). */
Places flink_placements(const FWorld *w, const FMap *m)
{
    Places out = { 0 };
    const u8 *g = m->gnmy.p;
    size_t q = 0, n = m->gnmy.n;
    while (q + 2 <= n) {
        unsigned size = be16(g + q), to, k;
        Place p;
        int np;
        if (size == 0) break;
        if (q + 12 > n) die("Flink world: a placement runs past its list");
        memset(&p, 0, sizeof(p));
        to = be16(g + q + 2);
        p.x = (short)be16(g + q + 4);
        p.y = (short)be16(g + q + 6);
        np = ((int)size - 12) / 2;
        if (np < 0) np = 0;
        p.params = xalloc((size_t)(np ? np : 1) * sizeof(int));
        for (k = 0; k < (unsigned)np; k++) p.params[k] = (int)be16(g + q + 12 + 2 * k);
        p.np = np;
        p.link = -1;
        if (g[q + 10] & 0x80 && p.np) p.link = p.params[--p.np];
        p.slot = (int)(to / 4);
        p.name = nmy_name(p.slot < w->nmy.n ? w->nmy.v[p.slot] : "");
        p.check = g[q + 11];
        p.off = (int)q;
        p.sf = g[q + 10];
        vpush(out, p);
        q += size;
    }
    return out;
}

int prm(const Place *p, int i, int def)
{
    return i < p->np ? p->params[i] : def;
}

/* A Flink ANIM -> [(w, h, 8-bit pixels)] per frame. A frame is a set of hardware sprites
   (u32: y offset, width-1 and height-1 in cells, x offset) whose 4bpp tiles follow in VDP sprite
   order (column by column); pixels become line*16 + colour, 0 transparent. */
/* flink_anim_frames: an ANIM -> its frames (w, h, 8-bit pixels: line * 16 + colour, 0 transparent).
   A frame is a set of hardware sprites (u32: y offset, width-1 and height-1 in cells, x offset)
   whose 4bpp tiles follow in VDP sprite order (column by column). Cached by the ANIM's address. */
static HMap g_frameCache;
static VEC(Frames) g_frameList;

Frames flink_anim_frames(Bytes a)
{
    int idx, w, h, nf, line, k;
    size_t q = 8;
    Frames out;
    if (hmap_get(&g_frameCache, &a.p, sizeof(a.p), &idx)) return g_frameList.v[idx];
    if (a.n < 8) die("Flink ANIM too short");
    w = (int)be16(a.p); h = (int)be16(a.p + 2); nf = (int)be16(a.p + 4); line = (int)be16(a.p + 6) & 3;
    out.n = nf;
    out.f = xalloc((size_t)(nf ? nf : 1) * sizeof(Frame));
    for (k = 0; k < nf; k++) {
        int npc = (int)be16(a.p + q), nt, i, W, H, t = 0, cx, cy;
        const u8 *pieces;
        const u8 *tiles;
        size_t ntiles;
        q += 2;
        pieces = a.p + q;
        q += 4 * (size_t)npc;
        nt = (int)be16(a.p + q);
        q += 2;
        tiles = a.p + q;
        ntiles = q + (size_t)nt * 32 <= a.n ? (size_t)nt * 32 : (q < a.n ? a.n - q : 0);
        q += (size_t)nt * 32;
        W = w * 8; H = h * 8;
        for (i = 0; i < npc; i++) {
            const u8 *p = pieces + 4 * i;
            W = imax(W, p[3] + (p[1] + 1) * 8);
            H = imax(H, p[0] + (p[2] + 1) * 8);
        }
        out.f[k].w = W; out.f[k].h = H;
        out.f[k].px = xalloc((size_t)(W * H != 0 ? W * H : 1));
        for (i = 0; i < npc; i++) {
            const u8 *p = pieces + 4 * i;
            int yo = p[0], pw = p[1], ph = p[2], xo = p[3];
            for (cx = 0; cx <= pw; cx++)
                for (cy = 0; cy <= ph; cy++) {
                    size_t ro = (size_t)t * 32, rl = ro < ntiles ? (ntiles - ro < 32 ? ntiles - ro : 32) : 0;
                    int j;
                    t++;
                    for (j = 0; j < 64 && (size_t)j < rl * 2; j++) {
                        int c = j & 1 ? tiles[ro + (j >> 1)] & 15 : tiles[ro + (j >> 1)] >> 4;
                        int X = xo + cx * 8 + (j & 7), Y = yo + cy * 8 + (j >> 3);
                        if (c) {
                            if (X >= W || Y >= H) die("Flink ANIM: a sprite outside its frame");
                            out.f[k].px[Y * W + X] = (u8)(line * 16 + c);
                        }
                    }
                }
        }
    }
    vpush(g_frameList, out);
    hmap_put(&g_frameCache, &a.p, sizeof(a.p), g_frameList.n - 1);
    return out;
}

/* (x, y) of frame f's first hardware sprite in a Flink ANIM (see flink_anim_frames) */
void anim_piece_offset(Bytes a, int f, int *x, int *y)
{
    size_t q = 8;
    int k;
    for (k = 0; k < f; k++) {
        int npc = (int)be16(a.p + q);
        q += 2 + 4 * (size_t)npc;
        q += 2 + (size_t)be16(a.p + q) * 32;
    }
    *x = a.p[q + 5];
    *y = a.p[q + 2];
}

int head_used_cells(Bytes head, int *out)
{
    int k = 0, n;
    for (n = 0; n < 8192; n++)
        if (head.p[0xD8 + (n >> 3)] & (0x80 >> (n & 7))) out[k++] = n;
    return k;
}

unsigned vdp_to_15(unsigned w)
{
    unsigned r = (w >> 1) & 7, g = (w >> 5) & 7, b = (w >> 9) & 7;
#define S(c) (((c) * 36) >> 3)
    return S(b) | S(g) << 5 | S(r) << 10;
#undef S
}

/* Flink level n (1-62) -> (world file, play map, background map), from LevelSequenceTable
   (0x4416C in the sub-CPU program FLINK.PRG, loaded at 0x62FA): 4-byte {world, map, background, 0}
   entries, a negative world ends it. */
/* level_maps: Flink level n (1-62) -> world, play map, background map, from LevelSequenceTable
   (0x4416C): 4-byte {world, map, background, 0} entries */
void level_maps(int n, const char **world, int *m, int *bg)
{
    static const char *const W[] = { NULL, "FOREST", "VILLAGE", "MOUNTAIN", "TREE", "META" };
    const u8 *e = g_flinkPrg + 0x4416C - PRG_LOAD + (n - 1) * 4;
    if (n < 1 || n > 62 || e[0] < 1 || e[0] > 5) die("no Flink level %d", n);
    *world = W[e[0]];
    *m = e[1];
    *bg = e[2];
}

/* Music. Flink's Music actor (0x20DAC) plays music index params[0] through CdAudio_PlayTrack
   (0x3575E), whose table at 0x35862 gives the disc track of each index (23 words; a negative one is
   not a CD track). music.c extracts Flink's track t as MODS\FLINK\MUSIC\TRACK<MUSIC_BASE + t>.WAV, so a map
   gets a Lomax Music trigger with param MUSIC_BASE + t - 1 (CD_PlayTrack(n) plays track n + 1). */
/* Flink's Music actor plays a music index through CdAudio_PlayTrack (0x3575E), whose table at
   0x35862 (23 words) gives the disc track */
int flink_music_track(int index)
{
    if (index < 0 || index >= 23) return -1;
    return (short)be16(g_flinkPrg + 0x35862 - PRG_LOAD + 2 * index);
}
