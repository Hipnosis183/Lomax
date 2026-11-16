/*
 * floating.c - Flink's Floating platform: the current of leaves blowing across (README "Floating
 * platforms"; TREE's 25-2 and 26-1).
 *
 * Flink (init 0x2B818; params area, timer, speed, pre-run): the placement is a spawner drawn as nothing,
 * activation window 0x18 (never culled). Its think (0x2B876), every frame:
 * - with its x - the camera's x (0x8746) over 400 or under -area it clears its pre-run and does nothing;
 * - else the pre-run counts down, and a countdown (2 at the start) runs; when it goes below 0 it is set
 *   again to timer, a quarter of that while the pre-run lasts, and a leaf is spawned (0x32CD8): at x =
 *   the spawner's x + area + 420 (+0x54), always off Flink's screen to the right, y = its y +
 *   (random & 15), x speed -(speed + (random & 3)) * 0x28F (16.16: speed is in 1/100 px a frame,
 *   leftward), phase (random & 0x3FF) * 2, FLY, draw list 2 (0x7D6E), window 0x18.
 * A leaf (think 0x2B932), every frame:
 * - off the screen (actor +6 bit 4, set while drawn) and left of the camera, it is freed (0x32D9E);
 * - phase += 0x10 (of 0x800: 128 frames a bob); vy = sin(phase) * 4 (table 0x1491E, amplitude 0x4000:
 *   up to 1 px a frame, about 20 px up and down); frame 0 of FLY while it rises faster than 0.25 px a
 *   frame, 2 while it sinks faster, 1 in between;
 * - on the screen it moves by its speeds carrying the player (0x1A28A, a platform); off it, it moves
 *   by them directly, its x speed 4 times while its spawner's pre-run lasts (+0x54 set, the spawner at
 *   +0x56 still that spawner's think): with the quartered timer the pre-run fills the current with
 *   leaves at their usual spacing before Flink sees it.
 * Here:
 * - the spawner is a 'Mega Floating' (params 5-8: area, timer, speed, pre-run), drawn as nothing and
 *   never culled. Flink spawns it once its window 0 reaches it (with the pre-run, x - camera <= 384:
 *   inside the 400); Lomax spawns placements farther ahead, which would clear the pre-run, so one spawned
 *   by proximity waits until Flink's window reaches it (flink_window); a Generator's starts at once,
 *   as Flink's Generator spawns it. The camera is Flink's (flink_cam_x: the player's x - 0xA4, or the
 *   Scroll Point's), so the leaves start where Flink's do, 256 px or more right of Lomax: off Lomax+'s
 *   view up to 512 px wide.
 * - the leaves are Actor_Alloc children of its type (record MAIN = FLY), never culled, on Lomax's layer
 *   for list 2. Flink's on-screen test is Lomax's view (enemy_on_screen), and a leaf goes once off it
 *   and left of Lomax's camera, where Flink's camera would still show it on a wider view.
 *   Platform_MoveAndCarryPlayer (0x413510) is Flink's 0x1A28A.
 */
#include "flink.h"
#include "enemy.h"
#include <math.h>

#define FLOAT_FIRST        2             /* the first countdown (+0x46) */
#define FLOAT_AHEAD        0x1A4         /* the leaves start at x + area + 420 */
#define FLOAT_RANGE        0x190         /* the spawner works while its x - the camera's <= 400 */
#define FLOAT_STEP         0x10          /* the bob phase's step, of 0x800 */
#define FLOAT_TILT         0x4000        /* vy past +-0.25 px a frame: frame 0 or 2 */
#define FLOAT_FAST         4             /* off the screen during the pre-run */
#define FLOAT_UNIT         0x28F         /* 1/100 px (16.16) */

/* per actor slot: their type; the spawner's area, timer, speed, pre-run, countdown, start x, the
   leaves' draw function and whether it still waits for Flink's window; a leaf's spawner (slot), its
   x speed (16.16), bob phase and whether its pre-run speed-up still holds */
static struct {
    BYTE *type;
    int area, timer, speed, prerun, count, startx, waiting;
    void *draw;
    int spawner, vx, phase, fast;
} g_float[MAX_ACTORS];

static short g_sine[1024];               /* Flink's table 0x1491E: round(0x4000 * sin) */

static void __cdecl floating_update(BYTE *a);
static void __cdecl floating_leaf(BYTE *a);

void floatings_reset(void)
{
    int k;
    memset(g_float, 0, sizeof(g_float));
    if (!g_sine[256])
        for (k = 0; k < 1024; k++) g_sine[k] = (short)floor(16384.0 * sin(k * 3.14159265358979 / 512) + 0.5);
}

static int float_rand(void) { static unsigned r = 0x2B818u; r = r * 1103515245u + 12345u; return (int)(r >> 16) & 0x7FFF; }

static int float_slot(const BYTE *a, void *update)
{
    int i = slot_of(a);
    return i >= 0 && g_float[i].type && g_float[i].type == A_TYPE(a) && A_UPDATE(a) == update ? i : -1;
}

static void __cdecl draw_none(BYTE *a) { (void)a; }

/* the leaf (0x2B932) */
static void __cdecl floating_leaf(BYTE *a)
{
    int i = float_slot(a, (void *)floating_leaf), vx, vy, s;
    BYTE *sp;
    if (i < 0) return;
    if (!enemy_on_screen(a) && A_X(a) < P_CAMX(PLANE(A_PLANE(a)))) {
        g_float[i].type = NULL;
        Actor_Despawn(a);
        return;
    }
    g_float[i].phase = (g_float[i].phase + FLOAT_STEP) & 0x7FF;
    vy = g_sine[g_float[i].phase >> 1] * 4;
    hold_frame(a, vy > FLOAT_TILT ? 2 : vy < -FLOAT_TILT ? 0 : 1);
    Actor_AdvanceAnim(a);
    vx = g_float[i].vx;
    if (enemy_on_screen(a)) {
        A_VX(a) = vx;
        A_VY(a) = vy;
        Platform_MoveAndCarryPlayer(a, NULL);
        A_VX(a) = A_VY(a) = 0;
        return;
    }
    s = g_float[i].spawner;
    sp = actor_at(s);
    if (g_float[i].fast && sp && float_slot(sp, (void *)floating_update) == s && g_float[s].prerun) vx *= FLOAT_FAST;
    else g_float[i].fast = 0;
    A_FX(a) += vx;
    A_FY(a) += vy;
    set_pos(a);
}

/* the spawner's leaf (0x2B8B6) */
static void floating_spawn_one(BYTE *a, int i)
{
    int k;
    BYTE *d = Actor_Alloc(a, NULL);
    if (!d || (k = slot_of(d)) < 0) return;
    memset(&g_float[k], 0, sizeof(g_float[k]));
    g_float[k].type = A_TYPE(d);
    g_float[k].spawner = i;
    g_float[k].fast = 1;
    g_float[k].vx = -(g_float[i].speed + (float_rand() & 3)) * FLOAT_UNIT;
    A_FX(d) = g_float[i].startx << 16;
    A_FY(d) = (A_Y(a) + (float_rand() & 15)) << 16;
    set_pos(d);
    g_float[k].phase = (float_rand() & 0x3FF) * 2;
    A_VX(d) = A_VY(d) = 0;
    A_FLAGS(d) = 0;
    A_CULL(d) = CULL_NEVER;
    A_LAYER(d) = (short)FLINK_LAYER(2);  /* (a5) = 0x7D6E: list 2 */
    A_UPDATE(d) = (void *)floating_leaf;
    A_DRAW(d) = g_float[i].draw;
    A_RECORD(d) = NULL;                  /* (Actor_Alloc leaves the slot's last one) */
    Actor_SetAnim(d, FOURCC("MAIN"));    /* the spawner's MAIN is FLY */
    if (!A_RECORD(d)) { g_float[k].type = NULL; Actor_Despawn(d); return; }
    hold_frame(d, 1);
    Actor_AdvanceAnim(d);
}

/* the spawner (0x2B876) */
static void __cdecl floating_update(BYTE *a)
{
    int i = float_slot(a, (void *)floating_update), dx;
    if (i < 0) return;
    if (g_float[i].waiting) {
        if (!flink_window(a)) return;
        g_float[i].waiting = 0;
    }
    dx = A_X(a) - flink_cam_x(PLANE(A_PLANE(a)));
    if (dx > FLOAT_RANGE || dx < -g_float[i].area) { g_float[i].prerun = 0; return; }
    if (g_float[i].prerun) g_float[i].prerun--;
    if (--g_float[i].count >= 0) return;
    g_float[i].count = g_float[i].prerun ? g_float[i].timer >> 2 : g_float[i].timer;
    floating_spawn_one(a, i);
}

/* 'Mega Floating': the spawner (init 0x2B818) */
void floating_spawn(BYTE *a, BYTE *def, int i)
{
    memset(&g_float[i], 0, sizeof(g_float[i]));
    g_float[i].type = A_TYPE(a);
    g_float[i].area = def_param(a, def, 5, 0) & 0xFFFF;
    g_float[i].timer = def_param(a, def, 6, 100) & 0xFFFF;
    g_float[i].speed = def_param(a, def, 7, 100) & 0xFFFF;
    g_float[i].prerun = def_param(a, def, 8, 0) & 0xFFFF;
    g_float[i].count = FLOAT_FIRST;
    g_float[i].startx = (short)(A_X(a) + g_float[i].area + FLOAT_AHEAD);
    g_float[i].waiting = def && !(D_FLAGS(def) & 0x2000);   /* (not a Generator's) */
    g_float[i].draw = A_DRAW(a);
    A_FX(a) = A_X(a) << 16;
    A_FY(a) = A_Y(a) << 16;
    A_VX(a) = A_VY(a) = 0;
    A_DRAW(a) = (void *)draw_none;
    A_CULL(a) = CULL_NEVER;
    A_UPDATE(a) = (void *)floating_update;
    flog("floating platform at (%d,%d): area %d, timer %d, speed %d, pre-run %d", A_X(a), A_Y(a),
         g_float[i].area, g_float[i].timer, g_float[i].speed, g_float[i].prerun);
}
