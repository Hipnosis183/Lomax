/*
 * holder.c - the Ingredient Holder (Flink 0x2F568, think 0x2F5CC), the bag carrier of FOREST and
 * the other worlds (README "Enemies"). The converter makes it a 'Muta Holder n' type with MAIN.WLD's
 * records WALK, JUMP, SL45 and DEAD (see the converter's ENEMIES); param 0: facing left. Flink's
 * behaviour:
 * - Walking: Actor_Walk (0x1D0C4, jump_walk) with flags 0x800 at 0.5 px a frame. Unlike the Forest
 *   Dummy (flag 0x1000) it jumps: at a ledge it looks for ground 16, 32, 48 and 64 px ahead (from 48
 *   px above its feet, 7 cells down) and jumps there; with none, flag 0x800 turns it back. It jumps
 *   up a 45 degree slope (4.0 px up) and slides down one (SL45). Its WALK steps at |vx| / 1000
 *   (0x2F5F2, Flink's anim speed +0x32). Touching it hurts.
 * - Stomped the first time (0x2F628): sound 0x18; its state word's bits 8 and 11 hide the calm head
 *   and arm (selectors 1 and 4) and bits 9 and 10 no longer hide the scared ones (2 and 3); for 60
 *   frames nothing touches it (+6 bit 3) and it blinks (+3 bit 1 every 4th frame); it stops, then
 *   runs at 1.125 px a frame for good, facing away from where Flink faces, and without flag 0x800:
 *   at a ledge with no ground ahead it jumps off (2.0 px up). It carries on walking as before.
 * - Stomped again, hit by Flink's slide or a spell, or fallen out of the map (Actor_Walk sets +3
 *   bit 6): drops the ingredient from its bag (Item_Spawn 0x2C21A) and dies (Enemy_Die 0x1B12E), as
 *   the Forest Dummy dies.
 * Here Lomax's stomp and attacks (enemy.c), his attacks counting as a stomp, as Lomax's zombies
 * take two hits (enemy_attacked): the first scares it, the second kills it; the bag is empty, so it drops a coin as the other enemies
 * (enemy_defeated). A fall out of the map just removes it, gone for good, with no coin.
 */
#include "enemy.h"

#define HOLDER_SPEED       0x8000        /* 0.5 px a frame (+0x4C) */
#define HOLDER_RUN         0x12000       /* after the first stomp, 1.125 */
#define HOLDER_BLINK       60
/* hidden selectors (Lomax +0x70 bit n hides slot n = Flink's state bit 7 + n): init 0x8600 hides
   2 and 3 (the scared head and arm), the first stomp sets bits 8 and 11 and clears 9 and 10 */
#define HIDE_CALM          (1 << 2 | 1 << 3)
#define HIDE_SCARED        (1 << 1 | 1 << 4)

/* Actor_AdjustToGround's reach (0x1AEDA), by speed in whole px (as enemy_walk) */
static const int SNAP[8] = { 3, 4, 6, 7, 10, 10, 10, 10 };

/* off the ground (0x1D412): bit 8, JUMP */
static void take_off(BYTE *a, int i)
{
    g_enemy[i].air = 1;
    set_anim(a, "JUMP");
}

/* a wall at x +-8 turns it (0x1D196); 1 if it did. The map's left and right edges count as walls
   here: scared, it ran off 1-2's left edge (no ground to the left of x 0) and fell out of the map,
   where Lomax can't go. The Intree dummy's walker (intree.c) uses it too. */
int turn_at_wall(BYTE *a, int i)
{
    int left = A_FLAGS(a) & AF_LEFT, x = left ? A_X(a) - 8 : A_X(a) + 8;
    if (!wall_at(a, x, !left) && x >= 0 && x < P_W(PLANE(A_PLANE(a))) * 16) return 0;
    A_FLAGS(a) ^= AF_LEFT;
    g_enemy[i].turning = 0;
    return 1;
}

/* Actor_Walk (0x1D0C4) at speed target (16.16 px a frame), with flags 0x800 (ledge_turn: with no
   ground close ahead it turns back) or 0 (it jumps off). The Ingredient Holder (calm 0x800, scared
   0) and the Thief (thief.c: chasing 0x800, fleeing 0) use it. Flink's walker bits: 8 = in the air
   (air), 9 = sliding down a 45 degree slope, and in the air no wall check (slide), 10 = stopping to
   turn (turning). Flink's y velocity +0x20 points up; Lomax's +0x50 down. 1 = fell out of the map. */
int jump_walk(BYTE *a, int i, int target, int ledge_turn)
{
    DWORD *fl = &A_FLAGS(a);
    int *vx = &A_VX(a), *vy = &A_VY(a);
    int dir = *fl & AF_LEFT ? -1 : 1, x = A_X(a), y = A_Y(a);
    int step = 0x1000, speed, g, k;
    if (g_enemy[i].air) {                                    /* 0x1D10C */
        int fast = target >= 0x10000;
        *vy += fast ? 0x2A00 : 0x2400;
        if (*vy > 0x40000) *vy = 0x40000;
        target = fast ? 0x18000 : 0x20000;
        A_FY(a) += *vy;
        set_pos(a);
        if (below_map(a)) return 1;
        if (!g_enemy[i].slide) turn_at_wall(a, i);
        step = 0x800;
    } else if (ground_y(a, x + 8 * dir, y) == NO_GROUND) {  /* a ledge ahead (0x1D1F6) */
        for (k = 1; k <= 4; k++) {                           /* ground to jump to (0x1D218) */
            g = (int)Plane_FindGroundY(PLANE(A_PLANE(a)), x + 16 * k * dir, y - 48, 7);
            if (g != NO_GROUND) break;
        }
        if (k <= 4) {                                        /* jumps there (0x1D276) */
            int dx = 16 * k;
            g_enemy[i].slide = k == 1;
            if (k == 1 || !turn_at_wall(a, i)) {
                /* up: (y - ground) / dx + dx / 16 px a frame, 16.16 */
                *vy = -((int)(((long long)(y - g) << 16) / dx) + dx * 0x1000);
                take_off(a, i);
            }
        } else if (ledge_turn) {                             /* flag 0x800: stops and turns */
            g_enemy[i].turning = 1;
            turn_at_wall(a, i);
        } else if (!turn_at_wall(a, i)) {                    /* jumps off, 2.0 px up (0x1D240) */
            *vy = -0x20000;
            g_enemy[i].slide = 0;
            take_off(a, i);
        }
    } else {
        turn_at_wall(a, i);                                  /* 0x1D2CC */
        if (g_enemy[i].slide) { step = 0x400; target = 0x18000; }
        g_enemy[i].slide = 0;
    }
    speed = abs(*vx);                                        /* 0x1D2EA */
    if (g_enemy[i].turning) {                /* (divu.w #6, as enemy_walk) */
        step = speed < 6 * 0x10000 ? (speed % 6) << 16 | speed / 6 : speed;
        target = 0;
        if (!*vx) { g_enemy[i].turning = 0; *fl ^= AF_LEFT; }
    }
    if (speed < target) speed = speed + step > target ? target : speed + step;
    else if (speed > target) speed = speed - step < target ? target : speed - step;
    *vx = *fl & AF_LEFT ? -speed : speed;
    A_FX(a) += *vx;
    set_pos(a);
    if (*vy < 0) { take_off(a, i); return 0; }               /* rising (0x1D354) */
    x = A_X(a); y = A_Y(a);
    g = ground_y(a, x, y);                                   /* Actor_AdjustToGround (0x1AE96) */
    k = SNAP[speed >> 16 < 7 ? speed >> 16 : 7];
    if (g == NO_GROUND || (g_enemy[i].air ? y < g - k || y - g > k + (*vy >> 16) : abs(g - y) > k)) {
        take_off(a, i);                                      /* no ground: falls */
        return 0;
    }
    g_enemy[i].slide = 0;
    place_y(a, g);
    *vy = 0;
    if (*G_GROUNDSLOPECLASS != 2) {                          /* lands, or walks on (0x1D404) */
        g_enemy[i].air = 0;
        set_anim(a, "WALK");
        return 0;
    }
    if (g_enemy[i].air) { g_enemy[i].air = 0; *vx /= 2; }    /* 45 degrees (0x1D398) */
    if (*fl & AF_LEFT ? *G_GROUNDMIRRORED : !*G_GROUNDMIRRORED) {
        g_enemy[i].slide = 1;                                /* downhill: slides */
        set_anim(a, "SL45");
    } else {
        *vy = -0x40000;                                      /* uphill: jumps, 4.0 px up */
        take_off(a, i);
    }
    return 0;
}

/* (from enemy_stomped, or holder_update on an attack; knock: thrown away from Lomax if it dies) */
void holder_stomped(BYTE *a, int i, int knock)
{
    if (g_enemy[i].timer > 0) return;        /* (nothing touches it then) */
    if (g_enemy[i].scared) { enemy_die(a, i, knock); return; }
    Sound_PlayEffect(a, SND_HIT);
    g_enemy[i].scared = 1;
    g_enemy[i].timer = HOLDER_BLINK;
    g_enemy[i].turning = 0;
    A_HIDE(a) = HIDE_SCARED;
    A_FLAGS(a) &= ~AF_CONTACT;
    A_VX(a) = 0;
    if (*G_PLAYERFLAGS & AF_LEFT) A_FLAGS(a) &= ~AF_LEFT;    /* runs the other way from where Lomax faces */
    else A_FLAGS(a) |= AF_LEFT;
    flog("ingredient holder at (%d,%d) hit, runs", A_X(a), A_Y(a));
}

static void __cdecl holder_update(BYTE *a)
{
    int i = enemy_slot(a);
    if (i < 0) return;
    if (g_enemy[i].dead) { enemy_dead_update(a, i); return; }
    switch (enemy_attacked(a, i)) {
    case 2: enemy_die(a, i, 1); return;      /* rolling down a slope: Flink's slide */
    case 1: holder_stomped(a, i, 1); if (g_enemy[i].dead) return;
    }
    if (jump_walk(a, i, g_enemy[i].scared ? HOLDER_RUN : HOLDER_SPEED, !g_enemy[i].scared)) {                 /* out of the map: gone for good */
        forget_placement(A_DEF(a));
        g_enemy[i].type = NULL;
        Actor_Kill(a);
        return;
    }
    *(short *)(a + 0x68) = (short)(abs(A_VX(a)) / 1000);    /* the anim's speed (0x2F5F2) */
    A_FLAGS(a) &= ~AF_HIDDEN;
    if (g_enemy[i].timer > 0) {              /* blinking (0x2F600) */
        if (!(g_enemy[i].timer & 3)) A_FLAGS(a) |= AF_HIDDEN;
        if (--g_enemy[i].timer == 0) A_FLAGS(a) |= AF_HURTS | AF_HITTABLE;
    }
    Actor_AdvanceAnim(a);
}

/* (from enemy_spawn) */
void holder_spawn(BYTE *a, int i)
{
    g_enemy[i].holder = 1;
    A_UPDATE(a) = (void *)holder_update;
    A_HIDE(a) = HIDE_CALM;
}
