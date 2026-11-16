/* scenes.c - Flink's intro and ending as SCENE0.WLD ... SCENE7.WLD (scenes,
   scene_record, layer_pixels, SceneCells, eye_colour, scene_hole, scene_inpaint; README "Intro and
   ending"; FLINK.DLL scenes.c runs them).

   Game_RunIntro (0x1511E) loads INTRO.WLD's maps (play map, background) from the table at 0x151DC, one
   a scene, after MAIN.WLD map 1 (the Psygnosis logo); Game_RunExtro (0x151F0) EXTRO.WLD's first
   world's from 0x152AE (SCENES). Each scene has four planes: Flink's priority tiles; the play plane,
   SCENE_SCREEN_W wide and empty (a host's widescreen shows Flink's screen with black bars beside it);
   the map's other tiles; the background. The play plane holds a Start that only marks it and one
   'Spik Scene' object whose type holds the scene's sprites (SCENE_SPRITES), BLAK (black bars), WHIT
   (a flash) and in the space scene STAR and EYES.

   INTRO map 6 has Flink painted into its tiles, standing in the pond: his pixels (scene_hole) are
   filled from the rest of the picture by exemplar-based inpainting (scene_inpaint: Criminisi, Perez and
   Toyama, 2004), and the 16-px blocks it touches become new cells. Which patch wins a tie decides the
   picture, so the arithmetic is fixed: the priorities are float32, each patch's confidence summed as
   numpy's pairwise sum, row by row (`pairwise`), and the patch distances exact integers (a best match
   past 2^24, where a float32 sum would round, stops the extractor). */
#include "convert.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCENE_INTRO 6
#define SCENE_SCREEN_W 320
#define SCENE_VIEW_H 240
#define SCENE_PAD 16
#define SCENE_BLACK 253
#define SCENE_WHITE 252
#define SCENE_STARS 240
#define EYE_TILE 64
#define EYE_PAL 160
#define EYE_LEVELS 7
#define SCENE_PATCH 6
#define SCENE_ROWS 6

/* Game_RunIntro (0x1511E) loads INTRO.WLD's maps (play map, background) from the table at 0x151DC,
   one a scene, after MAIN.WLD map 1 (0x199E0, the Psygnosis logo with its Presents); Game_RunExtro
   (0x151F0) EXTRO.WLD's first world's from 0x152AE. A background 0 is none. The space scene's Stars
   (0x1F5BC) loads INTRO map 1 (the villain's portrait) over its play map 8, which only holds the objects.

   the scenes' sprites: (Flink type, its record, the record's tag here, a bitmap Flink scales: scene_record's tl)
   Flink's objects for the hero (Flink, Head, Held but its CONG letters) have no sprites here: FLINK.DLL
   runs what else they do (the scene's timing and camera, Held's letters).

   Flink painted into a map's tiles: INTRO map 6, the dark sky's background, is him standing in the pond.
   His colours are line 0's 1-7 and 9-15, which nothing else in the picture uses: the hole is those
   pixels inside `box`, closed (scene_hole) so his outline and the gaps between his colours go too; it is
   filled from the rest of the picture (`src`: its 320 x 176 px) by scene_inpaint, and the 16-px blocks it
   touches become new cells. {(world, map): (box, src)}, each (x0, y0, x1, y1) in px. */
typedef struct { const char *type, *ftag, *tag; int tl; } SceneSprite;
static const struct { const char *world; int m, b, tiles_m; SceneSprite spr[8]; int nspr; } SCENES[8] = {
    { "MAIN", 1, 0, 0, { { "Presents", "PRES", "PRES", 1 } }, 1 },
    { "INTRO", 8, 0, 1, { { "Asteroid", "BIG ", "ABIG", 1 }, { "Asteroid", "SMAL", "ASML", 1 }, { "Asshole", "EINS", "DEM1", 1 },
                          { "Asshole", "ZWEI", "DEM2", 0 }, { "Asshole", "DREI", "DEM3", 1 }, { "Fart", "HEAD", "FART", 1 },
                          { "Isle", "ISLE", "ISLE", 1 } }, 7 },
    { "INTRO", 3, 2, 0, { { "Meteor", "FIRE", "METF", 0 }, { "Meteor", "PUFF", "METP", 0 } }, 2 },
    { "INTRO", 5, 4, 0, { { "Impact", "HEAD", "IHED", 0 }, { "Impact", "BOOM", "IBOM", 0 }, { "Impact", "PUF1", "IPF1", 0 },
                          { "Impact", "PUF2", "IPF2", 0 } }, 4 },
    { "INTRO", 7, 6, 0, { { 0 } }, 0 },
    { "INTRO", 9, 10, 0, { { "Lightning", "ZAP1", "ZAP1", 0 }, { "Lightning", "ZAP2", "ZAP2", 0 }, { "Lightning", "HEAD", "LHED", 0 },
                           { "Lightning", "TAIL", "LTAL", 0 } }, 4 },
    { "EXTRO", 1, 2, 0, { { "Arsch", "DEAD", "ARSC", 0 }, { "Arsch", "TAIL", "ARST", 0 }, { "Felsen", "ROCK", "ROCK", 0 } }, 3 },
    { "EXTRO", 3, 0, 0, { { "Held", "CONG", "CONG", 0 }, { "Wiz", "WIZ ", "WIZA", 0 }, { "Wiz", "TAIL", "WIZT", 0 },
                          { "Wiz", "EINS", "WIZ1", 0 }, { "Wiz", "ZWEI", "WIZ2", 0 }, { "Wiz", "DREI", "WIZ3", 0 },
                          { "Smoke", "PUF ", "SMOK", 0 } }, 7 },
};
#define DEM2_SHOWN 1

static int g_addId = 100;                                    /* SK_ADDED ids of the scenes' own banks */

static int anim_line(const FWorld *w, int a) { return (int)be16(w->anims.v[a].p + 6) & 3; }

/* A Flink record -> a Lomax record (tag), its pieces on their palette line: Flink's
   Sprite_EmitPiece (0x1D468) ORs piece flags bits 5-6 into the sprite's palette bits, so a piece's
   line is its ANIM's | (flags >> 5 & 3) (the Cloud's flags 0x60 put it on line 3). keep: which
   pieces (by flags) are kept.
   tl: a bitmap Flink scales (Presents, the asteroids, the island, the demon, the fart). Flink renders
   each frame into one bitmap (0x1DC4A: the box from the pieces' top-left to their frames' far
   corner, the last piece painted first, so the first is in front), scales it (0x1DAD4) and
   centres the smaller bitmap in the full one's box (0x1DEEA / 0x1DE66: (W - w) / 2, (H - h) / 2),
   which it places by the box's top-left. So each frame here is that one bitmap (no seams where
   scaled pieces meet), its point the box's middle, about which Actor_Draw scales it: its entry's
   dx, dy are minus the box's half size, which FLINK.DLL reads back to put the point at the box's
   top-left + its half. */
/* scene_record: a Flink record -> a Lomax record, its pieces on their palette line (the ANIM's | the
   piece flags' bits 5-6); keepSel: only pieces with that selector (-1: all). tl: a bitmap Flink
   scales: each frame is one bitmap of its pieces (the last painted first), its point the box's middle */
static LRec scene_record(const char *tag, const Rec *rec, Sprites *s, const FWorld *w, int tl, int keepSel)
{
    int st[4096], nf = flink_frames(rec, st), k, i;
    Ents ents = { 0 };
    LRec out;
    int zero[4] = { 0, 0, 0, 0 };
    for (k = 0; k < nf; k++) {
        int from = ents.n, nps = 0, ps[256], j;
        for (i = st[k]; i < st[k + 1]; i++) {
            const Piece *p = &rec->pc[i];
            if (p->a & 0x80 || p->f & 0x80 || (keepSel >= 0 && (p->fl & 7) != keepSel)) continue;
            if (nps == 256) die("scene %s: too many pieces", tag);
            ps[nps++] = i;
        }
        if (tl && nps) {
            int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30), W, H, x, y;
            u8 *img;
            Frames fr;
            Ent e;
            for (j = 0; j < nps; j++) {
                const Piece *p = &rec->pc[ps[j]];
                Frame f = flink_anim_frames(w->anims.v[p->a]).f[p->f];
                x0 = imin(x0, p->dx); y0 = imin(y0, p->dy);
                x1 = imax(x1, p->dx + f.w); y1 = imax(y1, p->dy + f.h);
            }
            W = x1 - x0; H = y1 - y0;
            img = xalloc((size_t)(W * H));
            for (j = nps - 1; j >= 0; j--) {
                const Piece *p = &rec->pc[ps[j]];
                Frame f = flink_anim_frames(w->anims.v[p->a]).f[p->f];
                int want = anim_line(w, p->a) | (p->fl >> 5 & 3);
                for (y = 0; y < f.h; y++)
                    for (x = 0; x < f.w; x++) {
                        int v = f.px[(p->fl & 0x10 ? f.h - 1 - y : y) * f.w + (p->fl & 0x08 ? f.w - 1 - x : x)];
                        if (v) img[(p->dy - y0 + y) * W + p->dx - x0 + x] = (u8)(want * 16 + (v & 15));
                    }
            }
            fr.n = 1;
            fr.f = xalloc(sizeof(Frame));
            fr.f[0].w = W; fr.f[0].h = H; fr.f[0].px = img;
            e.bank = sprites_add(s, sk(SK_ADDED, g_addId++, 0, 0), fr);
            e.frame = 1; e.dx = -(W / 2); e.dy = -(H / 2); e.fl = 0;
            vpush(ents, e);
        } else {
            for (j = 0; j < nps; j++) {
                const Piece *p = &rec->pc[ps[j]];
                int line = anim_line(w, p->a), want = line | (p->fl >> 5 & 3);
                Ent e;
                e.bank = sprites_bank(s, want == line ? SK_A(p->a) : sk(SK_LINE, p->a, want, 0));
                e.frame = p->f + 1; e.dx = p->dx; e.dy = p->dy; e.fl = p->fl & 0x18;
                vpush(ents, e);
            }
        }
        if (ents.n == from) { Ent b = { sprites_bank(s, sk(SK_BLANK, 0, 0, 0)), 1, 0, 0, 0 }; vpush(ents, b); }
        end_frame(&ents, from);
    }
    out = pack_record(tag, rec->speed, zero, ents.v, ents.n);
    free(ents.v);
    return out;
}

/* a Flink map's pixels (line * 16 + colour, 0 transparent), w * 8 by h * 8, row by row */
/* layer_pixels: a Flink map's pixels (line * 16 + colour, 0 transparent), row by row */
static u8 *layer_pixels(const FMap *fm, Bytes src, int *Wo, int *Ho)
{
    static int used[8192];
    int w8 = (int)be32(fm->head.p), h8 = (int)be32(fm->head.p + 4), mw = w8 / 2, mh = h8 / 2, W = w8 * 8;
    int nused = head_used_cells(fm->head, used), ntt = (int)(fm->ttab.n / 2), cx, cy, k, i;
    u8 *img = xalloc((size_t)(W * h8 * 8));
    static const int OX[4] = { 0, 0, 8, 8 }, OY[4] = { 0, 8, 0, 8 };
    for (cy = 0; cy < mh; cy++)
        for (cx = 0; cx < mw; cx++) {
            int t = (int)be16(fm->map.p + 2 * (cy * mw + cx)) & 0x7FF;
            for (k = 0; k < 4; k++) {
                unsigned e;
                int n, line;
                if (t * 4 + k >= ntt) continue;
                e = be16(fm->ttab.p + 2 * (t * 4 + k));
                n = (int)(e & 0x7FF) - 0x180;
                if (n < 0 || n >= nused) continue;
                line = (int)(e >> 13) & 3;
                for (i = 0; i < 64; i++) {
                    size_t b = (size_t)used[n] * 32 + (size_t)(i >> 1);
                    int v = b < src.n ? src.p[b] : 0, c = i & 1 ? v & 15 : v >> 4, x = i & 7, y = i >> 3;
                    if (!c) continue;
                    if (e & 0x800) x = 7 - x;
                    if (e & 0x1000) y = 7 - y;
                    img[(cy * 16 + OY[k] + y) * W + cx * 16 + OX[k] + x] = (u8)(line * 16 + c);
                }
            }
        }
    *Wo = W;
    *Ho = h8 * 8;
    return img;
}

/* The space scene's villain: Stars' palette level (main command 0x94, 0xFF1DF4) sets 8 colours of the
   palette buffer (0xFF16BA), line 1 colours 8-15 (24-31), from a copy of them (level -1, 0xFF1E48:
   movem.l (d16,pc) 4CFA 000F F89E, the displacement word at 0xFF1E4C, so from 0xFF16EA, colour 24, to
   0xFF1D48), each component (0-14, even) less (7 - level) * 2, at least 0: level 0 (from the scene's
   start) makes them black, 7 shows them as they are. Only the portrait's eyes use them; colour 24, the
   rectangle around the eyes, is black (0x000) at every level. So the plane has them in the black
   around them (the commonest other colour in their rectangle, line 0 colour 8; SCENE_BLACK, 0x0001,
   shows a shade apart from it) and EYES has a frame for each level 1-7, each pixel n in that level's
   colour of n (EYE_PAL + (level - 1) * 8 + n - 24; black: transparent), which FLINK.DLL draws over it.

   Flink's 0xFF1E0E: each component (0-14, even) less (7 - level) * 2, at least 0 */
/* eye_colour: Flink's 0xFF1E0E: each component (0-14, even) less (7 - level) * 2, at least 0 */
static unsigned eye_colour(unsigned vdp, int level)
{
    int d = (EYE_LEVELS - level) * 2, sh;
    unsigned out = 0;
    for (sh = 0; sh <= 8; sh += 4) out += (unsigned)imax(0, (int)((vdp >> sh) & 0xE) - d) << sh;
    return out;
}

/* ---- the inpainting ---- */
static void grow(const u8 *m, u8 *out, int W, int H, int r)
{
    int x, y, dx, dy;
    memcpy(out, m, (size_t)(W * H));
    for (dy = -r; dy <= r; dy++)
        for (dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy > r * r) continue;
            for (y = 0; y < H; y++)
                for (x = 0; x < W; x++)
                    out[y * W + x] |= m[fmodp(y - dy, H) * W + fmodp(x - dx, W)];   /* (np.roll wraps) */
        }
}

/* the pixels of `colours` inside box, dilated by 4 px and eroded by 2, wrapping at the picture's edges */
/* scene_hole: the pixels of the colours inside box, dilated by 4 px and eroded by 2 */
static u8 *scene_hole(const u8 *idx, int W, int H, const int box[4], int *count)
{
    u8 *m = xalloc((size_t)(W * H)), *a = xalloc((size_t)(W * H)), *b = xalloc((size_t)(W * H));
    int x, y, i;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            int v = idx[y * W + x], in = x >= box[0] && x < box[2] && y >= box[1] && y < box[3];
            m[y * W + x] = (u8)(in && v >= 1 && v <= 15 && v != 8);
        }
    grow(m, a, W, H, 4);
    for (i = 0; i < W * H; i++) a[i] = !a[i];
    grow(a, b, W, H, 2);
    *count = 0;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            int in = x >= box[0] && x < box[2] && y >= box[1] && y < box[3];
            m[y * W + x] = (u8)(!b[y * W + x] && in);
            *count += m[y * W + x];
        }
    free(a); free(b);
    return m;
}

/* the float32 pairwise sum of n values (numpy's pairwise_sum, n <= 128) */
static float pairwise(const float *a, int n)
{
    float res = 0.0f, r[8];
    int i, j;
    if (n < 8) {
        for (i = 0; i < n; i++) res = res + a[i];
        return res;
    }
    for (j = 0; j < 8; j++) r[j] = a[j];
    for (i = 8; i < n - (n % 8); i += 8) for (j = 0; j < 8; j++) r[j] = r[j] + a[i + j];
    {
        float s01 = r[0] + r[1], s23 = r[2] + r[3], s45 = r[4] + r[5], s67 = r[6] + r[7];
        float s03 = s01 + s23, s47 = s45 + s67;
        res = s03 + s47;
    }
    for (; i < n; i++) res = res + a[i];
    return res;
}

/* Exemplar-based filling (Criminisi, Perez and Toyama, 2004): idx (H x W) pixel values, rgb their
   colours, mask the hole, src (x0, y0, x1, y1) where patches may come from. Each step takes the point
   on the hole's edge of highest priority (the known share of its patch x (1 + the colour edge
   across it), so edges carry on into the hole first), finds the whole known patch within `rows` rows
   of it that matches its known pixels best (least squares on the colours) and copies that patch's
   pixels into its unknown ones. Whole pixels are copied, so the dithering stays Flink's. -> idx */
/* scene_inpaint: exemplar-based filling. Each step takes the point on the hole's edge of highest
   priority (the known share of its patch x (1 + the colour edge across it / 64)), finds the whole
   known patch within SCENE_ROWS rows of it that matches its known pixels best (least squares on the
   colours) and copies that patch's pixels into its unknown ones. idx is changed. */
static void scene_inpaint(u8 *idx, const int *lut, u8 *mask, int W, int H, const int src[4])
{
    const int P = SCENE_PATCH, S = 2 * P + 1, rows = SCENE_ROWS;
    int *rgb = xalloc((size_t)(W * H * 3) * sizeof(int)), i, x, y;
    float *conf = xalloc((size_t)(W * H) * sizeof(float)), *mean = xalloc((size_t)(W * H) * sizeof(float)),
          *grad = xalloc((size_t)(W * H) * sizeof(float));
    u8 *edge = xalloc((size_t)(W * H));
    for (i = 0; i < W * H; i++) {
        rgb[i * 3] = lut[idx[i] * 3]; rgb[i * 3 + 1] = lut[idx[i] * 3 + 1]; rgb[i * 3 + 2] = lut[idx[i] * 3 + 2];
        conf[i] = mask[i] ? 0.0f : 1.0f;
    }
    for (;;) {
        int any = 0, have = 0, by = 0, bx = 0, ra, rb, py, px, bpy = 0, bpx = 0, haveD = 0;
        float bestPr = 0, bestC = 0;
        long long bestD = 0;
        for (i = 0; i < W * H; i++) if (mask[i]) { any = 1; break; }
        if (!any) break;
        for (i = 0; i < W * H; i++)                         /* np.where(known, rgb.mean(2), 0) */
            mean[i] = mask[i] ? 0.0f : (float)((double)(rgb[i * 3] + rgb[i * 3 + 1] + rgb[i * 3 + 2]) / 3.0);
        for (y = 0; y < H; y++)                             /* np.gradient, then np.hypot */
            for (x = 0; x < W; x++) {
                float gy, gx;
                if (y == 0) gy = mean[W + x] - mean[x];
                else if (y == H - 1) gy = mean[y * W + x] - mean[(y - 1) * W + x];
                else gy = (float)((double)(float)(mean[(y + 1) * W + x] - mean[(y - 1) * W + x]) / 2.0);
                if (x == 0) gx = mean[y * W + 1] - mean[y * W];
                else if (x == W - 1) gx = mean[y * W + x] - mean[y * W + x - 1];
                else gx = (float)((double)(float)(mean[y * W + x + 1] - mean[y * W + x - 1]) / 2.0);
                grad[y * W + x] = (float)sqrt((double)gx * gx + (double)gy * gy);
            }
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++)
                edge[y * W + x] = (u8)(mask[y * W + x] &&
                    (!mask[fmodp(y - 1, H) * W + x] || !mask[fmodp(y + 1, H) * W + x] ||
                     !mask[y * W + fmodp(x - 1, W)] || !mask[y * W + fmodp(x + 1, W)]));
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                int ya, yb, xa, xb, yy, xx, kany = 0;
                float gmax = 0, csum = 0.0f, c, f1, pr, rowv[16];
                if (!edge[y * W + x]) continue;
                ya = imax(0, y - P); yb = imin(H, y + P + 1); xa = imax(0, x - P); xb = imin(W, x + P + 1);
                for (yy = ya; yy < yb; yy++) {
                    for (xx = xa; xx < xb; xx++) {
                        rowv[xx - xa] = conf[yy * W + xx];
                        if (!mask[yy * W + xx]) { if (!kany || grad[yy * W + xx] > gmax) gmax = grad[yy * W + xx]; kany = 1; }
                    }
                    csum = csum + pairwise(rowv, xb - xa);
                }
                c = (float)((double)csum / (double)(S * S));
                f1 = (float)(1.0 + (kany ? (double)gmax : 0.0) / 64.0);
                pr = (float)((double)c * (double)f1);
                if (!have || pr > bestPr) { have = 1; bestPr = pr; by = y; bx = x; bestC = c; }
            }
        y = imin(imax(by, P), H - P - 1);
        x = imin(imax(bx, P), W - P - 1);
        ra = imax(src[1], y - rows - P);
        rb = imin(src[3], y + rows + P + 1);
        if (rb - ra < S + 1) { ra = imax(src[1], imin(ra, src[3] - S - 1)); rb = ra + S + 1; }
        for (py = 0; py + S <= rb - ra; py++)
            for (px = 0; px + S <= src[2] - src[0]; px++) {
                int bad = 0, ii, jj;
                long long d = 0;
                for (ii = 0; ii < S && !bad; ii++)
                    for (jj = 0; jj < S; jj++) if (mask[(ra + py + ii) * W + src[0] + px + jj]) { bad = 1; break; }
                if (bad) continue;
                for (ii = 0; ii < S; ii++)
                    for (jj = 0; jj < S; jj++) {
                        int t = (y - P + ii) * W + x - P + jj, s2 = (ra + py + ii) * W + src[0] + px + jj, ch;
                        if (mask[t]) continue;
                        for (ch = 0; ch < 3; ch++) { long long dd = rgb[s2 * 3 + ch] - rgb[t * 3 + ch]; d += dd * dd; }
                    }
                if (!haveD || d < bestD) { haveD = 1; bestD = d; bpy = py; bpx = px; }
            }
        if (haveD && bestD >= (1 << 24)) die("the intro's inpainting: a best match of %lld, past float32's exact range", bestD);
        py = bpy + ra; px = bpx + src[0];
        {
            int ii, jj;
            for (ii = 0; ii < S; ii++)
                for (jj = 0; jj < S; jj++) {
                    int t = (y - P + ii) * W + x - P + jj, s2 = (py + ii) * W + px + jj;
                    if (!mask[t]) continue;
                    idx[t] = idx[s2];
                    rgb[t * 3] = rgb[s2 * 3]; rgb[t * 3 + 1] = rgb[s2 * 3 + 1]; rgb[t * 3 + 2] = rgb[s2 * 3 + 2];
                    conf[t] = bestC;
                }
            for (ii = 0; ii < S; ii++) for (jj = 0; jj < S; jj++) mask[(y - P + ii) * W + x - P + jj] = 0;
        }
    }
    free(rgb); free(conf); free(mean); free(grad); free(edge);
}

/* Cells with some pixel values replaced (remap: value -> value)

   Flink's intro and ending as Lomax worlds out_dir/SCENE0.WLD ... SCENE7.WLD (SCENES) -> LEVELS.INI
   lines ([Scenes]). Each has four planes: Flink's priority tiles; the play plane, SCENE_SCREEN_W wide
   and empty, so that a host's widescreen shows Flink's screen with black bars beside it as on any map
   narrower than its view (FLINK.DLL scenes.c draws the sprites on it at the map's place); the map's
   other tiles; the background. The play plane holds a Start that only marks it (0x2000, never spawned,
   as the title's) and one 'Spik Scene' object (FLINK.DLL scenes.c runs the scene from it), whose type holds the scene's
   sprites (SCENE_SPRITES) under tags of their own, BLAK (512 x 32 px of black: bars), WHIT (512 x 256
   px of white: a flash), and in the space scene STAR (its four shades) and EYES. A map lower than
   SCENE_VIEW_H gets SCENE_PAD px of black above and below; no background is opaque black. */
void scenes(const char *outDir, Buf *ini)
{
    u8 *tplData;
    size_t tplLen;
    LChunks tpl;
    const LChunk *tn, *th;
    LTypes types = { 0 };
    int nmy_end, t_start = -1, i, k, pads[8], eyesX = 0, eyesY = 0, zero[4] = { 0, 0, 0, 0 };
    char files[8][16];

    tplData = read_game_world("TITLE.WLD", &tplLen);
    tpl = lomax_chunks(tplData, tplLen);
    tn = lchunk(&tpl, "NMY "); th = lchunk(&tpl, "HEAD");
    if (!tn || !th) die("WORLDS\\TITLE.WLD: no NMY or HEAD");
    nmy_end = lomax_types(tn->p.p, tn->p.n, &types);
    for (i = 0; i < types.n; i++) if (!strcmp(types.v[i].name, "Start")) { t_start = i; break; }
    if (t_start < 0) die("WORLDS\\TITLE.WLD: no Start type");

    for (k = 0; k < 8; k++) {
        FWorld *w = world_of(SCENES[k].world);
        const FMap *play = &w->maps.v[SCENES[k].m - 1], *tiles = &w->maps.v[(SCENES[k].tiles_m ? SCENES[k].tiles_m : SCENES[k].m) - 1];
        int w8 = (int)be32(tiles->head.p), h8 = (int)be32(tiles->head.p + 4), b = SCENES[k].b, pad, prow, mw, mh, H8, bw8, bh8;
        int black, blackMeta, t_scene, cw8, ncur, nrec = 0;
        unsigned pal[256];
        Sprites sprites;
        LRec recs[16];
        u8 remap[256], animbits[32];
        Buf nmy = { 0 }, out = { 0 }, ttab_fg = { 0 }, ttab_pl = { 0 }, ttab_bg = { 0 }, map_fg = { 0 }, map_pl = { 0 }, map_bg = { 0 };
        IntVec meta_fg = { 0 }, meta_pl = { 0 }, meta_bg = { 0 }, rows_bg = { 0 };
        Cells cells;
        static int used[8192];
        Objs objs = { 0 }, none = { 0 };
        char path[MAX_PATH], extra[256] = "";
        int e8[8] = { 0 };

        for (i = 0; i < 256; i++) { pal[i] = 0; remap[i] = (u8)i; }
        for (i = 0; i < 64; i++) pal[i] = vdp_to_15(be16(play->head.p + 8 + 2 * i));
        pal[BACKDROP] = pal[0] ? pal[0] : 1;                   /* (opaque: the padding, no background) */
        pal[0] = 0;
        pal[SCENE_BLACK] = 0x0001; pal[SCENE_WHITE] = 0x7FFF;  /* (colour 0 is transparent) */
        if (SCENES[k].tiles_m) for (i = 0; i < 4; i++) pal[SCENE_STARS + i] = vdp_to_15(be16(play->head.p + 8 + 2 * (58 + i)));
        memset(&sprites, 0, sizeof(sprites));
        sprites.w = w;
        for (i = 0; i < SCENES[k].nspr; i++) {
            const SceneSprite *sp = &SCENES[k].spr[i];
            int j, slot = -1;
            const Rec *rec;
            for (j = 0; j < w->nmy.n && slot < 0; j++) {         /* (the names stripped) */
                const char *n = w->nmy.v[j];
                size_t l;
                while (*n == ' ') n++;
                l = strlen(n);
                while (l && n[l - 1] == ' ') l--;
                if (l == strlen(sp->type) && !strncmp(n, sp->type, l)) slot = j;
            }
            if (slot < 0 || !(rec = rec_get(&w->recs.v[slot], sp->ftag))) die("%s.WLD: no %s %s", SCENES[k].world, sp->type, sp->ftag);
            recs[nrec++] = scene_record(sp->tag, rec, &sprites, w, sp->tl, !strcmp(sp->tag, "DEM2") ? DEM2_SHOWN : -1);
        }
        {                                                       /* BLAK: 512 x 32 px of black; WHIT: 512 x 256 px of white */
            Frames f;
            Ent e[32];
            int bk;
            f.n = 1; f.f = xalloc(sizeof(Frame)); f.f[0].w = 64; f.f[0].h = 32; f.f[0].px = xalloc(2048); memset(f.f[0].px, SCENE_BLACK, 2048);
            bk = sprites_add(&sprites, sk(SK_ADDED, g_addId++, 0, 0), f);
            for (i = 0; i < 8; i++) { e[i].bank = bk; e[i].frame = 1; e[i].dx = 64 * i; e[i].dy = 0; e[i].fl = i == 7 ? 0x80 : 0; }
            recs[nrec++] = pack_record("BLAK", 0x40, zero, e, 8);
            f.f = xalloc(sizeof(Frame)); f.f[0].w = 64; f.f[0].h = 64; f.f[0].px = xalloc(4096); memset(f.f[0].px, SCENE_WHITE, 4096);
            bk = sprites_add(&sprites, sk(SK_ADDED, g_addId++, 0, 0), f);
            for (i = 0; i < 32; i++) { e[i].bank = bk; e[i].frame = 1; e[i].dx = 64 * (i % 8); e[i].dy = 64 * (i / 8); e[i].fl = i == 31 ? 0x80 : 0; }
            recs[nrec++] = pack_record("WHIT", 0x40, zero, e, 32);
        }
        if (SCENES[k].tiles_m) {                                 /* the space scene: stars and the villain's eyes */
            Frames f, ef = { 0 };
            Ent e[4];
            Ents ee = { 0 };
            int bk, W, H, x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1, x, y, lv, blank, cnt[256] = { 0 }, first[256], order = 0, dark = -1;
            u8 *img;
            VEC(Frame) frs = { 0 };
            f.n = 4; f.f = xalloc(4 * sizeof(Frame));
            for (i = 0; i < 4; i++) { f.f[i].w = 1; f.f[i].h = 1; f.f[i].px = xalloc(1); f.f[i].px[0] = (u8)(SCENE_STARS + i); }
            bk = sprites_add(&sprites, sk(SK_ADDED, g_addId++, 0, 0), f);
            for (i = 0; i < 4; i++) { e[i].bank = bk; e[i].frame = i + 1; e[i].dx = 0; e[i].dy = 0; e[i].fl = 0x80; }
            recs[nrec++] = pack_record("STAR", 0, zero, e, 4);
            img = layer_pixels(tiles, w->cells, &W, &H);
            for (i = 0; i < W * H; i++)
                if (img[i] >= 24 && img[i] < 32) { x0 = imin(x0, i % W); y0 = imin(y0, i / W); x1 = imax(x1, i % W + 1); y1 = imax(y1, i / W + 1); }
            if (x1 < 0) die("INTRO map %d: no eyes", SCENES[k].tiles_m);
            for (lv = 1; lv <= EYE_LEVELS; lv++) {
                unsigned fade[8];
                int from = ee.n, tx, ty;
                for (i = 0; i < 8; i++) {
                    fade[i] = eye_colour(be16(play->head.p + 8 + 2 * (24 + i)), lv);
                    pal[EYE_PAL + (lv - 1) * 8 + i] = fade[i] ? vdp_to_15(fade[i]) : 0;
                }
                for (ty = y0; ty < y1; ty += EYE_TILE)
                    for (tx = x0; tx < x1; tx += EYE_TILE) {
                        int tw = imin(EYE_TILE, x1 - tx), thh = imin(EYE_TILE, y1 - ty), any = 0;
                        Frame fr;
                        fr.w = tw; fr.h = thh; fr.px = xalloc((size_t)(tw * thh));
                        for (y = 0; y < thh; y++)
                            for (x = 0; x < tw; x++) {
                                int v = img[(ty + y) * W + tx + x];
                                if (v >= 24 && v < 32 && fade[v - 24]) { fr.px[y * tw + x] = (u8)(EYE_PAL + (lv - 1) * 8 + v - 24); any = 1; }
                            }
                        if (any) {
                            Ent q;
                            vpush(frs, fr);
                            q.bank = 0; q.frame = frs.n; q.dx = tx - x0; q.dy = ty - y0; q.fl = 0;
                            vpush(ee, q);
                        } else free(fr.px);
                    }
                e8[lv] = ee.n - from;
                if (!e8[lv]) { Ent q = { -1, 1, 0, 0, 0 }; vpush(ee, q); }
                end_frame(&ee, from);
            }
            ef.n = frs.n; ef.f = frs.v;
            bk = sprites_add(&sprites, sk(SK_ADDED, g_addId++, 0, 0), ef);
            blank = sprites_bank(&sprites, sk(SK_BLANK, 0, 0, 0));
            for (i = 0; i < ee.n; i++) ee.v[i].bank = ee.v[i].bank < 0 ? blank : bk;
            recs[nrec++] = pack_record("EYES", 0x40, zero, ee.v, ee.n);
            free(ee.v);
            for (y = y0; y < y1; y++)                            /* the commonest other colour around them, first seen first */
                for (x = x0; x < x1; x++) {
                    int v = img[y * W + x];
                    if (!cnt[v]) first[v] = order++;
                    cnt[v]++;
                }
            for (i = 1; i < 256; i++)
                if (cnt[i] && !(i >= 24 && i < 32) && (dark < 0 || cnt[i] > cnt[dark] || (cnt[i] == cnt[dark] && first[i] < first[dark]))) dark = i;
            if (dark < 0) die("INTRO map %d: nothing around the eyes", SCENES[k].tiles_m);
            for (i = 24; i < 32; i++) remap[i] = (u8)dark;
            eyesX = x0; eyesY = y0;
            free(img);
        }
        t_scene = types.n;
        buf_add(&nmy, tn->p.p, (size_t)nmy_end);
        lomax_type(&nmy, "Spik Scene", 0, 0, recs, nrec);
        buf_add(&nmy, NULL, 2);
        memset(animbits, 0, sizeof(animbits));
        for (i = 0; i < nrec; i++) bank_union((BankSet *)animbits, &recs[i].banks);

        pad = h8 * 8 < SCENE_VIEW_H ? SCENE_PAD : 0;
        cells_init(&cells, w->cells);
        cells.remap = remap;
        head_used_cells(tiles->head, used);
        convert_layer(tiles, used, &cells, keep_fg, 0, 0, &ttab_fg, &meta_fg);
        convert_layer(tiles, used, &cells, keep_pl, 0, 0, &ttab_pl, &meta_pl);
        black = cells_get(&cells, -1, 0, 1);                   /* (BACKDROP: Flink's colour 0, black in every scene) */
        blackMeta = (int)(ttab_pl.n / 16);
        { u8 t[16] = { 0 }; for (i = 0; i < 4; i++) { t[i * 4] = (u8)(black >> 8); t[i * 4 + 1] = (u8)black; } buf_add(&ttab_pl, t, 16); }
        mw = w8 / 2; mh = h8 / 2;
        prow = pad / 16;
        H8 = h8 + 4 * prow;
        {
            int r, total = mw * (mh + 2 * prow);
            for (r = 0; r < total; r++) {
                int row = r / mw, a = 0, p2 = blackMeta;
                u8 x2[2];
                if (row >= prow && row < prow + mh) {
                    unsigned wd = be16(tiles->map.p + 2 * (r - prow * mw));
                    a = meta_fg.v[wd & 0x7FF]; p2 = meta_pl.v[wd & 0x7FF];
                }
                x2[0] = (u8)(a >> 8); x2[1] = (u8)a; buf_add(&map_fg, x2, 2);
                x2[0] = (u8)(p2 >> 8); x2[1] = (u8)p2; buf_add(&map_pl, x2, 2);
            }
            buf_add(&map_fg, NULL, (size_t)total);
            buf_add(&map_pl, NULL, (size_t)total);
        }
        if (b) {
            const FMap *bg = &w->maps.v[b - 1];
            static int usedB[8192];
            bw8 = (int)be32(bg->head.p); bh8 = (int)be32(bg->head.p + 4);
            head_used_cells(bg->head, usedB);
            convert_layer(bg, usedB, &cells, keep_all, 0, 1, &ttab_bg, &meta_bg);
            for (i = 0; i < meta_bg.n; i++) if (!meta_bg.v[i]) meta_bg.v[i] = meta_bg.v[0];
            for (i = 0; i < bw8 / 2 * (bh8 / 2); i++) vpush(rows_bg, meta_bg.v[be16(bg->map.p + 2 * i) & 0x7FF]);
            if (!strcmp(SCENES[k].world, "INTRO") && b == 6) {   /* SCENE_PAINT: Flink in the tiles */
                static const int box[4] = { 40, 40, 270, 216 }, src[4] = { 0, 24, 320, 200 };
                int IW, IH, lut[64 * 3], holeN, r, c, nblocks = 0, x, y;
                u8 *idx = layer_pixels(bg, w->cells, &IW, &IH), *orig, *hole, *blocks;
                for (i = 0; i < 64; i++) {
                    unsigned cc = be16(play->head.p + 8 + 2 * i);
                    lut[i * 3] = 36 * (int)((cc >> 1) & 7); lut[i * 3 + 1] = 36 * (int)((cc >> 5) & 7); lut[i * 3 + 2] = 36 * (int)((cc >> 9) & 7);
                }
                orig = xalloc((size_t)(IW * IH));
                memcpy(orig, idx, (size_t)(IW * IH));
                hole = scene_hole(idx, IW, IH, box, &holeN);
                scene_inpaint(idx, lut, hole, IW, IH, src);
                blocks = xalloc((size_t)((IW / 16 + 1) * (IH / 16 + 1)));
                for (y = 0; y < IH; y++) for (x = 0; x < IW; x++) if (idx[y * IW + x] != orig[y * IW + x]) blocks[(y / 16) * (IW / 16 + 1) + x / 16] = 1;
                for (r = 0; r <= IH / 16; r++)
                    for (c = 0; c <= IW / 16; c++) {
                        static const int OX[4] = { 0, 0, 8, 8 }, OY[4] = { 0, 8, 0, 8 };
                        u8 t[16];
                        int q;
                        if (!blocks[r * (IW / 16 + 1) + c]) continue;
                        for (q = 0; q < 4; q++) {
                            u8 px[64];
                            int id, yy, xx;
                            for (yy = 0; yy < 8; yy++)
                                for (xx = 0; xx < 8; xx++) {
                                    int v = idx[(r * 16 + OY[q] + yy) * IW + c * 16 + OX[q] + xx];
                                    px[yy * 8 + xx] = (u8)(v ? v : BACKDROP);
                                }
                            id = cells_raw(&cells, px);
                            t[q * 4] = (u8)(id >> 8); t[q * 4 + 1] = (u8)id; t[q * 4 + 2] = 0; t[q * 4 + 3] = 0;
                        }
                        rows_bg.v[r * (bw8 / 2) + c] = (int)(ttab_bg.n / 16);
                        buf_add(&ttab_bg, t, 16);
                        nblocks++;
                    }
                printf("  scene %d: Flink painted out of %s map %d, %d px, %d blocks\n", k, SCENES[k].world, b, holeN, nblocks);
                free(idx); free(orig); free(hole); free(blocks);
            }
        } else {
            u8 t[32] = { 0 };
            bw8 = 64; bh8 = 32;
            for (i = 0; i < 4; i++) { t[16 + i * 4] = (u8)(black >> 8); t[16 + i * 4 + 1] = (u8)black; }
            buf_add(&ttab_bg, t, 32);
            for (i = 0; i < bw8 / 2 * (bh8 / 2); i++) vpush(rows_bg, 1);
        }
        for (i = 0; i < rows_bg.n; i++) { u8 x2[2]; x2[0] = (u8)(rows_bg.v[i] >> 8); x2[1] = (u8)rows_bg.v[i]; buf_add(&map_bg, x2, 2); }
        buf_add(&map_bg, NULL, (size_t)rows_bg.n);
        { Obj o = obj(t_start, 160, pad + 100, 0x2000, NULL, 0, NULL); vpush(objs, o); }
        { Obj o = obj(t_scene, 160, pad + 100, 0x4000, NULL, 0, NULL); vpush(objs, o); }
        cw8 = SCENE_SCREEN_W / 8;                               /* the play plane: Flink's screen wide, empty */
        ncur = cw8 / 2 * (H8 / 2);
        { u8 wr[8] = { 'P', 'S', '-', 'X', 0xD2, 0x04, 0, 0 }; chunk(&out, "WRLD", wr, 8); }
        chunk(&out, "NMY ", nmy.p, nmy.n);
        build_head(&out, w8, H8, pal, th->p, animbits);
        chunk(&out, "TTAB", ttab_fg.p, ttab_fg.n); chunk(&out, "MAP ", map_fg.p, map_fg.n); gnmy(&out, &none);
        {
            u8 *z = xalloc((size_t)(3 * ncur + 16));
            build_head(&out, cw8, H8, pal, th->p, animbits);
            chunk(&out, "TTAB", z, 16); chunk(&out, "MAP ", z, (size_t)(3 * ncur)); gnmy(&out, &objs);
            free(z);
        }
        build_head(&out, w8, H8, pal, th->p, animbits);
        chunk(&out, "TTAB", ttab_pl.p, ttab_pl.n); chunk(&out, "MAP ", map_pl.p, map_pl.n); gnmy(&out, &none);
        build_head(&out, bw8, bh8, pal, th->p, animbits);
        chunk(&out, "TTAB", ttab_bg.p, ttab_bg.n); chunk(&out, "MAP ", map_bg.p, map_bg.n); gnmy(&out, &none);
        chunk(&out, "CELL", cells.data.p, cells.data.n);
        sprites_chunks(&sprites, &out);
        chunk(&out, "END ", NULL, 0);
        snprintf(files[k], sizeof(files[k]), "SCENE%d.WLD", k);
        snprintf(path, sizeof(path), "%s\\%s", outDir, files[k]);
        write_file(path, out.p, out.n);
        pads[k] = pad;
        if (SCENES[k].tiles_m) snprintf(extra, sizeof(extra), " (objects of map %d)", SCENES[k].m);
        {
            char over[32], padded[32] = "";
            if (b) snprintf(over, sizeof(over), "map %d", b); else snprintf(over, sizeof(over), "black");
            if (pad) snprintf(padded, sizeof(padded), ", padded %d px", pad);
            printf("  scene %d %s: %s map %d%s over %s, %dx%d px%s, %d sprite records, %d cells, %d bytes\n",
                   k, files[k], SCENES[k].world, SCENES[k].tiles_m ? SCENES[k].tiles_m : SCENES[k].m, extra, over,
                   w8 * 8, h8 * 8, padded, SCENES[k].nspr + 2 + (SCENES[k].tiles_m ? 2 : 0), cells.n, (int)out.n);
        }
        for (i = 0; i < nrec; i++) free_lrec(&recs[i]);
        buf_free(&nmy); buf_free(&out); buf_free(&ttab_fg); buf_free(&ttab_pl); buf_free(&ttab_bg);
        buf_free(&map_fg); buf_free(&map_pl); buf_free(&map_bg);
        free(meta_fg.v); free(meta_pl.v); free(meta_bg.v); free(rows_bg.v); free(objs.v);
        buf_free(&cells.data); hmap_free(&cells.ids);
        free(sprites.keys.v); free(sprites.frames.v);
        (void)e8;
    }
    ini_add(ini, "; Flink's intro (the Psygnosis logo, then its five scenes) and ending (FLINK.DLL scenes.c):");
    ini_add(ini, "; the files, each map's black padding above and below, the villain's eyes' top-left (space scene)");
    ini_add(ini, "[Scenes]");
    {
        char a[256] = "", b2[256] = "", p[64] = "";
        for (i = 0; i < 8; i++) {
            char *d = i < SCENE_INTRO ? a : b2;
            snprintf(d + strlen(d), 256 - strlen(d), "%s%s", (i && i != SCENE_INTRO) ? " " : "", files[i]);
            snprintf(p + strlen(p), sizeof(p) - strlen(p), "%s%d", i ? "," : "", pads[i]);
        }
        ini_add(ini, "Intro=%s", a);
        ini_add(ini, "Ending=%s", b2);
        ini_add(ini, "Pad=%s", p);
        ini_add(ini, "Eyes=%d,%d", eyesX, eyesY);
    }
    free(tplData);
}
