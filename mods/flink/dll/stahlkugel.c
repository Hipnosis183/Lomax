/*
 * stahlkugel.c - Flink's Stahlkugeln, 'Stahlkugel' (init 0x24A24, VILLAGE; 13-2, 14-2, 15-1; README
 * "Stahlkugeln"): a spiked ball on a chain, drawn by Flink at 7 sizes for its depth. Flink's:
 * - Init: the pivot is the placement - (8, 9); angle 0 (2048 a turn, 16.16 at +0x48, 0 straight
 *   down). Then the think runs 'pre-run' times. Its first placement (spawn flag bit 6) only scales the
 *   sprites (0x24D88: EINS, ZWEI, DREI each at 70, 80 .. 130 %, 0x1DAD4) and is left out by the
 *   converter.
 * - Swinging (no 'rotating'; think 0x24B32, draw 0x24B60): the speed starts at 'speed' and changes by
 *   -1/4 a frame, the change reversing once |speed| reaches 'speed', as the swinging balls' pendulum;
 *   with 'turn around' (think 0x24B22) the angle just goes on by 'speed' a frame, a full loop. The
 *   chain swings in the plane across the screen: link k (k = 0 .. length, the ball at k = length) is
 *   at d = k * 14 px from the pivot, y + d * cos a on the screen and z = d * sin a toward the viewer.
 * - Rotating (think 0x24B22, draw 0x24C08): the angle goes on by 'speed' a frame, and the chain turns
 *   round a vertical axis: link k at d = k * 'x-offset' px, x + d * sin(a + 1024), z = d * cos(a +
 *   1024), and k * 'y-offset' px down: a cone.
 * - Its size: sprite list z / D + 3, clamped to 0..6 (70 % + 10 % a step), D = (length * 14 - 14) / 3
 *   swinging, length * x-offset / 3 - 1 rotating. With z >= 0 (angle 0..1024 swinging, 512..1536
 *   rotating) it is in draw layer 1, in front of Flink, drawn from the ball in; otherwise in layer 2,
 *   behind him, from the pivot out (Actors_DrawAll 0x33356: layer 1, Flink, layer 2; the first sprite
 *   drawn is in front).
 * - Hurting (0x24CD2, 0x1A46E): only in front and at sizes 2..4 (unclamped), the ball in a 24 x 24
 *   box round its middle, and with 'deadly' each link in a 16 x 16 one (EINS; without, DREI's links
 *   never hurt).
 * - The scaled sprites are centred where the full-size one is (0x1DE66: (w - scaled w) / 2), and the
 *   full-size ones are drawn with their top-left at the point (the ball 8 px left and 6 up): the
 *   links' middle is the point + (8, 8), the ball's + (8, 10), so the placement + (0, -1) and + (0, 1).
 * Here: the converter's 'Spik Stahl' (Lomax's decoration, object 51), records MAIN (blank), BALL,
 * LINK (EINS), RING (DREI), each a sprite anchored at its middle; params length, speed, x-offset,
 * y-offset, pre-run, checkboxes. One actor at the placement turns, hurts (Player_TakeHit) and draws
 * every piece through Actor_Draw at Flink's size (Actor_Draw's scale, per mille, scales the sprite
 * round its anchor), the actor moved onto each (so that Lomax+'s FPS Override, which follows each
 * Actor_Draw call's anchor, moves every piece smoothly), in Flink's order (in a bucket the first
 * queued is in front), in front of Lomax (layer 2) or behind him (layer 1). As balls.c's, it is never
 * culled by the game and despawns itself once it is STAHL_FAR px beyond the view and its reach.
 */
#include "flink.h"

#define STAHL_ACCEL      0x4000          /* the swing's change of speed, 1/4 a frame */
#define STAHL_LINK       14              /* px between a swinging chain's links */
#define STAHL_SIZES      7
#define STAHL_MID        3               /* the full size */
#define STAHL_FAR        96
#define STAHL_VIEW_W     512             /* Lomax+'s widest view */
#define STAHL_VIEW_H     240
#define BALL_DY          1               /* the middles from the placement (see above) */
#define LINK_DY          (-1)
#define BALL_HALF        12              /* the hurt boxes' half sizes */
#define LINK_HALF        8
#define LAYER_FRONT      2               /* layer 2 draws 8 over its plane, Lomax 6 */
#define LAYER_BACK       1               /* 4, as enemies */

#define CB_ROTATING      1
#define CB_DEADLY        2
#define CB_TURN          4

typedef struct {
    BYTE *type;
    int len, speed, xoff, yoff, checks;
    int ang, v, acc;                     /* 16.16; the angle's whole part 0..0x7FF */
    int div, reach;
} Stahl;
static Stahl g_stahl[MAX_ACTORS];

void stahls_reset(void) { memset(g_stahl, 0, sizeof(g_stahl)); }

static Stahl *stahl_of(const BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_stahl[i].type && g_stahl[i].type == A_TYPE(a) ? &g_stahl[i] : NULL;
}

/* 0x24B32 (swinging) and 0x24B22 (turn around, rotating) */
static void stahl_turn(Stahl *s)
{
    if (s->checks & (CB_ROTATING | CB_TURN))
        s->ang = (s->ang + (s->speed << 16)) & 0x7FFFFFF;
    else {
        int v = s->v + s->acc;
        if (abs(v) >= s->speed << 16) s->acc = -s->acc;
        s->v = v;
        s->ang = (s->ang + v) & 0x7FFFFFF;
    }
}

static int stahl_front(const Stahl *s)
{
    int an = s->ang >> 16;
    return s->checks & CB_ROTATING ? an >= 0x200 && an <= 0x600 : an <= 0x400;
}

/* link k's middle (k = len: the ball's) from the placement, and its unclamped size */
static void stahl_point(const Stahl *s, int k, int *dx, int *dy, int *size)
{
    int an = s->ang >> 16, d, z, sn, cs;
    if (s->checks & CB_ROTATING) an += 0x400;
    an &= 0x7FE;
    sn = G_SINTABLE[an >> 1];
    cs = G_SINTABLE[((an >> 1) + 256) & 1023];
    if (s->checks & CB_ROTATING) {
        d = k * s->xoff * 4;                     /* (Flink's units: 4 a px, as the table's 1.0 is 1/4 of 65536) */
        *dx = d * sn >> 16;
        z = d * cs >> 16;
        *dy = k * s->yoff;
    } else {
        d = k * STAHL_LINK * 4;
        *dx = 0;
        *dy = d * cs >> 16;
        z = d * sn >> 16;
    }
    *dy += k == s->len ? BALL_DY : LINK_DY;
    *size = (short)z / s->div + STAHL_MID;
}

static int far_away(const BYTE *a, int r)
{
    const BYTE *pl = PLANE(A_PLANE(a));
    int x = A_X(a) - P_CAMX(pl), y = A_Y(a) - P_CAMY(pl), m = r + STAHL_FAR;
    return x < -m || x > STAHL_VIEW_W + m || y < -m || y > STAHL_VIEW_H + m;
}

static void __cdecl stahl_update(BYTE *a)
{
    Stahl *s = stahl_of(a);
    const BYTE *pa = *G_PLAYERANIM;
    int k, dx, dy, size, h, x, y, px = *G_PLAYERX, py = *G_PLAYERY;
    if (!s) return;
    if (far_away(a, s->reach)) { s->type = NULL; Actor_Despawn(a); return; }
    stahl_turn(s);
    if (!pa || !stahl_front(s) || A_PLANE(a) != *G_PLAYERPLANE || *G_PLAYERHURTTIMER) return;
    for (k = 0; k <= s->len; k++) {
        if (k < s->len && !(s->checks & CB_DEADLY)) continue;
        stahl_point(s, k, &dx, &dy, &size);
        if (size < 2 || size > 4) continue;
        h = k == s->len ? BALL_HALF : LINK_HALF;
        x = A_X(a) + dx; y = A_Y(a) + dy;
        if (x + h < px + HB_X1(pa) || x - h > px + HB_X2(pa) || y + h < py + HB_Y1(pa) || y - h > py + HB_Y2(pa))
            continue;
        flog("stahlkugel %s at (%d,%d) hurts Lomax at (%d,%d)", k == s->len ? "ball" : "link", x, y, px, py);
        Player_TakeHit(x, 0, 0);
        return;
    }
}

static void __cdecl stahl_draw(BYTE *a)
{
    Stahl *s = stahl_of(a);
    int x0 = A_X(a), y0 = A_Y(a), front, k, i, dx, dy, size;
    if (!s) return;
    front = stahl_front(s);
    A_LAYER(a) = front ? LAYER_FRONT : LAYER_BACK;
    for (i = 0; i <= s->len; i++) {
        k = front ? s->len - i : i;              /* the nearest first */
        stahl_point(s, k, &dx, &dy, &size);
        size = size < 0 ? 0 : size >= STAHL_SIZES ? STAHL_SIZES - 1 : size;
        set_anim(a, k == s->len ? "BALL" : s->checks & CB_DEADLY ? "LINK" : "RING");
        A_X(a) = x0 + dx; A_Y(a) = y0 + dy;
        Actor_Draw(a, 700 + 100 * size, 0, 0);
    }
    A_X(a) = x0; A_Y(a) = y0;
}

/* (from spik_spawn) params: length, speed, x-offset, y-offset, pre-run, checkboxes */
void stahl_spawn(BYTE *a, BYTE *def)
{
    int i = slot_of(a), k;
    Stahl *s;
    if (i < 0 || !def || T_NPARAMS(A_TYPE(a)) < 6) return;
    s = &g_stahl[i];
    memset(s, 0, sizeof(*s));
    s->len = D_PARAM(def, 0) < 0 ? 0 : D_PARAM(def, 0);
    s->speed = D_PARAM(def, 1);
    s->xoff = D_PARAM(def, 2);
    s->yoff = D_PARAM(def, 3);
    s->checks = D_PARAM(def, 5);
    if (s->checks & CB_ROTATING) {
        s->div = s->len * s->xoff / 3 - 1;
        s->reach = s->len * (abs(s->xoff) > abs(s->yoff) ? abs(s->xoff) : abs(s->yoff));
    } else {
        s->div = (s->len * STAHL_LINK - STAHL_LINK) / 3;
        s->reach = s->len * STAHL_LINK;
        s->v = s->speed << 16;                   /* 0x24A54: at the bottom at full speed */
        s->acc = -STAHL_ACCEL;
    }
    if (s->div < 1) s->div = 1;
    s->reach += BALL_HALF * 2;
    s->type = A_TYPE(a);
    for (k = D_PARAM(def, 4); k > 0; k--) stahl_turn(s);
    A_FLAGS(a) &= ~(0x200000u | AF_CONTACT);     /* the actor itself touches nothing */
    A_CULL(a) = CULL_NEVER;
    A_UPDATE(a) = (void *)stahl_update;
    A_DRAW(a) = (void *)stahl_draw;
    flog("stahlkugel at (%d,%d): length %d, speed %d, offsets %d,%d, pre-run %d,%s%s%s", A_X(a), A_Y(a), s->len,
         s->speed, s->xoff, s->yoff, D_PARAM(def, 4), s->checks & CB_ROTATING ? " rotating" : " swinging",
         s->checks & CB_TURN ? ", turn around" : "", s->checks & CB_DEADLY ? ", deadly" : "");
}
