/*
 * wheel.c - Flink's wheels, the Swingplatform / Swing platform with arms > 0 (init 0x2B3EA), on Lomax's
 * wheel of platforms (object 21, Rota_RotatingPlatforms_Init 0x41F180; README "Swings", "Wheels"):
 * 16-1's chained wheels and META's cloud wheels, whose clouds also come in toward the pivot and go back
 * out ('stretch'). The converter makes each a 'Rota Flink n' with Lomax's four params (radius, arms,
 * speed / 2, phase: Lomax's own wheel, without FLINK.DLL) and Flink's: the speed * 8, the first link's
 * distance from the pivot, the speed, 'pre-run', the bounce angle, 'stretch-factor' and the stretch
 * checkboxes. Lomax's init allocates the platforms (+0xF4, one actor each, Rota_Platform_Update
 * 0x41F460: Platform_MoveTo the target +0x9C / +0xA0, 16.16, carrying Lomax; +0x94 ridden). Here the
 * wheel's update is Flink's, at a Flink frame per Lomax tick:
 * - The pivot's think 0x2B5E8 (a wheel: +0x54 is 0): the angle is 16.16, 2048 a turn, its whole part
 *   kept to 0..0x7FF. Without 'dont move' it goes on by 'speed' (+0x4C, speed << 16) a frame. With it
 *   the speed (+0x50) starts at 0 and, while a platform is ridden (+0x5B, set by the platforms' draw,
 *   0x2B772, cleared at the think's end; here Lomax's +0x94), goes by 0x2000 a frame toward 'speed',
 *   otherwise back toward 0. Then an angle within |speed| of the bounce angle (+0x58: 'winkel' * 2, or
 *   4000 for 0, never reached) reverses the speed and steps the angle back (sound 0x18, an FM effect:
 *   not played, as in swing.c); no converted wheel has a 'winkel'. The init runs the think 'pre-run'
 *   times (0x2B5A6), so 16-1's four wheels start a quarter turn apart.
 * - Each platform's think 0x2B6BA: with 'stretch' (checkbox 16) its count +0x4E goes by +0x50 (1 or
 *   -1) a frame, turning at 0 and at 'stretch-factor' (+0x52); its distance from the pivot is
 *   (length - 1) * 64 - (count >> 1) * (length - 1) quarter px (+0x54 is length - 1), so the platform
 *   comes in to (64 - factor / 2) / 64 of the radius and goes back out every 2 * factor frames (29-1's:
 *   64 px to 24 px and back in 160 frames). Its angle is the pivot's + arm k * 2048 / arms, & 0x7FE; it
 *   is at the pivot + d * (sin, cos) / 65536 (each product floored: muls, swap), through Platform_MoveTo
 *   here (Flink's 0x1A272). The count starts at 0 counting up; with checkbox 32 arm k's starts at
 *   k * (factor * 2 / arms) and, from 'stretch-factor' up, at 'stretch-factor' counting down (0x2B552);
 *   no converted wheel has checkbox 32, so every arm comes in and goes out together.
 * - The chains: Lomax's DrawSpokes draws them with Draw_RopeSegments (0x425D40), which centres the raw
 *   sprite by its size, and Flink's ring frame is 48 px wide: every chain would be 16 px left of its
 *   pivot. Flink's draw 0x2B752 (per platform) puts link k (1 .. length - 1) at k * (64 - (count >> 1))
 *   quarter px from the pivot, so the links close up with the platform; without 'knob' the pivot draws a
 *   RING itself (0x2B6B2). Here each link is one Actor_Draw of the converter's RING record (ring_record:
 *   anchored as Flink's) with the actor moved onto it, so that Lomax+'s FPS Override moves each link
 *   smoothly, as swing.c does.
 * Lomax's top speed / speed / phase (+0x84 / +0x88 / +0x8C) are kept in step for anything reading them.
 */
#include "flink.h"

#define WHEEL_MAX_ARMS  8
#define WHEEL_RAMP      0x2000           /* 'dont move': the speed's change a frame, 16.16 */
#define RO_RADIUS(a)    (*(int *)((BYTE *)(a) + 0x80))
#define RO_TOPSPEED(a)  (*(int *)((BYTE *)(a) + 0x84))
#define RO_SPEED(a)     (*(int *)((BYTE *)(a) + 0x88))
#define RO_PHASE(a)     (*(int *)((BYTE *)(a) + 0x8C))   /* 16 units a step, 1024 steps a turn */
#define RO_ARMS(a)      (*(int *)((BYTE *)(a) + 0x98))
#define RO_PIVOTX(a)    (*(int *)((BYTE *)(a) + 0x9C))
#define RO_PIVOTY(a)    (*(int *)((BYTE *)(a) + 0xA0))
#define RO_PLATFORM(a, k) (((BYTE **)((BYTE *)(a) + 0xF4))[k])
#define RP_TX(p)        (*(int *)((BYTE *)(p) + 0x9C))   /* the platform's target, 16.16 */
#define RP_TY(p)        (*(int *)((BYTE *)(p) + 0xA0))
#define RP_RIDDEN(p)    (*(int *)((BYTE *)(p) + 0x94))

typedef struct {
    BYTE *type;
    unsigned ang;                        /* 16.16, whole part 0..0x7FF (+0x46) */
    int v, top, ramp, bounce, knob;      /* +0x50, +0x4C, +0x5A, +0x58 */
    int max, mul;                        /* the platforms' +0x52 (stretch-factor) and +0x54 (length - 1) */
    int off[WHEEL_MAX_ARMS], cnt[WHEEL_MAX_ARMS], dir[WHEEL_MAX_ARMS];   /* +0x48, +0x4E, +0x50 */
} Wheel;
static Wheel g_wheel[MAX_ACTORS];

void wheels_reset(void) { memset(g_wheel, 0, sizeof(g_wheel)); }

static Wheel *wheel_of(const BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_wheel[i].type && g_wheel[i].type == A_TYPE(a) ? &g_wheel[i] : NULL;
}

static void wheel_add(Wheel *w, int d)
{
    unsigned n = w->ang + (unsigned)d;
    w->ang = (n & 0xFFFF) | (n & 0x7FF0000);
}

/* 0x2B5E8, a wheel's part */
static void wheel_turn(Wheel *w, int ridden)
{
    int d, lo, hi, an;
    if (!w->ramp) d = w->top;
    else {
        int step = w->top < 0 ? -WHEEL_RAMP : WHEEL_RAMP;
        d = w->v;
        if (!ridden) { if (d) d -= step; }
        else if (d != w->top) d += step;
        w->v = d;
    }
    wheel_add(w, d);
    an = (short)(w->ang >> 16);
    lo = w->bounce - abs(w->top >> 16); hi = w->bounce + abs(w->top >> 16);
    if (lo <= an && an <= hi) {
        w->v = -w->v;
        wheel_add(w, w->v);
    }
}

/* platform k's angle as Lomax's sine index (0x2B6EE) and its quarter px from the pivot per link */
static int arm_index(const Wheel *w, int k) { return ((((w->ang >> 16) + w->off[k]) & 0x7FE) >> 1); }
static int arm_step(const Wheel *w, int k) { return 0x40 - ((unsigned)(w->cnt[k] & 0xFFFF) >> 1); }

static void arm_point(const BYTE *a, int i, int q, int *x, int *y)
{
    *x = RO_PIVOTX(a) + (q * G_SINTABLE[i] >> 16);
    *y = RO_PIVOTY(a) + (q * G_SINTABLE[(i + 256) & 1023] >> 16);
}

static void __cdecl wheel_update(BYTE *a)
{
    Wheel *w = wheel_of(a);
    int k, ridden = 0, x, y, arms = RO_ARMS(a);
    if (!w) { Rota_RotatingPlatforms_Update(a); return; }
    for (k = 0; k < arms; k++) if (RP_RIDDEN(RO_PLATFORM(a, k))) ridden = 1;
    wheel_turn(w, ridden);
    RO_PHASE(a) = (int)(w->ang >> 13);
    RO_SPEED(a) = (w->ramp ? w->v : w->top) >> 13;
    for (k = 0; k < arms; k++) {                     /* 0x2B6BA */
        BYTE *p = RO_PLATFORM(a, k);
        w->cnt[k] = (short)(w->cnt[k] + w->dir[k]);
        if (w->cnt[k] == 0 || w->cnt[k] == w->max) w->dir[k] = -w->dir[k];
        arm_point(a, arm_index(w, k), RO_RADIUS(a) * 4 - ((unsigned)(w->cnt[k] & 0xFFFF) >> 1) * w->mul, &x, &y);
        RP_TX(p) = x << 16; RP_TY(p) = y << 16;
    }
}

static void __cdecl wheel_draw(BYTE *a)
{
    Wheel *w = wheel_of(a);
    int x0 = A_X(a), y0 = A_Y(a), k, n, x, y, arms = RO_ARMS(a);
    if (!w) { Rota_RotatingPlatforms_DrawSpokes(a); return; }
    if (!w->knob) draw_actor(a);                     /* the pivot's RING (0x2B6B2) */
    for (k = 0; k < arms; k++)                       /* 0x2B752 */
        for (n = 1; n * 16 <= RO_RADIUS(a); n++) {
            arm_point(a, arm_index(w, k), n * arm_step(w, k), &x, &y);
            A_X(a) = x; A_Y(a) = y;
            draw_actor(a);
        }
    A_X(a) = x0; A_Y(a) = y0;
}

/* (Rota_RotatingPlatforms_Init's wrapper) params: radius, arms, speed / 2, phase, speed * 8, first
   link, speed, pre-run, bounce angle, stretch-factor (0: none), stretch flags (1 stretch, 2 staggered) */
void __cdecl rota_spawn(BYTE *a, BYTE *def)
{
    int i, k, arms, d4 = 0, stagger;
    Wheel *w;
    Rota_RotatingPlatforms_Init(a, def);
    if (!g_active || !def || !is_type(a, "Rota ") || T_NPARAMS(A_TYPE(a)) < 11 || (i = slot_of(a)) < 0) return;
    if (A_UPDATE(a) != (void *)Rota_RotatingPlatforms_Update) return;   /* the init failed and despawned it */
    if ((arms = RO_ARMS(a)) < 1 || arms > WHEEL_MAX_ARMS) return;
    w = &g_wheel[i];
    memset(w, 0, sizeof(*w));
    w->type = A_TYPE(a);
    w->knob = D_PARAM(def, 5) > 0;
    w->top = D_PARAM(def, 6) << 16;
    w->ramp = (*(WORD *)(def + 0x10) & 2) != 0;      /* Lomax's flag 2: 'dont move' */
    w->bounce = D_PARAM(def, 8);
    if (D_PARAM(def, 10) & 1) {
        w->max = D_PARAM(def, 9);
        w->mul = RO_RADIUS(a) / 16;
    }
    stagger = (D_PARAM(def, 10) & 2) ? (int)((unsigned)(WORD)D_PARAM(def, 9) * 2 / arms) : 0;
    for (k = 0; k < arms; k++) {                     /* 0x2B4E4 */
        w->off[k] = k * (0x800 / arms);
        if (!(D_PARAM(def, 10) & 1)) continue;
        w->dir[k] = 1;
        if ((short)(d4 - w->max) >= 0) { w->cnt[k] = w->max; w->dir[k] = -1; }
        else w->cnt[k] = d4;
        d4 = (short)(d4 + stagger);
    }
    for (k = D_PARAM(def, 7); k > 0; k--) wheel_turn(w, 0);
    RO_TOPSPEED(a) = w->top >> 13;
    RO_PHASE(a) = (int)(w->ang >> 13);
    set_anim(a, "RING");
    A_UPDATE(a) = (void *)wheel_update;
    A_DRAW(a) = (void *)wheel_draw;
    flog("wheel at (%d,%d): radius %d, %d arms, speed %d%s, pre-run %d, stretch %d", RO_PIVOTX(a), RO_PIVOTY(a),
         RO_RADIUS(a), arms, D_PARAM(def, 6), w->ramp ? " (dont move)" : "", D_PARAM(def, 7), w->max);
}
