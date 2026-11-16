/*
 * dummy.c - the Forest Dummy (Flink 0x2F948, think 0x2F9A0), the basic enemy of FOREST: a 'Muta
 * Dummy n' type with Flink's records WALK, STUN, SL45 and DEAD (README "Enemies"). Flink's behaviour:
 * - Walking (enemy_walk, flags 0x1000, speed 0.5). Touching it hurts.
 * - Stomped (Actor_CheckStompedByPlayer 0x1A352): stunned for 300 frames (STUN), popped up at 3.0
 *   px a frame and falling back; harmless meanwhile. Then it hops up again at 3.0 and walks on.
 * - Stomped while stunned, or hit while walking by a slide or a spell: dies (enemy_die): DEAD, thrown
 *   up at 4.0 px, falls through everything at 0.25 px a frame (at most 4.0) until off the screen.
 * Here the stomp is Lomax's (enemy.c). His attacks (spin, tumble, helmet) count as a stomp, as
 * Lomax's zombies take two hits (enemy_attacked): the first stuns it, a second stomp or attack kills.
 * - Stunned, it can be picked up and thrown (grab.c, enemy_grab_kind): its stun timer is the fuse.
 * dummy_update also runs the City dummy (city.c), which walks, stands and charges its own way, and
 * the Mountain dummy (mountain.c), which walks and stands its own way and has a shield, and the
 * Intree dummy (intree.c), which jumps and glides.
 */
#include "enemy.h"

#define STUN_TICKS         300

/* Stomped (from enemy_stomped) or attacked (knock: thrown away from Lomax if it dies): stunned, or
   dies if it already was */
void dummy_stomped(BYTE *a, int i, int knock)
{
    if (g_enemy[i].timer > 0) { enemy_die(a, i, knock); return; }
    if (knock) Sound_PlayEffect(a, SND_HIT);   /* an attack: the death's hit sound (a stomp has
                                                  Lomax's bounce sound) */
    g_enemy[i].timer = STUN_TICKS;
    g_enemy[i].hop = 1;
    g_enemy[i].turning = g_enemy[i].slide = 0;
    A_VX(a) = 0;
    A_VY(a) = -0x30000;
    g_enemy[i].own = 0;
    if (g_enemy[i].city) city_walk_again(i);  /* (0x2FC84: walks, not charges, after) */
    if (g_enemy[i].moun) moun_walk_again(i);  /* (0x2FF7C: a new walk timer) */
    if (g_enemy[i].intr) g_enemy[i].air = g_enemy[i].glide = 0;   /* (lands from the stun's hop) */
    set_anim(a, "STUN");
    flog("enemy at (%d,%d) stunned", A_X(a), A_Y(a));
}

/* The stun effect (the converter's STUN_EFFECTS): records STUN (the bubbles, while 120 or more
   frames of the stun are left) and WAKE (the wake-up lines), with a frame for each (own frame,
   effect frame) pair. The own frames step at the record's speed, the effect's at Flink's Shot
   record's (0x332DC); Actor_AdvanceAnim then steps by 0. */
void stun_frame(BYTE *a, int i)
{
    static const struct { const char *tag; int frames, speed; } FX[2] = { { "STUN", 4, 35 }, { "WAKE", 2, 42 } };
    int w = g_enemy[i].timer < 120, own, idx;
    const BYTE *anim;
    set_anim(a, FX[w].tag);
    anim = A_RECORD(a);
    own = A_NFRAMES(a) / FX[w].frames;                  /* the record's frames / effect frames */
    if (!anim || own < 1) return;
    g_enemy[i].own += *(const short *)(anim + 0xC);     /* the record's speed */
    g_enemy[i].fx[w] += FX[w].speed;
    idx = (g_enemy[i].own >> 8) % own * FX[w].frames + (g_enemy[i].fx[w] >> 8) % FX[w].frames;
    hold_frame(a, idx);
}

void __cdecl dummy_update(BYTE *a)
{
    int i = enemy_slot(a);
    if (i < 0) return;
    if (g_enemy[i].dead) { enemy_dead_update(a, i); return; }
    switch (enemy_attacked(a, i)) {
    case 2: if (moun_blocks(a, i)) break;    /* (the Mountain dummy's shield: mountain.c) */
            enemy_die(a, i, 1); return;      /* rolling down a slope: Flink's slide */
    case 1: if (moun_blocks(a, i)) break;
            dummy_stomped(a, i, 1); if (g_enemy[i].dead) return;
    }
    if (g_enemy[i].moun && g_enemy[i].timer <= 0) moun_tick(i);   /* (its shield's 12 frames) */
    if (g_enemy[i].timer > 0) {              /* stunned (0x1D074) */
        fall_and_land(a, 0x1800, 1);
        if (--g_enemy[i].timer > 0) stun_frame(a, i);
        else A_VY(a) = -0x30000;             /* hops up, and walks on when it lands */
    } else if (g_enemy[i].hop) {
        set_anim(a, "WALK");
        if (fall_and_land(a, 0x1800, 1)) g_enemy[i].hop = 0;
    } else if (g_enemy[i].city) {
        city_walk(a, i);                     /* (steps its own animation) */
        return;
    } else if (g_enemy[i].moun) {
        moun_walk(a, i);                     /* (steps its own animation) */
        return;
    } else if (g_enemy[i].intr) {
        intree_think(a, i);                  /* (steps its own animation; may remove it) */
        return;
    } else
        enemy_walk(a, i);
    Actor_AdvanceAnim(a);
}
