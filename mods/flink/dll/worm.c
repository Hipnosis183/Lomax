/*
 * worm.c - Flink's Worm (init 0x28A1C, think 0x28AE2, draw 0x28C32), TREE's red worm (25-1, 25-2):
 * a 'Muta Worm n' type with Flink's records HEAD, BODY (2 frames each) and DEAD (README "Enemies").
 * Param 0: facing left (Flink's checkbox bit 0 set: right; no placement has it). Flink's behaviour:
 * - Four segments (+0x48: x, y, z words), 16 px apart on the placement's line, the head (segment 0)
 *   in front: facing left at x - 16, x, x + 16, x + 32, facing right at x + 16, x, x - 16, x - 32.
 *   Each segment's y word carries its own direction: negative, it goes left.
 * - A 64-frame timer (+0x46, from 0x40 down, 63 after 0): it moves while the timer's bit 4 is set
 *   (63..48 and 31..16), by the table at 0x28BDE (bit 5 set) or 0x28BFE (bit 5 clear), its second half
 *   on odd frames: per segment 1 px in x (or none) and -1, 0 or 1 px in z, the head and the third
 *   segment rising 8 px and coming down again while the rear and the front catch up. Every segment
 *   goes 16 px each 64 frames, 0.25 px a frame.
 * - A segment that moves looks for ground at its new x (Map_GetGroundHeight 0x1AEEA, from 16 px above
 *   its y, 4 cells down): within 4 px of its y it follows it, otherwise (a ledge, a wall, a step) that
 *   segment turns back. The head turns first and the others follow it one by one, so the worm turns
 *   round with its head in front again.
 * - The actor is between segments 1 and 2 (x their middle, y the middle of their y) with the DEAD
 *   record set (init's end), whose hitbox (-25, -21 .. 24, -1) covers the worm: touching it hurts
 *   (0x1A462); a stomp (Actor_CheckStompedByPlayer 0x1A352) bounces Flink and plays sound 0x18 and
 *   nothing else (it can't be stomped dead).
 * - Hit (+3 bit 6: a thrown object, the slide, a spell) it dies (0x28C1E): mirrored if its head goes
 *   left, then Actor_ForgetForever (0x1B1E8) and the break-up (0x1B1F8): sound 0x2A on the screen,
 *   and each piece of its DEAD frame (the head and three body sprites, in a line) flies on its own
 *   (0x1B276), from its place in the frame (mirrored: x - dx - 24, the ANIM's width), vx (random & 31)
 *   - 15 eighths of a px a frame, vy 1.0 + (random & 31) eighths up, falling 0x4000 a frame (0x1B388)
 *   until none is on the screen.
 * - Drawn (0x28C32): HEAD's frame (timer bit 5) at segment 0, BODY's at the others, flipped left-right
 *   going left, the head first (in front). Each is drawn as its frame's one hardware sprite
 *   (0x34132, Sprite_DrawScreen 0x3430E: its own offset in the ANIM frame not added), its top-left at
 *   (x - 12, y + z - 32), the body's at y + z - 24: 24 px wide, the head 32 tall, the body 24, all
 *   with their bottom on the segment's y. Lomax's sprites are the whole ANIM frame (24 x 32), where the
 *   body's sprite is 8 px down (the converter checks it), so the body's frame goes at y + z - 32.
 * Here Lomax's stomp bounces off it with sound 0x18 (SND_HIT). His spin, roll and thrown helmet kill
 * it when they hit its head (the head sprite's box against his hitbox or the helmet's), and the spin and
 * roll hurt him (Player_TakeHit) when they hit it elsewhere, where the helmet bounces off (it flies
 * back to him as after any hit); a thrown object kills it wherever it hits (enemy_struck).
 * Dead, it counts as defeated, may drop a coin and stays dead for the level visit. Flink's draw hook
 * 0x28C82 removes a Worm that a Generator spawns while the screen shows it (never drawn); here it
 * appears, as every Generator's object (generator.c).
 */
#include "enemy.h"

#define WORM_TIMER       0x40            /* +0x46 */
#define WORM_TURN_DY     4               /* ground farther than this from a segment turns it */
#define WORM_SPR_W       24              /* the ANIM's width */
#define WORM_SPR_DX      12
#define WORM_HEAD_DY     32
#define WORM_BODY_DY     (24 + 8)        /* (its sprite 8 px down in Lomax's whole frame) */
#define WORM_HEAD_H      32
#define WORM_GRAVITY     0x4000
#define WORM_DEAD_TICKS  300
#define VIEW_H           240

/* 0x28BDE (timer bit 5 set) and 0x28BFE: [bit 5 clear / set][frame bit 0][segment] = dx, dz */
static const signed char MOVES[2][2][WORM_SEGS][2] = {
    { { { 1, 1 }, { 1, 0 }, { 1, 1 }, { 0, 0 } },  { { 1, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } } },
    { { { 0, -1 }, { 1, 0 }, { 1, -1 }, { 1, 0 } }, { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 1, 0 } } },
};

static unsigned g_seed = 0x6C1E2D93u;
static int rnd(void) { g_seed = g_seed * 1103515245u + 12345u; return (int)(g_seed >> 16 & 0x7FFF); }

static int overlap(int ax1, int ay1, int ax2, int ay2, int bx1, int by1, int bx2, int by2)
{
    return ax1 < bx2 && bx1 < ax2 && ay1 < by2 && by1 < ay2;
}

/* Map_GetGroundHeight (0x1AEEA): none off the map's sides */
static int seg_ground(const BYTE *a, int x, int y)
{
    if (x < 0 || x >= P_W(PLANE(A_PLANE(a))) * 16) return NO_GROUND;
    return ground_y(a, x, y);
}

/* the segments' step (0x28B26) */
static void worm_move(BYTE *a, Enemy *e)
{
    const signed char (*mv)[2] = MOVES[e->t >> 5 & 1][e->t & 1];
    int k, dx, y, g;
    for (k = 0; k < WORM_SEGS; k++) {
        y = e->wy[k] < 0 ? -e->wy[k] : e->wy[k];
        dx = e->wy[k] < 0 ? -mv[k][0] : mv[k][0];
        e->wx[k] += dx;
        e->wz[k] += mv[k][1];
        if (!dx) continue;
        g = seg_ground(a, e->wx[k], y);
        if (g != NO_GROUND && abs(g - y) <= WORM_TURN_DY) e->wy[k] = e->wy[k] < 0 ? -g : g;
        else e->wy[k] = -e->wy[k];                               /* turns back */
    }
}

/* between segments 1 and 2 (0x28BB2) */
static void worm_place(BYTE *a, const Enemy *e)
{
    A_X(a) = (e->wx[1] + e->wx[2]) / 2;
    A_Y(a) = (abs(e->wy[1]) + abs(e->wy[2])) / 2;
    A_FX(a) = A_X(a) << 16; A_FY(a) = A_Y(a) << 16;
}

/* the thrown helmet's box, if one is out */
static int helmet_box(int *x1, int *y1, int *x2, int *y2)
{
    BYTE *h = thrown_helmet();
    return h && actor_box(h, x1, y1, x2, y2);
}

/* Lomax's attack (from enemy_attacked) hit its head: his box (the helmet's for its hit) */
static int hit_on_head(const BYTE *a, const Enemy *e, int helmet)
{
    const BYTE *pa = *G_PLAYERANIM;
    int x1, y1, x2, y2, hx = e->wx[0] - WORM_SPR_DX, hy = abs(e->wy[0]) + e->wz[0] - WORM_HEAD_DY;
    (void)a;
    if (helmet) { if (!helmet_box(&x1, &y1, &x2, &y2)) return 0; }
    else {
        if (!pa) return 0;
        x1 = *G_PLAYERX + HB_X1(pa); x2 = *G_PLAYERX + HB_X2(pa);
        y1 = *G_PLAYERY + HB_Y1(pa); y2 = *G_PLAYERY + HB_Y2(pa);
    }
    return overlap(x1, y1, x2, y2, hx, hy, hx + WORM_SPR_W, hy + WORM_HEAD_H);
}

/* killed (0x28C1E, the break-up 0x1B1F8): each piece of DEAD's frame flies off on its own */
void worm_kill(BYTE *a, int i)
{
    Enemy *e = &g_enemy[i];
    const BYTE *r = record_of(a, FOURCC("DEAD"));
    int left = e->wy[0] < 0, n = 0;
    BYTE *coin;
    for (; r && n < WORM_SEGS; r += 8) {
        int dx = *(const short *)(r + 2), dy = *(const short *)(r + 4);
        e->wx[n] = (left ? A_X(a) - dx - WORM_SPR_W : A_X(a) + dx) << 16;
        e->wy[n] = (A_Y(a) + dy) << 16;
        e->bvx[n] = ((rnd() & 31) - 15) << 13;
        e->bvy[n] = -(((rnd() & 31) << 13) + 0x10000);
        n++;
        if (r[6] & 0x80) break;
    }
    e->pieces = n;
    e->left = left;
    e->dead = 1;
    e->timer = 0;
    forget_placement(A_DEF(a));                                  /* stays dead (gone.c) */
    A_FLAGS(a) &= ~AF_CONTACT;
    A_CULL(a) = CULL_NEVER;
    flink_sound(a, 0x2A);
    coin = enemy_defeated(a);
    flog("worm at (%d,%d) killed, %d pieces%s", A_X(a), A_Y(a), n, coin ? ", drops a coin" : "");
}

/* the pieces fly (0x1B388) until none is on the screen */
static void worm_pieces(BYTE *a, int i)
{
    Enemy *e = &g_enemy[i];
    const BYTE *pl = PLANE(A_PLANE(a));
    int k, seen = 0, cx = P_CAMX(pl), cy = P_CAMY(pl), w = P_W(pl) * 16 - P_MAXX(pl);
    if (P_MAXX(pl) <= 0 || w < 320) w = 320;
    for (k = 0; k < e->pieces; k++) {
        int x, y;
        e->wx[k] += e->bvx[k];
        e->wy[k] += e->bvy[k];
        e->bvy[k] += WORM_GRAVITY;
        x = (e->wx[k] >> 16) - cx; y = (e->wy[k] >> 16) - cy;
        if (x > -WORM_SPR_W && x < w && y > -WORM_HEAD_H && y < VIEW_H) seen = 1;
    }
    if (!seen || ++e->timer > WORM_DEAD_TICKS) { e->type = NULL; Actor_Kill(a); }
}

static void __cdecl worm_update(BYTE *a)
{
    int i = enemy_slot(a), helmet = (A_FLAGS(a) & 0x20000) != 0;
    Enemy *e;
    if (i < 0) return;
    e = &g_enemy[i];
    if (e->dead) { worm_pieces(a, i); return; }
    if (enemy_attacked(a, i)) {              /* spin, roll, helmet: the head kills, the body hurts */
        if (hit_on_head(a, e, helmet)) { worm_kill(a, i); return; }
        if (!helmet && !*G_PLAYERHURTTIMER && !*G_PLAYERDEATHTIMER) {   /* (the helmet just bounces off) */
            flog("worm at (%d,%d) hit on its body, hurts Lomax", A_X(a), A_Y(a));
            Player_TakeHit(A_X(a), 0, 0);
        }
    }
    if (--e->t < 0) e->t = WORM_TIMER - 1;
    if (e->t & 0x10) worm_move(a, e);
    worm_place(a, e);
    Actor_AdvanceAnim(a);
}

/* one record entry's sprite, its top-left at x, y */
static void draw_entry(BYTE *a, const BYTE *r, int x, int y, unsigned fl, int bucket)
{
    const BYTE *bank = G_ANIMBANKS[r[0] | (T_BANKS(A_TYPE(a)) & 0x100)];
    if (bank) Sprite_QueueWorld(PLANE(A_PLANE(a)), bank + r[1] * 0x38 + 0x10, x, y,
                                (unsigned)bucket << 24 | ((r[6] & 0x18) ^ fl), 20000);
}

/* frame f of a record, its top-left at x, y (Flink's Sprite_DrawScreen: offsets ignored) */
static void draw_frame(BYTE *a, const char *tag, int f, int x, int y, unsigned fl, int bucket)
{
    const BYTE *r = record_of(a, FOURCC(tag));
    if (!r) return;
    for (; f > 0; r += 8) if (r[6] & 0x80) f--;
    for (;; r += 8) {
        draw_entry(a, r, x, y, fl, bucket);
        if (r[6] & 0x80) break;
    }
}

/* 0x28C32, or the flying pieces (0x1B4AE). In a bucket the first queued is in front: the head. */
static void __cdecl worm_draw(BYTE *a)
{
    int i = enemy_slot(a), k, f, bucket;
    const Enemy *e;
    if (i < 0 || A_FLAGS(a) & AF_HIDDEN) return;
    e = &g_enemy[i];
    bucket = Actor_GetDrawDepth(a) + 1;
    if (e->dead) {
        const BYTE *r = record_of(a, FOURCC("DEAD"));
        for (k = 0; r && k < e->pieces; k++, r += 8)
            draw_entry(a, r, e->wx[k] >> 16, e->wy[k] >> 16, e->left ? 0x08u : 0, bucket);
        return;
    }
    f = e->t >> 5 & 1;
    for (k = 0; k < WORM_SEGS; k++)
        draw_frame(a, k ? "BODY" : "HEAD", f, e->wx[k] - WORM_SPR_DX,
                   abs(e->wy[k]) + e->wz[k] - (k ? WORM_BODY_DY : WORM_HEAD_DY),
                   e->wy[k] < 0 ? 0x08u : 0, bucket);
}

/* (from enemy_stomped) 0x28B0C */
void worm_stomped(BYTE *a)
{
    Sound_PlayEffect(a, SND_HIT);
    flog("worm at (%d,%d) stomped", A_X(a), A_Y(a));
}

/* (from enemy_spawn; AF_LEFT from param 0) 0x28A1C */
void worm_spawn(BYTE *a, int i)
{
    Enemy *e = &g_enemy[i];
    int left = (A_FLAGS(a) & AF_LEFT) != 0, x = left ? A_X(a) - 32 : A_X(a) + 32, k;
    e->worm = 1;
    for (k = 0; k < WORM_SEGS; k++) {
        x += left ? 16 : -16;
        e->wx[k] = x;
        e->wy[k] = left ? -A_Y(a) : A_Y(a);
        e->wz[k] = 0;
    }
    e->t = WORM_TIMER;
    A_FLAGS(a) &= ~AF_LEFT;                  /* (not mirrored while it lives) */
    A_UPDATE(a) = (void *)worm_update;
    A_DRAW(a) = (void *)worm_draw;
    Actor_SetAnim(a, FOURCC("DEAD"));
    worm_place(a, e);
}
