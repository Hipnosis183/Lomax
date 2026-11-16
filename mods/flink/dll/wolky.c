/*
 * wolky.c - Flink's Wolky platform (init 0x230E4, think 0x23120; META): a cloud rising
 * until it is off the top of the map (README "Wolky platform").
 *
 * Flink's:
 * - Init (0x230E4): vy (+0x20) = param 0 * -0x28F (100 rises 1 px a frame), then 0x230F0, which the
 *   Blitz dude's cloud shares: think 0x23120, draw list 3 ((a5) = 0x7D70), activation window 8 (the
 *   mid one), +6 bit 5, the sway phase (+0x46), the dip count (+0x48) and +0x4A cleared; WOLK.
 * - Think (0x23120), every frame: at y -30 or above it is despawned (Actor_Despawn 0x32D8E: pending
 *   again, so its Generator brings it back). vy -= +0x4A (always 0); WOLK; the phase + 8,
 *   vx = sin(phase) (table 0x1491E, 1024 a turn, 0x4000 = 0.25 px: 5 px each way every 128 frames);
 *   a platform (0x1A28A: moved by vx, vy, carrying Flink). The frame Flink lands on it (0x82FE
 *   changed to it) the dip count is 20 and vy is made positive (it sinks); with the count running it
 *   counts down, otherwise vy is made negative (it rises). So it never stops: it rises at its own
 *   speed, sinking 20 frames each time it is landed on, until it is gone off the top.
 * - Outside the mid window Actors_DrawAll despawns it (pending again) too.
 * The Generator that spawns it brings the clouds back once they are gone while Flink stays in its
 * box: 30-2's start has three rising at 1, 0.75 and 0.5 px a frame, coming again and again.
 * Here: the converter's 'Mega Wolky' (a static MovingPlatform, object 64, whose spawn platforms.c
 * wraps): MAIN (WOLK), params Lomax's 5 (0) and the speed. wolky_update is Flink's think, through
 * wolky_step (which blitz.c's let-go cloud runs too); Platform_MoveAndCarryPlayer is 0x1A28A; gone
 * with Actor_Despawn, so the placement is pending again and its Gene spawns it anew.
 */
#include <math.h>
#include "flink.h"

#define WOLKY_UNIT      0x28F           /* a param's speed unit (0x230E8: muls #-0x28F) */
#define WOLKY_TOP       (-30)           /* 0xFFE2 */
#define WOLKY_STEP      8               /* the sway phase a frame */
#define WOLKY_DIP       20              /* 0x14: frames it sinks when landed on */
#define WIN_LEFT        128             /* Flink's mid window around its 320 x 224 screen */
#define WIN_RIGHT       128
#define WIN_ABOVE       112
#define WIN_BELOW       144
#define FLINK_VIEW_W    320
#define FLINK_VIEW_H    224

typedef struct { BYTE *type; int phase, dip, vy; } Wolky;
static Wolky g_wolky[MAX_ACTORS];
static short g_sine[1024];              /* Flink's table 0x1491E: round(0x4000 * sin) */

void wolky_reset(void)
{
    int k;
    memset(g_wolky, 0, sizeof(g_wolky));
    if (!g_sine[256])
        for (k = 0; k < 1024; k++) g_sine[k] = (short)floor(16384.0 * sin(k * 3.14159265358979 / 512) + 0.5);
}

/* Flink's think after its top test (0x23130): sways, moves at vy carrying Lomax, sinks 20 frames
   when landed on, rises otherwise. The caller sets WOLK. */
void wolky_step(BYTE *a, int *phase, int *dip, int *vy)
{
    BYTE *was = *G_PRIDINGPLATFORM;
    int on;
    if (!g_sine[256]) wolky_reset();
    *phase += WOLKY_STEP;
    A_VX(a) = g_sine[*phase & 0x3FF];
    A_VY(a) = *vy;
    on = Platform_MoveAndCarryPlayer(a, NULL);
    A_VX(a) = A_VY(a) = 0;
    if (on && was != a) { *dip = WOLKY_DIP; *vy = abs(*vy); }
    if (*dip) (*dip)--;
    else *vy = -abs(*vy);
    Actor_AdvanceAnim(a);
}

/* inside Flink's mid window (8): its margins around Lomax's view */
static int in_window(const BYTE *a)
{
    const BYTE *pl = PLANE(A_PLANE(a));
    int w = P_W(pl) * 16 - P_MAXX(pl), x = A_X(a) - P_CAMX(pl), y = A_Y(a) - P_CAMY(pl);
    if (P_MAXX(pl) <= 0 || w < FLINK_VIEW_W) w = FLINK_VIEW_W;
    return x >= -WIN_LEFT && x <= w + WIN_RIGHT && y >= -WIN_ABOVE && y <= FLINK_VIEW_H + WIN_BELOW;
}

static void __cdecl wolky_update(BYTE *a)
{
    int i = slot_of(a);
    Wolky *w;
    if (i < 0 || g_wolky[i].type != A_TYPE(a)) return;
    w = &g_wolky[i];
    if (A_Y(a) <= WOLKY_TOP || !in_window(a)) {      /* 0x32D8E; Actors_DrawAll's window */
        w->type = NULL;
        if (*G_PRIDINGPLATFORM == a) *G_PRIDINGPLATFORM = NULL;
        Actor_Despawn(a);
        return;
    }
    set_anim(a, "MAIN");
    wolky_step(a, &w->phase, &w->dip, &w->vy);
}

/* 'Mega Wolky' (0x230E4) */
void wolky_spawn(BYTE *a, BYTE *def, int i)
{
    Wolky *w = &g_wolky[i];
    memset(w, 0, sizeof(*w));
    w->type = A_TYPE(a);
    w->vy = -(short)def_param(a, def, 5, 100) * WOLKY_UNIT;
    A_FX(a) = A_X(a) << 16;
    A_FY(a) = A_Y(a) << 16;
    A_VX(a) = A_VY(a) = 0;
    A_LAYER(a) = (short)FLINK_LAYER(3);          /* (a5) = 0x7D70: list 3 */
    A_UPDATE(a) = (void *)wolky_update;
    set_anim(a, "MAIN");
    flog("wolky platform at (%d,%d): vy %d/65536", A_X(a), A_Y(a), w->vy);
}
