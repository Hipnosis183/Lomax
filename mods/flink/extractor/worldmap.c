/* worldmap.c - Flink's world map on Lomax's map screen (worldmap, cloud_frames,
   map_platform, platform_box, map_patches; README "World map").

   MAIN.WLD map 3 is the island (one opaque plane), map 4 the storm clouds over it, which Flink's
   MapControl (0x2D774) waves line by line and column by column. The waving screen is computed for
   CLOUD_STEPS phases (cloud_frames), each cut into bands of CLOUD_BAND lines cropped to the clouds,
   as Lomax ANIMs CLOUD_ANIM ... (a chunk must fit File_ReadChunk's 512 KB buffer). The map's objects
   are Lomax's own map's (WORLDS\MAP.WLD: MAP_KEEP) and a 'Little Hero' on each level's point. Three
   map changes (Flink's table at 0x2DA3C) copy cells over the island once their area's level is
   played: MAP.WLD, MAP1.WLD ... one file per set of changes, [MapFiles] saying from which level. */
#include "convert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Flink's world map is MAIN.WLD map 3 (1280 x 768) over map 4 (Game_LoadLevel 0x19A68: World_Load
   d2 = 3, d3 = 4). Map 3 is on plane A with no priority tile; map 4 (512 x 256, every tile with
   priority, transparent below a diagonal) is plane B, so it covers map 3: the storm clouds over the
   island's top right. MapControl (0x2D774) keeps plane B fixed on the screen and makes it wave (draw
   hook 0x2DC5E, VDP mode 0x8B07: a scroll value per line across and per 16-px column down): line l
   scrolls 0x1F2 + W(p + 4 l) across (14 px left), column c 16 + W(p + 16 c) down, with
   W(i) = sine[i & 1023] * 32 >> 16 (the table at 0x1491E, +-16384: +-8 px) and p counting 8 a frame
   (+0x10 bytes at +0x52), a period of 128 frames. Lomax scrolls a plane only as a whole, and drawing
   the clouds as 439 small blocks moved every tick would cost too much (below 120 fps with Lomax+'s
   FPS Override), so the screen the wave makes is computed here for CLOUD_STEPS
   phases of it (cloud_frames: Flink's own formula per pixel, as wide as Lomax+'s widest view), each
   cut into bands of CLOUD_BAND lines cropped to the clouds; FLINK.DLL (worldmap.c) draws one phase's
   bands a tick, a few sprites. The map points are the 'Map ' actors (0x2D3E8: params point-no.,
   level-entry (a LevelSequenceTable index), UP/DOWN/LEFT/RIGHT point-no.; checkbox 1 = the START sign, point 1, not a level). Flink walks
   between them; Lomax shows its level's 'Little Hero' marker on its point instead of the flags.

   Three map changes copy cells from the map's unused corners over the island (MapControl 0x2D980,
   table 0x2DA3C: source x, y, width, height, destination x, y, in bytes across and 8-px rows down), each
   once a flag is set: 0x825A by the Shrink area (0x22D44, VILLAGE map 13), 0x825B by the Kill area
   (0x22C70, MOUNTAIN map 10), 0x825C by the Final area (0x22898, MOUNTAIN map 13). Lomax plays the
   levels in a row, so a change shows from the level after the one holding its area; worldmap writes a
   file for each set of changes (MAP.WLD, MAP1.WLD ...) and LEVELS.INI's [MapFiles] says from which
   level each is shown.

   CLOUD_ANIM = 100                               # the clouds' first ANIM id in MAP.WLD (bank CLOUD_ANIM + 1; Lomax's use 0-9)
   CLOUD_PER_ANIM = 8                             # phases an ANIM: File_ReadChunk (0x416A70) streams a chunk
   # through the 512 KB g_pFileBuffer (0x4645D4, size at
   # 0x4645D8), so a bigger chunk crashes the load
   CLOUD_STEPS = 64                               # phases computed: the wave moves every 2 ticks (Flink: 1)
   CLOUD_BAND = 16                                # lines a band; each phase is SCREEN_H / CLOUD_BAND frames
   CLOUD_SCREEN = (512, 240)                      # Lomax+'s widest view, Lomax's height
   CLOUD_SCROLL = (14, 16)                        # plane B's h scroll 0x1F2 (14 px left) and v scroll
   WAVE_TABLE = 0x1491E                           # Flink's sine table, 1024 words
   # Lomax's tree top on his own map: the template's first type, a 'Mapforeground' prop ('PARA': the
   # flowery tree top and a branch, drawn twice as far as the camera moves, Mapf_MapParallaxProp_Draw
   # 0x423FD0) placed at (55, 186); Lomax lands on it at 23 px right of that and 64 above (at width 432
   # with Lomax+'s default widescreen). FLINK.DLL draws it under Lomax at the screen's bottom right.
   MAP_PLATFORM_TYPE, MAP_PLATFORM_FEET = 0, (23, -64) */
#define WORLDMAP 3
#define WORLDMAP_CLOUDS 4
#define CLOUD_ANIM 100
#define CLOUD_PER_ANIM 8
#define CLOUD_STEPS 64
#define CLOUD_BAND 16
#define CLOUD_SW 512
#define CLOUD_SH 240
#define CLOUD_HX 14
#define CLOUD_VY 16
#define WAVE_TABLE 0x1491E
#define MAP_PLATFORM_TYPE 0
#define MAP_PLATFORM_FEET_X 23
#define MAP_PLATFORM_FEET_Y (-64)
#define MAP_PATCH_TABLE 0x2DA3C

static unsigned le16(const u8 *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }

/* map 4's pixel at x, y, nothing below it. Flink's 320-px screen never reaches its right end, a
   512-px view does (from x 498, and the wave adds 8): there the plane is mirrored about its right
   edge, which leaves no seam (its diagonal edge levels off there). Repeating the last column would
   streak every line; its cells 7 left and 3 up, which carry on the diagonal edge exactly, would
   leave a straight cut through the clouds above it. */
/* map 4's pixel at x, y, nothing below it; past its right end mirrored about it (no seam) */
static int cloud_pixel(const u8 *img, int W, int H, int x, int y)
{
    if (x >= W) x = 2 * W - 1 - x;
    return y >= 0 && y < H && x >= 0 && x < W ? img[y * W + x] : 0;
}

static int wave(int i)
{
    int v = (short)be16(g_flinkPrg + WAVE_TABLE - PRG_LOAD + 2 * (i & 1023));
    return (v * 32) >> 16;
}

/* map 4 (512 x 256) waving on the screen as Flink's MapControl scrolls it (see above), at
   CLOUD_STEPS phases -> Lomax ANIMs of CLOUD_PER_ANIM phases each (ids CLOUD_ANIM ...): per phase,
   its CLOUD_SCREEN cut into bands of CLOUD_BAND lines, each cropped across to its pixels (a blank
   1 x 1 frame for a band without any), the frame's offset (the ANIM frame's 3rd and 4th words,
   which Lomax adds when drawing) its place on the screen; in ANIM k / CLOUD_PER_ANIM, frame
   (k % CLOUD_PER_ANIM) * bands + b + 1 is phase k's band b. Screen pixel (x, y) at phase p shows the plane
   at (x + 14 - W(p + 4 y), y + 16 + W(p + 16 (x / 16))), nothing below its 256 lines; past its
   right end, see cloud_pixel (the plane's own wrap to its left end, which is empty, would cut the
   clouds off).
   Pixels are line * 16 + colour in palette 0, as Cells. */
/* cloud_frames: screen pixel (x, y) at phase p shows map 4 at (x + 14 - W(p + 4 y), y + 16 +
   W(p + 16 (x / 16))) -> the ANIM chunks' payloads, the frame count */
static int cloud_frames(const FWorld *main, const FMap *m4, BufVec *out)
{
    int w8 = (int)be32(m4->head.p), h8 = (int)be32(m4->head.p + 4), W = w8 * 8, H = h8 * 8;
    int mw = w8 / 2, i, q, k, by, y, x, nframes = 0, per = CLOUD_PER_ANIM * (CLOUD_SH / CLOUD_BAND);
    static int used[8192];
    u8 *img = xalloc((size_t)(W * H)), rows[CLOUD_BAND][CLOUD_SW];
    Buf cur = { 0 };
    struct { int w, h, ox, oy; u8 *px; } *fr = xalloc((size_t)(CLOUD_STEPS * (CLOUD_SH / CLOUD_BAND)) * sizeof(*fr));
    head_used_cells(m4->head, used);
    for (i = 0; i < mw * (h8 / 2); i++) {
        unsigned wd = be16(m4->map.p + 2 * i);
        int mx = i % mw * 16, my = i / mw * 16;
        for (q = 0; q < 4; q++) {                           /* TL, BL, TR, BR */
            unsigned t = be16(m4->ttab.p + ((wd & 0x7FF) * 4 + (unsigned)q) * 2), n = t & 0x7FF;
            const u8 *raw;
            if (n < 0x180) continue;
            raw = main->cells.p + (size_t)used[n - 0x180] * 32;
            for (k = 0; k < 64; k++) {
                int c = k & 1 ? raw[k >> 1] & 15 : raw[k >> 1] >> 4, px = k & 7, py = k >> 3;
                if (!c) continue;
                if (t & 0x800) px = 7 - px;
                if (t & 0x1000) py = 7 - py;
                img[(my + (q & 1) * 8 + py) * W + mx + (q >> 1) * 8 + px] = (u8)(((t >> 13) & 3) * 16 + c);
            }
        }
    }
    for (k = 0; k < CLOUD_STEPS; k++) {
        int p = k * 1024 / CLOUD_STEPS, vs[CLOUD_SW / 16];
        for (i = 0; i < CLOUD_SW / 16; i++) vs[i] = CLOUD_VY + wave(p + 16 * i);
        for (by = 0; by < CLOUD_SH; by += CLOUD_BAND) {
            int x0 = 1 << 30, x1 = -1;
            for (y = by; y < by + CLOUD_BAND; y++) {
                int sh = CLOUD_HX - wave(p + 4 * y);
                for (x = 0; x < CLOUD_SW; x++) {
                    rows[y - by][x] = (u8)cloud_pixel(img, W, H, x + sh, y + vs[x >> 4]);
                    if (rows[y - by][x]) { x0 = imin(x0, x); x1 = imax(x1, x); }
                }
            }
            if (x1 < 0) {
                fr[nframes].w = 1; fr[nframes].h = 1; fr[nframes].ox = 0; fr[nframes].oy = 0;
                fr[nframes].px = xalloc(1);
            } else {
                int fw = x1 + 1 - x0;
                fr[nframes].w = fw; fr[nframes].h = CLOUD_BAND; fr[nframes].ox = x0; fr[nframes].oy = by;
                fr[nframes].px = xalloc((size_t)(fw * CLOUD_BAND));
                for (y = 0; y < CLOUD_BAND; y++) memcpy(fr[nframes].px + y * fw, rows[y] + x0, (size_t)fw);
            }
            nframes++;
        }
    }
    for (i = 0; i < nframes; i += per) {
        int n = imin(per, nframes - i), mw2 = 0, mh2 = 0, j;
        u8 h[12];
        cur.n = 0;
        for (j = 0; j < n; j++) { mw2 = imax(mw2, fr[i + j].w); mh2 = imax(mh2, fr[i + j].h); }
        put_le16(h, (unsigned)mw2); put_le16(h + 2, (unsigned)mh2); put_le16(h + 4, (unsigned)n);
        put_le16(h + 6, 0); put_le16(h + 8, (unsigned)(CLOUD_ANIM + i / per)); put_le16(h + 10, 0);
        buf_add(&cur, h, 12);
        for (j = 0; j < n; j++) {
            u8 fh[8];
            size_t np = (size_t)(fr[i + j].w * fr[i + j].h);
            put_le16(fh, (unsigned)fr[i + j].w); put_le16(fh + 2, (unsigned)fr[i + j].h);
            put_le16(fh + 4, (unsigned)fr[i + j].ox); put_le16(fh + 6, (unsigned)fr[i + j].oy);
            buf_add(&cur, fh, 8);
            buf_add(&cur, fr[i + j].px, np);
            buf_add(&cur, NULL, (4 - np % 4) % 4);
        }
        if (cur.n > 0x80000 - 0x1000) die("cloud ANIM %d: %u bytes, more than the file buffer", i / per, (unsigned)cur.n);
        {
            Buf b = { 0 };
            buf_add(&b, cur.p, cur.n);
            vpush(*out, b);
        }
    }
    for (i = 0; i < nframes; i++) free(fr[i].px);
    free(fr); free(img); buf_free(&cur);
    return nframes;
}

/* Lomax's tree top on his own map: the template's first type, a 'Mapforeground' prop ('PARA': the
   flowery tree top and a branch, drawn twice as far as the camera moves, Mapf_MapParallaxProp_Draw
   0x423FD0) placed at (55, 186); Lomax lands on it at 23 px right of that and 64 above (at width 432
   with Lomax+'s default widescreen). FLINK.DLL draws it under Lomax at the screen's bottom right.

   the pieces (bank, frame, dx, dy) of the first frame of MAP_PLATFORM_TYPE's first record */
/* map_platform: the pieces (bank, frame, dx, dy) of the first frame of MAP_PLATFORM_TYPE's first
   record (Lomax's tree top) */
static int map_platform(const u8 *nmy, int out[][4])
{
    size_t q = 0;
    int t, n = 0;
    for (t = 0; t <= MAP_PLATFORM_TYPE; t++) {
        unsigned ln = le16(nmy + q), nanim, k;
        int isTree = ln >= 13 && !memcmp(nmy + q + 2, "Mapforeground", 13) && (ln == 13 || !nmy[q + 2 + 13]);
        q += 2 + ln;
        nanim = le16(nmy + q + 2);
        q += 6;
        if (t == MAP_PLATFORM_TYPE) {
            unsigned ne = le16(nmy + q + 8), e;
            if (!isTree || !nanim) die("MAP.WLD: type %d isn't the tree top", t);
            for (e = 0; e < ne; e++) {
                const u8 *p = nmy + q + 28 + e * 8;
                if (n == 16) die("MAP.WLD: the tree top has too many pieces");
                out[n][0] = p[0]; out[n][1] = p[1]; out[n][2] = (short)le16(p + 2); out[n][3] = (short)le16(p + 4);
                n++;
                if (le16(p + 6) & 0x80) break;
            }
            return n;
        }
        for (k = 0; k < nanim; k++) q += 28 + le16(nmy + q + 8) * 8;
    }
    return n;
}

/* the tree top's opaque pixels' box (left, top, right, bottom; right and bottom exclusive) from its
   origin, unmirrored: FLINK.DLL places it by what is drawn (the branch's frame has an empty last
   line, which left an empty line at the screen's bottom) */
/* platform_box: the tree top's opaque pixels' box (left, top, right, bottom; exclusive) from its
   origin */
static void platform_box(int pl[][4], int npl, const LChunks *tpl, int box[4])
{
    int l = 10000, t = 10000, r = -10000, b = -10000, i, k;
    for (k = 0; k < npl; k++) {
        const u8 *a = NULL;
        size_t q = 12;
        int f, w = 0, h = 0, ox = 0, oy = 0, x, y;
        for (i = 0; i < tpl->n; i++)                         /* (the last ANIM of that id) */
            if (!strcmp(tpl->v[i].tag, "ANIM") && (int)le16(tpl->v[i].p.p + 8) + 1 == pl[k][0]) a = tpl->v[i].p.p;
        if (!a) die("MAP.WLD: no ANIM for bank %d", pl[k][0]);
        for (f = 0; f < pl[k][1]; f++) {
            w = (short)le16(a + q); h = (short)le16(a + q + 2); ox = (short)le16(a + q + 4); oy = (short)le16(a + q + 6);
            if (f < pl[k][1] - 1) q += 8 + (size_t)(w * h) + (4 - (size_t)(w * h) % 4) % 4;
        }
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
                if (a[q + 8 + y * w + x]) {
                    l = imin(l, pl[k][2] + ox + x); t = imin(t, pl[k][3] + oy + y);
                    r = imax(r, pl[k][2] + ox + x + 1); b = imax(b, pl[k][3] + oy + y + 1);
                }
    }
    box[0] = l; box[1] = t; box[2] = r; box[3] = b;
}

/* applies Flink's world-map changes `which` (indexes into the table at 0x2DA3C) to map 3's cells */
/* map_patches: Flink's world-map changes `which` (bits: indexes into the table at 0x2DA3C) applied
   to map 3's cells */
static void map_patches(unsigned *words, int mw, int which)
{
    int k, i, j;
    for (k = 0; k < 3; k++) {
        const u8 *e = g_flinkPrg + MAP_PATCH_TABLE - PRG_LOAD + k * 12;
        int sx = (int)be16(e), sy = (int)be16(e + 2), w = (int)be16(e + 4), h = (int)be16(e + 6),
            dx = (int)be16(e + 8), dy = (int)be16(e + 10);
        if (!(which >> k & 1)) continue;
        for (j = 0; j < h / 2; j++)
            for (i = 0; i < w / 2; i++)
                words[(dy / 2 + j) * mw + dx / 2 + i] = words[(sy / 2 + j) * mw + sx / 2 + i];
    }
}

/* Flink's world map as Lomax worlds (MAP.WLD and one more per map change) -> LEVELS.INI lines.
   One plane: map 3, opaque (Flink's backdrop where it is transparent), with the objects of Lomax's
   own map (map_tpl, WORLDS\MAP.WLD: MAP_KEEP; not its trapdoor or its scenery, the Mapforeground
   props) and a 'Little Hero' (param: the level, 1-based) on each level's point, listed first; map
   4's clouds are the ANIM CLOUD_ANIM (cloud_frames): [Map] Clouds = its bank, phases, bands a
   phase; Platform = Lomax's tree top's pieces (bank, frame, dx, dy; map_platform) and Feet = where
   he stands on it. FLINK.DLL (worldmap.c) loads it for the map section, scrolls it to the point,
   draws the clouds' phase and the tree top and puts Lomax on it at the screen's bottom right. */
void worldmap(const IntVec *levels, const IntVec *rooms, int nlev, const char *outDir, Buf *ini)
{
    static const char *const AREAS[3] = { "Shrink area", "Kill area", "Final area" };
    static const char *const KEEP[5] = { "Start", "Map Control", "Music", "Message", "Zoom Colors" };
    FWorld *main = world_of("MAIN");
    const FMap *m3 = &main->maps.v[WORLDMAP - 1], *m4 = &main->maps.v[WORLDMAP_CLOUDS - 1];
    Places *pts = places_of(main, WORLDMAP);
    const Place *start = NULL, *node[64];
    int fromLevel[3] = { -1, -1, -1 }, steps[4], nsteps = 0, k, i, j, d;
    u8 *tplData;
    size_t tplLen;
    LChunks tpl;
    const LChunk *tn, *th, *tg;
    LTypes types = { 0 };
    Objs objs = { 0 };
    unsigned pal[256];
    BufVec anims = { 0 }, clouds = { 0 };
    int nframes, plat[16][4], nplat, box[4], w8, h8, mw, mh, T_HERO = -1;
    u8 animbits[32];
    unsigned *words;
    char line[1024];

    for (i = 0; i < pts->n; i++) if (!strcmp(pts->v[i].name, "Map ") && pts->v[i].check & 1) { start = &pts->v[i]; break; }
    if (!start) die("MAIN.WLD map 3: no START point");
    for (k = 0; k < nlev; k++) {
        node[k] = NULL;
        for (i = 0; i < pts->n; i++)
            if (!strcmp(pts->v[i].name, "Map ") && !(pts->v[i].check & 1) && prm(&pts->v[i], 1, 0) + 1 == levels[k].v[0]) { node[k] = &pts->v[i]; break; }
        if (!node[k]) die("level %d: no world map point", k + 1);
    }
    /* the map changes: shown from the level after the one whose maps hold their area */
    for (k = 0; k < nlev; k++)
        for (j = 0; j < levels[k].n + rooms[k].n; j++) {
            const char *world;
            int m, bg;
            Places *pl = map_places(j < levels[k].n ? levels[k].v[j] : rooms[k].v[j - levels[k].n], &world, &m, &bg);
            for (i = 0; i < pl->n; i++)
                for (d = 0; d < 3; d++) if (!strcmp(pl->v[i].name, AREAS[d])) fromLevel[d] = k + 1;
        }
    for (d = 0; d < 3; d++)                                    /* steps: sorted, distinct */
        if (fromLevel[d] >= 0) {
            for (i = 0; i < nsteps && steps[i] != fromLevel[d]; i++) ;
            if (i == nsteps) steps[nsteps++] = fromLevel[d];
        }
    for (i = 0; i < nsteps; i++)
        for (j = i + 1; j < nsteps; j++) if (steps[j] < steps[i]) { int x = steps[i]; steps[i] = steps[j]; steps[j] = x; }

    tplData = read_game_world("MAP.WLD", &tplLen);
    tpl = lomax_chunks(tplData, tplLen);
    tn = lchunk(&tpl, "NMY "); th = lchunk(&tpl, "HEAD"); tg = lchunk(&tpl, "GNMY");
    if (!tn || !th || !tg) die("WORLDS\\MAP.WLD: no NMY, HEAD or GNMY");
    lomax_types(tn->p.p, tn->p.n, &types);
    for (i = 0; i < types.n; i++) if (!strcmp(types.v[i].name, "Little Hero")) { T_HERO = i; break; }
    if (T_HERO < 0) die("WORLDS\\MAP.WLD: no 'Little Hero' type");
    for (k = 0; k < nlev; k++) { int lv = k + 1; Obj o = obj(T_HERO, node[k]->x, node[k]->y, 0x4700, &lv, 1, NULL); vpush(objs, o); }
    {
        size_t q = 0;
        const u8 *g = tg->p.p;
        while (q + 2 <= tg->p.n) {
            unsigned size = le16(g + q), t;
            int p[24], np = 0;
            if (!size) break;
            t = le16(g + q + 2);
            for (i = 0; i < ((int)size - 10) / 2 && np < 24; i++) p[np++] = (int)le16(g + q + 10 + 2 * i);
            for (i = 0; i < 5; i++)
                if ((int)t < types.n && !strcmp(types.v[t].name, KEEP[i])) {
                    Obj o = obj((int)t, (short)le16(g + q + 4), (short)le16(g + q + 6), (int)le16(g + q + 8), p, np, NULL);
                    vpush(objs, o);
                }
            q += size;
        }
    }
    for (i = 0; i < 256; i++) pal[i] = 0;
    for (i = 0; i < 64; i++) pal[i] = vdp_to_15(be16(m3->head.p + 8 + 2 * i));
    pal[BACKDROP] = pal[0];
    pal[0] = 0;
    nframes = cloud_frames(main, m4, &clouds);
    nplat = map_platform(tn->p.p, plat);
    for (i = 0; i < tpl.n; i++) if (!strcmp(tpl.v[i].tag, "ANIM")) { Buf a = { 0 }; buf_add(&a, tpl.v[i].p.p, tpl.v[i].p.n); vpush(anims, a); }
    template_sprite_colours(&anims, th->p, pal);
    for (i = 0; i < clouds.n; i++) vpush(anims, clouds.v[i]);
    memcpy(animbits, th->p.p + 0x1458, 32);
    for (i = 0; i < clouds.n; i++) { int b = CLOUD_ANIM + 1 + i; animbits[b >> 3] |= (u8)(0x80 >> (b & 7)); }
    for (i = 0; i < nplat; i++) { int b = plat[i][0]; animbits[b >> 3] |= (u8)(0x80 >> (b & 7)); }
    w8 = (int)be32(m3->head.p); h8 = (int)be32(m3->head.p + 4);
    mw = w8 / 2; mh = h8 / 2;
    words = xalloc((size_t)(mw * mh) * sizeof(unsigned));

    ini_add(ini, "; Flink's world map (FLINK.DLL worldmap.c): each level's point, k=x,y (Little Hero's feet),");
    ini_add(ini, "; Start, Flink's START sign; Clouds: the storm clouds' first bank, phases, bands a phase, phases a bank;");
    ini_add(ini, "; Platform: Lomax's tree top's pieces (bank, frame, dx, dy ...), Feet: where he stands on it;");
    ini_add(ini, "; [MapFiles]: from=FILE, the file shown from that level on");
    ini_add(ini, "[Map]");
    ini_add(ini, "Start=%d,%d", start->x, start->y);
    ini_add(ini, "Clouds=%d,%d,%d,%d", CLOUD_ANIM + 1, CLOUD_STEPS, CLOUD_SH / CLOUD_BAND, CLOUD_PER_ANIM);
    line[0] = 0;
    for (i = 0; i < nplat; i++)
        snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s%d,%d,%d,%d", i ? "," : "", plat[i][0], plat[i][1], plat[i][2], plat[i][3]);
    ini_add(ini, "Platform=%s", line);
    ini_add(ini, "Feet=%d,%d", MAP_PLATFORM_FEET_X, MAP_PLATFORM_FEET_Y);
    platform_box(plat, nplat, &tpl, box);
    ini_add(ini, "PlatformBox=%d,%d,%d,%d", box[0], box[1], box[2], box[3]);
    for (k = 0; k < nlev; k++) ini_add(ini, "%d=%d,%d", k + 1, node[k]->x, node[k]->y);
    ini_add(ini, "");
    ini_add(ini, "[MapFiles]");
    for (i = 0; i <= nsteps; i++) {
        int lv = i ? steps[i - 1] : 0, which = 0, nw = 0;
        char name[16], path[MAX_PATH], wl[32] = "";
        Cells cells;
        static int used[8192];
        Buf ttab = { 0 }, map = { 0 }, out = { 0 };
        IntVec meta = { 0 };
        snprintf(name, sizeof(name), i ? "MAP%d.WLD" : "MAP.WLD", i);
        for (d = 0; d < 3; d++) if (fromLevel[d] >= 0 && fromLevel[d] <= lv) which |= 1 << d;
        cells_init(&cells, main->cells);
        head_used_cells(m3->head, used);
        convert_layer(m3, used, &cells, keep_all, 0, 1, &ttab, &meta);
        for (j = 0; j < meta.n; j++) if (!meta.v[j]) meta.v[j] = meta.v[0];
        for (j = 0; j < mw * mh; j++) words[j] = be16(m3->map.p + 2 * j);
        map_patches(words, mw, which);
        for (j = 0; j < mw * mh; j++) { u8 b2[2]; int t = meta.v[words[j] & 0x7FF]; b2[0] = (u8)(t >> 8); b2[1] = (u8)t; buf_add(&map, b2, 2); }
        buf_add(&map, NULL, (size_t)(mw * mh));
        { u8 w[8] = { 'P', 'S', '-', 'X', 0xD2, 0x04, 0, 0 }; chunk(&out, "WRLD", w, 8); }
        chunk(&out, "NMY ", tn->p.p, tn->p.n);
        build_head(&out, w8, h8, pal, th->p, animbits);
        chunk(&out, "TTAB", ttab.p, ttab.n);
        chunk(&out, "MAP ", map.p, map.n);
        gnmy(&out, &objs);
        chunk(&out, "CELL", cells.data.p, cells.data.n);
        for (j = 0; j < anims.n; j++) chunk(&out, "ANIM", anims.v[j].p, anims.v[j].n);
        chunk(&out, "END ", NULL, 0);
        snprintf(path, sizeof(path), "%s\\%s", outDir, name);
        write_file(path, out.p, out.n);
        ini_add(ini, "%d=%s", lv + 1, name);
        for (d = 0; d < 3; d++) if (which >> d & 1) snprintf(wl + strlen(wl), sizeof(wl) - strlen(wl), "%s%d", nw++ ? ", " : "[", d);
        if (nw) strcat(wl, "]"); else strcpy(wl, "none");
        printf("  world map %s: from level %d, changes %s, %d cells, %d cloud frames, %d points, %d bytes\n",
               name, lv + 1, wl, cells.n, nframes, nlev, (int)out.n);
        buf_free(&ttab); buf_free(&map); buf_free(&out); free(meta.v);
        buf_free(&cells.data); hmap_free(&cells.ids);
    }
    for (i = 0; i < anims.n; i++) buf_free(&anims.v[i]);
    free(anims.v); free(clouds.v); free(words); free(objs.v); free(tplData);
}
