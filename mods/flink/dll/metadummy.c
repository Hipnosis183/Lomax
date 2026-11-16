/*
 * metadummy.c - META's flying head, Flink's 'Meta dummy' (init 0x23618, think 0x2365A; 29-1 on): a
 * 'Muta Meta n' type with Flink's records FLY, STUN and DEAD (README "Enemies"). No params (Flink's
 * init reads none; param 0, facing left, is 0). Init: draw list 1 (0x7D6C), activation window 8,
 * stompable (+6 bit 3), held 6 px forward and 10 px down when carried (+0x10 / +0x12, as the Forest
 * Dummy), a random bob phase (+0x46 = Rand_GetNext 0x19C8A), FLY.
 * Flink's think, every frame:
 * - Hit (+3 bit 6: a thrown object, Flink's slide or a spell): Enemy_Die (0x1B12E).
 * - Stunned (+0x38 > 0): the shared stun (0x1D074: falls to the ground, STUN, can be picked up);
 *   stomped again (Actor_CheckStompedByPlayer 0x1A352) it dies (Enemy_Die).
 * - Otherwise, after a stun (its record not FLY): FLY, vx 0, vy -1.0 (it rises, braked as below).
 *   Stomped: stunned for 300 frames (+0x38 = 0x12C), popped up at 3.0, sound 0x48.
 * - Chasing (+0x30 set last frame): it faces Flink (+3 bit 3, mirrored, while its x >= his) and keeps
 *   on while Flink is within 120 px across (0x78) and its y + 20 within 100 px (0x64) of his; not
 *   chasing, it starts once he is within 100 px across and 80 (0x50) up or down. Chasing,
 *   Actor_MoveToward (0x1B008) takes it toward (Flink's x, his y - 20) at 0x400 a frame (twice that
 *   while still moving away), up to 1.0 on each axis.
 * - Idle: it bobs (+0x46 + 1 a frame, every 4th frame y + 1, or - 1 while the counter's bit 4 is set:
 *   4 px up and down every 32 frames), and its speeds brake by 0x400 a frame (0x1D448) as it drifts.
 * - Then touching it hurts (0x1A462; Flink's slide knocks it away, and it dies of the hit).
 * Here the stomp is Lomax's (enemy.c), and his attacks count as one, as on the Forest Dummy
 * (enemy_attacked): the first stuns, the next kills; his roll kills at once (Flink's slide). The stun,
 * the death, the carrying and the throws are the Forest Dummy's (dummy.c, enemy.c, grab.c). Sounds:
 * Lomax's enemy hit (SND_HIT) for the stomp's 0x48 (an FM sound), Flink's 0x22 for the death.
 */
#include "enemy.h"

#define START_DX        0x64            /* starts chasing within these */
#define START_DY        0x50
#define KEEP_DX         0x78            /* keeps chasing within these */
#define KEEP_DY         0x64
#define AIM_DY          0x14            /* it aims 20 px over Flink's feet */
#define CHASE_MAX       0x10000
#define CHASE_ACCEL     0x400
#define BRAKE           0x400
#define RISE_VY         (-0x10000)

typedef struct { int fly, chase, bob; } Meta;
static Meta g_meta[MAX_ACTORS];

static int meta_rand(void) { static unsigned r = 0x5D1A7E23u; r = r * 1103515245u + 12345u; return (int)(r >> 16) & 0xFFFF; }

/* Actor_MoveToward (0x1B008) on one axis: toward target by acc a frame (2 * acc while moving away)
   while under max. At the target it stops if slower than acc; faster, it goes on as if the target
   were ahead on the right (Flink's flags fall through to that branch). Then pos += v. */
static void move_toward(int *v, int *pos, int target, int max, int acc)
{
    int p = (short)(*pos >> 16);
    if (target == p && abs(*v) <= acc) *v = 0;
    else if (target >= p) { if (*v < max) *v += *v < 0 ? 2 * acc : acc; }
    else if (-*v < max) *v -= *v > 0 ? 2 * acc : acc;
    *pos += *v;
}

/* 0x1D448: v toward 0 by d */
static int brake(int v, int d) { return v > 0 ? (v > d ? v - d : 0) : v < 0 ? (v < -d ? v + d : 0) : 0; }

/* from enemy_stomped: stunned (0x236B0), or dies if it already was */
void meta_stomped(BYTE *a, int i, int knock)
{
    if (g_enemy[i].timer <= 0 && !knock) Sound_PlayEffect(a, SND_HIT);   /* (an attack: dummy_stomped plays it) */
    dummy_stomped(a, i, knock);
    if (!g_enemy[i].dead) g_meta[i].fly = g_meta[i].chase = 0;
}

/* 0x236D0: chase or bob */
static void meta_fly(BYTE *a, Meta *m)
{
    int dx = A_X(a) - *G_PLAYERX, dy = abs(A_Y(a) + AIM_DY - *G_PLAYERY), close;
    if (m->chase) {
        if (dx >= 0) A_FLAGS(a) |= AF_LEFT; else A_FLAGS(a) &= ~AF_LEFT;
        close = abs(dx) < KEEP_DX && dy < KEEP_DY;
    } else
        close = abs(dx) < START_DX && dy < START_DY;
    if (close) {
        if (!m->chase) flog("meta dummy at (%d,%d) chases Lomax", A_X(a), A_Y(a));
        m->chase = 1;
        move_toward(&A_VX(a), &A_FX(a), *G_PLAYERX, CHASE_MAX, CHASE_ACCEL);
        move_toward(&A_VY(a), &A_FY(a), *G_PLAYERY - AIM_DY, CHASE_MAX, CHASE_ACCEL);
    } else {
        if (!(++m->bob & 3)) A_FY(a) += m->bob & 0x10 ? -0x10000 : 0x10000;
        m->chase = 0;
        A_VX(a) = brake(A_VX(a), BRAKE);
        A_FX(a) += A_VX(a);
        A_VY(a) = brake(A_VY(a), BRAKE);
        A_FY(a) += A_VY(a);
    }
    set_pos(a);
}

static void __cdecl meta_update(BYTE *a)
{
    int i = enemy_slot(a);
    Meta *m;
    if (i < 0 || !g_enemy[i].meta) return;
    m = &g_meta[i];
    if (g_enemy[i].dead) { enemy_dead_update(a, i); return; }
    switch (enemy_attacked(a, i)) {
    case 2: enemy_die(a, i, 1); return;      /* Lomax's roll: Flink's slide */
    case 1: meta_stomped(a, i, 1);
            if (g_enemy[i].dead) return;
            stun_frame(a, i); Actor_AdvanceAnim(a); return;
    }
    if (g_enemy[i].timer > 0) {              /* stunned (0x1D074) */
        fall_and_land(a, 0x1800, 1);
        if (--g_enemy[i].timer > 0) stun_frame(a, i);
        Actor_AdvanceAnim(a);
        return;
    }
    if (!m->fly) {                           /* up again (0x2367C) */
        m->fly = 1;
        g_enemy[i].hop = 0;
        set_anim(a, "FLY ");
        A_VX(a) = 0;
        A_VY(a) = RISE_VY;
    }
    meta_fly(a, m);
    Actor_AdvanceAnim(a);
}

/* (from enemy_spawn) 0x23618 */
void meta_spawn(BYTE *a, int i)
{
    Meta *m = &g_meta[i];
    g_enemy[i].meta = 1;
    m->fly = 1;
    m->chase = 0;
    m->bob = meta_rand();
    A_LAYER(a) = FLINK_LAYER(1);
    A_UPDATE(a) = (void *)meta_update;
    Actor_SetAnim(a, FOURCC("FLY "));
}
