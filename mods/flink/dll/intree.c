/*
 * intree.c - the Intree dummy (Flink 0x2FA20, think 0x2FA96), TREE's small beetle (25-1, 25-2): a
 * 'Muta Intr n' type with Flink's records WALK, JUMP, FLY, STUN, SL45 and DEAD (README "Enemies").
 * Params: facing left. The Forest Dummy's ways (dummy.c: stunned 300 frames by a stomp, killed by a
 * second one, picked up while stunned, hurting on touch, Lomax's attacks counting as a stomp) and:
 * - Walking: Flink's Actor_Walk (0x1D0C4) with the Forest Dummy's flags 0x1000 at 0.5 px a frame
 *   (intree_walk). A ledge ahead sets the walker's "stopping to turn" bit (10), which its think takes
 *   as a jump (0x2FB28): it hops off ledges instead of turning back. It slides down a 45 degree slope
 *   (SL45) and turns at one going up (flag 0x1000).
 * - Jumping (0x2FB30): 3.0 px a frame up, JUMP played once (+3 bit 0: held on its last frame), sound
 *   0x39 on the screen (Flink's PCM 33; here Lomax's BOUNCE1, 0). Also on a timer (+0x4C):
 *   every 128 + (random & 127) frames, when on the ground. In the air the walker's gravity is 0x2400
 *   a frame and its speed goes up by 0x800 a frame toward 2.0.
 * - Gliding (0x2FADE): falling, it flies (FLY) and its fall is cut to 0.5 px a frame (after the
 *   walker's gravity: about 0.64 px a frame of movement), its speed back down by 0x1000 a frame to
 *   0.5; the walker then runs with bit 15 (no JUMP when the ground ends, a 45 degree slope downhill
 *   turns it instead of sliding).
 * - Small (+6 bit 1): carried 6 px forward and 6 down (+0x10 / +0x12), thrown as Flink's small shot
 *   (enemy_grab_kind in enemy.c, SHSM).
 * Falling out of the map (Actor_Walk sets the hit flag and it dies) removes it for good, with no coin,
 * as the Ingredient Holder.
 */
#include "enemy.h"

#define INTR_SPEED         0x8000        /* +0x48 */
#define INTR_JUMP_VY       0x30000       /* (Flink's vy points up) */
#define INTR_GLIDE_VY      0x8000
#define INTR_AIR_GRAVITY   0x2400        /* Actor_Walk in the air at a speed under 1.0 */
#define INTR_AIR_SPEED     0x20000
#define INTR_JUMP_EVERY    0x80          /* + (random & 0x7F) */

/* Actor_AdjustToGround's reach (0x1AEDA), by speed in whole px (as enemy_walk) */
static const int SNAP[8] = { 3, 4, 6, 7, 10, 10, 10, 10 };

static int intr_rand(void) { static unsigned r = 0x7D6E2FA9u; r = r * 1103515245u + 12345u; return (int)(r >> 16) & 0x7FFF; }

/* a solid cell that blocks from below (0x80 | 0x08) at its hitbox's top (0x1ADDE) */
static int ceiling_over(const BYTE *a)
{
    const BYTE *pl = PLANE(A_PLANE(a)), *coll = P_COLL(pl), *r = A_RECORD(a);
    int x = A_X(a), y = A_Y(a) + (r ? HB_Y1(r) : -16);
    BYTE b;
    if (P_EMPTY(pl) || !coll || x < 0 || y < 0 || x >= P_W(pl) * 16 || y >= P_H(pl) * 16) return 0;
    b = coll[(x >> 4) + (y >> 4) * P_W(pl)];
    return (b & 0xC0) == 0x80 && (b & 0x08);
}

/* in the air (the walker's bit 8): JUMP, unless it glides (bit 15: FLY stays) */
static void airborne(BYTE *a, int i)
{
    g_enemy[i].air = 1;
    if (!g_enemy[i].glide) set_anim(a, "JUMP");
}

/* Actor_Walk (0x1D0C4) with flags 0x1000 and the think's bit 15 (glide). Walker bits as jump_walk's
   (holder.c): 8 air, 9 slide, 10 turning. 1 = fell out of the map. */
static int intree_walk(BYTE *a, int i)
{
    Enemy *e = &g_enemy[i];
    DWORD *fl = &A_FLAGS(a);
    int *vx = &A_VX(a), *vy = &A_VY(a);
    int dir = *fl & AF_LEFT ? -1 : 1, x = A_X(a), y = A_Y(a);
    int target = INTR_SPEED, step = 0x1000, speed, g, k;
    if (e->air) {                                            /* 0x1D10C */
        *vy += INTR_AIR_GRAVITY;
        if (*vy > 0x40000) *vy = 0x40000;
        target = INTR_AIR_SPEED;
        A_FY(a) += *vy;
        set_pos(a);
        if (below_map(a)) return 1;
        if (!e->slide) turn_at_wall(a, i);                   /* (0x1D180) */
        step = 0x800;
    } else {
        if (ground_y(a, x + 8 * dir, y) == NO_GROUND) e->turning = 1;   /* flag 0x1000: a ledge (0x1D26E) */
        turn_at_wall(a, i);                                  /* 0x1D2CC */
        if (e->slide) { step = 0x400; target = 0x18000; }
        e->slide = 0;
    }
    speed = abs(*vx);                                        /* 0x1D2EA */
    if (e->turning) {                        /* (divu.w #6, as enemy_walk) */
        step = speed < 6 * 0x10000 ? (speed % 6) << 16 | speed / 6 : speed;
        target = 0;
        if (!*vx) { e->turning = 0; *fl ^= AF_LEFT; }
    }
    if (speed < target) speed = speed + step > target ? target : speed + step;
    else if (speed > target) speed = speed - step < target ? target : speed - step;
    *vx = *fl & AF_LEFT ? -speed : speed;
    A_FX(a) += *vx;
    set_pos(a);
    if (*vy < 0) {                                           /* rising (0x1D354) */
        if (ceiling_over(a)) *vy = 0x20000;                  /* (0x1D366: 2.0 down) */
        airborne(a, i);
        return 0;
    }
    x = A_X(a); y = A_Y(a);
    g = ground_y(a, x, y);                                   /* Actor_AdjustToGround (0x1AE96) */
    k = SNAP[speed >> 16 < 7 ? speed >> 16 : 7];
    if (g == NO_GROUND || (e->air ? y < g - k || y - g > k + (*vy >> 16) : abs(g - y) > k)) {
        airborne(a, i);                                      /* no ground: falls (0x1D412) */
        return 0;
    }
    e->slide = 0;
    place_y(a, g);
    *vy = 0;
    if (*G_GROUNDSLOPECLASS != 2) {                          /* lands, or walks on (0x1D404) */
        e->air = 0;
        set_anim(a, "WALK");
        return 0;
    }
    if (e->air) { e->air = 0; *vx /= 2; }                    /* 45 degrees (0x1D398) */
    if ((*fl & AF_LEFT ? *G_GROUNDMIRRORED : !*G_GROUNDMIRRORED) && !e->glide) {
        e->slide = 1;                                        /* downhill: slides */
        set_anim(a, "SL45");
    } else {                                                 /* uphill (flag 0x1000), or gliding: turns */
        *fl ^= AF_LEFT;
        e->turning = 0;
    }
    return 0;
}

/* 0x2FB30: 3.0 px up, JUMP once */
static void intree_jump(BYTE *a, int i)
{
    A_VY(a) = -INTR_JUMP_VY;
    g_enemy[i].air = 1;
    g_enemy[i].slide = 0;
    set_anim(a, "JUMP");
    g_enemy[i].jumped = 0;
    if (enemy_on_screen(a)) flink_sound(a, 0x39);
}

/* the think (0x2FAC4), not stunned: from dummy_update; steps its own animation */
void intree_think(BYTE *a, int i)
{
    Enemy *e = &g_enemy[i];
    int old = abs(A_VX(a)), n;
    if (intree_walk(a, i)) {                 /* out of the map: gone for good */
        forget_placement(A_DEF(a));
        e->type = NULL;
        Actor_Kill(a);
        return;
    }
    e->glide = 0;
    if (e->air && A_VY(a) > 0) {             /* falling: glides (0x2FAE2) */
        e->glide = 1;
        set_anim(a, "FLY ");
        if (old > INTR_SPEED) old -= 0x1000;
        A_VX(a) = A_FLAGS(a) & AF_LEFT ? -old : old;
        if (A_VY(a) > INTR_GLIDE_VY) A_VY(a) = INTR_GLIDE_VY;
    }
    if (e->turning) { e->turning = 0; intree_jump(a, i); }   /* a ledge: jumps (0x2FB28) */
    if (--e->t < 0) {                                         /* 0x2FB66 */
        e->t = INTR_JUMP_EVERY + (intr_rand() & 0x7F);
        if (!e->air) intree_jump(a, i);
    }
    if (A_RECORD(a) && *(const DWORD *)(A_RECORD(a) + 4) == FOURCC("JUMP")) {   /* once (+3 bit 0) */
        n = (++e->jumped * *(const short *)(A_RECORD(a) + 0xC)) >> 8;
        hold_frame(a, n < A_NFRAMES(a) ? n : A_NFRAMES(a) - 1);
    }
    Actor_AdvanceAnim(a);
}

/* (from enemy_spawn): at full speed (+0x1C = +0x48), the jump timer 128 + (random & 127) */
void intree_spawn(BYTE *a, int i)
{
    g_enemy[i].intr = 1;
    g_enemy[i].t = INTR_JUMP_EVERY + (intr_rand() & 0x7F);
    A_VX(a) = A_FLAGS(a) & AF_LEFT ? -INTR_SPEED : INTR_SPEED;
}
