/*
 * balls.c - Flink's deadly swinging balls, 'Ball (swinging)' with checkbox 8 'deadly' (README
 * "Swinging balls"; 3-5's spike wheels and pendulum). Flink's (the same code as its Swingplatform):
 * - Init 0x2B3EA: the pivot (KNOB with checkbox 4 'knob') and 'arms' balls, arm k at angle
 *   k * 2048 / arms from the pivot's; with 0 arms one ball, a pendulum. Angles are 2048 a turn
 *   (byte offsets into a 1024-entry sine table, 0x1491E), 0 straight down; the pivot's angle is 16.16
 *   (+0x46) and starts at 0. Then the pivot's think runs 'pre-run' times.
 * - The pivot's think 0x2B5E8, each frame: a wheel turns at 'speed' a frame (with 'dont move' only
 *   while ridden, which a deadly one never is: it stands still). A pendulum starts at the bottom at
 *   |speed| and its speed changes by -1/4 a frame, the change reversing once |speed| reaches |speed|:
 *   it swings either side. Then, with 'winkel' set, if the angle is within |speed| of winkel * 2 the
 *   speed reverses and the angle steps back (a bounce, sound 0x18 when on screen).
 * - Each ball's think 0x2B6BA: at the pivot + r * (sin a, cos a), r = (length - 1) * 16, a = the
 *   pivot's angle + its arm's (& 0x7FE); deadly, it hurts Flink when its hitbox (BIGG's) meets his
 *   (0x1A462).
 * - Each ball's draw 0x2B752: its chain, a RING link every 16 px from the pivot out to the ball (16,
 *   32, .. r), then the ball. The pivot draws its KNOB.
 * - The pivot's draw 0x2B6B2 (Actor_DrawFrame): its KNOB with 'knob', else its RING (init 0x2B472
 *   sets RING on it first): a link at the pivot itself.
 * Here: the converter's 'Spik Ball' (Lomax's decoration, object 51), records MAIN (the knob, or
 * the pivot's link), BALL, RING; params radius, arms, speed, bounce angle (-1: none), pre-run, dont move,
 * Flink's draw list (its init's table 0x2B40C picks it by the spawn flags' bits 2-3; FLINK_LAYER). One
 * actor at the pivot does it all: the angle as Flink's, the balls' hurt test (Player_TakeHit), and
 * the drawing, one Actor_Draw per sprite with the actor moved onto it (so that Lomax+'s FPS Override,
 * which follows each Actor_Draw call's anchor, moves every piece smoothly): balls first, then the
 * knob, then the links (in a bucket the first queued is in front). Lomax's own cull would despawn it
 * by the pivot while a ball still shows, so it is never culled by the game and despawns itself once
 * the pivot is BALL_FAR px beyond the view plus its radius (its placement spawns it again, as
 * Flink's restarts out of its window). The bounce sound is not played (Flink's 0x18 has no known
 * Lomax match).
 */
#include "flink.h"

#define BALL_MAX_ARMS   8
#define BALL_ACCEL      0x4000           /* the pendulum's change of speed, 1/4 a frame */
#define BALL_RAMP       0x2000           /* 'dont move' (only reached when ridden: never here) */
#define BALL_FAR        96               /* despawned this far beyond the view and its radius */
#define BALL_VIEW_W     512              /* Lomax+'s widest view */
#define BALL_VIEW_H     240
#define LINK_STEP       16

typedef struct {
    BYTE *type;
    int r, arms, bounce, stay;
    int ang, v, acc, max;                /* 16.16; the angle's whole part 0..0x7FF */
} Ball;
static Ball g_ball[MAX_ACTORS];

void balls_reset(void) { memset(g_ball, 0, sizeof(g_ball)); }

static Ball *ball_of(const BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_ball[i].type && g_ball[i].type == A_TYPE(a) ? &g_ball[i] : NULL;
}

/* 0x2B5E8 */
static void ball_turn(Ball *b)
{
    int lo, hi, s = abs(b->max >> 16), an;
    if (b->acc) {                                    /* pendulum */
        b->v += b->acc;
        if (abs(b->v) >= b->max) b->acc = -b->acc;
    } else if (b->stay) {                            /* 'dont move', never ridden: slows to a stop */
        if (b->v) b->v -= b->max < 0 ? -BALL_RAMP : BALL_RAMP;
    } else
        b->v = b->max;
    b->ang = (b->ang + b->v) & 0x7FFFFFF;
    if (b->bounce < 0) return;
    lo = b->bounce - s; hi = b->bounce + s; an = b->ang >> 16;
    if (lo <= an && an <= hi) {
        b->v = -b->v;
        b->ang = (b->ang + b->v) & 0x7FFFFFF;
    }
}

/* ball k's point, at distance d (r: the ball) */
static void ball_point(const BYTE *a, const Ball *b, int k, int d, int *x, int *y)
{
    int an = ((b->ang >> 16) + (b->arms ? k * (0x800 / b->arms) : 0)) & 0x7FE;
    *x = A_X(a) + (d * G_SINTABLE[an >> 1] >> 14);
    *y = A_Y(a) + (d * G_SINTABLE[((an >> 1) + 256) & 1023] >> 14);
}

static int far_away(const BYTE *a, int r)
{
    const BYTE *pl = PLANE(A_PLANE(a));
    int x = A_X(a) - P_CAMX(pl), y = A_Y(a) - P_CAMY(pl), m = r + BALL_FAR;
    return x < -m || x > BALL_VIEW_W + m || y < -m || y > BALL_VIEW_H + m;
}

static void __cdecl ball_update(BYTE *a)
{
    Ball *b = ball_of(a);
    const BYTE *pa = *G_PLAYERANIM, *hb = record_of(a, FOURCC("BALL"));
    int k, n, x, y, px = *G_PLAYERX, py = *G_PLAYERY;
    if (!b) return;
    if (far_away(a, b->r)) { b->type = NULL; Actor_Despawn(a); return; }
    ball_turn(b);
    if (!pa || !hb || A_PLANE(a) != *G_PLAYERPLANE || *G_PLAYERHURTTIMER) return;
    hb -= 0x20;                                      /* the record (HB_*: its hitbox; record_of gives its entries) */
    n = b->arms ? b->arms : 1;
    for (k = 0; k < n; k++) {
        ball_point(a, b, k, b->r, &x, &y);
        if (x + HB_X2(hb) < px + HB_X1(pa) || x + HB_X1(hb) > px + HB_X2(pa) ||
            y + HB_Y2(hb) < py + HB_Y1(pa) || y + HB_Y1(hb) > py + HB_Y2(pa)) continue;
        flog("swinging ball at (%d,%d) hurts Lomax at (%d,%d)", x, y, px, py);
        Player_TakeHit(x, 0, 0);
        return;
    }
}

static void draw_at(BYTE *a, const char *tag, int x, int y)
{
    set_anim(a, tag);
    A_X(a) = x; A_Y(a) = y;
    draw_actor(a);
}

static void __cdecl ball_draw(BYTE *a)
{
    Ball *b = ball_of(a);
    int k, d, n, x0 = A_X(a), y0 = A_Y(a), x, y;
    if (!b) return;
    n = b->arms ? b->arms : 1;
    for (k = 0; k < n; k++) { ball_point(a, b, k, b->r, &x, &y); draw_at(a, "BALL", x, y); A_X(a) = x0; A_Y(a) = y0; }
    draw_at(a, "MAIN", x0, y0);
    for (k = 0; k < n; k++)
        for (d = LINK_STEP; d <= b->r; d += LINK_STEP) {
            ball_point(a, b, k, d, &x, &y);
            draw_at(a, "RING", x, y);
            A_X(a) = x0; A_Y(a) = y0;
        }
    A_X(a) = x0; A_Y(a) = y0;
}

/* (from spik_spawn) params: radius, arms, speed, bounce angle (-1: none), pre-run, dont move, draw list */
void ball_spawn(BYTE *a, BYTE *def)
{
    int i = slot_of(a), speed, k;
    Ball *b;
    if (i < 0 || !def || T_NPARAMS(A_TYPE(a)) < 6) return;
    b = &g_ball[i];
    memset(b, 0, sizeof(*b));
    b->type = A_TYPE(a);
    b->r = D_PARAM(def, 0);
    b->arms = D_PARAM(def, 1) < 0 ? 0 : D_PARAM(def, 1) > BALL_MAX_ARMS ? BALL_MAX_ARMS : D_PARAM(def, 1);
    speed = D_PARAM(def, 2);
    b->bounce = D_PARAM(def, 3);
    b->stay = D_PARAM(def, 5) & 1;
    b->max = speed << 16;
    if (!b->arms) {                                  /* 0x2B4C8: the pendulum starts at the bottom at full speed */
        b->max = b->v = abs(speed) << 16;
        b->acc = -BALL_ACCEL;
    }
    for (k = D_PARAM(def, 4); k > 0; k--) ball_turn(b);
    A_FLAGS(a) &= ~(0x200000u | AF_CONTACT);         /* the pivot itself touches nothing */
    A_LAYER(a) = (short)FLINK_LAYER(def_param(a, def, 6, 3));   /* 13-1's: in front of the Wall platforms */
    A_CULL(a) = CULL_NEVER;
    A_UPDATE(a) = (void *)ball_update;
    A_DRAW(a) = (void *)ball_draw;
    flog("swinging ball at (%d,%d): radius %d, arms %d, speed %d, bounce %d, pre-run %d%s", A_X(a), A_Y(a),
         b->r, b->arms, speed, b->bounce, D_PARAM(def, 4), b->stay ? ", dont move" : "");
}
