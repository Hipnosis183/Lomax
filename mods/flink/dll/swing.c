/*
 * swing.c - Flink's pendulums on Lomax's swing (object 46, Swin_Swing_Init 0x425780; README "Swings"):
 * the Trunk (the logs, init 0x263C6), the Swingplatform / Swing platform with 0 arms and the harmless
 * swinging balls (init 0x2B3EA). The converter makes each a 'Swin Trunk n' or 'Swin Flink n' with
 * Lomax's params (radius, speed * 8, strength 2, arms 0, pre-run) and three more: the bounce angle
 * (-1: none), the second chain's x offset (0: none) and the first link's distance from the pivot.
 * Flink's code, at a Flink frame per Lomax tick:
 * - The pivot's think 0x2B5E8 (pendulum part): the angle is 16.16, 2048 a turn, 0 straight down, and
 *   starts at 0; the speed starts at |speed| and changes by -1/4 (0x4000) a frame, the change
 *   reversing once |speed| reaches |speed|, so it swings to either side. Then, with 'winkel' set, an
 *   angle within |speed| of winkel * 2 reverses the speed and the angle steps back by it: a bounce
 *   (sound 0x18, an FM effect, when on screen; not played here, as in balls.c). The Trunks of 4-1
 *   bounce this way, one just past the bottom (996: the log swings to the right only, bouncing back
 *   off the ground), one 34 degrees to the right (96: it swings to the left only); 4-2's (0) don't.
 *   The init runs the think 'pre-run' times.
 * - The platform's think 0x2B6BA: at the pivot + r * (sin a, cos a), r = (length - 1) * 16 px, a =
 *   the angle & 0x7FE (a 1024-entry sine table at 0x1491E, the same as Lomax's), each product's
 *   whole part floored (muls, swap).
 * - The Trunk's draw 0x26510: a RING link every 16 px from the pivot out to the log (0, 16, .. r),
 *   each drawn twice, at the point and at the log's hitbox right - 10 px to its right: two parallel
 *   chains, the log hanging level from both. The Swingplatform's (0x2B752): one chain, 16, 32, .. r,
 *   and without 'knob' its pivot draws a RING too (0x2B6B2), so the converter gives it 0 (16 with it).
 * Here the update moves the platform as Lomax's swing does (Platform_MoveTo: Lomax carried) and keeps
 * Lomax's phase (+0x8C) in step; the draw queues the platform, then each link with one Actor_Draw,
 * the actor moved onto it (so that Lomax+'s FPS Override, which follows each Actor_Draw call's
 * anchor, moves every link smoothly; Lomax's Draw_RopeSegments queues them with the camera only),
 * so the platform is in front of its chains, as Lomax's swing draws them.
 */
#include "flink.h"

#define SWING_ACCEL     0x4000           /* the speed's change, 1/4 of 2048 a turn a frame */
#define SWING_LINK      16               /* px between links */
#define SW_RADIUS(a)    (*(int *)((BYTE *)(a) + 0x80))
#define SW_PHASE(a)     (*(int *)((BYTE *)(a) + 0x8C))   /* Lomax's angle: 16 units a step, 1024 steps a turn */
#define SW_RIDDEN(a)    (*(int *)((BYTE *)(a) + 0x94))
#define SW_PIVOTX(a)    (*(int *)((BYTE *)(a) + 0x9C))
#define SW_PIVOTY(a)    (*(int *)((BYTE *)(a) + 0xA0))

typedef struct {
    BYTE *type;
    int ang, v, acc, max;                /* 16.16; the angle's whole part 0..0x7FF */
    int bounce, chain2, first;
} Swing;
static Swing g_swing[MAX_ACTORS];

void swings_reset(void) { memset(g_swing, 0, sizeof(g_swing)); }

static Swing *swing_of(const BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_swing[i].type && g_swing[i].type == A_TYPE(a) ? &g_swing[i] : NULL;
}

/* 0x2B5E8 with a pendulum's speed change (+0x54 not 0) */
static void swing_turn(Swing *s)
{
    int lo, hi, an, l = s->max >> 16;
    s->v += s->acc;
    if (abs(s->v) >= s->max) s->acc = -s->acc;
    s->ang = (s->ang + s->v) & 0x7FFFFFF;
    if (s->bounce < 0) return;
    lo = s->bounce - l; hi = s->bounce + l; an = s->ang >> 16;
    if (lo <= an && an <= hi) {
        s->v = -s->v;
        s->ang = (s->ang + s->v) & 0x7FFFFFF;
    }
}

/* the point d px out from the pivot along the chain (0x2B6BA, 0x26510) */
static void swing_point(const BYTE *a, const Swing *s, int d, int *x, int *y)
{
    int i = ((s->ang >> 16) & 0x7FE) >> 1;
    *x = SW_PIVOTX(a) + (d * 4 * G_SINTABLE[i] >> 16);
    *y = SW_PIVOTY(a) + (d * 4 * G_SINTABLE[(i + 256) & 1023] >> 16);
}

static void __cdecl swing_update(BYTE *a)
{
    Swing *s = swing_of(a);
    int x, y;
    if (!s) { Swin_Swing_Update(a); return; }
    swing_turn(s);
    SW_PHASE(a) = s->ang >> 13;
    swing_point(a, s, SW_RADIUS(a), &x, &y);
    A_VX(a) = (x - A_X(a)) << 16;                    /* Platform_MoveTo 0x4134D0 */
    A_VY(a) = (y - A_Y(a)) << 16;
    SW_RIDDEN(a) = Platform_MoveAndCarryPlayer(a, Swin_Swing_GetSurfaceHeight) != 0;
}

static void link_at(BYTE *a, int x, int y)
{
    A_X(a) = x; A_Y(a) = y;
    draw_actor(a);
}

static void __cdecl swing_draw(BYTE *a)
{
    Swing *s = swing_of(a);
    int x0 = A_X(a), y0 = A_Y(a), d, x, y;
    if (!s) { Swin_Swing_Draw(a); return; }
    draw_actor(a);                                   /* the platform, in front of its chains */
    set_anim(a, "RING");
    for (d = s->first; d <= SW_RADIUS(a); d += SWING_LINK) {
        swing_point(a, s, d, &x, &y);
        link_at(a, x, y);
        if (s->chain2) link_at(a, x + s->chain2, y);
    }
    A_X(a) = x0; A_Y(a) = y0;
    set_anim(a, "TREE");                             /* its standing surface (Platform_MoveAndCarryPlayer) */
}

/* (Swin_Swing_Init's wrapper) params: radius, speed * 8, strength, arms, pre-run, bounce angle (-1:
   none), second chain's x offset (0: none), first link's distance */
void __cdecl swin_spawn(BYTE *a, BYTE *def)
{
    int i, k, x, y;
    Swing *s;
    Swin_Swing_Init(a, def);
    if (!g_active || !def || !is_type(a, "Swin ") || T_NPARAMS(A_TYPE(a)) < 8 || (i = slot_of(a)) < 0) return;
    s = &g_swing[i];
    memset(s, 0, sizeof(*s));
    s->type = A_TYPE(a);
    s->max = s->v = (abs(D_PARAM(def, 1)) / 8) << 16;  /* 0x2646C / 0x2B4C8: at the bottom at |speed| */
    s->acc = -SWING_ACCEL;
    s->bounce = D_PARAM(def, 5);
    s->chain2 = D_PARAM(def, 6);
    s->first = D_PARAM(def, 7) < 0 ? 0 : D_PARAM(def, 7);
    for (k = D_PARAM(def, 4); k > 0; k--) swing_turn(s);
    SW_PHASE(a) = s->ang >> 13;
    swing_point(a, s, SW_RADIUS(a), &x, &y);
    A_X(a) = x; A_Y(a) = y; A_FX(a) = x << 16; A_FY(a) = y << 16;
    A_UPDATE(a) = (void *)swing_update;
    A_DRAW(a) = (void *)swing_draw;
    flog("swing at (%d,%d): radius %d, speed %d, bounce %d, pre-run %d, chains %s", SW_PIVOTX(a), SW_PIVOTY(a),
         SW_RADIUS(a), s->max >> 16, s->bounce, D_PARAM(def, 4), s->chain2 ? "two" : "one");
}
