/*
 * mountain.c - the Mountain dummy (Flink 0x2FE30, think 0x2FE96), MOUNTAIN's short soldier with a
 * shield (18-1 on): a 'Muta Moun n' type with Flink's records STAY, WALK, STUN, DEAD and SHLD (README
 * "Enemies"). Params: facing left. The Forest Dummy's ways (dummy.c: walking at 0.5 with flags 0x1000,
 * stunned 300 frames by a stomp, killed by a second one, picked up while stunned, hurting on touch) and:
 * - Walking (Actor_Walk 0x1D0C4 at its speed +0x48), a timer (+0x4E; 160 frames first, then 100 +
 *   (random & 255): 0x2FFD6) counts down; at 0 its speed becomes 0 and a stand timer (+0x50) is set
 *   to 184. Once it has stopped it stands (STAY) at the animation speed 0x20, one frame every 8 ticks
 *   whatever the record's speed (0x2FFA0), for 184 ticks: STAY's 18 frames (it looks behind it on
 *   frames 9-16), then frames 0-4 again, then it walks on with a new timer. A stomp also starts a new
 *   walk timer (0x2FF7C).
 * - Its shield. A thing that hits it sets its hit flag (+3 bit 6, the hitter at +0x34), and its think
 *   (0x2FEBC) asks: was the hitter Flink's Shot (think 0x32630: a light object thrown straight on, as a
 *   small stone or a stunned dummy) and is it in STAY or WALK? Then, unless it blocked one in the last
 *   12 frames (+0x4C), a shot moving the other way from the way the shield faces (+3 bit 3 against the
 *   shot's; the shield faces the way it walks, and behind it while standing from 80 to 143 ticks, its
 *   look back) is turned back: the shot's vx negated and mirrored, its own vx 0 (it speeds up again),
 *   the 12 frames of +0x4C set. A shot from behind, a heavy thrown object (Actor_ThrownThink 0x323C2),
 *   or any hit while stunned kills it (0x2FFF2).
 * - Dying (0x2FFF2), its shield flies off: a copy of the actor (0x32CD8, +3 cleared: not mirrored)
 *   with SHLD, thrown 3.0 up and 1.5 the way it faced, falling as a dead enemy (0x1B12E), as the City
 *   dummy's lance (city.c's flying_piece). A thrown dummy that breaks (0x325DE: Enemy_Die) drops none.
 * Here Lomax's attacks (spin, roll, the thrown helmet), which Flink doesn't have, are blocked by the
 * shield too: from the side the shield faces (Lomax's x against the dummy's, the
 * look back included) they only clank (moun_blocks, from dummy_update; Lomax's ROCKHITX, as the
 * Statue's). From behind, or while it is stunned, they count as on the Forest Dummy (a stomp, the
 * roll kills). Lomax hurts it only by stomping on it or from behind.
 */
#include "enemy.h"

#define MOUN_FIRST_WALK    0xA0
#define MOUN_SPEED         0x8000
#define MOUN_STAND         0xB8
#define STAND_ANIM_SPEED   0x20          /* 8.8: a frame every 8 ticks */
#define LOOK_FROM          10            /* (184 - stand left) >> 3 in 10..17: it looks back */
#define LOOK_TO            17
#define BLOCK_TICKS        12
#define SHIELD_VX          0x18000
#define SHIELD_VY          0x30000
#define SND_CLANK          38            /* ROCKHITX (statue.c) */

static int moun_rand(void) { static unsigned r = 0x2F6B4A19u; r = r * 1103515245u + 12345u; return (int)(r >> 16) & 0x7FFF; }

/* 0x2FFD6: walks at 0.5 for 100 + (random & 255) frames */
void moun_walk_again(int i)
{
    g_enemy[i].t = 100 + (moun_rand() & 255);
    g_enemy[i].speed = MOUN_SPEED;
}

/* 0x2FF80..0x2FFD2, not stunned: walk, stop, stand */
void moun_walk(BYTE *a, int i)
{
    Enemy *e = &g_enemy[i];
    if (!e->speed && !A_VX(a)) {             /* stopped: STAY, a frame every 8 ticks (0x2FF96) */
        int n;
        set_anim(a, "STAY");
        if (--e->stop == 0) { moun_walk_again(i); return; }   /* (0x2FFAE: no anim step this tick) */
        n = MOUN_STAND - e->stop;            /* ticks stood, this one included */
        hold_frame(a, A_NFRAMES(a) > 0 ? (n * STAND_ANIM_SPEED >> 8) % A_NFRAMES(a) : 0);
        Actor_AdvanceAnim(a);
        return;
    }
    enemy_walk(a, i);                        /* (speeds up to, or slows down from, e->speed) */
    if (--e->t == 0) {                       /* time to stand (0x2FFC0) */
        e->speed = 0;
        e->stop = MOUN_STAND;
    }
    Actor_AdvanceAnim(a);
}

/* the block's countdown (0x2FF42), every tick it isn't stunned */
void moun_tick(int i)
{
    if (g_enemy[i].block > 0) g_enemy[i].block--;
}

/* the way its shield faces (1 left): the way it walks, behind it while it looks back (0x2FEE2);
   -1: no shield up (not STAY or WALK: 0x2FF0E) */
static int shield_left(BYTE *t, int i)
{
    const BYTE *rec = A_RECORD(t);
    DWORD tag = rec ? *(const DWORD *)(rec + 4) : 0;
    int left = (A_FLAGS(t) & AF_LEFT) != 0;
    if (tag == FOURCC("STAY")) {             /* looking back: the shield faces behind it */
        int d = (MOUN_STAND - g_enemy[i].stop) >> 3;
        if (d >= LOOK_FROM && d <= LOOK_TO) left = !left;
    } else if (tag != FOURCC("WALK")) return -1;
    return left;
}

/* Lomax's attack hit it (dummy_update, enemy_attacked): 1 = the shield took it (he is on the side it
   faces), a clank */
int moun_blocks(BYTE *a, int i)
{
    int left;
    if (!g_enemy[i].moun || g_enemy[i].timer > 0 || (left = shield_left(a, i)) < 0) return 0;
    if ((*G_PLAYERX < A_X(a)) != left) return 0;   /* from behind */
    Sound_PlayEffect(a, SND_CLANK);
    flog("mountain dummy at (%d,%d) blocks Lomax's attack", A_X(a), A_Y(a));
    return 1;
}

/* A thrown object o touched it (grab.c's strike, before enemy_struck): Flink's 0x2FEBC. shot: o is
   Flink's Shot. 1 = the shield took it (turned back, or within the 12 frames after a block); 0 = it
   is hit (enemy_struck kills it). */
int moun_shield(BYTE *t, BYTE *o, int shot)
{
    int i = t ? enemy_slot(t) : -1, left;
    if (i < 0 || !g_enemy[i].moun || g_enemy[i].dead || g_enemy[i].timer > 0 || !shot) return 0;
    if ((left = shield_left(t, i)) < 0) return 0;
    if (g_enemy[i].block) return 1;          /* (0x2FF18: ignored) */
    if (((A_FLAGS(o) & AF_LEFT) != 0) == left) return 0;   /* from behind */
    g_enemy[i].block = BLOCK_TICKS;
    A_VX(t) = 0;
    A_VX(o) = -A_VX(o);
    A_FLAGS(o) ^= AF_LEFT;
    flog("mountain dummy at (%d,%d) blocks a shot", A_X(t), A_Y(t));
    return 1;
}

/* 0x2FFF2: its shield flies off as it dies */
void moun_shield_off(BYTE *a)
{
    flying_piece(a, FOURCC("SHLD"));
}

void moun_spawn(BYTE *a, int i)
{
    (void)a;
    g_enemy[i].moun = 1;
    g_enemy[i].block = 0;
    moun_walk_again(i);
    g_enemy[i].t = MOUN_FIRST_WALK;
}
