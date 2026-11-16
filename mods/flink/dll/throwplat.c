/*
 * throwplat.c - Flink's Throw platform (README "Grabbing and throwing", "Platforms").
 *
 * Flink (init 0x27952, its extra hook 0x279B8 run by Actor_ThrownThink 0x323C2 every frame): a
 * thrown object at rest from its spawn on, heavy (+6 bit 0), no fuse (bit 2), held at the hand,
 * record STAY; its param is a drift speed, |param| * 0x28F per frame (+0x4A).
 * - Out of the water it is a platform the player can stand on, also while it flies (0x27AE6: it is
 *   moved back and then by its speed with Platform_Stand 0x1A29A, which carries the player).
 * - Reaching the water surface + 5 (g_WaterSurfaceY 0x8718, the tide included) it floats there
 *   (+0x46), with a splash if it fell in. Floating, every frame:
 *   - it drifts at its speed the way it faces (the way it was thrown); a wall ahead (0x1AD4C: at its
 *     hitbox's right, in |its top| / 16 + 1 cells up from its feet, two) stops the drift for good
 *     (+0x4A = 0); a wall higher up (24-1's rock over the lava at x 1136) lets it drift under;
 *   - ground under its x at or above the surface + 5 (0x1AEEA, 3 cells) beaches it: it stops floating
 *     and is a thrown object at rest again;
 *   - it stays at the surface + 5; landed on, it dips for 31 frames, (15 - |t - 15|) / 2 px at t
 *     (+0x48), with a splash; more than 5 px off that line it rises 1 px a frame toward it;
 *   - it carries the player standing on it (0x1A27E: vy = the line - y, then Platform_Stand).
 * - Picked up it stops floating; thrown again, it floats again where it comes down into water.
 * - In lava (Flink's 0x82EA, the Water's 'lava' checkbox), lifted above the surface + 5 (by a
 *   Fountain, fountain.c) it stops floating (0x279FC) and is a thrown object again, which falls back
 *   and floats where it comes down.
 * - A Fountain with 'wait' erupts only under a floating one (throwplat_floating_near).
 * Here: a 'Mega Throw' (a static Mega, object 64, whose spawn platforms.c wraps) given to grab.c as
 * a thrown object at rest (grab_rest) with throwplat_think as the extra hook and Lomax standing on it
 * (the kind's platform flag: Platform_MoveAndCarryPlayer). Param 2 (the Mega's x speed, with an
 * amplitude of 0) is Flink's |param|.
 */
#include "enemy.h"

#define DRIFT_UNIT         0x28F
#define DIP_TICKS          31
#define CULL_PHASE         1             /* on land: Lomax's cull phase 1 (160 px past the view, x and y) */
#define FLOAT_NEAR         160           /* floating: despawned this far past the view's sides, x only */
#define VIEW_W             512           /* Lomax+'s widest view */

/* per actor slot: their type, drift speed (16.16), floating, the dip's countdown, stood on last tick */
static struct { BYTE *type; int drift, floating, dip, on; } g_throwplat[MAX_ACTORS];

void throwplats_reset(void) { memset(g_throwplat, 0, sizeof(g_throwplat)); }

static int throwplat_slot(BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_throwplat[i].type && g_throwplat[i].type == A_TYPE(a) ? i : -1;
}

static void __cdecl throwplat_picked(BYTE *a)
{
    int i = throwplat_slot(a);
    if (i >= 0) g_throwplat[i].floating = g_throwplat[i].on = 0;
}

/* stops floating: a thrown object at rest again, culled as on land */
static int land(BYTE *a, int i)
{
    g_throwplat[i].floating = 0;
    A_CULL(a) = CULL_PHASE;
    return 0;
}

/* 0x279B8: floating; 1 = it moved itself this tick (or was despawned) */
static int __cdecl throwplat_think(BYTE *a)
{
    int i = throwplat_slot(a), wl = *G_WATERLEVELY + 5, g, target, d, vx, was, right, x;
    if (i < 0) return 0;
    if (!g_throwplat[i].floating) {
        if (A_Y(a) < wl) return 0;
        g_throwplat[i].floating = 1;
        g_throwplat[i].dip = 8;
        if (A_VY(a)) Water_SpawnSplash(a);
        flog("throw platform floats at (%d,%d), drifting %s at %d/65536", A_X(a), A_Y(a),
             A_FLAGS(a) & AF_LEFT ? "left" : "right", g_throwplat[i].drift);
    }
    /* Floating, it follows the tide, which in 11-2 (the Water at the map's bottom, +-72 px) takes it
       77 px under the map, where Lomax's cull (Actors_DrawAndCull 0x40F040: phase 1, 110 px past the
       view's bottom) would despawn it whenever the camera is high. Flink's window (init 0x27952: +0xE = 8, the
       mid window) tests only x, so here it is never culled while floating and goes only past the
       view's sides. */
    A_CULL(a) = CULL_NEVER;
    x = A_X(a) - P_CAMX(PLANE(A_PLANE(a)));
    if (x < -FLOAT_NEAR || x >= VIEW_W + FLOAT_NEAR) {
        flog("floating throw platform despawned off the view at (%d,%d)", A_X(a), A_Y(a));
        g_throwplat[i].type = NULL;
        Actor_Despawn(a);                            /* (its placement spawns it again) */
        return 1;
    }
    g = ground_y(a, A_X(a), A_Y(a));
    if (g != NO_GROUND && g <= wl) return land(a, i);                    /* beached */
    if (A_Y(a) < wl && water_lava()) return land(a, i);                  /* lifted out of the lava */
    target = wl;
    if (g_throwplat[i].dip > 0) {
        d = --g_throwplat[i].dip - 15;
        target += (15 - (d < 0 ? -d : d)) >> 1;
    }
    if (abs(A_Y(a) - target) > 5) target = A_Y(a) - 1;
    if (g != NO_GROUND && g < target) return land(a, i);
    right = !(A_FLAGS(a) & AF_LEFT);
    vx = right ? g_throwplat[i].drift : -g_throwplat[i].drift;
    A_FY(a) = A_Y(a) << 16;
    A_VX(a) = vx == 1 ? 0 : vx;
    A_VY(a) = (target - A_Y(a)) << 16;
    was = g_throwplat[i].on;
    g_throwplat[i].on = Platform_MoveAndCarryPlayer(a, NULL);
    A_VX(a) = A_VY(a) = 0;
    if (g_throwplat[i].on && !was) { g_throwplat[i].dip = DIP_TICKS; Water_SpawnSplash(a); }
    if (g_throwplat[i].drift) {
        if (flink_wall_ahead(a, right)) {            /* (0x1AD4C: two cells, the lava's rocks' walls above pass) */
            g_throwplat[i].drift = 0;                 /* stopped for good */
            flog("throw platform at (%d,%d) stops at a wall", A_X(a), A_Y(a));
        }
    }
    return 1;
}

/* a floating one within its drift speed * 16 / 65536 px of x (Flink's Fountain 0x26D08: a live actor
   with an extra hook, +6 bit 6, floating, +0x46; |dx| <= +0x4A << 4 >> 16) */
int throwplat_floating_near(int x)
{
    int i;
    for (i = 0; i < MAX_ACTORS; i++) {
        BYTE *a = actor_at(i);
        if (!g_throwplat[i].type || g_throwplat[i].type != A_TYPE(a) || !g_throwplat[i].floating) continue;
        if (!actor_active(i) || A_LAYER(a) == KILLED_LAYER) continue;
        if (abs(x - A_X(a)) <= (short)((g_throwplat[i].drift << 4) >> 16)) return 1;
    }
    return 0;
}

void throwplat_spawn(BYTE *a, BYTE *def, int i)
{
    GrabKind k;
    memset(&g_throwplat[i], 0, sizeof(g_throwplat[i]));
    g_throwplat[i].type = A_TYPE(a);
    g_throwplat[i].drift = abs(def_param(a, def, 2, 0)) * DRIFT_UNIT;
    memset(&k, 0, sizeof(k));
    k.heavy = 1;
    k.platform = 1;
    k.think = throwplat_think;
    k.picked = throwplat_picked;
    A_CULL(a) = CULL_PHASE;
    grab_rest(a, &k, SRC_THROWPLAT);
}
