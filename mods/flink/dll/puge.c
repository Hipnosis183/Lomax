/*
 * puge.c - Flink's Puge, 'Puge' (Dutch "spugen", to spit; Flink init 0x24F40, think 0x24FD2,
 * draw 0x25108, its rocks 0x25138), FOREST 5-3 (map 15) and VILLAGE 9-2 (map 19) (README "Enemies").
 * The tree and its face are the map's own background cells; the actor is only the open mouth (PUGE,
 * one frame) drawn over the face, and the rocks it spits. The converter makes it a 'Muta Puge n' type
 * with PUGE and MAIN.WLD's clods ERDE; params: 'look left', 'time', and the mouth's offset (PUGE's
 * attach point with hook 1, which the type's table 0x251B6 -> 0x251A4 stores in +0x4A / +0x4C).
 * Flink's behaviour:
 * - Shut and open. +3 bit 1 (hidden) is set from the init: the mouth is shut (not drawn) for 31
 *   frames (+0x48 = 30, counted down below 0), then open for 151 (+0x48 = 150), then shut for 'time'
 *   + 1 (25 or 50 in the maps), and so on. While shut nothing of it is drawn, its rocks neither.
 * - Spitting. 30 frames after it opens (+0x48 = 120) it spits two groups of 5 rocks (Fx_SpawnDebris'
 *   particles 0x2E0C8 in two child slots +0x50 / +0x52, tables 0x250A4 and 0x250D6: a delay, x/y
 *   offsets from the mouth, vx and vy << 12, y up), with sound 0x31 (PCM 27) when it is on screen.
 *   The rocks are MAIN.WLD's clods (ERDE frames 0 and 1 in turn, the sprites at 0x8546).
 * - The rocks (0x2E33C, run from the draw while +0x48 <= 118): each counts up from minus its delay
 *   and moves once it reaches 0, gravity 0x2000 a frame (at most 4.0 down), vx braked by 0x400 a frame;
 *   0x25138 then holds its count at 20, so it never runs out as a chest's clod does (60 frames) and
 *   flies on until the mouth shuts. Each one bounces once on the line 8 px over the ground under the
 *   tree (Map_GetGroundHeight 0x1AEEA from the placement, 20 cells, - 8: +0x4E), its vy negated and
 *   halved, then falls on through the ground. A rock hurts: its 4 x 4 box (x .. x + 4, inclusive)
 *   against the player's hitbox (0x1A46E, then Player_TakeHit unless the player is hurt already).
 *   Flink tests one rock of each group a frame (the one whose index is the main CPU's 0x8712); here
 *   every rock every tick.
 * - Nothing else touches it: it doesn't hurt, isn't stomped and can't be killed (no hit checks), and
 *   Flink despawns it when it leaves the activation window (here Lomax's culling, cull phase 1).
 * Here the rocks move in the update (Lomax+'s FPS Override may draw several times a tick) at the
 * same point of the frame as Flink's draw (after the mouth's count), and are drawn with
 * Sprite_QueueWorld one bucket over the mouth, as the Firemonster's flames. Lomax has no spitting
 * sound: the debris sound (SND_DEBRIS) for Flink's 0x31.
 */
#include "enemy.h"

#define PUGE_ROCKS     10                /* two groups of 5 */
#define PUGE_FIRST     30                /* shut at first (0x24F7C) */
#define PUGE_OPEN      150               /* open (0x2509C) */
#define PUGE_SPIT      120               /* the spit, 30 frames after it opens (0x25022) */
#define PUGE_ROCKS_AT  118               /* the rocks move and show from there (0x25108) */
#define PUGE_HOLD      20                /* each rock's count is held there (0x25154) */
#define PUGE_LIFE      60                /* 0x2E35A: a count over it stops the rock (never reached) */
#define PUGE_GROUND    20                /* cells searched for the ground under it (0x24FA0) */
#define PUGE_LINE      8                 /* the bounce line over that ground (0x24FA8) */
#define ROCK_BOX       4                 /* its hurting box, x .. x + 4 (0x2516A) */
#define ROCK_GRAVITY   0x2000            /* 0x2E368 */
#define ROCK_MAXFALL   0x40000           /* (Actor_FallAndLand's cap, 0x1B0E6) */
#define ROCK_FRICTION  0x400             /* 0x2E36E */

typedef struct { int count, fx, fy, vx, vy, bounced; } Rock;
typedef struct {
    int open, t, every, mx, my, line;    /* shut or open, +0x48, 'time', the mouth, the bounce line */
    Rock rock[PUGE_ROCKS];
} Puge;
static Puge g_puge[MAX_ACTORS];

void puges_reset(void) { memset(g_puge, 0, sizeof(g_puge)); }

/* 0x250A4 and 0x250D6: delay, dx, dy, vx, vy (<< 12, y up) of each rock */
static const short SPIT[PUGE_ROCKS][5] = {
    { 0, -5, 1, -12, 24 }, { -18, -3, 1, -4, 12 }, { -13, 2, 0, 8, 16 }, { -5, 0, -2, -10, 20 }, { -8, 7, -1, 4, 16 },
    { 20, -5, 1, -6, 12 }, { -12, -3, 1, -10, 20 }, { -25, 2, 0, 10, 18 }, { -17, 0, -2, -4, 24 }, { -22, 7, -1, 12, 16 },
};

static int overlap_in(int ax1, int ay1, int ax2, int ay2, int bx1, int by1, int bx2, int by2)
{
    return ax1 <= bx2 && bx1 <= ax2 && ay1 <= by2 && by1 <= ay2;   /* (inclusive, as 0x1A46E) */
}

static int player_box(int *x1, int *y1, int *x2, int *y2)
{
    const BYTE *pa = *G_PLAYERANIM;
    if (!pa) return 0;
    *x1 = *G_PLAYERX + HB_X1(pa); *x2 = *G_PLAYERX + HB_X2(pa);
    *y1 = *G_PLAYERY + HB_Y1(pa); *y2 = *G_PLAYERY + HB_Y2(pa);
    return 1;
}

/* Fx_SpawnDebris (0x2E0C8) twice, at the mouth */
static void puge_spit(BYTE *a, Puge *p)
{
    int k;
    for (k = 0; k < PUGE_ROCKS; k++) {
        Rock *r = &p->rock[k];
        r->count = SPIT[k][0];
        r->fx = (p->mx + SPIT[k][1]) << 16;
        r->fy = (p->my + SPIT[k][2]) << 16;
        r->vx = SPIT[k][3] * 0x1000;
        r->vy = -SPIT[k][4] * 0x1000;
        r->bounced = 0;
    }
    flink_sound(a, 0x31);
    flog("tree at (%d,%d) spits", A_X(a), A_Y(a));
}

/* 0x2E33C and 0x25138: the rocks move, bounce once and hurt */
static void puge_rocks(BYTE *a, Puge *p)
{
    int px1 = 0, py1 = 0, px2 = 0, py2 = 0, k;
    int can_hurt = A_PLANE(a) == *G_PLAYERPLANE && !*G_PLAYERHURTTIMER && !*G_PLAYERDEATHTIMER &&
                   player_box(&px1, &py1, &px2, &py2);
    for (k = 0; k < PUGE_ROCKS; k++) {
        Rock *r = &p->rock[k];
        int c = ++r->count, x, y;
        if (c < 0) continue;
        if (c <= PUGE_LIFE) {                /* (0x1B0E2: gravity first, then both moves, then friction) */
            r->vy += ROCK_GRAVITY;
            if (r->vy > ROCK_MAXFALL) r->vy = ROCK_MAXFALL;
            r->fy += r->vy;
            r->fx += r->vx;
            r->vx = r->vx > 0 ? (r->vx > ROCK_FRICTION ? r->vx - ROCK_FRICTION : 0)
                              : (r->vx < -ROCK_FRICTION ? r->vx + ROCK_FRICTION : 0);
        }
        r->count = PUGE_HOLD;
        x = r->fx >> 16;
        y = r->fy >> 16;
        if (can_hurt && overlap_in(x, y, x + ROCK_BOX, y + ROCK_BOX, px1, py1, px2, py2)) {
            flog("tree at (%d,%d): a rock at (%d,%d) hurts Lomax", A_X(a), A_Y(a), x, y);
            Player_TakeHit(x + ROCK_BOX / 2, 0, 0);
            can_hurt = 0;
        }
        if (!r->bounced && y >= p->line) {   /* once, at half the speed (0x25180) */
            r->vy = -(r->vy >> 1);
            r->bounced = 1;
        }
    }
}

static void __cdecl puge_update(BYTE *a)
{
    int i = enemy_slot(a);
    Puge *p;
    if (i < 0) return;
    p = &g_puge[i];
    if (!p->open) {                          /* shut (0x2508E) */
        if (--p->t < 0) { p->open = 1; p->t = PUGE_OPEN; }
        return;
    }
    if (p->t == PUGE_SPIT) puge_spit(a, p);
    if (--p->t < 0) { p->open = 0; p->t = p->every; return; }
    if (p->t <= PUGE_ROCKS_AT) puge_rocks(a, p);
    Actor_AdvanceAnim(a);
}

/* one rock: ERDE's frame k & 1 (one piece a frame), one bucket over the mouth */
static void rock_draw(BYTE *a, int k, int x, int y)
{
    const BYTE *e, *bank, *type = A_TYPE(a);
    if (!(e = record_of(a, FOURCC("ERDE")))) return;
    e += (k & 1) * 8;
    if (!(bank = G_ANIMBANKS[e[0] | (T_BANKS(type) & 0x100)])) return;
    Sprite_QueueWorld(PLANE(A_PLANE(a)), bank + e[1] * 0x38 + 0x10, x + *(const short *)(e + 2),
                      y + *(const short *)(e + 4), (unsigned)(Actor_GetDrawDepth(a) + 1) << 24 | (e[6] & 0x18), 20000);
}

static void __cdecl puge_draw(BYTE *a)
{
    int i = enemy_slot(a), k;
    Puge *p;
    if (i < 0) return;
    p = &g_puge[i];
    if (!p->open) return;                    /* (+3 bit 1: hidden) */
    draw_actor(a);
    if (p->t > PUGE_ROCKS_AT) return;
    for (k = 0; k < PUGE_ROCKS; k++)
        if (p->rock[k].count >= 0) rock_draw(a, k, p->rock[k].fx >> 16, p->rock[k].fy >> 16);
}

/* (from enemy_spawn) params: 'look left' (enemy_spawn faces it), 'time', the mouth's offset */
void puge_spawn(BYTE *a, int i, const BYTE *def)
{
    Puge *p = &g_puge[i];
    int g, k;
    memset(p, 0, sizeof(*p));
    g_enemy[i].puge = 1;
    p->t = PUGE_FIRST;
    p->every = (WORD)def_param(a, def, 1, 0);
    p->mx = A_X(a) + (A_FLAGS(a) & AF_LEFT ? -def_param(a, def, 2, 0) : def_param(a, def, 2, 0));
    p->my = A_Y(a) + (short)def_param(a, def, 3, 0);
    g = (int)Plane_FindGroundY(PLANE(A_PLANE(a)), A_X(a), A_Y(a), PUGE_GROUND);
    p->line = g == NO_GROUND ? 0x7FFFFFFF : g - PUGE_LINE;
    for (k = 0; k < PUGE_ROCKS; k++) p->rock[k].count = -0x8000;
    A_FLAGS(a) &= ~AF_CONTACT;               /* the tree doesn't hurt and nothing hits it */
    A_UPDATE(a) = (void *)puge_update;
    A_DRAW(a) = (void *)puge_draw;
    Actor_SetAnim(a, FOURCC("PUGE"));
}
