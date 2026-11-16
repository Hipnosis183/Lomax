/*
 * buzzard.c - MOUNTAIN's Buzzard, Flink's 'Buzzard' (init 0x27740, think 0x2777E; 19-1 on): a
 * 'Muta Buzz n' type with Flink's records STAY (perched), FLY (frame 0 diving, frame 1 gliding) and
 * BOOM (Flink's DEAD frame, which breaks up) (README "Enemies"). Params: facing left (Flink's
 * checkbox 'look left', +3 bit 3), 'distance' (+0x46), 'wait-timer' (+0x48, stored negated). Init:
 * draw list 2 (0x7D6E: behind Flink, in front of list 3), stompable (+6 bit 3), STAY.
 * Flink's think, every frame:
 * - Hit (+3 bit 6: a thrown object, Flink's slide or a spell) or stomped (Actor_CheckStompedByPlayer
 *   0x1A352): DEAD, sound 0x48 when on screen, and Actor_ForgetForever (0x1B1E8): gone for good, its
 *   frame broken up into its sprites, which fly up and fall. Otherwise touching it hurts (0x1A462).
 * - Perched (+0x48 < 0, 0x278C2), STAY animating: on screen (+6 bit 4: it drew a sprite last frame,
 *   set by Actors_DrawAll 0x334FE), Flink at least 40 px below it (Buzzard y <= Flink's y - 0x28), within
 *   'distance' px across, on the side it faces, and Flink facing away from it (his facing flag, bit 3
 *   of 0x81E5, isn't the side he is on): +0x48 = the wait-timer (negated back).
 * - Waiting (+0x48 > 0, 0x27898), STAY animating: - 1 a frame; at 0 sound 0x43 when on screen and FLY.
 *   A wait-timer of 0 means flying from the start (in Flink such a Buzzard is spawned off the screen, so
 *   its first flying frame frees it: 545,136 in 19-1 is never seen).
 * - Flying (+0x48 = 0, 0x277A4), FLY's frame set by hand (+0x30's frame byte, no Actor_AdvanceAnim):
 *   vx goes by 1/16 a frame toward 2.0 the way it faces; x += vx, y += vy; off the screen (+6 bit 4
 *   clear) it is freed (Actor_Free 0x32D9E: not despawned, so its placement doesn't spawn it again
 *   until a lost life). Then on frame 0 (the dive): vy + 1/8 a frame up to 3.0, and once Map_GetGround
 *   Height (0x1AEEA: 7 cells down from y - 16) finds ground 30 px or less under it, frame 1. On frame 1
 *   (the glide): with ground in those 7 cells it keeps to 30 px over it (above the line vy + 1/8 a frame
 *   while under 1.0, at or below it vy - 1/8 while over -1.0); with none it climbs, vy - 1/16 a frame
 *   with no limit, and so flies away up off the screen.
 * Here: "on screen" is the Buzzard within Lomax's view (32 px margin), taken at the end of each update as
 * Flink's draw follows its think; a Buzzard starts not seen, so a flying start frees it as in Flink.
 * Lomax's stomp, spin, roll and thrown helmet kill it at once (one hit, as Flink's slide and spells),
 * thrown objects through enemy_struck. Freed here is Actor_Kill (its placement stays spawned until a
 * lost life resets the placements). Sounds (Lomax has no Buzzard): ZOOM1 (60) for the take-off's 0x43,
 * Lomax's enemy hit (SND_HIT) for the death's 0x48.
 */
#include "enemy.h"

#define SEE_DY          0x28
#define FLY_MAXVX       0x20000
#define FLY_ACCEL       0x1000
#define DIVE_MAXVY      0x30000
#define DIVE_ACCEL      0x2000
#define GLIDE_VY        0x10000
#define GLIDE_ACCEL     0x2000
#define CLIMB_ACCEL     0x1000
#define GROUND_CELLS    7               /* 0x1AEEA with d2 = 6: dbra, 7 cells */
#define OVER_GROUND     0x1E

typedef struct { int dist, t, frame, seen; } Buzzard;
static Buzzard g_buzzard[MAX_ACTORS];

static int is_buzzard(int i) { return i >= 0 && g_enemy[i].buzzard; }

/* 0x27924: DEAD and Actor_ForgetForever's break-up */
void buzzard_die(BYTE *a, int i)
{
    BYTE *coin;
    forget_placement(A_DEF(a));
    g_enemy[i].type = NULL;
    Sound_PlayEffect(a, SND_HIT);
    coin = enemy_defeated(a);
    Actor_ShatterIntoPieces(a, 0xC00);
    flog("buzzard at (%d,%d) killed%s", A_X(a), A_Y(a), coin ? ", drops a coin" : "");
}

/* the ground within 7 cells under it (0x1AEEA), NO_GROUND if none */
static int buzzard_ground(const BYTE *a)
{
    return (int)Plane_FindGroundY(PLANE(A_PLANE(a)), A_X(a), A_Y(a), GROUND_CELLS);
}

/* 0x278C2: perched, it sees Lomax */
static int buzzard_sees(const BYTE *a, const Buzzard *b)
{
    int dx = A_X(a) - *G_PLAYERX, right = dx < 0;          /* (d1 bit 3: Lomax right of it) */
    if (!b->seen || *G_PLAYERY - SEE_DY < A_Y(a)) return 0;
    if (abs(dx) > b->dist) return 0;
    if (((A_FLAGS(a) & AF_LEFT) != 0) == right) return 0;   /* behind it */
    if (((*G_PLAYERFLAGS & AF_LEFT) != 0) == right) return 0;   /* he faces it */
    return 1;
}

/* 0x277A4 */
static int buzzard_fly(BYTE *a, Buzzard *b)
{
    int g;
    if (A_FLAGS(a) & AF_LEFT) { if (A_VX(a) != -FLY_MAXVX) A_VX(a) -= FLY_ACCEL; }
    else if (A_VX(a) != FLY_MAXVX) A_VX(a) += FLY_ACCEL;
    A_FX(a) += A_VX(a);
    A_FY(a) += A_VY(a);
    set_pos(a);
    if (!b->seen) return 0;                                 /* (Actor_Free) */
    g = buzzard_ground(a);
    if (!b->frame) {                                        /* the dive (0x2785A) */
        if (A_VY(a) != DIVE_MAXVY) A_VY(a) += DIVE_ACCEL;
        if (g != NO_GROUND && g - OVER_GROUND <= A_Y(a)) b->frame = 1;
    } else if (g == NO_GROUND) A_VY(a) -= CLIMB_ACCEL;      /* climbs away (0x27850) */
    else if (g - OVER_GROUND > A_Y(a)) { if (A_VY(a) < GLIDE_VY) A_VY(a) += GLIDE_ACCEL; }
    else if (A_VY(a) > -GLIDE_VY) A_VY(a) -= GLIDE_ACCEL;
    hold_frame(a, b->frame);
    Actor_AdvanceAnim(a);
    return 1;
}

static void __cdecl buzzard_update(BYTE *a)
{
    int i = enemy_slot(a);
    Buzzard *b;
    if (!is_buzzard(i)) return;
    b = &g_buzzard[i];
    if (Actor_CheckHitByPlayer(a)) { buzzard_die(a, i); return; }   /* (as Flink's slide and spells) */
    if (b->t < 0) {                                         /* perched */
        if (buzzard_sees(a, b)) {
            b->t = -b->t;
            flog("buzzard at (%d,%d) sees Lomax, waits %d", A_X(a), A_Y(a), b->t);
        }
        Actor_AdvanceAnim(a);
    } else if (b->t > 0) {                                  /* waiting */
        if (--b->t == 0) {
            if (b->seen) flink_sound(a, 0x43);
            Actor_SetAnim(a, FOURCC("FLY "));
            b->frame = 0;
            flog("buzzard at (%d,%d) takes off", A_X(a), A_Y(a));
        } else Actor_AdvanceAnim(a);
    } else if (!buzzard_fly(a, b)) {
        flog("buzzard at (%d,%d) flew off the screen", A_X(a), A_Y(a));
        g_enemy[i].type = NULL;
        Actor_Kill(a);
        return;
    }
    b->seen = enemy_on_screen(a);
}

/* (from enemy_spawn) 0x27740: perched, STAY, on Flink's draw list 2 */
void buzzard_spawn(BYTE *a, int i, const BYTE *def)
{
    Buzzard *b = &g_buzzard[i];
    memset(b, 0, sizeof(*b));
    g_enemy[i].buzzard = 1;
    b->dist = def_param(a, def, 1, 0);
    b->t = -def_param(a, def, 2, 0);
    A_LAYER(a) = FLINK_LAYER(2);
    A_UPDATE(a) = (void *)buzzard_update;
    Actor_SetAnim(a, FOURCC("STAY"));
}
