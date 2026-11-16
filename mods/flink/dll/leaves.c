/*
 * leaves.c - Flink's Leaveplatform, the falling leaves (README "Platforms", "Leaves").
 *
 * Flink (init 0x2B9F0, a platform: 0x1A28A moves it by its speeds, carries the player standing on it
 * and reports him there):
 * - "lonely leave" (checkbox 0): a leaf at rest, STAY (think 0x2BB24). Its fall speed is 0.5 or
 *   0.625 px a frame at random (+0x46: (4 + (random & 1)) << 13), its drift -0.25 px a frame, +0.25
 *   when flipped (checkbox 1, 'look left': +3 bit 3; +0x4E). The frame the player stands on it, it
 *   starts to fall: FALL, its swing phase (+0x4A) 0x400 (0 when flipped), its bob phase (+0x4C) 0.
 * - Falling (think 0x2BB5C), every frame, phases as byte offsets into the 1024-word sine table
 *   0x1491E (amplitude 0x4000, 2048 a turn):
 *   - swing phase += 0x10 (a sway every 128 frames); vx = sin(swing) * 4 + drift (up to 1 px a frame
 *     sideways, toward its tip first);
 *   - frame = |((swing + (flipped ? 0 : 0x400)) & 0x7FF) - 0x400| / 0x15F, 0-2 of FALL (2 at the
 *     turns, 0 in the middle, the leaf tilting as it sways);
 *   - bob phase += 0x20; vy = sin(bob) + fall speed (0.25 - 0.875 px a frame down);
 *   - it moves, carrying the player (0x1A28A), through everything, and despawns (pending again: it
 *     comes back at its placement) once at the map's height + 20 (0x877C).
 * - the leaf spawner (no checkbox 0; think 0x2BA74, drawn as nothing: 0x328C4): every 64 frames it
 *   spawns a falling leaf (0x32CD8) 80 px above itself, x + {-90, 0, -60, 30, -30, 60, 0, 90}[n & 7]
 *   for the n-th; it falls at 0.5 px a frame, no drift, its two phases (random & 0x3F) * 16 and
 *   twice that, flipped when bit 6 of the bob phase is set. Spawner and leaves have activation window
 *   0x18 (despawned far outside the view).
 * Here:
 * - a lonely leaf is a static 'Mega Leaf' (Lomax's MovingPlatform, object 64, whose spawn
 *   platforms.c wraps) with MAIN = STAY and FALL; flipped ones are the converter's mirrored type, and
 *   param 2 (the Mega's x speed, with an amplitude of 0) is 1 for them. leaf_rest / leaf_fall replace
 *   MovingPlatform_Update; Platform_MoveAndCarryPlayer (0x413510) is Flink's 0x1A28A. Under the map it
 *   is despawned (Actor_Despawn), so the placement spawns it again, as Flink's.
 * - the spawner is a 'Mega Leaves' (MAIN = FALL) drawn as nothing and never culled; it counts all the
 *   time and spawns only within LEAF_NEAR px of the view. Its leaves are Actor_Alloc children (its
 *   type, record MAIN: there is no FALL in it), never culled, despawned under the map or farther than LEAF_NEAR from the view (Flink's
 *   far window), flipped with AF_LEFT (Lomax mirrors the drawing; the standing surface stays
 *   unmirrored: FALL's is 3 px off centre).
 */
#include "flink.h"
#include <math.h>

#define LEAF_SWING_STEP    0x10
#define LEAF_BOB_STEP      0x20
#define LEAF_FRAME_DIV     0x15F
#define LEAF_DRIFT         0x4000          /* 0.25 px a frame */
#define LEAF_BELOW         20              /* despawned at the map's height + 20 */
#define LEAF_PERIOD        64              /* the spawner's */
#define LEAF_ABOVE         80
#define LEAF_NEAR          256             /* Flink's far activation window, about */
#define VIEW_W             512             /* Lomax+'s widest view */
#define VIEW_H             240

static const int SPAWN_DX[8] = { -90, 0, -60, 30, -30, 60, 0, 90 };   /* 0x2BB14 */

/* per actor slot: their type, falling, flipped, swing and bob phases (Flink's byte offsets), fall
   speed and drift (16.16), the spawner's count and its leaves' draw function */
static struct { BYTE *type; int falling, flipped, swing, bob, vy, drift, count; void *draw; } g_leaf[MAX_ACTORS];

static short g_sine[1024];                 /* Flink's table 0x1491E: round(0x4000 * sin) */

static void __cdecl leaf_fall(BYTE *a);
static void __cdecl leaf_rest(BYTE *a);
static void __cdecl leaves_update(BYTE *a);

void leaves_reset(void)
{
    int k;
    memset(g_leaf, 0, sizeof(g_leaf));
    if (!g_sine[256])
        for (k = 0; k < 1024; k++) g_sine[k] = (short)floor(16384.0 * sin(k * 3.14159265358979 / 512) + 0.5);
}

static int leaf_rand(void) { static unsigned r = 0x6C078965u; r = r * 1103515245u + 12345u; return (int)(r >> 16) & 0x7FFF; }

static int sine(int phase) { return g_sine[(phase & 0x7FF) >> 1]; }

static int leaf_slot(const BYTE *a, void *update)
{
    int i = slot_of(a);
    return i >= 0 && g_leaf[i].type && g_leaf[i].type == A_TYPE(a) && A_UPDATE(a) == update ? i : -1;
}

static int near_view(const BYTE *a, int margin)
{
    const BYTE *pl = PLANE(A_PLANE(a));
    int x = A_X(a) - P_CAMX(pl), y = A_Y(a) - P_CAMY(pl);
    return x >= -margin && x < VIEW_W + margin && y >= -margin && y < VIEW_H + margin;
}

static void __cdecl draw_none(BYTE *a) { (void)a; }

/* 0x1A28A: moves by vx, vy (16.16) and carries Lomax; 1 = he stands on it */
static int leaf_move(BYTE *a, int vx, int vy)
{
    int on;
    A_VX(a) = vx == 1 ? 0 : vx;              /* (vx 1 means "don't move") */
    A_VY(a) = vy;
    on = Platform_MoveAndCarryPlayer(a, NULL);
    A_VX(a) = A_VY(a) = 0;
    return on;
}

/* falling (0x2BB5C) */
static void __cdecl leaf_fall(BYTE *a)
{
    int i = leaf_slot(a, (void *)leaf_fall), q, f, vx, vy;
    if (i < 0) return;
    g_leaf[i].swing = (g_leaf[i].swing + LEAF_SWING_STEP) & 0x7FF;
    vx = sine(g_leaf[i].swing) * 4 + g_leaf[i].drift;
    q = ((g_leaf[i].swing + (g_leaf[i].flipped ? 0 : 0x400)) & 0x7FF) - 0x400;
    f = q / LEAF_FRAME_DIV;                  /* (divs.w: toward 0) */
    hold_frame(a, f < 0 ? -f : f);
    Actor_AdvanceAnim(a);
    g_leaf[i].bob = (g_leaf[i].bob + LEAF_BOB_STEP) & 0x7FF;
    vy = sine(g_leaf[i].bob) + g_leaf[i].vy;
    leaf_move(a, vx, vy);
    if (A_Y(a) >= P_H(PLANE(A_PLANE(a))) * 16 + LEAF_BELOW || (!A_DEF(a) && !near_view(a, LEAF_NEAR))) {
        g_leaf[i].type = NULL;
        Actor_Despawn(a);
    }
}

/* at rest (0x2BB24): stood on, it falls */
static void __cdecl leaf_rest(BYTE *a)
{
    int i = leaf_slot(a, (void *)leaf_rest);
    if (i < 0 || !leaf_move(a, 0, 0)) return;
    g_leaf[i].falling = 1;
    g_leaf[i].swing = g_leaf[i].flipped ? 0 : 0x400;
    g_leaf[i].bob = 0;
    if (!record_of(a, FOURCC("FALL"))) return;   /* (the converter always gives it one) */
    Actor_SetAnim(a, FOURCC("FALL"));
    hold_frame(a, 2);
    Actor_AdvanceAnim(a);
    A_UPDATE(a) = (void *)leaf_fall;
    flog("leaf at (%d,%d) falls", A_X(a), A_Y(a));
}

/* the spawner's leaf (0x2BAAA) */
static void leaves_spawn_one(BYTE *a, int i)
{
    int k, n = g_leaf[i].count >> 6;
    BYTE *d = Actor_Alloc(a, NULL);
    if (!d || (k = slot_of(d)) < 0) return;
    memset(&g_leaf[k], 0, sizeof(g_leaf[k]));
    g_leaf[k].type = A_TYPE(d);
    g_leaf[k].falling = 1;
    g_leaf[k].vy = 4 << 16 >> 3;             /* 0.5 px (its random bit is dropped: moveq #0) */
    g_leaf[k].swing = (leaf_rand() & 0x3F) << 4;
    g_leaf[k].bob = g_leaf[k].swing * 2;
    g_leaf[k].flipped = (g_leaf[k].bob & 0x40) != 0;
    A_FX(d) = (A_X(a) + SPAWN_DX[n & 7]) << 16;
    A_FY(d) = (A_Y(a) - LEAF_ABOVE) << 16;
    set_pos(d);
    A_VX(d) = A_VY(d) = 0;
    A_FLAGS(d) = g_leaf[k].flipped ? AF_LEFT : 0;
    A_CULL(d) = CULL_NEVER;
    A_UPDATE(d) = (void *)leaf_fall;
    A_DRAW(d) = g_leaf[i].draw;
    A_RECORD(d) = NULL;                      /* (Actor_Alloc leaves the slot's last one) */
    Actor_SetAnim(d, FOURCC("MAIN"));        /* the spawner's MAIN is FALL */
    if (!A_RECORD(d)) { g_leaf[k].type = NULL; Actor_Despawn(d); return; }
    hold_frame(d, 2);
    Actor_AdvanceAnim(d);
}

/* the spawner (0x2BA74) */
static void __cdecl leaves_update(BYTE *a)
{
    int i = leaf_slot(a, (void *)leaves_update);
    if (i < 0) return;
    if (!(++g_leaf[i].count & (LEAF_PERIOD - 1)) && near_view(a, LEAF_NEAR)) leaves_spawn_one(a, i);
}

/* 'Mega Leaf': a lonely leaf; spawner: 'Mega Leaves' */
void leaf_spawn(BYTE *a, BYTE *def, int i, int spawner)
{
    memset(&g_leaf[i], 0, sizeof(g_leaf[i]));
    g_leaf[i].type = A_TYPE(a);
    A_FX(a) = A_X(a) << 16;
    A_FY(a) = A_Y(a) << 16;
    A_VX(a) = A_VY(a) = 0;
    if (spawner) {
        g_leaf[i].draw = A_DRAW(a);
        A_DRAW(a) = (void *)draw_none;
        A_CULL(a) = CULL_NEVER;
        A_UPDATE(a) = (void *)leaves_update;
        return;
    }
    g_leaf[i].flipped = def_param(a, def, 2, 0) != 0;
    g_leaf[i].vy = (4 + (leaf_rand() & 1)) << 16 >> 3;
    g_leaf[i].drift = g_leaf[i].flipped ? LEAF_DRIFT : -LEAF_DRIFT;
    hold_frame(a, 0);
    A_UPDATE(a) = (void *)leaf_rest;
}
