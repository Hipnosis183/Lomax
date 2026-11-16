/* convert.c - one Flink map over a background map -> a Lomax world (convert(), with
   ground_at, trapdoor_y, floor_along, water_tint, template_sprite_colours, chest_content and
   generator_links). The comments at those functions, and README "What the converter writes",
   explain each object kind; the steps here follow convert()'s order, which decides the order of the
   sprite banks and so the ANIM chunks.

   The output world has three planes: 0 Flink's high-priority tiles (in front), 1 the play map (low-
   priority tiles, collision, the objects), 2 the background map (half speed across). */
#include "convert.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRAPDOOR_HEIGHT 80
#define TRAPDOOR_SMALL 60
#define WAVE_TOP (-4)
#define HINT_HAT_HEIGHT 40
#define HINT_BASE 1000
#define CHEST_COINS (-5)
#define CHEST_SPIRITS 100
#define CHEST_KEY_SPIRITS 10
#define CHEST_ITEM_SPIRITS 5
#define DONK_SCREEN 320
#define GENE_ONCE 1
#define GENE_NO_GROUP 2

static unsigned word_at(const Ctx *c, int tx, int ty)
{
    if (tx < 0 || tx >= c->w || ty < 0 || ty >= c->h) die("a map cell (%d, %d) outside the map", tx, ty);
    return c->words[ty * c->w + tx];
}

/* the floor line at or below y in column x, or none: a surface's line at its shape's height in the
   cell, a solid block's top at the cell top */
int ground_at(const Ctx *c, int x, int y, int *g)
{
    static const int H[8] = { 16, 16, 12, 12, 4, 4, 8, 8 };
    int tx = fdiv(x, 16), ty;
    if (tx < 0 || tx >= c->w) return 0;
    for (ty = imax(0, fdiv(y - 8, 16)); ty < c->h; ty++) {
        unsigned wd = c->words[ty * c->w + tx];
        if (wd & 0x8000) { *g = ty * 16 + (wd & 0x4000 ? H[(wd >> 11) & 7] : 0); return 1; }
    }
    return 0;
}

/* the bottom of the nearest solid block above y in column x (one-way floors don't count), or 0 */
static int ceiling_above(const Ctx *c, int x, int y)
{
    int tx = fdiv(x, 16), ty;
    for (ty = fdiv(y - 1, 16); ty >= 0; ty--) {
        unsigned wd = tx >= 0 && tx < c->w ? word_at(c, tx, ty) : 0;
        if ((wd & 0xC000) == 0x8000) return ty * 16 + 16;
    }
    return 0;
}

/* a trapdoor's y over a floor line: TRAPDOOR_HEIGHT up, less under a low ceiling */
static int trapdoor_y(const Ctx *c, int x, int floor, int keep)
{
    int room = floor - ceiling_above(c, x, floor) - keep;
    return floor - imax(24, imin(TRAPDOOR_HEIGHT, imax(TRAPDOOR_SMALL, room)));
}

/* Lomax collision bytes for one-way floors and their floor line in a 16 px cell at x 0..15
   (Plane_FindGroundY 0x413B90): flat, the two half-height slopes, 45 degrees; 0x08 mirrors.

   Writes one-way floor cells into coll (Lomax collision bytes) for columns x0..x1 so that the floor
   follows line(x), choosing per cell the shape that fits best. A column that already has collision
   in the rows the line crosses is left as it is: that is its floor or wall there. (A cell put in the
   row above it made a ledge over 3-1's slopes down to the spikes, 8-15 px above them: the Ingredient
   Holder lost its ground there and fell through into the water, where Flink's turns back at the
   gap's walls, Flink's solid blocks with only bit 13 / 12 set, which block moving right / left.) */
/* floor_along: one-way floor cells for columns x0..x1 following the line y = line (the best fitting
   shape a cell; Plane_FindGroundY's shapes), where the column has no collision there yet */
static int floor_shape(int k, int x)
{
    switch (k) {
    case 0: return 16;
    case 1: return 8 + (x >> 1);
    case 2: return 15 - (x >> 1);
    case 3: return x >> 1;
    case 4: return 7 - (x >> 1);
    case 5: return x;
    default: return 15 - x;
    }
}
static void floor_along(const Ctx *c, u8 *coll, int x0, int x1, int line)
{
    static const int B[7] = { 0xC0, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8 };
    int cx;
    for (cx = imax(0, fdiv(x0, 16)); cx < imin(c->w, fdiv(x1 + 15, 16)); cx++) {
        int r, r0 = fdiv(line - 16, 16), r1 = fdiv(line, 16), any = 0, have = 0, bestErr = 0, bestRow = 0, bestB = 0, k, x;
        for (r = r0; r <= r1; r++) if (r >= 0 && r < c->h && coll[r * c->w + cx]) any = 1;
        if (any) continue;
        for (r = r0; r <= r1; r++) {
            if (r < 0 || r >= c->h) continue;
            for (k = 0; k < 7; k++) {
                int err = 0;
                for (x = 0; x < 16; x += 2) err += abs(r * 16 + floor_shape(k, x) - line);
                if (!have || err < bestErr) { have = 1; bestErr = err; bestRow = r; bestB = B[k]; }
            }
        }
        if (have) coll[bestRow * c->w + cx] = (u8)bestB;
    }
}

/* Flink's mean water level, or the high tide */
/* Flink's mean water level, or the high tide */
static int water_surface(const Place *p, int high)
{
    int a = s16v(prm(p, 3, 0));
    if (a < 0) a = -a;
    return p->y - (high ? a / 2 : 0);
}

/* Flink's tint (r, g, b levels added to the map's 64 colours) -> (Lomax 15-bit colour, alpha)
   of the translucent strip that comes nearest to it (least squares over the colours, 0-252 scale) */
/* water_tint: Flink's tint (r, g, b levels added to the map's 64 colours) -> the Lomax 15-bit
   colour of the translucent strip that comes nearest to it (least squares over the colours). Each
   double operation in a fixed order (sums left to right). */
static unsigned water_tint(Bytes head, const int tint[3])
{
    int lv[64][3], n = 0, i, alpha, ch, bestRgb[3] = { 0, 0, 0 }, have = 0;
    double bestErr = 0;
    for (i = 0; i < 64; i++) {
        unsigned c = be16(head.p + 8 + 2 * i);
        if (!(i % 16)) continue;
        lv[n][0] = (int)(c >> 1) & 7; lv[n][1] = (int)(c >> 5) & 7; lv[n][2] = (int)(c >> 9) & 7;
        n++;
    }
    for (alpha = 0x10; alpha < 0x100; alpha += 8) {
        double a = alpha / 255.0, err = 0;
        int rgb[3];
        for (ch = 0; ch < 3; ch++) {
            double s = 0, e = 0, v;
            int c;
            for (i = 0; i < n; i++) {
                int P = lv[i][ch] * 36, T = imin(7, imax(0, lv[i][ch] + tint[ch])) * 36;
                s += T - (1 - a) * P;
            }
            v = rint(s / n / a);
            c = (int)v;
            c = imin(252, imax(0, c));
            for (i = 0; i < n; i++) {
                int P = lv[i][ch] * 36, T = imin(7, imax(0, lv[i][ch] + tint[ch])) * 36;
                double d = (1 - a) * P + a * c - T;
                e += d * d;
            }
            err += e;
            rgb[ch] = c;
        }
        if (!have || err < bestErr) { have = 1; bestErr = err; memcpy(bestRgb, rgb, sizeof(rgb)); }
    }
    return (unsigned)(bestRgb[2] >> 3) | (unsigned)(bestRgb[1] >> 3) << 5 | (unsigned)(bestRgb[0] >> 3) << 10;
}

/* Template ANIMs drawn with palette 0 (u16 +6 & 0xFF == 0: the level's own palette, e.g. the hint
   hat and hint guy, MEDIEVAL.WLD banks 38-45) would take Flink's colours, which fill palette 0's
   entries 0-63. The template's palette-0 colours they use go into the free entries from FREE_COLOURS
   up (pal is changed) and their pixels are repointed there; -> the ANIMs. */
/* template_sprite_colours: template ANIMs drawn with palette 0 get their colours into palette 0's
   free entries from FREE_COLOURS up (pal changed), their pixels repointed there */
void template_sprite_colours(BufVec *anims, Bytes thead, unsigned *pal)
{
    int remap[256], nremap = 1, i;
    for (i = 0; i < 256; i++) remap[i] = -1;
    remap[0] = 0;
    for (i = 0; i < anims->n; i++) {
        u8 *d = anims->v[i].p;
        int nf, k;
        size_t q = 12;
        if ((d[6] | d[7] << 8) & 0xFF) continue;
        nf = d[4] | d[5] << 8;
        for (k = 0; k < nf; k++) {
            int w = (short)(d[q] | d[q + 1] << 8), h = (short)(d[q + 2] | d[q + 3] << 8), j;
            for (j = 0; j < w * h; j++) {
                u8 *v = &d[q + 8 + j];
                if (remap[*v] < 0) {
                    int nn = FREE_COLOURS + nremap - 1;
                    unsigned c = (unsigned)(thead.p[8 + 2 * *v] | thead.p[9 + 2 * *v] << 8);
                    if (nn >= WATER_TINT) die("template sprites: more than %d palette-0 colours", WATER_TINT - FREE_COLOURS);
                    remap[*v] = nn;
                    nremap++;
                    pal[nn] = c ? c : 1;
                }
                *v = (u8)remap[*v];
            }
            q += 8 + (size_t)(w * h) + ((4 - (size_t)(w * h) % 4) % 4);
        }
    }
}

/* Flink's chests (Chest 0x2E000, MAIN.WLD's 'Chest' type: CHES closed, OPEN, LOCK the padlock).
   On spawn a chest bursts out of the ground: it starts 16 px under its placement, behind the
   ground's tiles (redrawn with the priority bit, draw hook 0x2E27C), is thrown up at 2.5 px a frame
   and falls back (gravity 0x2000, 0x1B0E2) to its placement, while 5 clods of earth (MAIN.WLD
   Dustcloud 'ERDE', Fx_SpawnDebris 0x2E0BC) fly off with sound 0x49. Stomped (0x2E184,
   Actor_CheckStompedByPlayer) it opens (OPEN, sound 0x26); a hook on its OPEN frame (table 0x2E3AC ->
   0x2E3A0) calls Item_Spawn (0x2C21A) with its param, the item id (Item_CheckCollect 0x2C4FE):
   0 extra life, 1 magic key, 2-6 potions, 7-19 ingredients, -1..-99 an information note (touching
   it shows Flink's message -id - 1, HelpTextTable 0x18250), <= -100 a recipe scroll. The open chest
   can then be carried and thrown; it is never opened again. Checkbox 1 = locked: it opens only
   with the MAGIC KEY spell.
   Lomax has no spells, keys or ingredients. The converter gives the chest Lomax's pot contents
   instead (ClayPot_Spawn's param: 0 helmet, 1 1UP, -n n coins): an extra life gives a 1UP balloon,
   an ingredient CHEST_COINS coins. A magic key gives CHEST_KEY_SPIRITS spirits (what a locked chest
   costs here), a potion or a recipe scroll CHEST_ITEM_SPIRITS spirits, param CHEST_SPIRITS + n,
   which only FLINK.DLL's chest_drop knows (not a Lomax pot content); no chest holds a helmet. A chest
   is a 'Pots Chest' object (object 85, ClayPot_Spawn 0x42BEE0, wrapped by FLINK.DLL: chest_update)
   with Flink's records CHES, OPEN and LOCK (the padlock) and the debris record ERDE; param 1 is 1
   for a locked chest (checkbox 1), which FLINK.DLL opens for 10 spirits (Lomax has no keys:
   chest_try). A chest holding an information note becomes a Lomax hint hat instead (the
   template's 'Lorock', object 69, HintHat_Init 0x4339B0) HINT_HAT_HEIGHT px above the chest's
   floor, with param HINT_BASE + Flink's message number; FLINK.DLL shows Flink's text in the hint box
   (hint_text_draw). The texts go into LEVELS.INI's [Hints] section (see hint_lines).

   a Flink chest's item id -> Lomax pot content (or CHEST_SPIRITS + n: FLINK.DLL's spirits) */
/* a Flink chest's item id -> Lomax pot content (or CHEST_SPIRITS + n: FLINK.DLL's spirits) */
static int chest_content(int item)
{
    if (item == 0) return 1;
    if (item == 1) return CHEST_SPIRITS + CHEST_KEY_SPIRITS;
    if ((item >= 2 && item <= 6) || item <= -100) return CHEST_SPIRITS + CHEST_ITEM_SPIRITS;
    return CHEST_COINS;
}

/* Hidden objects. Flink's Generator (0x2DD54) is an unseen actor; when Flink's box (0x82D0..0x82D6)
   touches x - 16 .. x + 16, y - height .. y + 16 (height = param 0 with checkbox 1, else 16), it
   spawns the GNMY entry its link points to, and the one that entry links to, and so on (a linked
   entry has spawn flag bit 7, and its last word is the offset of the next one in the list). Those
   entries usually have spawn flag bit 5 (never spawned by proximity), so they appear only then: most
   chests burst out of the ground when Flink comes near, and secret ones (1-1's 1UP over the sinking
   platform, 1-2's leaf for a blind jump) only where he must go to find them. Param 1, when not 0, is
   a flag of the level that the Generator sets: it fires once. Checkbox 3 ('no scr check') lets the
   spawned actors despawn one by one; otherwise they stay while any of them is near the camera.
   Lomax's placements have the same links (World_LoadWLD: flags 0x2000 never spawned by proximity,
   0x8000 linked, the next placement's index in the plane's list after the params) and its own
   trigger, 'Gene' (object 86, Trigger_Spawn 0x42D0D0), spawns a chain of them. A Flink Generator
   becomes 'Gene Flink' (params: height, mode 1 = once, 2 = no group) linked to the first converted
   entry of its chain, and FLINK.DLL (gene_update) fires it with Flink's box. Entries that aren't
   converted are left out of the chains.

   objs: (type, x, y, flags, params[, Flink placement]) -> the GNMY entries with the Flink
   placements' links and hiding, then a 'Gene Flink' (type t_gene) per Flink Generator; and
   (Generators placed, hidden objects) */
/* generator_links: the Flink placements' links and hiding on the objects, then a 'Gene Flink' (type
   t_gene) per Flink Generator linked to the first converted entry of its chain */
static int gl_first(const Places *pl, const HMap *byOff, const HMap *lomax, IntVec *lists, int off, HMap *seen)
{
    int pi, li, dummy;
    while (off >= 0 && hmap_get(byOff, &off, sizeof(off), &pi) && !hmap_get(seen, &off, sizeof(off), &dummy)) {
        const Place *p = &pl->v[pi];
        hmap_put(seen, &off, sizeof(off), 1);
        if (hmap_get(lomax, &off, sizeof(off), &li)) return lists[li].v[0];
        off = p->sf & 0x80 ? p->link : -1;
    }
    return -1;
}

static void generator_links(const Places *pl, Objs *objs, int t_gene, int *ngenes, int *nhidden)
{
    HMap byOff = { 0 }, lomax = { 0 };
    IntVec *lists = NULL;
    VEC(int) order = { 0 };
    int i, nl = 0, n = objs->n;
    for (i = 0; i < pl->n; i++) hmap_put(&byOff, &pl->v[i].off, sizeof(int), i);
    lists = xalloc((size_t)(n ? n : 1) * sizeof(*lists));
    for (i = 0; i < n; i++) {
        int li;
        if (!objs->v[i].p) continue;
        if (!hmap_get(&lomax, &objs->v[i].p->off, sizeof(int), &li)) {
            li = nl++;
            hmap_put(&lomax, &objs->v[i].p->off, sizeof(int), li);
            vpush(order, objs->v[i].p->off);
        }
        vpush(lists[li], i);
    }
    *nhidden = 0;
    for (i = 0; i < nl; i++) {
        int off = order.v[i], pi, nxt = -1, k;
        const Place *p;
        VEC(int) chain = { 0 };
        hmap_get(&byOff, &off, sizeof(int), &pi);
        p = &pl->v[pi];
        if (p->sf & 0x80) {
            HMap seen = { 0 };
            hmap_put(&seen, &off, sizeof(int), 1);
            nxt = gl_first(pl, &byOff, &lomax, lists, p->link, &seen);
            hmap_free(&seen);
        }
        for (k = 0; k < lists[i].n; k++) vpush(chain, lists[i].v[k]);
        if (nxt >= 0) vpush(chain, nxt);
        for (k = 0; k + 1 < chain.n; k++) {
            Obj *o = &objs->v[chain.v[k]];
            if (o->np >= 24) die("too many params");
            o->flags |= 0x8000;
            o->prm[o->np++] = chain.v[k + 1];
        }
        if (p->sf & 0x20) {
            for (k = 0; k < lists[i].n; k++) objs->v[lists[i].v[k]].flags |= 0x2000;
            *nhidden += lists[i].n;
        }
        free(chain.v);
    }
    *ngenes = 0;
    for (i = 0; i < pl->n; i++) {
        const Place *p = &pl->v[i];
        HMap seen = { 0 };
        int f, gp[3];
        if (strcmp(p->name, "Generator") || p->link < 0) continue;
        f = gl_first(pl, &byOff, &lomax, lists, p->link, &seen);
        hmap_free(&seen);
        if (f < 0) continue;
        gp[0] = p->check & 1 ? prm(p, 0, 0) : 16;
        gp[1] = (prm(p, 1, 0) ? GENE_ONCE : 0) | (p->check & 4 ? GENE_NO_GROUP : 0);
        gp[2] = f;
        { Obj o = obj(t_gene, p->x, p->y, 0x8000, gp, 3, NULL); vpush(*objs, o); }
        ++*ngenes;
    }
    for (i = 0; i < nl; i++) free(lists[i].v);
    free(lists);
    free(order.v);
    hmap_free(&byOff);
    hmap_free(&lomax);
}

/* ---- convert ---- */
int keep_fg(unsigned w, int prio) { return (w & 0x8000) && !prio; }
int keep_pl(unsigned w, int prio) { return !(w & 0x8000) || prio; }
int keep_all(unsigned w, int prio) { (void)w; (void)prio; return 1; }

static const Place *find_place(const Places *pl, const char *name)
{
    int i;
    for (i = 0; i < pl->n; i++) if (!strcmp(pl->v[i].name, name)) return &pl->v[i];
    return NULL;
}

static int tid(const LTypes *types, const char *name)
{
    int i;
    for (i = 0; i < types->n; i++) if (!strcmp(types->v[i].name, name)) return i;
    die("template: no '%s' type", name);
    return -1;
}

/* the placements of a name whose records have all these tags */
typedef VEC(const Place *) PlaceList;
static PlaceList with(FWorld *fw, const Places *pl, const char *name, const char *const *tags, int ntags)
{
    PlaceList out = { 0 };
    int i, k;
    for (i = 0; i < pl->n; i++) {
        const Place *p = &pl->v[i];
        Recs *r;
        if (strcmp(p->name, name)) continue;
        r = world_recs(fw, p->slot);
        if (!r) die("%s: NMY slot %d out of range", name, p->slot);
        for (k = 0; k < ntags; k++) if (!recs_has(r, tags[k])) break;
        if (k == ntags) vpush(out, p);
    }
    return out;
}

static void add_type(BufVec *ptypes, BankSet *banks, const char *name, int np, int nc, LRec *r, int n)
{
    Buf t = { 0 };
    int i;
    lomax_type(&t, name, np, nc, r, n);
    vpush(*ptypes, t);
    for (i = 0; i < n; i++) { bank_union(banks, &r[i].banks); free_lrec(&r[i]); }
}

static const Piece *first_sprite_of(const Rec *r)
{
    int i;
    for (i = 0; i < r->npc; i++) if (!(r->pc[i].a & 0x80) && !(r->pc[i].f & 0x80)) return &r->pc[i];
    die("record %.4s: no sprite", r->tag);
    return NULL;
}

static LRec blank_record(const char *fourcc, Sprites *s, int edge)
{
    RecSel r = { NULL, 0, 0 };
    return lomax_record(fourcc, &r, 1, s, 0, 0, 0, edge, 0, 0, 0);
}

static void pushf(Objs *o, int t, int x, int y, int flags, const int *p, int np, const Place *pl)
{
    Obj e = obj(t, x, y, flags, p, np, pl);
    vpush(*o, e);
}

/* appends "fmt" to the info line */
static void info_add(char *info, size_t size, const char *fmt, ...)
{
    va_list ap;
    size_t n = strlen(info);
    va_start(ap, fmt);
    vsnprintf(info + n, size - n, fmt, ap);
    va_end(ap);
}

/* Collision cells (column, row) taken out of a play map, by (world, map): MOUNTAIN map 8 (19-2) has four
   one-way floor cells at x 672-735, y 128 with no tile drawn (only the blank background tile 5), an
   invisible floor at y 144 between the exploding platforms at 584 and 777; Flink's Map_GetGroundHeight
   reads them, but nothing shows a floor there.

   One Flink map over a background map -> (Lomax world bytes, summary line, links for
   arrival_points).
   end_kind 'finish': the map ends its level with a Finish trapdoor over end_p (a Warp or an Exit
   point), or near the map's right end without one. 'exit': it leads on to the next sub-level
   through Lomax Exits at end_p and at every other Exit point to the same map (FLINK.DLL triggers
   them there; the game itself only at the map's right end). exits, if given, are the Flink Exit
   points that get Lomax Exits instead (every one leading to a map of the level, doors
   included; see arrival_points). warp, with 'exit': the map also ends its level at that Warp (a
   Finish over it), as 14-1 does besides its exit to 14-2. */
void convert(FWorld *fw, const char *world, int m, int bgmap, int finish, const Place *end_p,
             const Place *const *exits, int nexits, const Place *warp, Buf *out, char *info, size_t infoSize, Links *links)
{
    const FMap *play = &fw->maps.v[m - 1], *bg = &fw->maps.v[bgmap - 1];
    int w8 = (int)be32(play->head.p), h8 = (int)be32(play->head.p + 4);
    int bw8 = (int)be32(bg->head.p), bh8 = (int)be32(bg->head.p + 4);
    unsigned pal[256];
    int i, k, mw = w8 / 2, mh = h8 / 2;
    Ctx ctx;
    const LChunk *tnmy, *thead;
    u8 *nmy;
    size_t nmyLen;
    LTypes types = { 0 };
    int nmy_end, T_START, T_FINISH, T_EXIT, T_REVIVE, T_MUSIC, T_SCROLL = -1, T_HAT = -1;
    Places pl;
    PlaceList revive = { 0 }, entries = { 0 };
    Place defStart;
    const Place *start, *mus, *boss = NULL;
    int sy, track, g, boss_finish = -1, endx = 0, endy = 0;
    Objs objs = { 0 };
    u8 animbits[32];
    BufVec tpl_anims = { 0 };
    Sprites sprites;
    const Place *wp;
    PlatOut po;
    BufVec *ptypes;
    BankSet *pbanks;
    u8 *coll;
    int nhats = 0, nchests = 0, nstones = 0, nlianas = 0, nmonkeys = 0, nknols = 0, nballs = 0, nballTypes = 0, nstahls = 0,
        nspikes = 0, ndrops = 0, napples = 0, nrollTypes = 0, nbutterflies = 0, nlavas = 0, nlavaKinds = 0, nslides = 0,
        nsnakes = 0, ngrounds = 0, nbridges = 0, nscrolls = 0, ntrees = 0, nlianTypes = 0, ngenes = 0, nhidden = 0, netypes = 0;
    int nrevive = 0, t_apple = -1, t_app2 = -1;
    char extras[2048] = "";
    struct { const char *name; int n; } rolls[2], lavaKinds[5];
    PlaceList chests = { 0 };
    VEC(int) chestItem = { 0 };
    EnemyOut eo;
    struct { int t, back, prm[3]; } trees[16];

    memset(links, 0, sizeof(*links));
    for (i = 0; i < 256; i++) pal[i] = 0;
    for (i = 0; i < 64; i++) pal[i] = vdp_to_15(be16(play->head.p + 8 + 2 * i));
    pal[BACKDROP] = pal[0];
    pal[0] = 0;
    memset(&ctx, 0, sizeof(ctx));
    ctx.w = mw; ctx.h = mh;
    ctx.words = xalloc((size_t)(mw * mh) * sizeof(unsigned));
    if (play->map.n < (size_t)(mw * mh * 2)) die("%s map %d: its MAP is short", world, m);
    for (i = 0; i < mw * mh; i++) ctx.words[i] = be16(play->map.p + 2 * i);
    if (!strcmp(world, "MOUNTAIN") && m == 8) {            /* NO_COLLISION: four invisible one-way floor cells */
        static const int NC[4][2] = { { 42, 8 }, { 43, 8 }, { 44, 8 }, { 45, 8 } };
        for (i = 0; i < 4; i++) ctx.words[NC[i][1] * mw + NC[i][0]] &= 0x7FF;
    }

    /* template: object types, ANIM chunks, the first plane's HEAD (palettes 1-9, editor bytes) */
    tnmy = lchunk(&g_tpl.ch, "NMY ");
    thead = lchunk(&g_tpl.ch, "HEAD");
    if (!tnmy || !thead) die("WORLDS\\MEDIEVAL.WLD: no NMY or HEAD");
    nmyLen = tnmy->p.n;
    nmy = xalloc(nmyLen);
    memcpy(nmy, tnmy->p.p, nmyLen);
    for (i = 0; i + 6 <= (int)nmyLen; i++)                  /* the template's Water type is renamed (see plat_water) */
        if (!memcmp(nmy + i, "Water\0", 6)) {
            if (i >= 2 && (nmy[i - 2] | nmy[i - 1] << 8) >= 6) nmy[i + 4] = 'x';
            break;
        }
    nmy_end = lomax_types(nmy, nmyLen, &types);
    T_START = tid(&types, "Start"); T_FINISH = tid(&types, "Finish"); T_EXIT = tid(&types, "Exit");
    T_REVIVE = tid(&types, "Revive"); T_MUSIC = tid(&types, "Music");
    for (i = 0; i < types.n; i++) if (!strcmp(types.v[i].name, "ScrollMap") && types.v[i].nparams == 7) { T_SCROLL = i; break; }
    if (T_SCROLL < 0) die("template: no ScrollMap type");

    /* objects: Flink's start (the revive point with parameter 1) and other revive points, the end */
    pl = flink_placements(fw, play);
    for (i = 0; i < pl.n; i++) if (!strcmp(pl.v[i].name, "Revive point")) vpush(revive, &pl.v[i]);
    start = NULL;
    for (i = 0; i < revive.n; i++) if (revive.v[i]->np >= 1 && revive.v[i]->params[0] == 1) { start = revive.v[i]; break; }
    if (!start) {
        if (revive.n) start = revive.v[0];
        else { memset(&defStart, 0, sizeof(defStart)); defStart.x = 64; defStart.y = 64; defStart.name = ""; start = &defStart; }
    }
    sy = trapdoor_y(&ctx, start->x, ground_at(&ctx, start->x, start->y, &g) && g ? g : start->y, 72);
    mus = find_place(&pl, "Music");
    track = mus && mus->np && mus->params[0] < 23 ? flink_music_track(mus->params[0]) : -1;
    if (track > 0) { int p1 = MUSIC_BASE + track - 1; pushf(&objs, T_MUSIC, start->x + 30, start->y, 0x4001, &p1, 1, NULL); }
    pushf(&objs, T_START, start->x, sy, 0x4000, NULL, 0, NULL);
    /* (an entry point, spawned at once with an entry number, only places the arriving player and
       removes itself, 0x2B0EC / 0x32CBA: it is no checkpoint; see arrival_points) */
    for (i = 0; i < revive.n; i++) if (revive.v[i]->sf & 0x40 && prm(revive.v[i], 0, 0)) vpush(entries, revive.v[i]);
    for (i = 0; i < revive.n; i++) {
        int isEntry = 0;
        for (k = 0; k < entries.n; k++) if (entries.v[k] == revive.v[i]) isEntry = 1;
        if (revive.v[i] != start && !isEntry) { pushf(&objs, T_REVIVE, revive.v[i]->x, revive.v[i]->y, 0, NULL, 0, NULL); nrevive++; }
    }
    for (i = 0; i < nexits; i++) pushf(&objs, T_EXIT, exits[i]->x, exits[i]->y, 0, NULL, 0, NULL);
    for (i = 0; i < nexits; i++) {
        ExitEntry e;
        int ent = prm(exits[i], 1, 0);
        e.x = exits[i]->x; e.y = exits[i]->y; e.e = ent ? ent : 1; e.t = prm(exits[i], 0, 0) + 1; e.door = (exits[i]->check & 2) != 0;
        vpush(links->exits, e);
    }
    if (!finish) {
        if (end_p) { endx = end_p->x; endy = end_p->y; }
        if (warp) {
            int fy, has = ground_at(&ctx, warp->x, warp->y, &fy) && fy;
            pushf(&objs, T_FINISH, warp->x, has ? trapdoor_y(&ctx, warp->x, fy, TRAPDOOR_SMALL) : warp->y - 48, 0, NULL, 0, NULL);
        }
    } else {
        /* a converted boss (boss_params): the Finish waits in the middle of the map, never spawned by
           proximity (0x2000), and the boss links to it (BOSS_FINISH below); FLINK.DLL spawns it once
           the boss is defeated (forestboss.c) */
        int ex, fy = 0, has;
        if (!end_p)
            for (i = 0; i < pl.n && !boss; i++) {
                const Place *p = &pl.v[i];
                Recs *r = world_recs(fw, p->slot);
                if (!r) continue;
                if (!strcmp(p->name, "Donkey monkey") || !strcmp(p->name, "Bos1")) boss = p;
                else if (!strcmp(p->name, "Boss")) {
                    static const char *const BT[5] = { "WALK", "JUMP", "OUCH", "NODE", "DEAD" };
                    static const char *const VT[12] = { "RUN ", "WALK", "STAY", "DEAD", "MEN1", "MEN2", "MEN3", "MEN4", "MEN5", "BAL1", "BAL2", "BAL3" };
                    static const char *const MT[8] = { "EINS", "ZWEI", "DREI", "VIER", "ZAP1", "ZAP2", "ZAP3", "WOLK" };
                    int a = 1, b = 1, c = 1;
                    for (k = 0; k < 5; k++) if (!recs_has(r, BT[k])) a = 0;
                    for (k = 0; k < 12; k++) if (!recs_has(r, VT[k])) b = 0;
                    for (k = 0; k < 8; k++) if (!recs_has(r, MT[k])) c = 0;
                    if (a || b || c) boss = p;
                }
            }
        if (end_p) { ex = end_p->x; has = ground_at(&ctx, end_p->x, end_p->y, &fy); }
        else if (boss) {
            ex = !strcmp(boss->name, "Donkey monkey") ? w8 * 8 - DONK_SCREEN / 2 : w8 * 4;
            has = ground_at(&ctx, ex, boss->y, &fy);
        } else {
            has = 0;
            ex = w8 * 8 - 48;
            for (k = w8 * 8 - 48; k > w8 * 4; k -= 16) {
                int f;
                if (ground_at(&ctx, k, start->y - 64, &f) && f) { ex = k; fy = f; has = 1; break; }
            }
        }
        endx = ex;
        endy = has && fy ? trapdoor_y(&ctx, ex, fy, TRAPDOOR_SMALL) : (end_p ? end_p->y : start->y) - 48;
        if (boss) boss_finish = objs.n;
        pushf(&objs, T_FINISH, endx, endy, boss ? 0x2000 : 0, NULL, 0, NULL);
    }
    /* Flink's chests holding an information note: Lomax hint hats (see chest_content) */
    for (i = 0; i < types.n; i++) if (!strncmp(types.v[i].name, "Loro", 4) && types.v[i].nparams >= 1) { T_HAT = i; break; }
    for (i = 0; i < pl.n; i++) {
        const Place *p = &pl.v[i];
        int item;
        if (strcmp(p->name, "Chest")) continue;
        item = s16v(prm(p, 0, 0));
        if (item > -100 && item < 0 && T_HAT >= 0) {
            int hp = HINT_BASE + (-item - 1);
            pushf(&objs, T_HAT, p->x, p->y - HINT_HAT_HEIGHT, 0, &hp, 1, p);
            nhats++;
            hints_add(-item - 1);                           /* (its text goes into LEVELS.INI's [Hints]) */
        } else { vpush(chests, p); vpush(chestItem, item); }
    }
    memset(animbits, 0, sizeof(animbits));
    for (i = 0; i < objs.n; i++) for (k = 0; k < 32; k++) animbits[k] |= types.v[objs.v[i].t].banks.b[k];
    for (i = 0; i < g_tpl.ch.n; i++) {                  /* only the template sprites they use */
        const LChunk *c = &g_tpl.ch.v[i];
        int b;
        if (strcmp(c->tag, "ANIM")) continue;
        b = (c->p.p[8] | c->p.p[9] << 8) + 1;
        if (b < 256 && animbits[b >> 3] & (0x80 >> (b & 7))) {
            Buf a = { 0 };
            buf_add(&a, c->p.p, c->p.n);
            vpush(tpl_anims, a);
        }
    }
    template_sprite_colours(&tpl_anims, thead->p, pal);

    /* Flink's platforms: new types appended to the template's, with Flink's sprites */
    memset(&sprites, 0, sizeof(sprites));
    sprites.w = fw;
    wp = find_place(&pl, "Water");
    if (wp) {
        int tint[3];
        Recs *wr = world_recs(fw, wp->slot);
        Rec *wave = wr ? (Rec *)rec_get(wr, "WAVE") : NULL;
        ctx.hasWater = 1;
        ctx.water = water_surface(wp, 1);
        ctx.mean = water_surface(wp, 0);
        for (i = 0; i < 3; i++) tint[i] = s16v(prm(wp, i, 0));
        ctx.tint = (int)water_tint(play->head, tint);
        pal[WATER_TINT] = ctx.tint ? (unsigned)ctx.tint : 1;
        if (!wave) die("%s map %d: its Water has no WAVE record", world, m);
        for (i = 0; i < wave->npc; i++)                  /* the waves where Flink draws them (WAVE_TOP) */
            if (!(wave->pc[i].a & 0x80) && !(wave->pc[i].f & 0x80)) wave->pc[i].dy = WAVE_TOP;
        ctx.wave_dy = WAVE_TOP;
    }
    memset(&po, 0, sizeof(po));
    convert_platforms(fw, &pl, &sprites, types.n, &ctx, &po);
    ptypes = &po.types;
    pbanks = &po.banks;
    for (i = 0; i < po.objs.n; i++) vpush(objs, po.objs.v[i]);
    coll = xalloc((size_t)(mw * mh));
    for (i = 0; i < mw * mh; i++) coll[i] = (u8)((ctx.words[i] >> 8) & 0xF8);

    /* Flink's Deadly ground (0x28498): spikes, a strip x +- param, y..y+16 that hurts Flink when
       touched; the spikes are tiles over a gap in the floor. Lomax gets a floor there, level with the
       floor beside it, and a 'Spik Deadly' object (Lomax's 'Spik' is a decoration, object 51) that
       FLINK.DLL makes hurt him (Player_TakeHit) while he stands in it. */
    {
        PlaceList deadly = { 0 };
        for (i = 0; i < pl.n; i++) if (!_stricmp(pl.v[i].name, "deadly ground")) vpush(deadly, &pl.v[i]);
        if (deadly.n) {
            LRec r = blank_record("MAIN", &sprites, PLATFORM_EDGE);
            int t = types.n + ptypes->n;
            add_type(ptypes, pbanks, "Spik Deadly", 1, 0, &r, 1);
            for (i = 0; i < deadly.n; i++) {
                const Place *p = deadly.v[i];
                int hw = s16v(prm(p, 0, 32)), line, have = 0, q;
                if (hw < 0) hw = -hw;
                hw = imax(8, hw);
                line = 0;
                for (q = 0; q < 2; q++) {
                    int x = q ? p->x + hw + 8 : p->x - hw - 8, gg;
                    if (ground_at(&ctx, x, p->y - 16, &gg) && abs(gg - p->y) <= 24) { line = have ? imin(line, gg) : gg; have = 1; }
                }
                if (!have) line = p->y + 8;
                floor_along(&ctx, coll, p->x - hw, p->x + hw, line);
                pushf(&objs, t, p->x, line, 0, &hw, 1, p);
            }
        }
        free(deadly.v);
    }

    /* Flink's Knol spiky (0x2B1FC, think 0x2B214, FOREST): a Knol on a vine that loops its KNOL
       animation (2 frames) and hurts Flink wherever his hitbox meets its own (0x1A462: from above too).
       Lomax's decoration does the same with no FLINK.DLL code: Deco_Spawn (0x427060) sets the actor's
       flags 0x200020 (hurts, whatever Lomax's animation) and Deco_Update (0x427090) only advances the
       animation; without the hittable flag 0x10000, Player_Update's contact loop hurts Lomax on any
       overlap instead of letting him stomp it. A 'Spik Knol' with Flink's KNOL record and hitbox. */
    {
        static const char *const T[1] = { "KNOL" };
        PlaceList knols = with(fw, &pl, "Knol spiky", T, 1);
        if (knols.n) {
            LRec r = lomax_record1("KNOL", rec_get(world_recs(fw, knols.v[0]->slot), "KNOL"), &sprites, 0, 0);
            int t = types.n + ptypes->n;
            add_type(ptypes, pbanks, "Spik Knol", 0, 0, &r, 1);
            for (i = 0; i < knols.n; i++) pushf(&objs, t, knols.v[i]->x, knols.v[i]->y, 0, NULL, 0, knols.v[i]);
        }
        nknols = knols.n;
        free(knols.v);
    }

    /* Flink's deadly swinging balls, 'Ball (swinging)' with checkbox 8 'deadly' (init 0x2B3EA, the
       pivot's think 0x2B5E8, each ball's think 0x2B6BA and draw 0x2B752): a pivot (KNOB with checkbox
       4 'knob') and 'arms' spiked balls (BIGG) turning round it, (length - 1) * 16 px out, each with its
       chain of RING links every 16 px from the pivot; with 0 arms one ball swinging as a pendulum.
       Touching a ball hurts (0x1A462). A 'Spik Ball' (Lomax's decoration, object 51) that FLINK.DLL's
       balls.c turns and draws. Without the knob the pivot draws its RING record (init 0x2B472 sets
       RING on it, KNOB only with the checkbox; its draw 0x2B6B2 is Actor_DrawFrame), a link at the
       pivot, before the chain's from 16 px out. Records: MAIN the knob (RING without), BALL, RING (as Flink's
       Sprite_DrawScreen 0x3430E draws it: its first hardware sprite's top-left 8 px up-left of the
       point, see the lianas). Params: radius px, arms, speed (2048 a turn), the bounce angle (Flink's
       'winkel' * 2, -1 for none: 0 is none in Flink), pre-run frames, checkbox 1 'dont move', and its
       draw list (0x2B404: spawn flags bits 2-3 pick 0x7D70, 0x7D6E, 0x7D6C, 0x7D6A, lists 3, 2, 1, 0;
       FLINK.DLL's FLINK_LAYER gives Lomax's draw layer): 13-1's and 16-1's balls are on list 2, in
       front of the Wall platforms (list 3) and behind Flink. */
    {
        static const char *const T[3] = { "BIGG", "RING", "KNOB" };
        PlaceList all = with(fw, &pl, "Ball (swinging)", T, 3), balls = { 0 };
        int tball[5] = { -1, -1, -1, -1, -1 };
        for (i = 0; i < all.n; i++) if (all.v[i]->check & 8) vpush(balls, all.v[i]);
        if (balls.n) {
            Recs *rec = world_recs(fw, balls.v[0]->slot);
            LRec r0 = lomax_record1("MAIN", rec_get(rec, "KNOB"), &sprites, 0, 0);
            LRec rb = lomax_record1("MAIN", rec_get(rec, "RING"), &sprites, 0, 0);
            LRec r1 = lomax_record1("BALL", rec_get(rec, "BIGG"), &sprites, 0, 0);
            LRec r2 = ring_record(fw, rec_get(rec, "RING"), &sprites);
            int knob;
            for (knob = 4; knob >= 0; knob -= 4) {
                int any = 0;
                for (i = 0; i < balls.n; i++) if ((balls.v[i]->check & 4) == knob) any = 1;
                if (any) {
                    LRec rr[3];
                    rr[0].d.n = 0;
                    tball[knob] = types.n + ptypes->n;
                    rr[0] = knob ? r0 : rb; rr[1] = r1; rr[2] = r2;
                    {
                        Buf t = { 0 };
                        lomax_type(&t, "Spik Ball", 7, 0, rr, 3);
                        vpush(*ptypes, t);
                        bank_union(pbanks, &rr[0].banks); bank_union(pbanks, &r1.banks); bank_union(pbanks, &r2.banks);
                    }
                    nballTypes++;
                }
            }
            for (i = 0; i < balls.n; i++) {
                const Place *p = balls.v[i];
                int length = prm(p, 0, 0), wk = prm(p, 3, 0), bp[7];
                bp[0] = imax(0, length - 1) * 16; bp[1] = prm(p, 2, 0); bp[2] = prm(p, 1, 0);
                bp[3] = wk ? wk * 2 : 0xFFFF; bp[4] = prm(p, 4, 0); bp[5] = p->check & 1; bp[6] = 3 - ((p->sf & 0xC) >> 2);
                pushf(&objs, tball[p->check & 4], p->x, p->y, 0, bp, 7, p);
            }
            free_lrec(&r0); free_lrec(&rb); free_lrec(&r1); free_lrec(&r2);
        }
        nballs = balls.n;
        free(all.v); free(balls.v);
    }

    /* Flink's Stahlkugeln, Stahlkugel (init 0x24A24, VILLAGE: 13-2, 14-2, 15-1): a spiked ball on a
       chain that swings toward and away from the screen, loops round that way ('turn around') or, with
       'rotating', turns round a vertical axis, its chain a cone; Flink draws it at 7 sizes for the
       depth. See stahlkugel.c. Its first placement (spawn flag bit 6, no params) only builds the scaled
       sprites (0x24D88) and is left out. A 'Spik Stahl' (Lomax's decoration, object 51) with records
       MAIN (blank), BALL (ZWEI), LINK (EINS, a deadly one's link) and RING (DREI, a harmless link),
       each one sprite anchored at its middle, as Actor_Draw scales a sprite round its anchor. Params:
       Flink's five (length, speed, x-offset, y-offset, pre-run) and the checkboxes (1 rotating,
       2 deadly, 4 turn around). */
    {
        static const char *const T[3] = { "EINS", "ZWEI", "DREI" };
        PlaceList all = with(fw, &pl, "Stahlkugel", T, 3), st = { 0 };
        for (i = 0; i < all.n; i++) if (!(all.v[i]->sf & 0x40)) vpush(st, all.v[i]);
        if (st.n) {
            static const char *const TAG[3] = { "BALL", "LINK", "RING" }, *const SRC[3] = { "ZWEI", "EINS", "DREI" };
            Recs *rec = world_recs(fw, st.v[0]->slot);
            LRec made[4];
            int t;
            made[0] = blank_record("MAIN", &sprites, 0);
            for (k = 0; k < 3; k++) {
                const Rec *r = rec_get(rec, SRC[k]);
                int a = r->pc[0].a, f = r->pc[0].f, x0 = 1 << 30, x1 = -1, y0 = 1 << 30, y1 = -1, j;
                Frames fr = flink_anim_frames(fw->anims.v[a]);
                Frame *F = &fr.f[f];
                Ent e;
                for (j = 0; j < F->w * F->h; j++)
                    if (F->px[j]) { x0 = imin(x0, j % F->w); x1 = imax(x1, j % F->w); y0 = imin(y0, j / F->w); y1 = imax(y1, j / F->w); }
                if (x1 < 0) die("Stahlkugel %s: an empty sprite", SRC[k]);
                e.bank = sprites_bank(&sprites, SK_A(a)); e.frame = f + 1;
                e.dx = -fdiv(x0 + x1 + 1, 2); e.dy = -fdiv(y0 + y1 + 1, 2); e.fl = 0x80;
                made[k + 1] = pack_record(TAG[k], r->speed, r->hb, &e, 1);
            }
            t = types.n + ptypes->n;
            add_type(ptypes, pbanks, "Spik Stahl", 6, 0, made, 4);
            for (i = 0; i < st.n; i++) {
                const Place *p = st.v[i];
                int sp[6];
                for (k = 0; k < 5; k++) sp[k] = prm(p, k, 0);
                sp[5] = p->check & 7;
                pushf(&objs, t, p->x, p->y, 0, sp, 6, p);
            }
        }
        nstahls = st.n;
        free(all.v); free(st.v);
    }

    /* Flink's Spikes, Spike (init 0x2856E, think 0x28608; VILLAGE 7, 11 = 13-1, 20, TREE 3): a
       spike that shoots out of the ground on a timer and hurts while fully out (0x1A462). See spike.c.
       All the converted ones point up (VERT, checkbox 1 'horizontal' and 2 'reverse' unset), most in
       rows that a Generator spawns, staggered by their pre-runs into a wave. A 'Spik Spike' (Lomax's
       decoration, object 51) with Flink's VERT record and hitbox; params: hidden frames, out frames
       (param 1: Flink adds 30), pre-run frames. */
    {
        static const char *const T[1] = { "VERT" };
        PlaceList all = with(fw, &pl, "Spike", T, 1), sp = { 0 };
        for (i = 0; i < all.n; i++) if (!(all.v[i]->check & 3)) vpush(sp, all.v[i]);
        if (sp.n) {
            LRec r = lomax_record1("VERT", rec_get(world_recs(fw, sp.v[0]->slot), "VERT"), &sprites, 0, 0);
            int t = types.n + ptypes->n;
            add_type(ptypes, pbanks, "Spik Spike", 3, 0, &r, 1);
            for (i = 0; i < sp.n; i++) {
                int q[3];
                for (k = 0; k < 3; k++) q[k] = prm(sp.v[i], k, 0);
                pushf(&objs, t, sp.v[i]->x, sp.v[i]->y, 0, q, 3, sp.v[i]);
            }
        }
        nspikes = sp.n;
        free(all.v); free(sp.v);
    }

    /* Flink's Drop Ball (init 0x27B24, think 0x27B94, chain draw 0x27C60; VILLAGE 6 = 10-3): a spike
       ball on a chain that waits 'fall-timer' frames, drops 'fall-height' px, shakes and is pulled back
       up; touching the ball hurts (0x1A462). See drop.c. A 'Spik Drop' (Lomax's decoration, object 51)
       with Flink's BALL record (the ball and its top link, its hitbox) and APPE (the chain's link),
       which FLINK.DLL draws every 32 px up off the screen. BALL's hook piece (selector 1, at -3, -33)
       calls the +8 hook (Actor_DrawFrame 0x32972) at that point, and the hook's Sprite_DrawScreen puts
       each link's top-left 32 px further up from it: so APPE is the link at the hook's offsets.
       Params: fall-height, fall-timer, pre-run. Flink's init runs the think $10(a4) times: the word
       after its two params, which is the next GNMY entry's size (0 at the list's end). */
    {
        static const char *const T[2] = { "BALL", "APPE" };
        PlaceList dr = with(fw, &pl, "Drop Ball", T, 2);
        if (dr.n) {
            Recs *rec = world_recs(fw, dr.v[0]->slot);
            LRec r[2];
            int hx, hy, t;
            const Piece *lp;
            Ent e;
            r[0] = lomax_record1("BALL", rec_get(rec, "BALL"), &sprites, 0, 0);
            attach_point(rec_get(rec, "BALL"), 1, &hx, &hy);
            lp = first_sprite_of(rec_get(rec, "APPE"));
            e.bank = sprites_bank(&sprites, SK_A(lp->a)); e.frame = lp->f + 1; e.dx = hx; e.dy = hy; e.fl = 0x80;
            r[1] = pack_record("APPE", 0x40, rec_get(rec, "APPE")->hb, &e, 1);
            t = types.n + ptypes->n;
            add_type(ptypes, pbanks, "Spik Drop", 3, 0, r, 2);
            for (i = 0; i < dr.n; i++) {
                const Place *p = dr.v[i];
                int q[3];
                q[0] = prm(p, 0, 0); q[1] = prm(p, 1, 0);
                /* Flink's init runs the think the word after its two params times: the next entry's size */
                q[2] = (size_t)p->off + 18 <= play->gnmy.n ? (int)be16(play->gnmy.p + p->off + 16) : 0;
                pushf(&objs, t, p->x, p->y, 0, q, 3, p);
            }
        }
        ndrops = dr.n;
        free(dr.v);
    }

    /* Flink's Falling apple (init 0x28E02, think 0x28E30; TREE 3, 7, 8 = 25-1, 28-1, 25-2): an apple
       hanging in a tree that shakes (WARN) when Flink walks under it, falls and bursts on the ground or
       on him. See apple.c. A 'Spik Apple' (Lomax's decoration, object 51) with Flink's HANG and WARN
       (and their hitbox) and DEAD as BOOM (Actor_ShatterIntoPieces: Flink's Actor_ForgetForever breaks
       the DEAD frame up); param: distance. */
    {
        static const char *const T[3] = { "HANG", "WARN", "DEAD" };
        PlaceList ap = with(fw, &pl, "Falling apple", T, 3);
        if (ap.n) {
            Recs *rec = world_recs(fw, ap.v[0]->slot);
            LRec r[3];
            r[0] = lomax_record1("HANG", rec_get(rec, "HANG"), &sprites, 0, 0);
            r[1] = lomax_record1("WARN", rec_get(rec, "WARN"), &sprites, 0, 0);
            r[2] = lomax_record1("BOOM", rec_get(rec, "DEAD"), &sprites, 0, 0);
            t_apple = types.n + ptypes->n;
            add_type(ptypes, pbanks, "Spik Apple", 1, 0, r, 3);
            for (i = 0; i < ap.n; i++) { int q = prm(ap.v[i], 0, 0); pushf(&objs, t_apple, ap.v[i]->x, ap.v[i]->y, 0, &q, 1, ap.v[i]); }
        }
        napples = ap.n;
        free(ap.v);
    }

    /* Flink's Apple (big) (init 0x24242) and App2 (init 0x24204; TREE 4, 6, 7 = 26-1, 27-2, 28-1):
       rolling apples, the big one pushed, App2 carried and broken. See bigapple.c. Each map's first
       placement of each (spawn flag bit 6) only builds the rotated frames (0x24724) and is left out;
       its checkbox 1 ('16 phases') gives every other one its phases. A 'Spik Appl' / 'Spik App2'
       (Lomax's decoration, object 51) with ROLL at 16 angles (bigapple_records), App2's DEAD (its
       break-up) and MAIN.WLD's Shot SPIN (thrown as a shot); params start speed, phases (16 or 8). */
    {
        static const char *const N[2] = { "Apple (big)", "App2" }, *const L[2] = { "Spik Appl", "Spik App2" };
        static const char *const T[1] = { "ROLL" };
        for (k = 0; k < 2; k++) {
            PlaceList mine = with(fw, &pl, N[k], T, 1), setup = { 0 }, live = { 0 };
            for (i = 0; i < mine.n; i++) { if (mine.v[i]->sf & 0x40) vpush(setup, mine.v[i]); else vpush(live, mine.v[i]); }
            if (live.n) {
                int phases = !setup.n || setup.v[0]->check & 1 ? 16 : 8, n, t;
                LRec r[3];
                n = bigapple_records(N[k], world_recs(fw, live.v[0]->slot), &sprites, r);
                t = types.n + ptypes->n;
                if (k == 1) t_app2 = t;
                add_type(ptypes, pbanks, L[k], 2, 0, r, n);
                for (i = 0; i < live.n; i++) { int q[2] = { prm(live.v[i], 0, 0), phases }; pushf(&objs, t, live.v[i]->x, live.v[i]->y, 0, q, 2, live.v[i]); }
                rolls[nrollTypes].name = N[k]; rolls[nrollTypes].n = live.n; nrollTypes++;
            }
            free(mine.v); free(setup.v); free(live.v);
        }
    }

    /* Flink's Butterflies (init 0x247D8, think 0x2488A, each pair's draw 0x2497C; TREE 3, 4, 5, 8 =
       25-1, 26-1, 27-1, 25-2): a placement that lets 'how many' pairs of butterflies fly off, one pair
       every 20 frames, and is then removed. See butterfly.c. Flink draws each butterfly with
       Sprite_DrawScreen (0x3430E): the FLY frame's one hardware sprite (0x34132), its top-left at the
       butterfly's point, h-flipped (sprite bit 11) without 'Move left'. A 'Spik Butterfly' (Lomax's
       decoration, object 51) with FLY's five sprites as one-sprite frames at (0, 0) less the sprite's
       offset in its ANIM frame, and FLYR, the same flipped in place; params how many, x-speed,
       y-speed, checkbox 1 'Move left'. */
    {
        static const char *const T[1] = { "FLY " };
        PlaceList bf = with(fw, &pl, "Butterflies", T, 1);
        if (bf.n) {
            const Rec *fly = rec_get(world_recs(fw, bf.v[0]->slot), "FLY ");
            int sa[5], sf[5], ns = 0, q, t;
            LRec r[2];
            for (i = 0; i < fly->npc && ns < 5; i++)
                if (!(fly->pc[i].a & 0x80) && !(fly->pc[i].f & 0x80)) { sa[ns] = fly->pc[i].a; sf[ns] = fly->pc[i].f; ns++; }
            for (q = 0; q < 2; q++) {
                Ent e[5];
                for (i = 0; i < ns; i++) {
                    int xo, yo;
                    anim_piece_offset(fw->anims.v[sa[i]], sf[i], &xo, &yo);
                    e[i].bank = sprites_bank(&sprites, SK_A(sa[i])); e[i].frame = sf[i] + 1;
                    e[i].dx = -xo; e[i].dy = -yo; e[i].fl = 0x80 | (q ? 0x08 : 0);
                }
                r[q] = pack_record(q ? "FLYR" : "FLY ", 0x40, fly->hb, e, ns);
            }
            t = types.n + ptypes->n;
            add_type(ptypes, pbanks, "Spik Butterfly", 4, 0, r, 2);
            for (i = 0; i < bf.n; i++) {
                int pp[4] = { prm(bf.v[i], 0, 0), prm(bf.v[i], 1, 0), prm(bf.v[i], 2, 0), bf.v[i]->check & 1 };
                pushf(&objs, t, bf.v[i]->x, bf.v[i]->y, 0, pp, 4, bf.v[i]);
            }
        }
        nbutterflies = bf.n;
        free(bf.v);
    }

    /* Flink's Lava (init 0x2712A, think 0x271F8, draw 0x273BE; MOUNTAIN 14, 15 = 23-1, 23-2): a
       column of lava blobs rising out of the lava's surface, a hop and a burst in turn, its tail falling
       back and droplets spraying at its foot; its height above the surface hurts (0x1A46E). See lava.c.
       It reads the surface (g_WaterSurfaceY + 10), not its placement's y. A 'Spik Lava' (Lomax's
       decoration, object 51) with Flink's HEAD (2 frames, its hitbox) and TAIL (3 frames: the tail's
       blobs and the droplets, each piece at Flink's (-16, -32)); no params. */
    {
        static const char *const T[2] = { "HEAD", "TAIL" };
        PlaceList lv = with(fw, &pl, "Lava", T, 2);
        if (lv.n) {
            Recs *rec = world_recs(fw, lv.v[0]->slot);
            LRec r[2];
            int t;
            r[0] = lomax_record1("HEAD", rec_get(rec, "HEAD"), &sprites, 0, 0);
            r[1] = lomax_record1("TAIL", rec_get(rec, "TAIL"), &sprites, 0, 0);
            t = types.n + ptypes->n;
            add_type(ptypes, pbanks, "Spik Lava", 0, 0, r, 2);
            for (i = 0; i < lv.n; i++) pushf(&objs, t, lv.v[i]->x, lv.v[i]->y, 0, NULL, 0, lv.v[i]);
        }
        nlavas = lv.n;
        free(lv.v);
    }

    /* Flink's other lava kinds (MOUNTAIN 14, 15, 16 = 23-1, 23-2 and map 16), each a Lomax decoration
       (object 51) run by FLINK.DLL, its records Flink's with their pieces at Flink's offsets and hitboxes
       (a row of a column is two halves, the right one flipped: the records' frames):
       - Protuberance (init 0x265CA): a stream of fireballs leaping in an arc; 'Spik Prot' with FIRE;
         params timer, height, how-many, checkbox 1 'explode-left'. See protuberance.c.
       - Great balls of fire (init 0x26924): fireballs thrown up four at a time; 'Spik Great' with HEAD,
         TAIL (their sparks) and LAVA (the droplets); param timer. See greatballs.c.
       - Burst of lava (init 0x26EF4): a column shooting up off the top of the screen; 'Spik Burst' with
         EINS (the bubble), ZWEI (the body), DREI (the top end) and DRUV (DREI upside down: the bottom
         end, Flink's DREI drawn v-flipped); params length, timer. See burst.c.
       - Fountain (init 0x26C28): a geyser throwing a Throw platform up; 'Spik Fountain' with EINS (its
         foot), ZWEI (the column), DREI (the top); params speed-y, timer, checkbox 1 'wait'. See
         fountain.c. */
    {
        static const struct { const char *name, *lname; int nt; const char *tag[4], *src[4]; } K[4] = {
            { "Protuberance", "Spik Prot", 1, { "FIRE" }, { "FIRE" } },
            { "Great balls of fire", "Spik Great", 3, { "HEAD", "TAIL", "LAVA" }, { "HEAD", "TAIL", "LAVA" } },
            { "Burst of lava", "Spik Burst", 4, { "EINS", "ZWEI", "DREI", "DRUV" }, { "EINS", "ZWEI", "DREI", "DREI" } },
            { "Fountain", "Spik Fountain", 3, { "EINS", "ZWEI", "DREI" }, { "EINS", "ZWEI", "DREI" } } };
        int q;
        for (q = 0; q < 4; q++) {
            PlaceList mine = with(fw, &pl, K[q].name, K[q].src, K[q].nt);
            if (mine.n) {
                Recs *rec = world_recs(fw, mine.v[0]->slot);
                LRec r[4];
                int t, np = q == 0 ? 4 : q == 1 ? 1 : q == 2 ? 2 : 3;
                for (k = 0; k < K[q].nt; k++) {
                    const Rec *src = rec_get(rec, K[q].src[k]);
                    if (q == 2 && k == 3) {                     /* vflip */
                        Rec fl = *src;
                        int j;
                        fl.pc = xalloc((size_t)(src->npc ? src->npc : 1) * sizeof(Piece));
                        for (j = 0; j < src->npc; j++) {
                            fl.pc[j] = src->pc[j];
                            if (!(fl.pc[j].a & 0x80) && !(fl.pc[j].f & 0x80)) fl.pc[j].fl ^= 0x10;
                        }
                        r[k] = lomax_record1(K[q].tag[k], &fl, &sprites, 0, 0);
                        free(fl.pc);
                    } else r[k] = lomax_record1(K[q].tag[k], src, &sprites, 0, 0);
                }
                t = types.n + ptypes->n;
                add_type(ptypes, pbanks, K[q].lname, np, 0, r, K[q].nt);
                for (i = 0; i < mine.n; i++) {
                    const Place *p = mine.v[i];
                    int pp[4];
                    if (q == 0) { pp[0] = prm(p, 0, 0); pp[1] = prm(p, 1, 0); pp[2] = prm(p, 2, 0); pp[3] = p->check & 1; }
                    else if (q == 1) pp[0] = prm(p, 0, 0);
                    else if (q == 2) { pp[0] = prm(p, 0, 0); pp[1] = prm(p, 1, 0); }
                    else { pp[0] = prm(p, 0, 0); pp[1] = prm(p, 1, 0); pp[2] = p->check & 1; }
                    pushf(&objs, t, p->x, p->y, 0, pp, np, p);
                }
                lavaKinds[nlavaKinds].name = K[q].name; lavaKinds[nlavaKinds].n = mine.n; nlavaKinds++;
            }
            free(mine.v);
        }
    }

    /* Flink's Rolling stone (init 0x23E26, think 0x23E74, draw 0x24062; MOUNTAIN 14 = 23-1): a stone ball
       the player stands on and rolls the other way by walking on it, across the shallow lava. See
       rollingstone.c. The map's first placement (spawn flag bit 6) only builds the rotated frames
       (0x24148) and is left out; its checkbox 1 ('16 phases') gives every other one its phases. A 'Spik
       Rolling' (Lomax's decoration, object 51) with ROLL at 16 angles (bigapple_records); param phases. */
    {
        static const char *const T[1] = { "ROLL" };
        PlaceList mine = with(fw, &pl, "Rolling stone", T, 1), setup = { 0 }, live = { 0 };
        for (i = 0; i < mine.n; i++) { if (mine.v[i]->sf & 0x40) vpush(setup, mine.v[i]); else vpush(live, mine.v[i]); }
        if (live.n) {
            int phases = !setup.n || setup.v[0]->check & 1 ? 16 : 8, n, t;
            LRec r[3];
            n = bigapple_records("Rolling stone", world_recs(fw, live.v[0]->slot), &sprites, r);
            t = types.n + ptypes->n;
            add_type(ptypes, pbanks, "Spik Rolling", 1, 0, r, n);
            for (i = 0; i < live.n; i++) pushf(&objs, t, live.v[i]->x, live.v[i]->y, 0, &phases, 1, live.v[i]);
            lavaKinds[nlavaKinds].name = "Rolling stone"; lavaKinds[nlavaKinds].n = live.n; nlavaKinds++;
        }
        free(mine.v); free(setup.v); free(live.v);
    }

    /* Flink's Rope slide ('Rope slide', init 0x27EB8; VILLAGE 7, 8, 15 = 10-4, 11-1, 15-2): a
       handle that carries Flink down a cable drawn in the background, 1 px down every 'angle' px for
       'length' px (left with checkbox 1). See ropeslide.c. A 'Spik Slide' (Lomax's decoration, object 51)
       with Flink's ROPE record (the handle, its hitbox); params angle, length, left. */
    {
        static const char *const T[1] = { "ROPE" };
        static const int D[2] = { 2, 0 };
        PlaceList sl = with(fw, &pl, "Rope slide", T, 1);
        if (sl.n) {
            LRec r = lomax_record1("ROPE", rec_get(world_recs(fw, sl.v[0]->slot), "ROPE"), &sprites, 0, 0);
            int t = types.n + ptypes->n;
            add_type(ptypes, pbanks, "Spik Slide", 3, 0, &r, 1);
            for (i = 0; i < sl.n; i++) {
                int q[3] = { prml(sl.v[i], 0, D), prml(sl.v[i], 1, D), sl.v[i]->check & 1 };
                pushf(&objs, t, sl.v[i]->x, sl.v[i]->y, 0, q, 3, sl.v[i]);
            }
        }
        nslides = sl.n;
        free(sl.v);
    }

    /* Flink's Snake, Snake (0x251BA, FOREST 8 = 3-2): see snake_records. Its first placement
       (spawn flag bit 6, no params) only builds the scaled sprites (0x2552E) and is left out. */
    {
        static const char *const T[4] = { "HEAD", "BODY", "BUB1", "BUB2" };
        PlaceList all = with(fw, &pl, "Snake", T, 4), sn = { 0 };
        for (i = 0; i < all.n; i++) if (!(all.v[i]->sf & 0x40)) vpush(sn, all.v[i]);
        if (sn.n) {
            LRec r[5];
            int n = snake_records(world_recs(fw, sn.v[0]->slot), &sprites, r), t = types.n + ptypes->n;
            add_type(ptypes, pbanks, "Spik Snake", 3, 0, r, n);
            for (i = 0; i < sn.n; i++) {
                int q[3] = { prm(sn.v[i], 0, 0), prm(sn.v[i], 1, 0), prm(sn.v[i], 2, 0) };
                pushf(&objs, t, sn.v[i]->x, sn.v[i]->y, 0, q, 3, sn.v[i]);
            }
        }
        nsnakes = sn.n;
        free(all.v); free(sn.v);
    }

    /* Flink's Ground type, its Ground type actors (0x2905E, think 0x29082; FOREST maps 7 and 11): ordinary
       ground, made mud by an actor at each edge that sets the ground type to param 0 left of it and
       param 1 right of it as Flink passes; on type 1 he sinks (FLINK.DLL's groundtype.c). A 'Spik Ground' (a
       Lomax decoration) with the same params, a blank MAIN (it draws nothing) and Flink's MUD record,
       which FLINK.DLL draws at Lomax's feet while he is on the mud. */
    {
        static const char *const T[1] = { "MUD " };
        PlaceList gr = with(fw, &pl, "Ground type", T, 1);
        if (gr.n) {
            LRec r[2];
            int t;
            r[0] = blank_record("MAIN", &sprites, 0);
            r[1] = lomax_record1("MUD ", rec_get(world_recs(fw, gr.v[0]->slot), "MUD "), &sprites, 0, 0);
            t = types.n + ptypes->n;
            add_type(ptypes, pbanks, "Spik Ground", 2, 0, r, 2);
            for (i = 0; i < gr.n; i++) {
                int q[2] = { prm(gr.v[i], 0, 0), prm(gr.v[i], 1, 0) };
                pushf(&objs, t, gr.v[i]->x, gr.v[i]->y, 0, q, 2, gr.v[i]);
            }
        }
        ngrounds = gr.n;
        free(gr.v);
    }

    /* Flink's Bridge (see BRIDGE_SAG): a 'Mega Bridge', a static Mega (object 64) whose update and
       draw FLINK.DLL replaces (bridge.c): it draws the 12 planks and is the standing surface, plank by
       plank, until they fall. Its record MAIN is one plank, WOOD's first hardware sprite (the one
       Flink's init 0x27CA4 builds and its draw 0x27E5E queues at each plank's top-left), with its
       top-left at the actor's point, and Flink's standing box (+0x46: x - 80 .. x + 80) as the hitbox,
       cut on the left to x - 64, where the sag table starts (further left Flink reads the code before
       the table: no floor there), and y1 2: the surface is y + 2 + sag (bridge_surface adds the sag). */
    {
        static const char *const T[1] = { "WOOD" };
        PlaceList br = with(fw, &pl, "Bridge", T, 1);
        if (br.n) {
            const Piece *p0 = first_sprite_of(rec_get(world_recs(fw, br.v[0]->slot), "WOOD"));
            int xo, yo, hb[4] = { -64 - JUMPPAD_EDGE, 2, 80 + JUMPPAD_EDGE, 30 }, t, zero[5] = { 0, 0, 0, 0, 0 };
            Ent e;
            LRec r;
            anim_piece_offset(fw->anims.v[p0->a], p0->f, &xo, &yo);
            e.bank = sprites_bank(&sprites, SK_A(p0->a)); e.frame = p0->f + 1; e.dx = -xo; e.dy = -yo; e.fl = 0x80;
            r = pack_record("MAIN", 0x40, hb, &e, 1);
            t = types.n + ptypes->n;
            add_type(ptypes, pbanks, "Mega Bridge", 5, 5, &r, 1);
            for (i = 0; i < br.n; i++) pushf(&objs, t, br.v[i]->x, br.v[i]->y, 1, zero, 5, br.v[i]);
        }
        nbridges = br.n;
        free(br.v);
    }

    /* Flink's enemies (see enemies.c) */
    memset(&eo, 0, sizeof(eo));
    convert_enemies(fw, &pl, &sprites, types.n + ptypes->n, &eo);
    for (i = 0; i < eo.types.n; i++) vpush(*ptypes, eo.types.v[i]);
    bank_union(pbanks, &eo.banks);
    for (i = 0; i < eo.objs.n; i++) vpush(objs, eo.objs.v[i]);
    netypes = eo.types.n;

    /* the Donkey monkey's apples (see DONK_APPLE_Y): a Falling apple and an App2 that FLINK.DLL
       (donkey.c) spawns where Flink's think spawns them, never spawned by proximity (0x2000) */
    for (i = 0; i < pl.n; i++) {
        const Place *p = &pl.v[i];
        int placed = 0;
        if (strcmp(p->name, "Donkey monkey")) continue;
        for (k = 0; k < eo.objs.n; k++) if (eo.objs.v[k].p == p) placed = 1;
        if (!placed) continue;
        if (t_apple < 0 || t_app2 < 0) die("Donkey monkey: its map has no Falling apple or App2 type");
        {
            int a = 1000, b[2] = { 0, 16 };                 /* DONK_APPLE_DISTANCE; App2's start speed 0, 16 phases */
            pushf(&objs, t_apple, p->x, 0x3C, 0x2000, &a, 1, NULL);   /* DONK_APPLE_Y */
            pushf(&objs, t_app2, p->x, 0x3C, 0x2000, b, 2, NULL);
        }
    }

    /* Flink's other chests (see chest_content) */
    if (chests.n) {
        int t = types.n + ptypes->n;
        for (i = 0; i < chests.n; i++) {
            int q[2] = { chest_content(chestItem.v[i]) & 0xFFFF, chests.v[i]->check & 1 };
            pushf(&objs, t, chests.v[i]->x, chests.v[i]->y, 0, q, 2, chests.v[i]);
        }
        chest_type(&sprites, ptypes, pbanks);
    }
    nchests = chests.n;

    /* Flink's stones (see stone_type) */
    {
        PlaceList st = { 0 };
        for (i = 0; i < pl.n; i++) if (!strcmp(pl.v[i].name, "Stone")) vpush(st, &pl.v[i]);
        if (st.n) {
            int t = types.n + ptypes->n;
            for (i = 0; i < st.n; i++) { int q = st.v[i]->check & 1; pushf(&objs, t, st.v[i]->x, st.v[i]->y, 0, &q, 1, st.v[i]); }
            stone_type(&sprites, ptypes, pbanks);
        }
        nstones = st.n;
        free(st.v);
    }

    /* Flink's lianas (Liane 0x2B220) as Lomax ropes ('Rope', object 67, Rope_Spawn 0x429F60), which
       behave the same (climb up and down, slide off the bottom, jump off) except that Lomax grabs a
       rope by touching it, where Flink needs the grab button held (Player_CheckGrab 0x1A9B0): kept
       Lomax's way. Liane_Draw (0x2B3AE) puts the MAIN sprite's top-left at (x - 8, y - 32 + 32 k),
       k = 0..param, and TAIL's under the last one; Rope_Draw (0x42A280) draws record frame 0 at
       y + 32 k, k = 0..n - 1, then frame 1 at the last of them. With n = param + 1, frame 0 = MAIN at
       (-8, -32) and frame 1 = TAIL at (-8, 0) both draw the same column. Liane_Draw takes from its
       records only the sprite: Sprite_DrawScreen (0x3430E) queues the frame's first hardware sprite
       (built by 0x34132) at that point, without the record's offset or the hardware sprite's own
       offset in the frame (FOREST's TAIL is 16 px down its frame), so that offset is taken off here
       (anim_piece_offset). Lomax grabs with his feet between y + 20 and
       y + 20 + 32 n (Player_TryGrabHangPoint 0x413D60); Flink with his head (feet - 43) between y - 32
       and y + 32 param, i.e. his feet between y + 11 and y + 32 n + 11: 9 px lower in Lomax. */
    {
        /* Flink's Monkey (0x2E4F0, FOREST): it sits on a branch with a liana 32 px under it as its tail
           (drawn 4 px left of its x, not 8: +0x50), and walks right param 1 px at 0.5 px a frame while the
           tail is held (see monkey.c in FLINK.DLL). A 'Rope Monkey': Lomax's rope at the tail's point,
           'ROPE' from its MAIN and TAIL sprites as for the lianas, and its STAY (with the eyelids' hide
           slot) and WALK for the body, which FLINK.DLL draws 32 px above the rope's point. Params: the
           rope's segments (Flink's param 0 + 1), the walk. */
        int lianSlot[64], lianType[64];
        for (k = 0; k < 2; k++) {
            const char *name = k ? "Monkey" : "Liane";
            for (i = 0; i < pl.n; i++) {
                const Place *p = &pl.v[i];
                int t = -1, j;
                if (strcmp(p->name, name)) continue;
                for (j = 0; j < nlianTypes; j++) if (lianSlot[j] == p->slot) t = lianType[j];
                if (t < 0) {
                    Recs *recs = world_recs(fw, p->slot);
                    Ent e[2];
                    int q, hb[4] = { -20, -20, 20, 10 };
                    LRec r[3];
                    for (q = 0; q < 2; q++) {
                        const Rec *rr = rec_get(recs, q ? "TAIL" : "MAIN");
                        const Piece *sp;
                        int xo, yo;
                        if (!rr) die("%s: no %s record", name, q ? "TAIL" : "MAIN");
                        sp = first_sprite_of(rr);
                        anim_piece_offset(fw->anims.v[sp->a], sp->f, &xo, &yo);
                        e[q].bank = sprites_bank(&sprites, SK_A(sp->a)); e[q].frame = sp->f + 1;
                        e[q].dx = (k ? -4 : -8) - xo; e[q].dy = (q ? 0 : -32) - yo; e[q].fl = 0x80;
                    }
                    r[0] = pack_record("ROPE", 0x40, hb, e, 2);
                    t = types.n + ptypes->n;
                    if (nlianTypes >= 64) die("too many liana types");
                    lianSlot[nlianTypes] = p->slot; lianType[nlianTypes] = t; nlianTypes++;
                    if (k) {
                        RecSel sel;
                        sel.only = 0; sel.hasOnly = 0;
                        sel.rec = rec_get(recs, "STAY");
                        r[1] = lomax_record("STAY", &sel, 1, &sprites, 0, 0, 0, 0, 1, 0, 0);
                        sel.rec = rec_get(recs, "WALK");
                        r[2] = lomax_record("WALK", &sel, 1, &sprites, 0, 0, 0, 0, 1, 0, 0);
                        add_type(ptypes, pbanks, "Rope Monkey", 2, 0, r, 3);
                    } else add_type(ptypes, pbanks, "Rope Liana", 1, 0, r, 1);
                }
                if (k) {
                    int q[2] = { prm(p, 0, 0) + 1, prm(p, 1, 0) };
                    pushf(&objs, t, p->x, p->y + 32, 0, q, 2, p);
                    nmonkeys++;
                } else {
                    int q = prm(p, 0, 0) + 1;
                    pushf(&objs, t, p->x, p->y, 0, &q, 1, p);
                    nlianas++;
                }
            }
        }
    }

    /* Flink's Tree actor (0x30B22, always spawned): a column of 8 copies of its TREE sprite 32 px
       apart down the whole screen, the foreground Trees of FOREST and VILLAGE. Its draw hook (0x30BC2)
       puts it at screen x = ((0x1FF - P & 0x1FF) - 0x20) & 0x1FF - 0x80 (not drawn at 0x80), P =
       camera x * num / den + param 0, and the copies at y = -(camera y * num / den & 31) + 32 k:
       num / den = 100 / 70, in front of everything (sprite priority set). With checkbox 1 it is
       70 / 100 without the priority bit, behind the tiles (all of them in its maps, the Priority
       maps): a vine further back. Lomax gets a 'Spik Tree' decoration (object 51, wrapped by FLINK.DLL,
       which draws it in screen space by that formula) on plane 0, in front of everything, or with
       checkbox 1 on the background plane, behind the play plane. Its record is the sprite once:
       FLINK.DLL's tree_draw draws it once per piece of the column (and again 512 px to the left, for
       widescreen views), so that each piece moves smoothly at Lomax+'s high frame rates. Lomax spawns
       objects only as they scroll into view, so the placement is repeated every screen across the
       plane (tree_grid); FLINK.DLL keeps the first one that spawns and never despawns it. */
    for (i = 0; i < pl.n; i++) {
        const Place *p = &pl.v[i];
        Recs *recs = world_recs(fw, p->slot);
        const Rec *tr;
        const Piece *sp;
        Ent e;
        LRec r;
        char name[32];
        int zero[4] = { 0, 0, 0, 0 };
        if (strcmp(p->name, "Tree") || !recs || !(tr = rec_get(recs, "TREE"))) continue;
        sp = first_sprite_of(tr);
        e.bank = sprites_bank(&sprites, SK_A(sp->a)); e.frame = sp->f + 1; e.dx = 0; e.dy = 0; e.fl = 0x80;
        r = pack_record("TREE", 0x40, zero, &e, 1);
        if (ntrees >= 16) die("too many Trees");
        trees[ntrees].t = types.n + ptypes->n;
        trees[ntrees].back = p->check & 1;
        trees[ntrees].prm[0] = s16v(prm(p, 0, 0));
        trees[ntrees].prm[1] = trees[ntrees].back ? 70 : 100;
        trees[ntrees].prm[2] = trees[ntrees].back ? 100 : 70;
        ntrees++;
        snprintf(name, sizeof(name), "Spik Tree %d", ntrees);
        add_type(ptypes, pbanks, name, 3, 0, &r, 1);
    }

    /* Flink's Scroll Point (MAIN.WLD's system type 107, 0x2E628; 13 in 11 maps): it sets the camera
       once and removes itself. Params: X speed and Y speed (x 0x28F: hundredths of a px a frame; 0 =
       follow Flink), Flink's height on the screen (0x40..0xDC, else unchanged), checkboxes bit 0 = the
       camera's x stays, bit 1 its y. 'Forc Flink': Lomax's ForceScrolling (object 8, ForceScroll_Init
       0x41C270), with Flink's params and checkboxes as a 4th; FLINK.DLL (scroll.c) runs it. Spawn
       flag bit 6 (set at the map's start) is Lomax's 0x4000: spawned at every start and restart.
       A Generator spawns the others (bit 5, generator_links). */
    {
        PlaceList sc = { 0 };
        for (i = 0; i < pl.n; i++) if (!_stricmp(pl.v[i].name, "scroll point")) vpush(sc, &pl.v[i]);
        if (sc.n) {
            LRec r = blank_record("MAIN", &sprites, 0);
            int t = types.n + ptypes->n;
            add_type(ptypes, pbanks, "Forc Flink", 4, 0, &r, 1);
            for (i = 0; i < sc.n; i++) {
                const Place *p = sc.v[i];
                int q[4] = { prm(p, 0, 0) & 0xFFFF, prm(p, 1, 0) & 0xFFFF, prm(p, 2, 0) & 0xFFFF, p->check & 3 };
                pushf(&objs, t, p->x, p->y, p->sf & 0x40 ? 0x4000 : 0, q, 4, p);
            }
        }
        nscrolls = sc.n;
        free(sc.v);
    }

    /* the spirits, the HUD's magic glass, the spells and the landing dust */
    spirit_type(&sprites, ptypes, pbanks);
    glass_type(&sprites, ptypes, pbanks);
    spells_type(&sprites, pal, ptypes, pbanks);
    cloud_type(&sprites, ptypes, pbanks);
    dustcloud_type(&sprites, ptypes, pbanks);

    /* BOSS_FINISH: the boss links to its hidden Finish (flag 0x8000, the Finish's index after the
       params); then Flink's Generators and the hidden objects they spawn */
    if (boss_finish >= 0)
        for (i = 0; i < objs.n; i++)
            if (objs.v[i].p == boss) {
                if (objs.v[i].flags & 0x8000 || boss->sf & 0x80) die("the boss is linked already");
                objs.v[i].flags |= 0x8000;
                objs.v[i].prm[objs.v[i].np++] = boss_finish;
            }
    {
        LRec r = blank_record("MAIN", &sprites, PLATFORM_EDGE);
        generator_links(&pl, &objs, types.n + ptypes->n, &ngenes, &nhidden);
        if (ngenes) add_type(ptypes, pbanks, "Gene Flink", 2, 0, &r, 1);
        else free_lrec(&r);
    }

    for (i = 0; i < 32; i++) animbits[i] |= pbanks->b[i];
    {
        Buf nb = { 0 };
        buf_add(&nb, nmy, (size_t)nmy_end);
        for (i = 0; i < ptypes->n; i++) buf_add(&nb, ptypes->v[i].p, ptypes->v[i].n);
        buf_add(&nb, NULL, 2);
        free(nmy);
        nmy = nb.p;
        nmyLen = nb.n;
    }

    /* the layers */
    {
        Cells cells;
        static int usedP[8192], usedB[8192];
        int prio = find_place(&pl, "Priority") != NULL, scrollFg[7] = { 0, 100, 0, 100, 0, 0, 0 }, scrollBg[7], span, yf, q;
        Buf ttab_fg = { 0 }, ttab_pl = { 0 }, ttab_bg = { 0 }, map_fg = { 0 }, map_pl = { 0 }, map_bg = { 0 };
        IntVec meta_fg = { 0 }, meta_pl = { 0 }, meta_bg = { 0 };
        Objs sfg = { 0 }, sbg = { 0 };
        u8 hdr[8];
        int nbwords = bw8 / 2 * (bh8 / 2), ntpl = 0;
        /* Flink's Priority actor (0x255E8) sets the priority bit of every tile of the map and of the
           sprites, so in its maps sprites are drawn over all the tiles: no tile goes in front of Lomax.
           Maps narrower than the view are not padded: Lomax+ shows them at their own width, centred,
           with black bars beside them and the HUD and Lomax kept inside (its narrowed view, ws_narrowed
           in LOADER\dll\lomax.h). (Padding them to 512 px with black showed the black at width 320 too,
           and Lomax and the HUD went into it at 512.) */
        cells_init(&cells, fw->cells);
        head_used_cells(play->head, usedP);
        head_used_cells(bg->head, usedB);
        convert_layer(play, usedP, &cells, keep_fg, prio, 0, &ttab_fg, &meta_fg);
        convert_layer(play, usedP, &cells, keep_pl, prio, 0, &ttab_pl, &meta_pl);
        convert_layer(bg, usedB, &cells, keep_all, 0, 1, &ttab_bg, &meta_bg);
        for (i = 0; i < meta_bg.n; i++) if (!meta_bg.v[i]) meta_bg.v[i] = meta_bg.v[0];   /* (metatile 0 is opaque too) */
        for (i = 0; i < mw * mh; i++) {
            int a = meta_fg.v[ctx.words[i] & 0x7FF], b = meta_pl.v[ctx.words[i] & 0x7FF];
            hdr[0] = (u8)(a >> 8); hdr[1] = (u8)a; buf_add(&map_fg, hdr, 2);
            hdr[0] = (u8)(b >> 8); hdr[1] = (u8)b; buf_add(&map_pl, hdr, 2);
        }
        buf_add(&map_fg, NULL, (size_t)(mw * mh));
        buf_add(&map_pl, coll, (size_t)(mw * mh));
        if (bg->map.n < (size_t)nbwords * 2) die("%s map %d: its MAP is short", world, bgmap);
        for (i = 0; i < nbwords; i++) {
            int a = meta_bg.v[be16(bg->map.p + 2 * i) & 0x7FF];
            hdr[0] = (u8)(a >> 8); hdr[1] = (u8)a; buf_add(&map_bg, hdr, 2);
        }
        buf_add(&map_bg, NULL, (size_t)nbwords);

        /* background scroll: half speed across; down, only as far as its height covers the screen */
        span = h8 * 8 - 240;
        yf = span > 0 ? imax(0, imin(100, fdiv((bh8 * 8 - 240) * 100, span))) : 0;
        scrollBg[0] = 0; scrollBg[1] = 50; scrollBg[2] = 0; scrollBg[3] = yf; scrollBg[4] = scrollBg[5] = scrollBg[6] = 0;
        pushf(&sfg, T_SCROLL, 16, 16, 0x4000, scrollFg, 7, NULL);
        pushf(&sbg, T_SCROLL, 16, 16, 0x4000, scrollBg, 7, NULL);
        for (q = 0; q < ntrees; q++) {                  /* tree_grid: a placement every screen across the plane */
            int pw = trees[q].back ? bw8 * 8 : w8 * 8, ph = trees[q].back ? bh8 * 8 : h8 * 8, x, y;
            for (x = 160; x < imax(pw, 320); x += 320)
                for (y = 120; y < imax(ph, 240); y += 240)
                    pushf(trees[q].back ? &sbg : &sfg, trees[q].t, x, y, 0, trees[q].prm, 3, NULL);
        }

        {
            u8 w[8] = { 'P', 'S', '-', 'X', 0xD2, 0x04, 0, 0 };   /* "PS-X", 1234 */
            chunk(out, "WRLD", w, 8);
        }
        chunk(out, "NMY ", nmy, nmyLen);
        build_head(out, w8, h8, pal, thead->p, animbits);
        chunk(out, "TTAB", ttab_fg.p, ttab_fg.n); chunk(out, "MAP ", map_fg.p, map_fg.n); gnmy(out, &sfg);
        build_head(out, w8, h8, pal, thead->p, animbits);
        chunk(out, "TTAB", ttab_pl.p, ttab_pl.n); chunk(out, "MAP ", map_pl.p, map_pl.n); gnmy(out, &objs);
        build_head(out, bw8, bh8, pal, thead->p, animbits);
        chunk(out, "TTAB", ttab_bg.p, ttab_bg.n); chunk(out, "MAP ", map_bg.p, map_bg.n); gnmy(out, &sbg);
        chunk(out, "CELL", cells.data.p, cells.data.n);
        for (i = 0; i < tpl_anims.n; i++) { chunk(out, "ANIM", tpl_anims.v[i].p, tpl_anims.v[i].n); ntpl++; }
        sprites_chunks(&sprites, out);
        chunk(out, "END ", NULL, 0);

        /* the summary line (convert's info) */
        {
            int q2;
            for (q2 = 0; q2 < 2; q2++) {                   /* skipped and enemies, each sorted by name */
                int ns = q2 ? eo.placedName.n : po.skippedName.n, *ord = xalloc((size_t)(ns ? ns : 1) * sizeof(int)), j;
                const char **nm = q2 ? eo.placedName.v : po.skippedName.v;
                int *cnt = q2 ? eo.placedCount.v : po.skippedCount.v;
                for (i = 0; i < ns; i++) ord[i] = i;
                for (i = 0; i < ns; i++)
                    for (j = i + 1; j < ns; j++)
                        if (strcmp(nm[ord[j]], nm[ord[i]]) < 0) { int x = ord[i]; ord[i] = ord[j]; ord[j] = x; }
                for (i = 0; i < ns; i++)
                    info_add(extras, sizeof(extras), "%s%s x%d", i ? ", " : q2 ? "; enemies: " : "; skipped ", nm[ord[i]], cnt[ord[i]]);
                free(ord);
            }
        }
        if (ntrees) info_add(extras, sizeof(extras), "; trees: %d", ntrees);
        if (nchests) info_add(extras, sizeof(extras), "; chests: %d", nchests);
        if (nstones) info_add(extras, sizeof(extras), "; stones: %d", nstones);
        if (nlianas) info_add(extras, sizeof(extras), "; lianas: %d", nlianas);
        if (nknols) info_add(extras, sizeof(extras), "; knols: %d", nknols);
        if (nballs) info_add(extras, sizeof(extras), "; swinging balls: %d", nballs);
        if (nstahls) info_add(extras, sizeof(extras), "; stahlkugeln: %d", nstahls);
        if (nspikes) info_add(extras, sizeof(extras), "; spikes: %d", nspikes);
        if (ndrops) info_add(extras, sizeof(extras), "; drop balls: %d", ndrops);
        if (napples) info_add(extras, sizeof(extras), "; falling apples: %d", napples);
        for (i = 0; i < nrollTypes; i++) info_add(extras, sizeof(extras), "; %s: %d", rolls[i].name, rolls[i].n);
        if (nbutterflies) info_add(extras, sizeof(extras), "; butterflies: %d", nbutterflies);
        if (nlavas) info_add(extras, sizeof(extras), "; lava columns: %d", nlavas);
        for (i = 0; i < nlavaKinds; i++) {
            char lower[64];
            int j;
            for (j = 0; lavaKinds[i].name[j] && j < 63; j++) lower[j] = (char)(lavaKinds[i].name[j] >= 'A' && lavaKinds[i].name[j] <= 'Z' ? lavaKinds[i].name[j] + 32 : lavaKinds[i].name[j]);
            lower[j] = 0;
            info_add(extras, sizeof(extras), "; %s: %d", lower, lavaKinds[i].n);
        }
        if (nslides) info_add(extras, sizeof(extras), "; rope slides: %d", nslides);
        if (nbridges) info_add(extras, sizeof(extras), "; bridges: %d", nbridges);
        if (nsnakes) info_add(extras, sizeof(extras), "; snakes: %d", nsnakes);
        if (ngrounds) info_add(extras, sizeof(extras), "; ground types: %d", ngrounds);
        if (nmonkeys) info_add(extras, sizeof(extras), "; monkeys: %d", nmonkeys);
        if (nscrolls) info_add(extras, sizeof(extras), "; scroll points: %d", nscrolls);
        if (nhats) info_add(extras, sizeof(extras), "; hint hats: %d", nhats);
        if (ngenes || nhidden) info_add(extras, sizeof(extras), "; generators: %d, hidden objects: %d", ngenes, nhidden);
        snprintf(info, infoSize, "%s map %d over map %d, %dx%d px, %d cells, tiles fg %d / play %d / bg %d, start (%d,%d), "
                 "%s (%d,%d), %d revive, %d bytes; platforms: %d placed (%d types, %d Flink sprites)%s",
                 world, m, bgmap, w8 * 8, h8 * 8, cells.n, (int)ttab_fg.n / 16, (int)ttab_pl.n / 16, (int)ttab_bg.n / 16,
                 start->x, sy, finish ? "finish" : "exit", endx, endy, nrevive, (int)out->n, po.objs.n,
                 ptypes->n - netypes - ntrees - (nchests > 0) - (nstones > 0) - nlianTypes - (nknols > 0) - nballTypes -
                 (nstahls > 0) - (nspikes > 0) - (ndrops > 0) - (napples > 0) - nrollTypes - (nbutterflies > 0) -
                 (nlavas > 0) - nlavaKinds - (nslides > 0) - (nsnakes > 0) - (ngrounds > 0) - (nscrolls > 0) - 2,
                 sprites.keys.n, extras);
        (void)ntpl;
        buf_free(&ttab_fg); buf_free(&ttab_pl); buf_free(&ttab_bg); buf_free(&map_fg); buf_free(&map_pl); buf_free(&map_bg);
        free(meta_fg.v); free(meta_pl.v); free(meta_bg.v); free(sfg.v); free(sbg.v);
        buf_free(&cells.data); hmap_free(&cells.ids);
    }

    /* where Lomax arrives from the previous map (see arrival_points): Flink's entry -> (x, floor) */
    for (i = 0; i < entries.n; i++) {
        Arrive a;
        int gg;
        a.entry = entries.v[i]->params[0];
        a.x = entries.v[i]->x;
        a.y = ground_at(&ctx, a.x, entries.v[i]->y, &gg) && gg ? gg : entries.v[i]->y;
        for (k = 0; k < links->arrive.n; k++) if (links->arrive.v[k].entry == a.entry) { links->arrive.v[k] = a; break; }
        if (k == links->arrive.n) vpush(links->arrive, a);
    }
    for (k = 0; k < links->arrive.n; k++) if (links->arrive.v[k].entry == 1) break;
    if (k == links->arrive.n) {
        Arrive a;
        int gg;
        a.entry = 1; a.x = start->x;
        a.y = ground_at(&ctx, start->x, start->y, &gg) && gg ? gg : start->y;
        vpush(links->arrive, a);
    }
    links->music = track > 0 ? MUSIC_BASE + track - 1 : 0;

    free(nmy); free(coll); free(ctx.words); free(objs.v); free(revive.v); free(entries.v); free(chests.v); free(chestItem.v);
    for (i = 0; i < tpl_anims.n; i++) buf_free(&tpl_anims.v[i]);
    free(tpl_anims.v);
    for (i = 0; i < po.types.n; i++) buf_free(&po.types.v[i]);
    free(po.types.v); free(po.objs.v); free(po.skippedName.v); free(po.skippedCount.v);
    free(eo.types.v); free(eo.objs.v); free(eo.placedName.v); free(eo.placedCount.v);   /* (its types are ptypes' now) */
    for (i = 0; i < types.n; i++) free(types.v[i].name);
    free(types.v);
    free(sprites.keys.v); free(sprites.frames.v);
    (void)T_FINISH; (void)T_EXIT;
}
