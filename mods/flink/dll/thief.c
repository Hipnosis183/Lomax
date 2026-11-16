/*
 * thief.c - the Thief (Flink 0x2F6CE, think 0x2F718), FOREST and the other worlds (README
 * "Enemies"). The converter makes it a 'Muta Thief n' type with MAIN.WLD's records STAY, GASP, WALK,
 * JUMP, SL45 and DEAD (see the converter's ENEMIES); params: facing left, its hand's offset (WALK's
 * attach point with flags 1). Flink's behaviour:
 * - Waiting (STAY) until Flink is within 0x60 px and 0x20 px up or down; then it faces him and
 *   gasps (GASP, 20 frames) and chases him: Actor_Walk (0x1D0C4, jump_walk in holder.c) with flags
 *   0x800 at 1.5 px a frame (it jumps gaps and turns at ledges). Facing away from him it goes on for
 *   30 frames and then stops; more than 0x80 px from him it stops at once; stopped, it waits again.
 * - Touching Flink while it chases (Actor_OverlapsPlayer 0x1A41C): it takes one ingredient he carries
 *   (Thief_PickIngredient 0x2F8AC: a random one of 13, the first carried in ThiefStealOrder from
 *   there) and bumps him (0x1A71A: 2.0 px the way it faces and 4.0 px up, sound 0x33, no harm). It
 *   turns and runs away for good: flags 0 (it jumps off ledges) at 1.25 px a frame, its state word
 *   now hiding the empty arm (selector 3) instead of the arm with the loot (4), and the loot in its
 *   hand (hook 1). With nothing to take, touching it hurts, as it does otherwise.
 * - One stomp, Flink's slide or a spell kills it (Enemy_Die 0x1B12E); the loot drops (Item_Spawn).
 * Here it steals Lomax's coins instead: up to 10 at once (all he has if fewer; with none it hurts, as
 * in Flink). A Lomax coin is held in its hand. Killed while holding them, it throws them all out
 * (as Lomax's own thief, Enemy_Thief_UpdateStealCoins 0x424A30, throws a stolen coin), and the
 * usual enemy coin (enemy_defeated). The steal goes through Lomax's contact (contact.c, enemy_touched):
 * where the touch would hurt Lomax, it steals. For 20 frames after it, it doesn't hurt.
 */
#include "enemy.h"

#define THIEF_RUN          0x18000       /* chasing, 1.5 px a frame */
#define THIEF_FLEE         0x14000       /* with the loot, 1.25 */
#define THIEF_SEE_X        0x60
#define THIEF_SEE_Y        0x20
#define THIEF_LOSE_X       0x80
#define THIEF_GASP         20
#define THIEF_TURNED       30
#define THIEF_MAX          10            /* coins taken at once */
#define THIEF_GRACE        20            /* frames without hurting after a steal */
/* hidden selectors: init 0x8800 hides 4 (the arm with the loot), the steal 0x2F7F2 clears bit 11 and
   sets bit 10 (3, the empty arm) */
#define HIDE_EMPTY         (1 << 4)
#define HIDE_LOOT          (1 << 3)
/* Lomax's thief throws its stolen coins with these {vx, vy} (vy - 0.5 px), one after another */
#define THIEF_COIN_DIRS    ((const int *)0x004645E8u)

enum { WAITING, GASPING, CHASING, FLEEING };

/* the hand's coin's owner, by the coin's actor slot */
static BYTE *g_hand_owner[MAX_ACTORS];

static void __cdecl hand_coin_draw(BYTE *c) { draw_actor(c); }

/* a coin held in the hand: never picked up, removed with its thief */
static void __cdecl hand_coin_update(BYTE *c)
{
    int s = slot_of(c), i;
    BYTE *a = s >= 0 ? g_hand_owner[s] : NULL;
    i = a ? enemy_slot(a) : -1;
    if (i < 0 || !g_enemy[i].thief || g_enemy[i].coin != c || g_enemy[i].dead || A_LAYER(a) == KILLED_LAYER) {
        if (i >= 0 && g_enemy[i].coin == c) g_enemy[i].coin = NULL;
        if (s >= 0) g_hand_owner[s] = NULL;
        Actor_Kill(c);
        return;
    }
    A_X(c) = A_X(a) + (A_FLAGS(a) & AF_LEFT ? -g_enemy[i].mdx : g_enemy[i].mdx);
    A_Y(c) = A_Y(a) + g_enemy[i].mdy + 2;                   /* (the coin in the hand) */
    A_FX(c) = A_X(c) << 16;
    A_FY(c) = A_Y(c) << 16;
    A_VX(c) = A_VY(c) = 0;
    A_FLAGS(c) = (A_FLAGS(c) & ~AF_HIDDEN) | (A_FLAGS(a) & AF_HIDDEN);
    Actor_AdvanceAnim(c);
}

static void hold_coin(BYTE *a, int i)
{
    BYTE *c = Item_SpawnCoin(a);
    int s = c ? slot_of(c) : -1;
    if (s < 0) return;
    g_hand_owner[s] = a;
    g_enemy[i].coin = c;
    A_DEF(c) = NULL;                         /* (not the thief's placement) */
    A_UPDATE(c) = (void *)hand_coin_update;
    A_DRAW(c) = (void *)hand_coin_draw;
    A_CULL(c) = CULL_NEVER;
    A_LAYER(c) = A_LAYER(a);
    hand_coin_update(c);
}

/* (from enemy_touched, Lomax's contact) steals instead of hurting: 1 if it did */
int thief_touched(BYTE *a, int i)
{
    int n = *G_COINS < THIEF_MAX ? *G_COINS : THIEF_MAX, left = A_FLAGS(a) & AF_LEFT;
    if (g_enemy[i].state != CHASING || n <= 0) return 0;
    *G_COINS -= n;
    g_enemy[i].stack = n;
    *G_PLAYERVX = left ? -0x20000 : 0x20000; /* bumped the way it faces, and up (0x1A71A) */
    *G_PLAYERVY = -0x40000;
    *G_PLAYERSTATE |= 3;
    *G_JUMPHOLDFORCE = 0;
    *G_JUMPTAKEOFFTIMER = 0;
    *G_BAIRSPIN = 0;
    flink_sound(a, 0x33);
    A_FLAGS(a) ^= AF_LEFT;                   /* turns and runs */
    A_VX(a) = 0;
    A_HIDE(a) = HIDE_LOOT;
    g_enemy[i].state = FLEEING;
    g_enemy[i].turning = 0;
    g_enemy[i].timer = THIEF_GRACE;
    hold_coin(a, i);
    flog("thief at (%d,%d) steals %d coins", A_X(a), A_Y(a), n);
    return 1;
}

/* killed (stomp or attack): dies as the dummy, throwing out the coins it holds */
void thief_die(BYTE *a, int i, int knock)
{
    int k;
    if (g_enemy[i].dead) return;
    for (k = 0; k < g_enemy[i].stack; k++) {
        BYTE *c = Item_SpawnCoin(a);
        if (!c) break;
        A_VX(c) = THIEF_COIN_DIRS[(k & 7) * 2];
        A_VY(c) = THIEF_COIN_DIRS[(k & 7) * 2 + 1] - 0x8000;
        A_LAYER(c) = 1;
    }
    if (g_enemy[i].stack) flog("thief at (%d,%d) throws out %d coins", A_X(a), A_Y(a), k);
    g_enemy[i].stack = 0;
    enemy_die(a, i, knock);
}

static void __cdecl thief_update(BYTE *a)
{
    int i = enemy_slot(a), dx, fell = 0;
    if (i < 0) return;
    if (g_enemy[i].dead) { enemy_dead_update(a, i); return; }
    if (Actor_CheckHitByPlayer(a)) { thief_die(a, i, 1); return; }
    if (g_enemy[i].timer > 0) g_enemy[i].timer--;
    dx = *G_PLAYERX - A_X(a);
    switch (g_enemy[i].state) {
    case WAITING:                            /* 0x2F742 */
        set_anim(a, "STAY");
        if (abs(dx) <= THIEF_SEE_X && abs(*G_PLAYERY - A_Y(a)) <= THIEF_SEE_Y) {
            if (dx < 0) A_FLAGS(a) |= AF_LEFT; else A_FLAGS(a) &= ~AF_LEFT;
            g_enemy[i].stop = 0;
            g_enemy[i].t = THIEF_GASP;
            g_enemy[i].state = GASPING;
            set_anim(a, "GASP");
        }
        break;
    case GASPING:                            /* 0x2F7A6 */
        if (--g_enemy[i].t < 0) {
            set_anim(a, "WALK");
            g_enemy[i].speed = THIEF_RUN;
            g_enemy[i].state = CHASING;
        }
        break;
    case CHASING:                            /* 0x2F80E */
        fell = jump_walk(a, i, g_enemy[i].speed, 1);
        if (!g_enemy[i].speed) {
            if (!A_VX(a)) { g_enemy[i].state = WAITING; set_anim(a, "STAY"); }
        } else if (g_enemy[i].stop) {
            if (--g_enemy[i].stop == 0) g_enemy[i].speed = 0;
        } else if (!(A_FLAGS(a) & AF_LEFT) != !(dx < 0))
            g_enemy[i].stop = THIEF_TURNED;  /* facing away from him */
        else if (abs(dx) > THIEF_LOSE_X)
            g_enemy[i].speed = 0;
        break;
    default:                                 /* fleeing (0x2F72C) */
        fell = jump_walk(a, i, THIEF_FLEE, 0);
    }
    if (fell) {                              /* out of the map: gone for good, and its coins */
        forget_placement(A_DEF(a));
        g_enemy[i].type = NULL;
        Actor_Kill(a);
        return;
    }
    Actor_AdvanceAnim(a);
}

/* (from enemy_spawn) params 1 and 2: the hand's offset */
void thief_spawn(BYTE *a, int i, const BYTE *def)
{
    g_enemy[i].thief = 1;
    g_enemy[i].state = WAITING;
    if (def && T_NPARAMS(A_TYPE(a)) >= 3) {
        g_enemy[i].mdx = (short)D_PARAM(def, 1);
        g_enemy[i].mdy = (short)D_PARAM(def, 2);
    }
    A_UPDATE(a) = (void *)thief_update;
    A_HIDE(a) = HIDE_EMPTY;
    Actor_SetAnim(a, FOURCC("STAY"));
}
