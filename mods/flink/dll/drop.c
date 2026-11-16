/*
 * drop.c - Flink's Drop Ball (init 0x27B24, think 0x27B8E / 0x27B94, chain draw 0x27C60; VILLAGE map
 * 6 = 10-3; README "Drop balls"): a spiked ball on a chain that drops onto the floor on a timer and is
 * pulled back up. Flink's:
 * - Init: home = the placement's y (+0x4E), floor = home + 'fall-height' (+0x50), the countdown
 *   (+0x4A) and its reload (+0x4C) 'fall-timer'; record BALL (the ball and the chain's lowest link;
 *   its hitbox is the ball's), draw list 3 ((a5) = 0x7D70: behind Flink); then the step runs
 *   $10(a4) times, the word after its two params: the next GNMY entry's size (the converter's
 *   third param).
 * - Each frame (0x27B8E): touching the ball hurts Flink (0x1A462), then the step (0x27B94), by the
 *   16.16 speed (+0x20):
 *   0 (waiting at home): the countdown runs out -> speed 0.25 (no move this frame);
 *   falling: speed + 0.25 up to 4.0, y + speed; at the floor: y = the floor, sound 0x35 when on
 *     screen (+6 bit 4), the countdown 30, speed 1 (a marker: landed);
 *   1 (landed): while the countdown is 14 or more, y + {1, 1, -1, -1}[countdown & 3] (a shake,
 *     0x27BEC); the countdown runs out -> its reload, speed -1.0;
 *   negative (pulled up): y + speed, 1 px a frame; at home speed 0.
 * - Draw: the actor's BALL, whose hook piece (selector 1, at x - 3, y - 33) makes Actor_DrawFrame
 *   (0x32972) call the +8 hook (0x27C5C, a pointer to 0x27C60) at that point; it draws the APPE link
 *   (Sprite_DrawScreen 0x3430E: the sprite's top-left at the point) every 32 px up from there, until
 *   one is off the screen.
 * Here: the converter's 'Spik Drop' (Lomax's decoration, object 51), records BALL and APPE (the link
 * at the hook's offsets, so drawn at y - 32 k); params fall-height, fall-timer, pre-run. The actor touches
 * nothing itself: the hurt is Flink's test, Lomax's hitbox against BALL's, then Player_TakeHit. Its
 * draw queues the ball, then the links, one Actor_Draw each with the actor moved onto it (Lomax+'s
 * FPS Override follows each call's anchor; as balls.c), on Lomax's layer for list 3 (FLINK_LAYER).
 * Lomax culls and respawns it by its point as any decoration; a respawn starts it again from home.
 */
#include "flink.h"

#define DROP_ACCEL      0x4000           /* 0.25 a frame */
#define DROP_MAX        0x40000          /* 4.0 */
#define DROP_LANDED     1                /* the speed while it lies on the floor */
#define DROP_RISE       (-0x10000)       /* -1.0 */
#define DROP_REST       30               /* frames on the floor */
#define DROP_SHAKE_END  14               /* it shakes while the countdown is this or more */
#define LINK_STEP       32               /* px between the chain's links */
#define LINK_BOTTOM     (-1)             /* a link's sprite ends this far below its point (APPE: y - 33 .. y - 1) */
#define VIEW_W          512              /* Lomax+'s widest view */
#define VIEW_H          240

static const int g_shake[4] = { 1, 1, -1, -1 };     /* 0x27BEC */

typedef struct {
    BYTE *type;
    int home, floor, reload;             /* +0x4E, +0x50, +0x4C */
    int t, y, v;                         /* +0x4A; y and speed 16.16 (+0x18, +0x20) */
} Drop;
static Drop g_drop[MAX_ACTORS];

void drops_reset(void) { memset(g_drop, 0, sizeof(g_drop)); }

static Drop *drop_of(const BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_drop[i].type && g_drop[i].type == A_TYPE(a) ? &g_drop[i] : NULL;
}

static int on_screen(const BYTE *a)
{
    const BYTE *pl = PLANE(A_PLANE(a));
    int x = A_X(a) - P_CAMX(pl), y = A_Y(a) - P_CAMY(pl);
    return x >= 0 && x < VIEW_W && y >= 0 && y < VIEW_H;
}

/* 0x27B94 (a = NULL: no sound, the pre-run) */
static void drop_step(Drop *d, BYTE *a)
{
    if (!d->v) {
        if (--d->t == 0) d->v = DROP_ACCEL;
    } else if (d->v < 0) {
        d->y += d->v;
        if (d->y >> 16 == d->home) d->v = 0;
    } else if (d->v == DROP_LANDED) {
        if (d->t >= DROP_SHAKE_END) d->y += g_shake[d->t & 3] << 16;
        if (--d->t == 0) { d->t = d->reload; d->v = DROP_RISE; }
    } else {
        d->v += DROP_ACCEL;
        if (d->v > DROP_MAX) d->v = DROP_MAX;
        d->y += d->v;
        if (d->y >> 16 >= d->floor) {
            if (a && on_screen(a)) flink_sound(a, 0x35);
            d->t = DROP_REST;
            d->y = d->floor << 16 | (d->y & 0xFFFF);     /* 0x27C38 sets the whole part only */
            d->v = DROP_LANDED;
        }
    }
}

static void __cdecl drop_update(BYTE *a)
{
    Drop *d = drop_of(a);
    const BYTE *pa = *G_PLAYERANIM, *hb = record_of(a, FOURCC("BALL"));
    int x = A_X(a), y = A_Y(a), px = *G_PLAYERX, py = *G_PLAYERY;
    if (!d) return;
    if (pa && hb && A_PLANE(a) == *G_PLAYERPLANE && !*G_PLAYERHURTTIMER) {
        hb -= 0x20;                                  /* the record (HB_*: its hitbox) */
        if (x + HB_X2(hb) >= px + HB_X1(pa) && x + HB_X1(hb) <= px + HB_X2(pa) &&
            y + HB_Y2(hb) >= py + HB_Y1(pa) && y + HB_Y1(hb) <= py + HB_Y2(pa)) {
            flog("drop ball at (%d,%d) hurts Lomax at (%d,%d)", x, y, px, py);
            Player_TakeHit(x, 0, 0);
        }
    }
    drop_step(d, a);
    place_y(a, d->y >> 16);
}

static void __cdecl drop_draw(BYTE *a)
{
    const BYTE *pl = PLANE(A_PLANE(a));
    int y0 = A_Y(a), y, top = P_CAMY(pl) - LINK_STEP;     /* one link more, for the FPS Override's in-between camera */
    if (!drop_of(a)) return;
    set_anim(a, "BALL");
    draw_actor(a);
    set_anim(a, "APPE");
    for (y = y0 - LINK_STEP; y + LINK_BOTTOM > top; y -= LINK_STEP) {
        A_Y(a) = y;
        draw_actor(a);
    }
    A_Y(a) = y0;
    set_anim(a, "BALL");
}

/* (from spik_spawn) params: fall-height, fall-timer, pre-run */
void drop_spawn(BYTE *a, BYTE *def)
{
    int i = slot_of(a), k;
    Drop *d;
    if (i < 0 || !def || T_NPARAMS(A_TYPE(a)) < 3) return;
    d = &g_drop[i];
    memset(d, 0, sizeof(*d));
    d->home = A_Y(a);
    d->floor = d->home + D_PARAM(def, 0);
    d->t = d->reload = D_PARAM(def, 1);
    d->y = d->home << 16;
    d->type = A_TYPE(a);
    for (k = D_PARAM(def, 2); k > 0; k--) drop_step(d, NULL);
    A_FLAGS(a) &= ~(0x200000u | AF_CONTACT);         /* the actor itself touches nothing */
    A_LAYER(a) = (short)FLINK_LAYER(3);              /* (a5) = 0x7D70: list 3 */
    A_UPDATE(a) = (void *)drop_update;
    A_DRAW(a) = (void *)drop_draw;
    set_anim(a, "BALL");
    place_y(a, d->y >> 16);
    flog("drop ball at (%d,%d): fall %d px, timer %d, pre-run %d", A_X(a), d->home, D_PARAM(def, 0), d->reload, D_PARAM(def, 2));
}
