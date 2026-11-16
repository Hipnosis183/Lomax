/*
 * butterfly.c - Flink's Butterflies (type 38, init 0x247D8, think 0x2488A, each pair's draw
 * 0x2497C; TREE maps 3, 4, 5, 8 = 25-1, 26-1, 27-1, 25-2; README "Butterflies"): decoration, pairs
 * of butterflies flying off from a placement. They touch nothing. Flink's:
 * - Init (the spawner): hidden (+3 bit 1), activation window 0x18 (never culled), +3 bit 7 (its
 *   sprites get the priority bit: in front of the high-priority tiles), y + 20; 'x-speed' (+0x4A,
 *   negated and the sprites flipped, +3 bit 3, without checkbox 1 'Move left'), 'y-speed' (+0x4C),
 *   'how many' (+0x48), the timer (+0x46) 0. It takes the FLY record's five frames' hardware sprites
 *   (0x34132) into 0x8490 .. 0x84A0.
 * - Each frame (0x2488A): the timer runs down; past 0 it is 20 again and 'how many' goes down: past
 *   0 the spawner is freed (Actor_Free 0x32D9E: not forgotten), otherwise a child (0x32CD8, draw list
 *   1, 0x7D6C: in front of Flink; flipped as the spawner) with two butterflies (+0x46, 16 bytes
 *   each), in random order (0x19C8A) per butterfly: x = spawner's x + (rand & 127), y = spawner's
 *   y + (rand & 31), vx = (x-speed + (rand & 31), the random part with x-speed's sign) * -655,
 *   vy = (y-speed + (rand & 31)) * -655 (16.16: 1/100 px a frame, so they fly up, left with
 *   'Move left'); then each butterfly's flap speed, ((rand & 3) + 4) / 16 of a frame a frame
 *   (+0x1C, +0x20; its counter +0x14, +0x18 from 0), and the lifetime 200 (+0x38).
 * - The child's think is empty (0x34130); its draw (0x2497C) does the rest: each counter + its speed,
 *   & 7, picks the sprite by the table 0x24A14 {0, 1, 2, 3, 4, 3, 2, 1}; each butterfly moves by
 *   its speeds and is queued by Sprite_DrawScreen (0x3430E: its top-left at the point, only when it
 *   is inside -32 .. 320 x -32 .. 224 of the screen, exclusive). When neither was queued the lifetime
 *   counts down and the child is freed past 0; when one was it is 60 again.
 * Here: the converter's 'Spik Butterfly' (Lomax's decoration, object 51), records FLY (the five
 * sprites, top-left at the point) and FLYR (the same flipped in place); params how many, x-speed,
 * y-speed, 'Move left'. The spawner is the placement's actor, drawn as nothing and never culled;
 * when it is done it is Actor_Kill'd (its placement stays spawned: back after a lost life, as
 * Actor_Free leaves Flink's). A Generator's group may despawn it earlier, as Flink's watcher does.
 * The pairs are Actor_Alloc children of it, never culled, moved in their update and drawn by two
 * Actor_Draw calls, the actor moved onto each butterfly (Lomax+'s FPS Override follows each call's
 * anchor, two calls every tick); drawn through plane 0 (Flink's priority tiles, a ScrollMap at
 * 100/100 with the play plane's camera) on draw layer FLINK_LAYER(1), so in front of the priority
 * tiles and of Lomax. The on-screen test is Flink's box with Lomax's view (as enemy_on_screen).
 */
#include "flink.h"

#define BF_EVERY        20               /* frames between pairs (0x248A0) */
#define BF_LIFE_FIRST   200              /* (+0x38, 0x24972) */
#define BF_LIFE         60               /* (0x24A0C) */
#define BF_DY           20               /* the spawner's y + 20 (0x24820) */
#define BF_SPEED_UNIT   (-655)           /* muls.w #$FD71 */
#define BF_EDGE         32               /* Flink's box: -32 .. the screen, exclusive (0x34340) */
#define FLINK_VIEW_W    320
#define VIEW_H          240

static const int BF_FRAME[8] = { 0, 1, 2, 3, 4, 3, 2, 1 };   /* 0x24A14 (byte offsets / 4) */

typedef struct {
    BYTE *type;
    void *update;                        /* bf_spawner_update or bf_pair_update: which this slot is */
    int timer, count, vx, vy, left;      /* the spawner: +0x46, +0x48, +0x4A, +0x4C, checkbox 1 */
    int x[2], y[2], dx[2], dy[2];        /* a pair: 16.16 positions and speeds (+0x46 ...) */
    int anim[2], flap[2], life;          /* the flap counters (+0x14, +0x18), speeds (+0x1C, +0x20), +0x38 */
} Butterfly;
static Butterfly g_bf[MAX_ACTORS];

void butterflies_reset(void) { memset(g_bf, 0, sizeof(g_bf)); }

static int bf_rand(void) { static unsigned r = 0x2C1B3C6Du; r = r * 1103515245u + 12345u; return (int)(r >> 16) & 0x7FFF; }

static void __cdecl bf_spawner_update(BYTE *a);
static void __cdecl bf_pair_update(BYTE *a);

static Butterfly *bf_of(const BYTE *a, void *update)
{
    int i = slot_of(a);
    return i >= 0 && g_bf[i].type && g_bf[i].type == A_TYPE(a) && g_bf[i].update == update &&
           A_UPDATE(a) == update ? &g_bf[i] : NULL;
}

/* the butterfly's top-left inside Flink's box, on Lomax's view (Sprite_DrawScreen's test) */
static int bf_on_screen(const BYTE *a, int x, int y)
{
    const BYTE *pl = PLANE(A_PLANE(a));
    int w = P_W(pl) * 16 - P_MAXX(pl), sx = x - P_CAMX(pl), sy = y - P_CAMY(pl);
    if (P_MAXX(pl) <= 0 || w < FLINK_VIEW_W) w = FLINK_VIEW_W;
    return sx > -BF_EDGE && sx < w && sy > -BF_EDGE && sy < VIEW_H;
}

/* the child's draw (0x2497C): each butterfly by its own Actor_Draw, the actor on it */
static void __cdecl bf_pair_draw(BYTE *a)
{
    Butterfly *b = bf_of(a, (void *)bf_pair_update);
    int x = A_X(a), y = A_Y(a), k;
    short plane = A_PLANE(a);
    if (!b || !g_active) return;
    if (P_DEPTH(PLANE(0)) > P_DEPTH(PLANE(plane)) && P_CAMX(PLANE(0)) == P_CAMX(PLANE(plane)) &&
        P_CAMY(PLANE(0)) == P_CAMY(PLANE(plane)))
        A_PLANE(a) = 0;                              /* the priority bit: over plane 0's tiles */
    set_anim(a, b->left ? "FLY " : "FLYR");
    for (k = 0; k < 2; k++) {
        hold_frame(a, BF_FRAME[(b->anim[k] >> 16) & 7]);
        Actor_AdvanceAnim(a);
        A_X(a) = b->x[k] >> 16;
        A_Y(a) = b->y[k] >> 16;
        draw_actor(a);
    }
    A_PLANE(a) = plane;
    A_X(a) = x;
    A_Y(a) = y;
}

/* the motion and lifetime of the child's draw (0x2497C), run as its update */
static void __cdecl bf_pair_update(BYTE *a)
{
    Butterfly *b = bf_of(a, (void *)bf_pair_update);
    int k, seen = 0;
    if (!b || !g_active) return;
    for (k = 0; k < 2; k++) {
        b->anim[k] = (b->anim[k] + b->flap[k]) & 0x7FFFF;            /* andi.w #7 on the whole part */
        b->x[k] += b->dx[k];
        b->y[k] += b->dy[k];
        seen |= bf_on_screen(a, b->x[k] >> 16, b->y[k] >> 16);
    }
    A_X(a) = b->x[0] >> 16;                          /* (the actor stays on the first) */
    A_Y(a) = b->y[0] >> 16;
    if (seen) b->life = BF_LIFE;
    else if (--b->life < 0) { b->type = NULL; Actor_Despawn(a); }
}

/* a pair (0x248A0) */
static void bf_spawn_pair(BYTE *a, Butterfly *s)
{
    BYTE *d = Actor_Alloc(a, NULL);
    Butterfly *b;
    int k, r;
    if (!d || (k = slot_of(d)) < 0) return;
    b = &g_bf[k];
    memset(b, 0, sizeof(*b));
    for (r = 0; r < 2; r++) {
        int v;
        b->x[r] = (A_X(a) + (bf_rand() & 127)) << 16;
        b->y[r] = (A_Y(a) + (bf_rand() & 31)) << 16;
        v = bf_rand() & 31;
        b->dx[r] = (s->vx < 0 ? s->vx - v : s->vx + v) * BF_SPEED_UNIT;
        b->dy[r] = ((bf_rand() & 31) + s->vy) * BF_SPEED_UNIT;
    }
    for (r = 0; r < 2; r++) b->flap[r] = ((bf_rand() & 3) + 4) << 12;
    b->life = BF_LIFE_FIRST;
    b->left = s->left;
    b->type = A_TYPE(d);
    b->update = (void *)bf_pair_update;
    A_FX(d) = b->x[0]; A_FY(d) = b->y[0];
    set_pos(d);
    A_VX(d) = A_VY(d) = 0;
    A_FLAGS(d) = 0;                                  /* touches nothing */
    A_CULL(d) = CULL_NEVER;
    A_LAYER(d) = (short)FLINK_LAYER(1);              /* (a5) = 0x7D6C: list 1 */
    A_UPDATE(d) = (void *)bf_pair_update;
    A_DRAW(d) = (void *)bf_pair_draw;
    A_RECORD(d) = NULL;                              /* (Actor_Alloc leaves the slot's last one) */
    Actor_SetAnim(d, FOURCC(b->left ? "FLY " : "FLYR"));
    if (!A_RECORD(d)) { b->type = NULL; Actor_Despawn(d); }
}

/* the spawner (0x2488A) */
static void __cdecl bf_spawner_update(BYTE *a)
{
    Butterfly *s = bf_of(a, (void *)bf_spawner_update);
    if (!s || !g_active) return;
    if (--s->timer >= 0) return;
    if (--s->count < 0) {
        flog("butterflies at (%d,%d) done", A_X(a), A_Y(a));
        s->type = NULL;
        Actor_Kill(a);
        return;
    }
    s->timer = BF_EVERY;
    bf_spawn_pair(a, s);
}

static void __cdecl bf_spawner_draw(BYTE *a) { (void)a; }   /* hidden (+3 bit 1) */

/* (from spik_spawn) params: how many, x-speed, y-speed, Move left */
void butterfly_spawn(BYTE *a, BYTE *def)
{
    int i = slot_of(a);
    Butterfly *s;
    if (i < 0 || !def || T_NPARAMS(A_TYPE(a)) < 4) return;
    s = &g_bf[i];
    memset(s, 0, sizeof(*s));
    s->count = D_PARAM(def, 0);
    s->vx = (short)D_PARAM(def, 1);
    s->vy = (short)D_PARAM(def, 2);
    s->left = D_PARAM(def, 3) & 1;
    if (!s->left) s->vx = -s->vx;                    /* (0x2480E) */
    s->type = A_TYPE(a);
    s->update = (void *)bf_spawner_update;
    A_FLAGS(a) &= ~(0x200000u | AF_CONTACT);         /* touches nothing */
    A_CULL(a) = CULL_NEVER;                          /* window 0x18 */
    A_UPDATE(a) = (void *)bf_spawner_update;
    A_DRAW(a) = (void *)bf_spawner_draw;
    place_y(a, A_Y(a) + BF_DY);
    flog("butterflies at (%d,%d): %d pairs, speed %d,%d, %s", A_X(a), A_Y(a), s->count, s->vx, s->vy,
         s->left ? "left" : "flipped");
}
