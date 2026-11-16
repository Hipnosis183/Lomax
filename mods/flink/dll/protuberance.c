/*
 * protuberance.c - Flink's Protuberance ('Protuberance', type 51, init 0x265CA, think 0x266B2, draw
 * 0x267E4; MOUNTAIN 14, 16 = 23-1 and its map 16; README "Lava"): a stream of fireballs leaping out of
 * the lava in an arc. Flink's:
 * - Init: 4 droplets (+0x46) and 45 balls (+0x5A: words, the high byte the ball's phase, 0xFF free,
 *   the low byte its animation count). Draw list 1, window 0x18; checkbox 1 'explode-left' flips it
 *   (+3 bit 3). Params 'timer' (+0x58, the countdown +0x56 starting there), 'height' (x 4, +0x5C) and
 *   'how-many' (+0x5E, the balls left +0x60). Its y is the lava's surface + 10 (the base). It keeps
 *   FIRE's three frames' hardware sprites.
 * - Each frame (0x266B2): the countdown runs down; past 0 it is 'timer' again and the balls left
 *   'how-many'. Whenever the countdown is a multiple of 8, the balls left go down and, still above 0,
 *   the first free ball starts at phase 0: so 'how-many' - 1 balls, one every 8 frames, every
 *   'timer' + 1 frames. While the spray timer (+0x62) runs (10 frames after a ball lands), every 8th
 *   frame of its count (+0x48) a droplet as the Lava column's (lava.c) and sound 0x32 (on screen).
 * - Its draw (0x267E4): the droplets, 240 px to the right of it (256 to the left when flipped: where
 *   the balls come down); each ball's phase + 1, and at 128 it lands (freed; the spray timer 10).
 *   Otherwise its sprite's top-left is (x - 24 +- 2 * phase, base - 24 - sin(phase * 4 of 1024)
 *   * height * 4 / 65536): 254 px across and 'height' px up, as FIRE frame {2, 1, 0, 2}[(count >> 2)
 *   & 3], its count + 1. Every fourth ball drawn (the frame counter + its number, & 3 = 0) tests its
 *   16 x 16 hurt box, the sprite's top-left + 8 (0x1A46E).
 * Here: the converter's 'Spik Prot' (Lomax's decoration, object 51) with Flink's FIRE (each piece at
 * (-16, -32): the anchor is the sprite's top-left + (16, 32)); params timer, height, how-many, flipped.
 * The motion is all in the update, the draw one Actor_Draw a piece in a fixed order (lava.c's
 * lava_piece). Lomax's culling of the decoration stands for Flink's window.
 */
#include "flink.h"
#include "enemy.h"

#define PROT_BALLS       45              /* (+0x5A: dbra from 0x2C) */
#define PROT_FREE        0x80            /* a phase >= this: free (the byte's sign) */
#define PROT_BASE        10              /* the surface + 10 (0x26676) */
#define PROT_STEP        2               /* px across a phase (0x268A0) */
#define PROT_SPRITE      24              /* the ball's top-left: x - 24, base - 24 (0x26866) */
#define PROT_DROP_RIGHT  240             /* the droplets' offset (0x267E4) */
#define PROT_DROP_LEFT   (-256)
#define PROT_SPRAY       10              /* (0x26892) */
#define PROT_HURT        8               /* the hurt box: the top-left + 8 .. + 24 (0x268FA) */
#define PROT_HURT_W      16
#define PROT_ANCHOR_X    16              /* FIRE's pieces at (-16, -32) */
#define PROT_ANCHOR_Y    32
#define SND_PROT_SPRAY   0x32            /* (0x267D8) */

static const int PROT_FRAME[4] = { 2, 1, 0, 2 };   /* 0x2691C: +0x4A, +0x4E, +0x52 = FIRE frames 2, 1, 0 */

typedef struct {
    BYTE *type;
    int ready, base, left;
    int timer, height4, many;            /* +0x58, +0x5C, +0x5E */
    int countdown, remaining, spray, count, frame;   /* +0x56, +0x60, +0x62, +0x48, the frame counter */
    struct { int phase, anim; } ball[PROT_BALLS];
    LavaDrop drop[LAVA_DROPS];
} Prot;
static Prot g_prot[MAX_ACTORS];

void prot_reset(void) { memset(g_prot, 0, sizeof(g_prot)); }

static Prot *prot_of(const BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_prot[i].type && g_prot[i].type == A_TYPE(a) ? &g_prot[i] : NULL;
}

/* a ball's sprite top-left (x0: the Protuberance's x) */
static void ball_at(int x0, const Prot *p, int k, int *x, int *y)
{
    int ph = p->ball[k].phase, dx = PROT_STEP * ph;
    *x = x0 - PROT_SPRITE + (p->left ? -dx : dx);
    *y = p->base - PROT_SPRITE - (int)(((unsigned)(WORD)G_SINTABLE[(ph * 4) & 0x3FF] * (unsigned)p->height4) >> 16);
}

/* the think (0x266B2) and the draw's motion (0x267E4) */
static void __cdecl prot_update(BYTE *a)
{
    Prot *p = prot_of(a);
    int k, n = 0;
    if (!p || !g_active) return;
    if (!p->ready) {
        if (lava_surface() == NO_GROUND) return;
        p->ready = 1;
        p->base = lava_surface() + PROT_BASE;
        place_y(a, p->base);
        flog("protuberance at (%d,%d): timer %d, height %d, %d balls%s", A_X(a), p->base, p->timer,
             p->height4 / 4, p->many - 1, p->left ? ", left" : "");
    }
    if (--p->countdown < 0) {
        p->countdown = p->timer;
        p->remaining = p->many;
    }
    if (!(p->countdown & 7) && --p->remaining > 0) {   /* a ball */
        for (k = 0; k < PROT_BALLS && p->ball[k].phase < PROT_FREE; k++) ;
        if (k < PROT_BALLS) p->ball[k].phase = 0;
    }
    p->count++;
    if (p->spray) {
        p->spray--;
        if (!(p->count & 7)) {
            lava_drop_add(p->drop, p->count, A_X(a) - 16, p->base - 0x1B, 0);
            if (enemy_on_screen(a)) flink_sound(a, SND_PROT_SPRAY);
        }
    }
    lava_drops_move(p->drop);
    p->frame++;
    for (k = 0; k < PROT_BALLS; k++) {
        int x, y;
        if (p->ball[k].phase >= PROT_FREE) continue;
        if (++p->ball[k].phase >= PROT_FREE) {      /* landed */
            p->spray = PROT_SPRAY;
            continue;
        }
        p->ball[k].anim = (p->ball[k].anim + 1) & 0xFF;
        if ((p->frame + ++n) & 3) continue;
        ball_at(A_X(a), p, k, &x, &y);
        if (lava_touches(a, x + PROT_HURT, y + PROT_HURT, x + PROT_HURT + PROT_HURT_W, y + PROT_HURT + PROT_HURT_W)) {
            flog("protuberance at (%d,%d): a ball at (%d,%d) hurts Lomax", A_X(a), p->base, x, y);
            lava_hurt_by(0, 0);
        }
    }
}

/* the droplets, then the balls (Flink's order: the first queued in front) */
static void __cdecl prot_draw(BYTE *a)
{
    Prot *p = prot_of(a);
    int x = A_X(a), y = A_Y(a), k;
    WORD hide = A_HIDE(a);
    if (!p || !g_active || !p->ready) return;
    lava_drops_draw(a, p->drop, "FIRE", (p->left ? PROT_DROP_LEFT : PROT_DROP_RIGHT) + PROT_ANCHOR_X, PROT_ANCHOR_Y);
    for (k = 0; k < PROT_BALLS; k++) {
        int bx = 0, by = 0, on = p->ball[k].phase < PROT_FREE;
        if (on) ball_at(x, p, k, &bx, &by);
        lava_piece(a, "FIRE", PROT_FRAME[(p->ball[k].anim >> 2) & 3], bx + PROT_ANCHOR_X, by + PROT_ANCHOR_Y, on);
    }
    A_HIDE(a) = hide;
    A_X(a) = x;
    A_Y(a) = y;
    A_Y(a) = y;
}

/* (from spik_spawn) params: timer, height, how-many, flipped */
void prot_spawn(BYTE *a, BYTE *def)
{
    int i = slot_of(a), k;
    Prot *p;
    if (i < 0 || !def || T_NPARAMS(A_TYPE(a)) < 4 || !record_of(a, FOURCC("FIRE"))) return;
    p = &g_prot[i];
    memset(p, 0, sizeof(*p));
    for (k = 0; k < PROT_BALLS; k++) p->ball[k].phase = 0xFF;
    for (k = 0; k < LAVA_DROPS; k++) p->drop[k].t = -1;
    p->timer = p->countdown = D_PARAM(def, 0);
    p->height4 = (D_PARAM(def, 1) * 4) & 0xFFFF;
    p->many = p->remaining = D_PARAM(def, 2);
    p->left = D_PARAM(def, 3) & 1;
    p->type = A_TYPE(a);
    A_FLAGS(a) &= ~(0x200000u | AF_CONTACT);     /* the hurt is Flink's box (prot_update) */
    A_LAYER(a) = (short)FLINK_LAYER(1);          /* (a5) = 0x7D6C: list 1 */
    A_UPDATE(a) = (void *)prot_update;
    A_DRAW(a) = (void *)prot_draw;
    set_anim(a, "FIRE");
}
