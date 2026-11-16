/*
 * wurm.c - Flink's Raketen wurm, 'Raketen wurm' (init 0x25AA6, think 0x25B94 / 0x25D36 / 0x25C74, draw
 * 0x26016 / 0x25DE8, death 0x26246), FOREST (4-1, 4-2, 8-1) and TREE (27-1) (README "Enemies"). The
 * converter makes it a 'Muta Wurm n' type with Flink's HEAD and TAIL (3 frames each: level, sloped,
 * rising; frames 3-5 are their mirror images and unused), its MUD and the mud flipped upside down
 * (MUDV), and MAIN.WLD's clods ERDE; params: 2 = 'head-over-heals' (it hangs from a ceiling),
 * 'length', 'height'. Flink's behaviour, the arching Raketen wurm ('height' 0: 4-1, 27-1):
 * - Waiting in its mud (MUD, frame flipping every 8 frames): it faces Flink, and when Flink's x is
 *   60 to 120 px from its own (any height) it comes out (speed 8, 120 frames of rest set for after).
 * - Out (0x25D36): its path position p grows by 8 a frame up to length * 8, then shrinks back to
 *   below 0, when it is in its mud again (the 120 frames of rest count down there). At p = 240 on
 *   the way out it throws clods (Fx_SpawnDebris 0x2E0BC; hanging, 0x2E0C8 throws them down).
 * - The path (0x26016), from the sprite's top-left at (x + 48, y + 16), facing right: s < 512 is a
 *   quarter circle of radius 64 (Flink's sine table 0x1491E, 8.8 words, 64 a quarter at s 512):
 *   (-64 cos, -64 sin), sprite frame 2 below s 250, 1 below 350, 0 above; then level, s - 512 / 5 px
 *   on and 67 px up, wiggling 0-4 px. Facing left it is mirrored around x - 68 and the sprites
 *   flipped; hanging, around y - 32 and flipped upside down. The head is at s = p, the body segments
 *   (TAIL) every 100 back to the mud, drawn behind the mud and in front of the head (Flink's sprite
 *   order). 100 path units are about 20 px.
 * The vertical Raketen wurm ('height' h not 0: 4-2, 8-1; think 0x25C74, draw 0x25DE8):
 * - It comes out with Flink 40 to 80 px away, at speed 2, facing right, and its p grows until
 *   p > length + h + 128: it has gone into a second hole h px above its own (below, hanging) and
 *   waits there (it comes out of its own hole again). Clods at p = 50.
 * - Its head and length / 16 segments (all frame 2, the rising sprite) are 16 px apart in a column at
 *   x - 12, wiggling 0-4 px with their y (|(y & 63) - 32| / 8; Flink's screen y, here the world's),
 *   flipped left-right where (y & 63) < 32: the head's top at y + 40 - p - 16 * (length / 16) +
 *   length (hanging: y - 56 + p - length + 16 * (length / 16)), the segments behind it. Only those
 *   between the holes are drawn and touch (top from the far hole - 24 to its own). Its own hole's mud
 *   shows while p < length + 64, the far one's once p > h + 64 (hanging, h + 48).
 * Both:
 * - The head is stompable (Actor_CheckStompedByPlayer 0x1A352 with its centre, sound 0x18): Flink
 *   bounces (g_PlayerBounceReq) and the Raketen wurm goes back into its mud at once (speed negated); while it
 *   does, nothing of it hurts or can be stomped. Touching the head or a segment hurts (0x1A46E, the
 *   sprite's 16 x 16 middle; Flink checks one segment in four a frame, here all).
 * - Flink's spell or slide (+3 bit 6) kills it (0x26246): sound 0x2A, a Spirit at the head, and the
 *   head and segments shown fly apart (each 0..1 px sideways, 1..5 px up, falling 0.25 px a frame)
 *   and the mud goes.
 * Lomax's way: the head is the actor (HEAD's hitbox, flagged hurts + hittable
 * while it shows), so his stomp bounces off it as off an enemy (Player_BounceOffEnemy) and it goes
 * back; his spin, roll or thrown helmet on the head send it back too (enemy_attacked); only a thrown
 * object kills it, touching the head or any segment shown (wurm_struck, from grab.c's strike), even
 * while it goes back. Dead, it counts as defeated, may drop a coin, and stays dead for the level
 * visit. While the head doesn't show the actor sits at its mud and touches nothing.
 * In a map with a Steer platform (27-1) it stays in its mud until Lomax has stood on the platform
 * since the last start (steer_waiting, steer.c: Lomax's view shows the shaft under the ledge, where
 * Flink's screen doesn't).
 * Drawing: while it waits nothing is drawn, its mud neither (Flink's waiting think ends in 0x3333E,
 * which leaves the actor off the draw list), so the mud shows only while it is out. Going back after
 * a stomp or a hit it blinks (not drawn every 4th frame), as the Ingredient Holder after its first hit
 * (Flink's doesn't blink).
 */
#include "enemy.h"

#define WURM_UPSIDE      2              /* (the converter's param 0) */
#define WURM_STEP        100            /* between the arching Raketen wurm's segments */
#define WURM_ARC         512            /* the quarter circle's end */
#define WURM_LEVEL_DY    67
#define WURM_REST        120            /* frames in its mud before it can come out again */
#define WURM_SPR         24             /* the sprites' size */
#define WURM_HURT_IN     4              /* the hurting 16 x 16 inside it */
#define WURM_SEGS        16             /* length 156 (the longest) has 13 */
#define WURM_GRAVITY     0x4000
#define WURM_DEAD_TICKS  150
#define WURM_MUD_TICKS   8
#define VIEW_H           240
/* the arching Raketen wurm / the vertical one: speed, Flink's distance to come out, clods at */
#define ARC_SPEED        8
#define ARC_NEAR         60
#define ARC_FAR          120
#define ARC_CLODS        240
#define VERT_SPEED       2
#define VERT_NEAR        40
#define VERT_FAR         80
#define VERT_CLODS       50
#define VERT_STEP        16

typedef struct {
    BYTE *type;
    int hx, hy, up, left, len, h;       /* its mud, hanging, facing left, length, height (0: arching) */
    int p, v, rest, back, dying, t;     /* path position, speed (0: waiting), rest, going back; t: ticks */
    int n, x[WURM_SEGS], y[WURM_SEGS], vx[WURM_SEGS], vy[WURM_SEGS];   /* dying: the pieces (16.16) */
} Wurm;
static Wurm g_wurm[MAX_ACTORS];
static unsigned g_seed = 0x2545F491u;

/* a segment shown this tick: its sprite's top-left, frame, flips, and whether it is the head */
typedef struct { int x, y, f, head; unsigned fl; } Seg;

void wurms_reset(void) { memset(g_wurm, 0, sizeof(g_wurm)); }

static Wurm *wurm_of(const BYTE *a)
{
    int i = a ? slot_of(a) : -1;
    return i >= 0 && g_wurm[i].type && g_wurm[i].type == A_TYPE(a) ? &g_wurm[i] : NULL;
}

static int rnd(void) { g_seed = g_seed * 1103515245u + 12345u; return (int)(g_seed >> 16 & 0x7FFF); }

static unsigned flips(const Wurm *w) { return (w->left ? 0x08u : 0) | (w->up ? 0x10u : 0); }

/* the arching Raketen wurm's path position s -> the sprite's top-left in the world and its frame (0x260CA) */
static void path_at(const Wurm *w, int s, int *x, int *y, int *f)
{
    int dx, dy;
    if (s < WURM_ARC) {
        dy = -(G_SINTABLE[s >> 1] >> 8);                         /* -64 sin */
        dx = -(G_SINTABLE[((s + WURM_ARC) & 0x7FF) >> 1] >> 8);  /* -64 cos */
        *f = s > 350 ? 0 : s > 250 ? 1 : 2;
    } else {
        int t = s - WURM_ARC, wig = (t >> 3 & 31) - 16;
        dx = t / 5;
        dy = (abs(wig) >> 2) - WURM_LEVEL_DY;
        *f = 0;
    }
    *x = w->left ? w->hx - 68 - dx : w->hx + 48 + dx;
    *y = w->up ? w->hy - 32 - dy : w->hy + 16 + dy;
}

/* the segments Flink draws this frame, the head first (when it shows) */
static int seg_list(const Wurm *w, Seg *sg)
{
    int n = 0, s, k, cnt, head, lo, hi;
    if (w->p < 0) return 0;
    if (!w->h) {                                                 /* 0x26016 */
        for (s = w->p; s >= 0 && n < WURM_SEGS; s -= WURM_STEP, n++) {
            path_at(w, s, &sg[n].x, &sg[n].y, &sg[n].f);
            sg[n].head = n == 0;
            sg[n].fl = flips(w);
        }
        return n;
    }
    cnt = w->len / VERT_STEP + 1;                                 /* 0x25DE8 */
    if (w->up) {
        head = w->hy + w->p - 56 - w->len + VERT_STEP * (cnt - 1);
        lo = w->hy - WURM_SPR; hi = w->hy + w->h;
    } else {
        head = w->hy + w->len + 40 - w->p - VERT_STEP * (cnt - 1);
        lo = w->hy - w->h - WURM_SPR; hi = w->hy;
    }
    for (k = 0; k < cnt && n < WURM_SEGS; k++) {
        int y = w->up ? head - VERT_STEP * k : head + VERT_STEP * k, ph = (y & 63) - 32;
        if (y < lo || y > hi) continue;
        sg[n].x = w->hx - WURM_SPR / 2 + (abs(ph) >> 3);
        sg[n].y = y;
        sg[n].f = 2;
        sg[n].head = k == 0;
        sg[n].fl = (ph < 0 ? 0x08u : 0) | (w->up ? 0x10u : 0);
        n++;
    }
    return n;
}

static int overlap(int ax1, int ay1, int ax2, int ay2, int bx1, int by1, int bx2, int by2)
{
    return ax1 < bx2 && bx1 < ax2 && ay1 < by2 && by1 < ay2;
}

static int player_box(int *x1, int *y1, int *x2, int *y2)
{
    const BYTE *pa = *G_PLAYERANIM;
    if (!pa) return 0;
    *x1 = *G_PLAYERX + HB_X1(pa); *x2 = *G_PLAYERX + HB_X2(pa);
    *y1 = *G_PLAYERY + HB_Y1(pa); *y2 = *G_PLAYERY + HB_Y2(pa);
    return 1;
}

/* the actor: at the head's sprite's bottom middle (HEAD's hitbox is from there) */
static void put_at(BYTE *a, int x, int y)
{
    A_X(a) = x + WURM_SPR / 2; A_Y(a) = y + WURM_SPR;
    A_FX(a) = A_X(a) << 16; A_FY(a) = A_Y(a) << 16;
}

/* at its mud, touching nothing */
static void rest_at_home(BYTE *a, const Wurm *w)
{
    put_at(a, w->hx - WURM_SPR / 2, w->hy - WURM_SPR);
    A_FLAGS(a) &= ~AF_CONTACT;
}

/* back into its mud, harmless meanwhile (a stomp or an attack: 0x25F90) */
static void go_back(BYTE *a, Wurm *w)
{
    w->back = 1;
    if (w->v > 0) w->v = -w->v;
    A_FLAGS(a) &= ~AF_CONTACT;
}

/* the body hurts (0x25FA6); the head is the actor and hurts through Lomax's contact loop */
static void body_hurt(BYTE *a, Wurm *w, const Seg *sg, int n)
{
    int px1, py1, px2, py2, hx1, hy1, hx2, hy2, k;
    if (w->back || A_PLANE(a) != *G_PLAYERPLANE || *G_PLAYERHURTTIMER || *G_PLAYERDEATHTIMER) return;
    if (!player_box(&px1, &py1, &px2, &py2)) return;
    /* falling onto the head: the stomp wins over the segment behind it */
    if (*G_PLAYERVY > 0 && A_FLAGS(a) & AF_HITTABLE && actor_box(a, &hx1, &hy1, &hx2, &hy2) &&
        overlap(px1, py1, px2, py2, hx1, hy1, hx2, hy2)) return;
    for (k = 0; k < n; k++) {
        if (sg[k].head) continue;
        if (!overlap(px1, py1, px2, py2, sg[k].x + WURM_HURT_IN, sg[k].y + WURM_HURT_IN,
                     sg[k].x + WURM_SPR - WURM_HURT_IN, sg[k].y + WURM_SPR - WURM_HURT_IN)) continue;
        flog("raketen wurm at (%d,%d) hurts Lomax with a segment", w->hx, w->hy);
        Player_TakeHit(sg[k].x + WURM_SPR / 2, 0, 0);
        return;
    }
}

static void __cdecl wurm_update(BYTE *a)
{
    int i = enemy_slot(a), k, n;
    Wurm *w = wurm_of(a);
    Seg sg[WURM_SEGS];
    if (i < 0 || !w) return;
    if (w->dying) {                                              /* the pieces fly (0x26306) */
        int low = 1, bottom = P_CAMY(PLANE(A_PLANE(a))) + VIEW_H + WURM_SPR;
        for (k = 0; k < w->n; k++) {
            w->x[k] += w->vx[k]; w->y[k] += w->vy[k]; w->vy[k] += WURM_GRAVITY;
            if (w->y[k] >> 16 < bottom) low = 0;
        }
        if (low || ++w->t > WURM_DEAD_TICKS) { w->type = NULL; g_enemy[i].type = NULL; Actor_Kill(a); }
        return;
    }
    w->t++;                                                      /* (the mud's frame) */
    if (!w->v) {                                                 /* waiting (0x25B94) */
        int d = w->hx - *G_PLAYERX;
        rest_at_home(a, w);
        if (w->rest > 0) { w->rest--; return; }
        if (steer_waiting()) return;                             /* (27-1: until the leaf goes, steer.c) */
        if (!w->h) w->left = d > 0;                             /* (a vertical one: always right) */
        if (d < 0) d = -d;
        if (d > (w->h ? VERT_FAR : ARC_FAR) || d < (w->h ? VERT_NEAR : ARC_NEAR)) return;
        w->v = w->h ? VERT_SPEED : ARC_SPEED; w->p = 0; w->back = 0;
        w->rest = WURM_REST;
        if (w->left) A_FLAGS(a) |= AF_LEFT; else A_FLAGS(a) &= ~AF_LEFT;
        flog("raketen wurm at (%d,%d) comes out%s", w->hx, w->hy, w->h ? " (vertical)" : w->left ? " left" : " right");
    } else if (!w->back && enemy_attacked(a, i)) {              /* spin, roll or helmet: back */
        Sound_PlayEffect(a, SND_HIT);
        flog("raketen wurm at (%d,%d) hit, goes back", w->hx, w->hy);
        go_back(a, w);
    }
    if (w->p == (w->h ? VERT_CLODS : ARC_CLODS) && w->v > 0) {     /* 0x25D46 / 0x25C84 */
        if (!w->up) ground_clods(a, w->hx, w->hy);
        flink_sound(a, 0x49);
    }
    w->p += w->v;
    if (w->p < 0) { w->v = 0; w->p = -1; rest_at_home(a, w); return; }   /* in its mud again */
    if (!w->h) {
        if (w->p > w->len * 8 && w->v > 0) w->v = -w->v;
    } else if (w->p > w->len + w->h + 128) {                     /* gone into the far hole */
        flog("raketen wurm at (%d,%d) gone into the far hole", w->hx, w->hy);
        w->v = 0;
        rest_at_home(a, w);
        return;
    }
    n = seg_list(w, sg);
    if (n && sg[0].head) {
        put_at(a, sg[0].x, sg[0].y);
        if (!w->back) A_FLAGS(a) |= AF_HURTS | AF_HITTABLE;
    } else rest_at_home(a, w);
    body_hurt(a, w, sg, n);
}

/* frame f of a record, its sprites' top-left at x, y (offsets ignored: Flink's Sprite_DrawScreen) or
   with them (offs) */
static void draw_frame(BYTE *a, const char *tag, int f, int x, int y, unsigned fl, int offs, int bucket)
{
    const BYTE *e = record_of(a, FOURCC(tag)), *bank;
    if (!e) return;
    for (; f > 0; e += 8) if (e[6] & 0x80) f--;
    for (;; e += 8) {
        if ((bank = G_ANIMBANKS[e[0] | (T_BANKS(A_TYPE(a)) & 0x100)]))
            Sprite_QueueWorld(PLANE(A_PLANE(a)), bank + e[1] * 0x38 + 0x10,
                              x + (offs ? *(const short *)(e + 2) : 0), y + (offs ? *(const short *)(e + 4) : 0),
                              (unsigned)bucket << 24 | ((e[6] & 0x18) ^ fl), 20000);
        if (e[6] & 0x80) break;
    }
}

/* in a bucket the first queued is in front: the mud, then the segments from the tail to the head */
static void __cdecl wurm_draw(BYTE *a)
{
    Wurm *w = wurm_of(a);
    Seg sg[WURM_SEGS];
    int bucket = Actor_GetDrawDepth(a) + 1, k, n, mf;
    const char *mud;
    if (!w || A_FLAGS(a) & AF_HIDDEN) return;
    if (w->dying) {
        for (k = 0; k < w->n; k++)
            draw_frame(a, k ? "TAIL" : "HEAD", w->h ? 2 : 0, w->x[k] >> 16, w->y[k] >> 16, flips(w), 0, bucket);
        return;
    }
    if (!w->v) return;                     /* waiting: Flink's think skips the draw list (0x3333E) */
    if (w->back && !(w->t & 3)) return;    /* going back after a hit: blinks, as the Ingredient Holder */
    mud = w->up ? "MUDV" : "MUD ";
    mf = (w->t / WURM_MUD_TICKS) & 1;
    if (!w->h || w->p < w->len + 64) draw_frame(a, mud, mf, w->hx, w->hy, 0, 1, bucket);
    if (w->h && w->p > w->h + (w->up ? 48 : 64))                  /* the far hole */
        draw_frame(a, mud, mf, w->hx, w->up ? w->hy + w->h : w->hy - w->h, 0, 1, bucket);
    n = seg_list(w, sg);
    for (k = n - 1; k >= 0; k--)
        draw_frame(a, sg[k].head ? "HEAD" : "TAIL", sg[k].f, sg[k].x, sg[k].y, sg[k].fl, 0, bucket);
}

/* killed (0x26246): the segments shown fly apart, the mud goes */
static void wurm_die(BYTE *a, int i, Wurm *w)
{
    Seg sg[WURM_SEGS];
    int k, n = seg_list(w, sg);
    BYTE *coin;
    for (k = 0; k < n; k++) {
        w->x[k] = sg[k].x << 16; w->y[k] = sg[k].y << 16;
        w->vx[k] = ((rnd() & 15) - 7) << 13;
        w->vy[k] = -(((rnd() & 31) << 13) + 0x10000);
    }
    w->n = n;
    w->dying = 1; w->t = 0;
    g_enemy[i].dead = 1;
    forget_placement(A_DEF(a));                                  /* stays dead (gone.c) */
    A_FLAGS(a) &= ~AF_CONTACT;
    A_CULL(a) = CULL_NEVER;
    flink_sound(a, 0x2A);
    coin = enemy_defeated(a);
    flog("raketen wurm at (%d,%d) killed%s", w->hx, w->hy, coin ? ", drops a coin" : "");
}

/* Lomax bounced off its head (contact.c through enemy_bounce) */
void wurm_stomped(BYTE *a)
{
    Wurm *w = wurm_of(a);
    if (!w || !w->v || w->back || w->dying) return;
    flog("raketen wurm at (%d,%d) stomped, goes back", w->hx, w->hy);
    go_back(a, w);
}

/* a thrown object's box (grab.c's strike) touching a Raketen wurm out of its mud, its head or a segment
   shown, kills it: 1 (it was a Raketen wurm and is hit) */
int wurm_struck(BYTE *t, int x1, int y1, int x2, int y2)
{
    Wurm *w = wurm_of(t);
    Seg sg[WURM_SEGS];
    int i = slot_of(t), k, n;
    if (!w || i < 0 || !w->v || w->dying || g_enemy[i].type != A_TYPE(t)) return 0;
    n = seg_list(w, sg);
    for (k = 0; k < n; k++) {
        if (!overlap(x1, y1, x2, y2, sg[k].x, sg[k].y, sg[k].x + WURM_SPR, sg[k].y + WURM_SPR)) continue;
        flog("raketen wurm at (%d,%d) hit by a thrown object", w->hx, w->hy);
        wurm_die(t, i, w);
        return 1;
    }
    return 0;
}

/* (from enemy_spawn) params: 2 = hanging, length, height (0: the arching Raketen wurm) */
void wurm_spawn(BYTE *a, int i, const BYTE *def)
{
    Wurm *w = &g_wurm[i];
    memset(w, 0, sizeof(*w));
    w->type = A_TYPE(a);
    w->hx = A_X(a); w->hy = A_Y(a);
    w->up = (def_param(a, def, 0, 0) & WURM_UPSIDE) != 0;
    w->len = (WORD)def_param(a, def, 1, 0);
    w->h = (WORD)def_param(a, def, 2, 0);
    w->p = -1;
    g_enemy[i].wurm = 1;
    A_FLAGS(a) &= ~AF_LEFT;
    A_UPDATE(a) = (void *)wurm_update;
    A_DRAW(a) = (void *)wurm_draw;
    Actor_SetAnim(a, FOURCC("HEAD"));
    rest_at_home(a, w);                                          /* in its mud it touches nothing */
}
