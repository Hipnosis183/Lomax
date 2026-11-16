/*
 * city.c - the City dummy (Flink 0x2FBD6, think 0x2FC30), VILLAGE's soldier with a lance (9-1 on):
 * a 'Muta City n' type with Flink's records STAY, WALK, ATCK, STUN, SL45, DEAD and LANS (README
 * "Enemies"). Params: facing left, then its lance tip (ATCK's second box: left, top, right, bottom
 * from its feet, mirrored when it faces left). The Forest Dummy's ways (dummy.c) and:
 * - Walking (Actor_Walk 0x1D0C4 at its speed +0x48, flags 0x1000, as the Forest Dummy): a timer
 *   (+0x4C; 160 frames first, then 100 + (random & 255): 0x2FDBE) counts down; at 0 its speed
 *   becomes 0, and once it has stopped it stands (STAY, 8 frames, played once), looking back on its
 *   6th frame (frame 5), then walks on with a new timer.
 * - It sees Lomax (0x2FD62) within 96 px across and 80 px up or down on the side it faces (walking,
 *   or on STAY's frame 5 the side behind it, and then turns to him): it charges (ATCK) at 1.5 px a
 *   frame. Charging, with Lomax behind it it goes on for 30 frames (+0x4E) and then walks; with him
 *   more than 128 px ahead it walks at once. Its lance tip touching Lomax (0x1A41C, not while he is
 *   hurt) hurts him (0x1A7DC), and it turns round and walks.
 * - Stomped it is stunned as the Forest Dummy (and walks, not charges, after: 0x2FC84), and has no
 *   lance in its STUN frames. Stomped again while stunned or hit by a thrown object (0x2FDE2) it
 *   dies, and its lance flies off (city_lance): a copy of the actor (0x32CD8) with LANS, thrown 3.0
 *   up and 1.5 the way it faced, falling as a dead enemy (0x1B12E), not mirrored (the copy's +3 is
 *   cleared). A thrown dummy that breaks (0x325DE) drops none: enemy_grab_break clears city first.
 */
#include "enemy.h"

#define CITY_FIRST_WALK    160
#define CITY_SPEED         0x8000
#define CHARGE_SPEED       0x18000
#define LOOK_FRAME         5
#define SEE_DX             0x60
#define SEE_DY             0x50
#define CHARGE_LOSE_DX     0x80
#define CHARGE_ON          30
#define LANCE_VX           0x18000
#define LANCE_VY           0x30000
#define LANCE_TICKS        150

static int city_rand(void) { static unsigned r = 0x1B873593u; r = r * 1103515245u + 12345u; return (int)(r >> 16) & 0x7FFF; }

/* 0x2FDBE: walks at 0.5 for 100 + (random & 255) frames */
void city_walk_again(int i)
{
    g_enemy[i].t = 100 + (city_rand() & 255);
    g_enemy[i].speed = CITY_SPEED;
    g_enemy[i].charge = 0;
}

/* 0x2FD62: Lomax within 96 x 80 px on side left (1) or right (0): turn to him and charge */
static void city_look(BYTE *a, int i, int left)
{
    int dx = *G_PLAYERX - A_X(a), dy = *G_PLAYERY - A_Y(a), side = dx < 0;
    if (abs(dy) > SEE_DY || abs(dx) > SEE_DX || side != left) return;
    if (side) A_FLAGS(a) |= AF_LEFT; else A_FLAGS(a) &= ~AF_LEFT;
    g_enemy[i].charge = 1;
    g_enemy[i].speed = CHARGE_SPEED;
    g_enemy[i].stop = 0;
    flog("city dummy at (%d,%d) charges", A_X(a), A_Y(a));
}

/* 0x1A41C: the lance tip against Lomax's hitbox (inclusive), unless he is hurt */
static int lance_touches(const BYTE *a, int i)
{
    const BYTE *pa = *G_PLAYERANIM;
    const int *b = g_enemy[i].lance;
    int x1, x2, y1 = A_Y(a) + b[1], y2 = A_Y(a) + b[3];
    if (!pa || b[0] == b[2] || A_PLANE(a) != *G_PLAYERPLANE || *G_PLAYERHURTTIMER || *G_PLAYERDEATHTIMER) return 0;
    if (A_FLAGS(a) & AF_LEFT) { x1 = A_X(a) - b[2]; x2 = A_X(a) - b[0]; }
    else { x1 = A_X(a) + b[0]; x2 = A_X(a) + b[2]; }
    return x1 <= *G_PLAYERX + HB_X2(pa) && *G_PLAYERX + HB_X1(pa) <= x2 &&
           y1 <= *G_PLAYERY + HB_Y2(pa) && *G_PLAYERY + HB_Y1(pa) <= y2;
}

/* 0x2FC88..0x2FDBA, not stunned: stand, walk or charge */
void city_walk(BYTE *a, int i)
{
    Enemy *e = &g_enemy[i];
    int left = (A_FLAGS(a) & AF_LEFT) != 0, f;
    if (!e->speed && !A_VX(a)) {             /* stopped: STAY once, looking back on frame 5 */
        set_anim(a, "STAY");
        if (A_FRAME(a) == LOOK_FRAME) { city_look(a, i, !left); Actor_AdvanceAnim(a); return; }
        f = A_FRAME(a);
        Actor_AdvanceAnim(a);
        if (A_FRAME(a) < f) city_walk_again(i);   /* played through */
        return;
    }
    enemy_walk(a, i);                        /* (ATCK or WALK, or SL45 sliding) */
    if (e->charge) {
        if (e->stop) {                       /* Lomax got behind it: on for 30 frames */
            if (!--e->stop) { city_walk_again(i); Actor_AdvanceAnim(a); return; }
        } else {
            int dx = *G_PLAYERX - A_X(a), side = dx < 0;
            if (side != left) e->stop = CHARGE_ON;
            else if (abs(dx) > CHARGE_LOSE_DX) { city_walk_again(i); Actor_AdvanceAnim(a); return; }
        }
        if (lance_touches(a, i)) {
            Player_TakeHit(A_X(a), 0, 0);
            A_FLAGS(a) ^= AF_LEFT;
            e->turning = 0;
            city_walk_again(i);
            flog("city dummy at (%d,%d) lances Lomax", A_X(a), A_Y(a));
        }
    } else {
        if (--e->t == 0) e->speed = 0;       /* time to stand */
        city_look(a, i, left);
    }
    Actor_AdvanceAnim(a);
}

/* the flying lance: Enemy_Die's fall (0x1B1AA: 0.25 a frame, at most 4.0, vx braked by 0x2AA) */
static void __cdecl lance_update(BYTE *d)
{
    fall_and_land(d, 0x2AA, 0);
    Actor_AdvanceAnim(d);
    if (++*(int *)(d + 0x80) > LANCE_TICKS || below_map(d)) Actor_Despawn(d);
}

static void __cdecl lance_draw(BYTE *d) { draw_actor(d); }

/* 0x2FDE2: the lance flies off as it dies */
void city_lance(BYTE *a) { flying_piece(a, FOURCC("LANS")); }

/* a copy of the dying actor (0x32CD8, +3 cleared: not mirrored) with record tag, thrown 3.0 up and
   1.5 the way it faced, falling as a dead enemy: the City dummy's lance, the Mountain dummy's shield
   (mountain.c, 0x2FFF2) */
void flying_piece(BYTE *a, DWORD tag)
{
    BYTE *d = Actor_Alloc(a, NULL);
    int k;
    if (!d || (k = slot_of(d)) < 0) return;
    g_enemy[k].type = NULL;
    A_UPDATE(d) = (void *)lance_update;
    A_DRAW(d) = (void *)lance_draw;
    A_CULL(d) = CULL_NEVER;
    A_FLAGS(d) = 0;
    A_LAYER(d) = A_LAYER(a);
    A_X(d) = A_X(a); A_Y(d) = A_Y(a);
    A_FX(d) = A_X(a) << 16; A_FY(d) = A_Y(a) << 16;
    A_VX(d) = A_FLAGS(a) & AF_LEFT ? -LANCE_VX : LANCE_VX;
    A_VY(d) = -LANCE_VY;
    *(int *)(d + 0x80) = 0;
    Actor_SetAnim(d, tag);
    Actor_AdvanceAnim(d);
}

void city_spawn(BYTE *a, int i, const BYTE *def)
{
    int n;
    g_enemy[i].city = 1;
    city_walk_again(i);
    g_enemy[i].t = CITY_FIRST_WALK;
    for (n = 0; n < 4; n++) g_enemy[i].lance[n] = (short)def_param(a, def, 1 + n, 0);
}
