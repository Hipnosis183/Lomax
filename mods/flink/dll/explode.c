/*
 * explode.c - Flink's Exploding platform (init 0x2397A, think 0x239C0, digit hook 0x23A12; MOUNTAIN
 * map 7 = 19-2 (13) and map 13 = 23-1 (2); README "Exploding platforms").
 *
 * Flink's:
 * - Init: draw list 3 ((a5) = 0x7D70), window 0, record EINS; +0x4A its speed (param 0 * 0x28F,
 *   signed: < 0 goes left), +0x48 its time (param 1 * 60 - 1 frames), +0x46 the countdown, -1 (off).
 * - Each frame (0x239C0): EINS; with the countdown running it counts down, and at 0 it takes BOOM
 *   and breaks up (0x1B1F8: Actor_BreakUp, its current frame's pieces flying off), without carrying
 *   Flink that frame. Else Platform_Stand (0x1A28A: moves it by its speed and carries Flink, the
 *   surface its hitbox top) and, stood on with the countdown off, the countdown starts at its time
 *   and its speed is set. It moves on for good, through walls (nothing stops it), whether Flink
 *   stays on it or not.
 * - Its sub-object table +8 = 0x23A0E: the hook for EINS's attach point (0, 10, flags 1) draws ZAHL
 *   frame countdown / 60 (the seconds left, 5 frames: 1 .. 5) at the point, while it runs.
 * - The break-up is 0x1B1F8 with no pending bit set: it isn't spawned again before a lost life.
 * Here: the converter's 'Mega Explode' (a static MovingPlatform, object 64, whose spawn platforms.c
 * wraps): records MAIN (EINS), BOOM, ZAHL; params Lomax's 5, then speed, time (s). explode_update is
 * Flink's think (before Player_Update); Platform_MoveAndCarryPlayer is 0x1A28A; the break-up is
 * wagon.c's actor_break_up. The digit is a second Actor_Draw of the same actor with ZAHL (not a
 * queued sprite) so that Lomax+'s FPS Override moves it with the platform.
 */
#include "flink.h"

#define EXPLODE_UNIT    0x28F            /* a param's speed unit (0x23998) */
#define FRAMES_A_SEC    60               /* 0x239A4 */
#define DIGIT_DY        10               /* EINS's attach point (0, 10) */
#define DIGITS          5                /* ZAHL's frames */

typedef struct {
    BYTE *type;
    int speed, time;                     /* +0x4A (16.16), +0x48 (frames) */
    int count;                           /* +0x46: frames left, -1 not started */
    int vx;                              /* +0x1C */
} Explode;
static Explode g_explode[MAX_ACTORS];

void explodes_reset(void) { memset(g_explode, 0, sizeof(g_explode)); }

static Explode *explode_of(const BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_explode[i].type && g_explode[i].type == A_TYPE(a) ? &g_explode[i] : NULL;
}

static int __cdecl explode_surface(BYTE *a, int relx) { (void)a; (void)relx; return 0; }

/* the hook's digit (0x23A12) while the countdown runs, then the platform: Flink's attach point is the
   frame's first piece, so the digit is queued first, in front (Lomax draws the earlier of two draws at
   one depth in front too) */
static void __cdecl explode_draw(BYTE *a)
{
    Explode *e = explode_of(a);
    int k;
    if (e && e->count >= 0) {
        k = e->count / FRAMES_A_SEC;
        if (k >= DIGITS) k = DIGITS - 1;
        set_anim(a, "ZAHL");
        hold_frame(a, k);
        Actor_AdvanceAnim(a);
        A_Y(a) += DIGIT_DY;
        draw_actor(a);
        A_Y(a) -= DIGIT_DY;
        set_anim(a, "MAIN");
    }
    draw_actor(a);
}

/* 0x239C0 */
static void __cdecl explode_update(BYTE *a)
{
    Explode *e = explode_of(a);
    int stood, rising;
    if (!e) { MovingPlatform_Update(a); return; }

    if (e->count >= 0 && !e->count--) {          /* (0x239CC) */
        if (*G_PRIDINGPLATFORM == a) *G_PRIDINGPLATFORM = NULL;
        flog("exploding platform breaks up at (%d,%d)", A_X(a), A_Y(a));
        e->type = NULL;
        set_anim(a, "BOOM");
        hold_frame(a, 0);
        Actor_AdvanceAnim(a);
        actor_break_up(a);
        return;
    }

    /* as wagon.c: Lomax's carry would hold a rider's feet on it after his jump's wind-up, so a
       rising Lomax isn't carried (0x1A2DC: Flink's Platform_Stand doesn't stand a rising player) */
    rising = (*G_PLAYERSTATE & PS_AIR) && *G_PLAYERVY < 0 && !*G_JUMPTAKEOFFTIMER;
    if (rising && *G_PRIDINGPLATFORM == a) *G_PRIDINGPLATFORM = NULL;
    if (rising) {
        A_FX(a) += e->vx;
        set_pos(a);
        stood = 0;
    } else {                                     /* (0x239EE: Platform_Stand) */
        A_VX(a) = e->vx;
        A_VY(a) = 0;
        stood = Platform_MoveAndCarryPlayer(a, explode_surface);
        A_VX(a) = A_VY(a) = 0;
    }
    if (stood && e->count < 0) {                 /* (0x239F8) */
        e->count = e->time;
        e->vx = e->speed;
        flog("exploding platform at (%d,%d) stood on: goes at %d/65536, breaks in %d frames",
             A_X(a), A_Y(a), e->vx, e->count);
    }
}

/* 0x2397A (from mega_spawn); params: Lomax's 5, speed, time */
void explode_spawn(BYTE *a, BYTE *def, int i)
{
    Explode *e = &g_explode[i];
    memset(e, 0, sizeof(*e));
    e->type = A_TYPE(a);
    e->speed = (short)def_param(a, def, 5, 0) * EXPLODE_UNIT;
    e->time = (WORD)def_param(a, def, 6, 0) * FRAMES_A_SEC - 1;
    e->count = -1;
    A_FX(a) = A_X(a) << 16;
    A_FY(a) = A_Y(a) << 16;
    A_VX(a) = A_VY(a) = 0;
    A_LAYER(a) = (short)FLINK_LAYER(3);          /* (a5) = 0x7D70: list 3 */
    A_UPDATE(a) = (void *)explode_update;
    A_DRAW(a) = (void *)explode_draw;
    set_anim(a, "MAIN");
    flog("exploding platform at (%d,%d): speed %d/65536, %d frames", A_X(a), A_Y(a), e->speed, e->time);
}
