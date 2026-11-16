/*
 * snake.c - Flink's Snake, 'Snake' (init 0x251BA, thinks 0x25254 / 0x2527E, draw 0x252D8),
 * FOREST map 8 = 3-2 (README "Snake"). The converter makes it a 'Spik Snake' decoration
 * (object 51) with params height, length and delay, records MAIN (blank), BUB1, BUB2 and HEAD /
 * BODY at Flink's 32 sizes (see the converter's snake_records). A Generator spawns it. Flink's:
 * - Init: y -= 30; up to 11 segments (length, at most 11), all out (-1).
 * - Waiting: 'delay' + 1 frames, then it takes Flink's x as its target (+0x4C = 0x81F6) and plays
 *   sound 0x43.
 * - Attacking, frame t from 0: every 7 frames, while t / 7 < length, segment t / 7 starts (speed 1.0,
 *   z 0; the head first) and the bubbles (BUB1) show for 12 frames. At t = 300 it is deleted.
 * - Each segment, each frame (draw 0x252D8, 0x2542A): speed += 1/32, z += speed; out past z 700.
 *   Its point on the screen: x from the placement's point on the background plane (x0 / 2 -
 *   background scroll) to the target's screen x, z / 512 of the way (going on past it); y from the
 *   start y up a sine arc 'height' px high (sin(z) * height, 1024 a turn) while z < 512, then down
 *   (z - 512) * 2 / 3 px. Its size is sprite list 31 - z / 16 (0 from 512 on). Below z 256 it is
 *   drawn behind the tiles (low sprite priority), then in front of everything. Flink's Priority actor
 *   (0x255E8, as in 3-2) raises the map's tiles and the prebuilt sprite lists of its table 0x2562A,
 *   not the Snake's, so the far segments stay behind all of the play map's tiles there too. From z 400 on it
 *   hurts Flink (0x1A46E) in a 32 x 32 box from its sprite's left edge, 8 px under its point
 *   (head: 5 px higher); Flink checks one segment in four a frame, here all. Falling, within 30 px
 *   above the water's surface (0x8718), it makes the splash BUB2 show at the target x on the
 *   surface for 5 frames (sound 0x30 the first time).
 * - The bubbles: BUB1 at the background point, 16 px under the start y, behind the tiles.
 * Here: the background plane is plane 2 (a ScrollMap at 50%, as Flink's), its camera the background
 * scroll; the surface is g_WaterLevelY; the sounds are Lomax's SPLASH3 (Flink's 0x43 and 0x30). It
 * can't be hurt. Done, it is despawned (Actor_Despawn), so its Generator spawns it again when Lomax
 * comes back, as Flink's (the Generator respawns what is no longer out). While it attacks the actor
 * sits at its head (or its target) so that Lomax doesn't cull it while it is on screen. Draw order:
 * far segments in a bucket under the play plane's, near ones in FRONT_BUCKET, or once they reach into
 * the water under its waves and tint (Flink's water is a palette change under the surface line, which
 * tints the sprites there too); Sprite_QueueWorld draws
 * the first queued of a bucket in front, so the head (the nearest) goes first.
 */
#include "flink.h"

#define SNAKE_SEGS        11
#define SNAKE_DROP        30            /* the start y above the placement */
#define SNAKE_LIFE        300
#define SNAKE_EVERY       7
#define SNAKE_BUBBLES     12
#define SNAKE_SPLASH      5
#define SNAKE_ACCEL       0x800
#define SNAKE_END         700
#define SNAKE_NEAR        512           /* at the target's x; full size */
#define SNAKE_FRONT       256           /* in front of the tiles from here */
#define SNAKE_HURTS       400
#define SNAKE_SIZES       32
#define SNAKE_BUB_FRAMES  2             /* BUB1's and BUB2's (the converter checks) */
#define SNAKE_BUB1_SPEED  50
#define SNAKE_BUB2_SPEED  49
#define BG_PLANE          2
#define FRONT_BUCKET      0xA8          /* over the priority plane (0xA0) */
#define UNDER_WATER       13            /* the play plane + 13: under the waves (+ 14) and the tint (+ 15, water.c) */
#define HEAD_BOTTOM       31            /* the full-size sprites' bottom under the point (converter: */
#define BODY_BOTTOM       27            /*   (h + h * 125 / 100) / 2, the head 5 px higher) */

typedef struct {
    BYTE *type;
    int x0, y0, height, len;
    int wait, attacking, t, tx;
    int bub1, bub2, splashed, f1, f2;
    int v[SNAKE_SEGS], z[SNAKE_SEGS];   /* 16.16; v < 0: out */
} Snake;
static Snake g_snake[MAX_ACTORS];

void snakes_reset(void) { memset(g_snake, 0, sizeof(g_snake)); }

static Snake *snake_of(BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_snake[i].type && g_snake[i].type == A_TYPE(a) ? &g_snake[i] : NULL;
}

/* segment k's point (play plane, world), its size (0 = the biggest) and z (whole) */
static void seg_point(const Snake *s, int k, int *x, int *y, int *size, int *z)
{
    BYTE *pl = PLANE(1);
    int camx = P_CAMX(pl), zz = s->z[k] >> 16;
    int sx = s->x0 / 2 - P_CAMX(PLANE(BG_PLANE));
    sx += (s->tx - camx - sx) * zz / SNAKE_NEAR;
    *x = sx + camx;
    if (zz < SNAKE_NEAR) {
        *y = s->y0 - (G_SINTABLE[zz] * s->height >> 14);
        *size = SNAKE_SIZES - 1 - (zz >> 4);
    } else {
        *y = s->y0 + (zz - SNAKE_NEAR) * 2 / 3;
        *size = 0;
    }
    *z = zz;
}

static void snake_hurt(BYTE *a, int x1, int y1)
{
    const BYTE *pa = *G_PLAYERANIM;
    int px = *G_PLAYERX, py = *G_PLAYERY;
    if (!pa || A_PLANE(a) != *G_PLAYERPLANE || *G_PLAYERHURTTIMER) return;   /* (no log while he blinks) */
    if (x1 + 32 < px + HB_X1(pa) || x1 > px + HB_X2(pa) || y1 + 32 < py + HB_Y1(pa) || y1 > py + HB_Y2(pa)) return;
    flog("snake at (%d,%d) hurts Lomax", x1 + 16, y1 + 16);
    Player_TakeHit(x1 + 16, 0, 0);
}

static void __cdecl snake_update(BYTE *a)
{
    Snake *s = snake_of(a);
    int k, x, y, size, z, w = *G_WATERLEVELY, ax, ay, first = 1;
    if (!s) return;
    if (!s->attacking) {                                          /* 0x25254 */
        if (--s->wait >= 0) return;
        s->attacking = 1;
        s->tx = *G_PLAYERX;
        A_CULL(a) = CULL_NEVER;
        flink_sound(a, 0x43);
        flog("snake at (%d,%d) attacks x %d", s->x0, s->y0, s->tx);
    }
    if (s->t >= SNAKE_LIFE) {                                     /* 0x2527E */
        flog("snake at (%d,%d) done", s->x0, s->y0);
        s->type = NULL;
        Actor_Despawn(a);
        return;
    }
    if (s->t % SNAKE_EVERY == 0 && s->t / SNAKE_EVERY < s->len) {
        s->v[s->t / SNAKE_EVERY] = 0x10000;
        s->z[s->t / SNAKE_EVERY] = 0;
        s->bub1 = SNAKE_BUBBLES;
    }
    s->t++;
    if (s->bub1) { s->bub1--; s->f1 += SNAKE_BUB1_SPEED; }
    if (s->bub2) { s->bub2--; s->f2 += SNAKE_BUB2_SPEED; }
    ax = s->tx; ay = s->y0;
    for (k = 0; k < s->len; k++) {                                /* 0x25392 */
        if (s->v[k] < 0) continue;
        s->v[k] += SNAKE_ACCEL;
        s->z[k] += s->v[k];
        if (s->z[k] >> 16 > SNAKE_END) { s->v[k] = -1; continue; }
        seg_point(s, k, &x, &y, &size, &z);
        if (first) { ax = x; ay = y; first = 0; }
        if (z >= SNAKE_NEAR && y <= w && y >= w - 30) {          /* (0x2547C) */
            s->bub2 = SNAKE_SPLASH;
            if (!s->splashed) { s->splashed = 1; flink_sound(a, 0x30); }
        }
        if (z >= SNAKE_HURTS)
            snake_hurt(a, k ? x - 16 : x - (24 - 24 * (125 - 3 * size) / 100) / 2, k ? y + 8 : y + 3);
    }
    A_X(a) = ax; A_Y(a) = ay;                                     /* (kept on screen) */
    A_FX(a) = ax << 16; A_FY(a) = ay << 16;
}

/* frame f of a record (frames of entries, 0x80 ending one) at x, y in the play plane */
static void draw_frame(BYTE *a, const char *tag, int f, int x, int y, int bucket)
{
    const BYTE *e = record_of(a, FOURCC(tag)), *bank;
    if (!e) return;
    for (; f > 0; e += 8) if (e[6] & 0x80) f--;
    for (;; e += 8) {
        if ((bank = G_ANIMBANKS[e[0] | (T_BANKS(A_TYPE(a)) & 0x100)]))
            Sprite_QueueWorld(PLANE(1), bank + e[1] * 0x38 + 0x10, x + *(const short *)(e + 2),
                              y + *(const short *)(e + 4), (unsigned)bucket << 24 | (e[6] & 0x18), 20000);
        if (e[6] & 0x80) break;
    }
}

static void __cdecl snake_draw(BYTE *a)
{
    Snake *s = snake_of(a);
    int k, x, y, size, z, own, behind, under, w = *G_WATERLEVELY;
    if (!s || !s->attacking) return;
    own = Actor_GetDrawDepth(a) + 1;
    behind = P_DEPTH(PLANE(1)) - 1;                               /* (behind the play plane's tiles) */
    under = P_DEPTH(PLANE(1)) + UNDER_WATER;
    if (s->bub1)                                                  /* behind the tiles, at the background point */
        draw_frame(a, "BUB1", (s->f1 >> 8) % SNAKE_BUB_FRAMES,
                   s->x0 / 2 - P_CAMX(PLANE(BG_PLANE)) + P_CAMX(PLANE(1)), s->y0 + 16, behind);
    if (s->bub2) draw_frame(a, "BUB2", (s->f2 >> 8) % SNAKE_BUB_FRAMES, s->tx, *G_WATERLEVELY, own);
    for (k = 0; k < s->len; k++) {        /* the head first: in a bucket the first queued is in front */
        if (s->v[k] < 0) continue;
        seg_point(s, k, &x, &y, &size, &z);
        draw_frame(a, k ? "BODY" : "HEAD", size, x, y, z < SNAKE_FRONT ? behind :
                   z >= SNAKE_NEAR && y + (k ? BODY_BOTTOM : HEAD_BOTTOM) > w ? under : FRONT_BUCKET);
    }
}

/* (from spik_spawn) params: height, length, delay */
void snake_spawn(BYTE *a, BYTE *def)
{
    int i = slot_of(a), k;
    Snake *s;
    if (i < 0 || !def || T_NPARAMS(A_TYPE(a)) < 3) return;
    s = &g_snake[i];
    memset(s, 0, sizeof(*s));
    s->type = A_TYPE(a);
    s->x0 = A_X(a);
    s->y0 = A_Y(a) - SNAKE_DROP;
    s->height = D_PARAM(def, 0);
    s->len = D_PARAM(def, 1) > SNAKE_SEGS ? SNAKE_SEGS : D_PARAM(def, 1) < 0 ? 0 : D_PARAM(def, 1);
    s->wait = D_PARAM(def, 2);
    for (k = 0; k < SNAKE_SEGS; k++) s->v[k] = -1;
    A_FLAGS(a) &= ~(0x200000u | AF_CONTACT);                     /* touching the actor does nothing */
    A_UPDATE(a) = (void *)snake_update;
    A_DRAW(a) = (void *)snake_draw;
    flog("snake at (%d,%d): height %d, length %d, delay %d", s->x0, s->y0, s->height, s->len, s->wait);
}
