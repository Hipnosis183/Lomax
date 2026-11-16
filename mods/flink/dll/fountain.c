/*
 * fountain.c - Flink's Fountain ('Fountain', type 53, init 0x26C28, thinks 0x26CB4 / 0x26D5E, draw
 * 0x26DE4; MOUNTAIN 14, 16 = 23-1 and its map 16; README "Lava"): a geyser of lava that throws a
 * Throw platform up. Flink's:
 * - Init: draw list 3 (0x7D70, behind); its y the lava's surface (no + 10); params 'speed-y' (+0x46,
 *   about 500) and 'timer' (+0x48), checkbox 1 'wait' (+0x5C); the wait (+0x4A) 30. ZWEI's two frames
 *   (+0x4C, +0x50: the column), EINS's (+0x54, +0x58: the foot, at the surface); it shows DREI (the
 *   top, 3 frames).
 * - Waiting (0x26CB4): the wait runs down, nothing drawn (0x3333E). At 0, with 'wait' it looks for a
 *   floating Throw platform (an actor with an extra hook, +6 bit 6, floating, +0x46) within its drift
 *   speed (+0x4A) * 16 / 65536 px across: none, the wait stays 0 and it looks again next frame. Then
 *   (or at once without 'wait') the wait is 'timer' again, the height (+0x62, 16.16) -70, the bob
 *   (+0x5E) 7, vy (+0x20) 'speed-y' * 0x369 (500: 6.66 px a frame), sound 0x4B; it erupts (think
 *   0x26D5E, draw 0x26DE4, drawn at once). All of 23-1's Fountains have 'wait': the first (x 811)
 *   under its Throw platform (drift 0: right under it), the others (x 2415, 2552) in the way of one
 *   drifting at 50 (within 7 px).
 * - Erupting (0x26D5E): vy - 1/8; once vy and the height are both below 0, the bob (if 0) is -7. The
 *   height + vy. Its top is a platform at y - height + 32 (0x1A27E: vy = that - y, then the platform
 *   carry 0x1A28A with DREI's hitbox, top -25); every heavy thrown object resting on that top (within
 *   10 px going up, 6 going down; 0x1A1BA) is put on it, and while 0 <= vy < 4.5 thrown up at 8.0
 *   (the hook 0x26DC8). So a Throw platform over it rides it up and is thrown high, the player on it.
 * - Its draw (0x26DE4): the bob as the Burst of lava's (burst.c; at its end the think is 0x26CB4
 *   again: it waits 'timer' frames); the foot, EINS (frame by the frame counter & 4) at y + 2 * the
 *   bob's step; the top, DREI, at y - height + 32 once the height is -22 or more; the column, ZWEI
 *   rows from y - height + 16 down to the surface - 10 (frames alternating). Its hurt box (0x1A46E):
 *   x +- 16, from y - height to the top of the first row not drawn. The player standing on its top
 *   is inside it: Flink is hurt there; only a Throw platform rides it.
 * Here: the converter's 'Spik Fountain' (Lomax's decoration, object 51) with Flink's EINS, ZWEI and
 * DREI (each piece at Flink's offsets: a row's anchor is its top + 16, the foot's y + bob, the top's
 * y - height + 32); params speed-y, timer, wait. While it erupts the actor's y is its top's (DREI is
 * its record: Platform_MoveAndCarryPlayer carries Lomax by the move, as Flink's carry does), and the
 * thrown objects are lifted by grab.c's grab_lift; waiting, its y is the surface. Its top starts
 * 102 px under the surface, below the map's bottom, where Lomax's culling (Actors_DrawAndCull, by the
 * actor's x and y) would despawn it at once (Flink's window test uses its y, the surface): it is never
 * culled while it erupts (cull phase 3), and gets its own phase back when it waits. In lava a Throw
 * platform lifted above the surface + 5 stops floating (throwplat.c). The draw is one Actor_Draw a
 * piece in a fixed order (lava.c's lava_piece).
 */
#include "flink.h"
#include "enemy.h"

#define FOUNT_FIRST      30              /* the first wait (0x26C5A) */
#define FOUNT_HEIGHT0    (-70)           /* (0x26CE0) */
#define FOUNT_BOB        7               /* (0x26CE8) */
#define FOUNT_SPEED_UNIT 0x369           /* (0x26CF2) */
#define FOUNT_GRAVITY    0x2000          /* (0x26D62) */
#define FOUNT_TOP        32              /* the top's y: y - height + 32 (0x26D94) */
#define FOUNT_SHOWN      (-10)           /* the top drawn from a height - 32 of this (0x26E56) */
#define FOUNT_REACH_UP   10              /* (0x26DAC) */
#define FOUNT_REACH_DOWN 6
#define FOUNT_FLING_MAX  0x48000         /* the hook flings while 0 <= vy < this (0x26DCE) */
#define FOUNT_FLING      (-0x80001)      /* 8.0 up (0x26DD8) */
#define FOUNT_ROW        16
#define FOUNT_LIMIT      10              /* rows above the surface - 10 (0x26E82) */
#define FOUNT_HALF_W     16
#define FOUNT_ROWS_MAX   16
#define FOUNT_HEAD_SPEED 21              /* DREI's record speed (8.8 frames a tick) */
#define SND_FOUNTAIN     0x4B            /* (0x26CFC) */

typedef struct {
    BYTE *type;
    int ready, y, speed, timer, waits;   /* the surface (+0x18), +0x46, +0x48, +0x5C */
    int erupting, wait, height, vy, bob, off;   /* +0x4A, +0x62 (16.16), +0x20, +0x5E, the bob's offset */
    int frame, anim;                     /* the frame counter, DREI's position (8.8) */
    short cull;                          /* its cull phase while it waits (Deco_Spawn's) */
} Fountain;
static Fountain g_fount[MAX_ACTORS];

void fountain_reset(void) { memset(g_fount, 0, sizeof(g_fount)); }

static Fountain *fount_of(const BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_fount[i].type && g_fount[i].type == A_TYPE(a) ? &g_fount[i] : NULL;
}

static int surface_of(const Fountain *f) { return lava_surface() == NO_GROUND ? f->y : lava_surface(); }

/* the top's y and the column's rows: the top of the first row not drawn (0x26E6C) */
static int fount_top(const Fountain *f) { return f->y - (f->height >> 16) + FOUNT_TOP; }
static int fount_rows(const Fountain *f, int *tops, int *n)
{
    int limit = surface_of(f) - FOUNT_LIMIT, t = f->y - (f->height >> 16);
    *n = 0;
    for (;;) {
        t += FOUNT_ROW;
        if (t >= limit) return t;
        if (*n < FOUNT_ROWS_MAX) tops[(*n)++] = t;
    }
}

/* the bob (0x26DE4): its offset this frame; at its end the Fountain waits */
static int fount_bob(Fountain *f)
{
    int d = f->bob;
    if (!d) return 0;
    if (d < 0) {
        d += FOUNT_BOB;
        f->bob += 2;
        if (f->bob >= 0) f->erupting = 0;
    }
    f->bob--;
    return d * 2;
}

/* the top as a platform (0x1A27E): moved there, carrying Lomax, lifting thrown objects (0x1A1BA) */
static void fount_platform(BYTE *a, Fountain *f)
{
    const BYTE *r;
    int top = fount_top(f), x1, x2;
    A_FY(a) = A_Y(a) << 16;
    A_VX(a) = 0;
    A_VY(a) = (top - A_Y(a)) << 16;
    Platform_MoveAndCarryPlayer(a, NULL);
    A_VX(a) = A_VY(a) = 0;
    place_y(a, top);
    if (!(r = A_RECORD(a))) return;
    x1 = A_X(a) + HB_X1(r);
    x2 = A_X(a) + HB_X2(r);
    if (grab_lift(a, top + HB_Y1(r), x1, x2, f->vy >= 0 ? FOUNT_REACH_UP : FOUNT_REACH_DOWN,
                  f->vy >= 0 && f->vy < FOUNT_FLING_MAX ? FOUNT_FLING : 0))
        flog("fountain at %d lifts at y %d (vy %d/65536)", A_X(a), top + HB_Y1(r), f->vy);
}

static void __cdecl fount_update(BYTE *a)
{
    Fountain *f = fount_of(a);
    int rows[FOUNT_ROWS_MAX], n, bottom;
    if (!f || !g_active) return;
    if (!f->ready) {
        if (lava_surface() == NO_GROUND) return;
        f->ready = 1;
        f->y = lava_surface();
        place_y(a, f->y);
        flog("fountain at (%d,%d): speed %d, timer %d%s", A_X(a), f->y, f->speed, f->timer, f->waits ? ", waits" : "");
    }
    f->frame++;
    if (!f->erupting) {                              /* 0x26CB4: nothing drawn while it waits */
        f->off = 0;
        if (--f->wait > 0) return;
        if (f->waits && !throwplat_floating_near(A_X(a))) { f->wait = 0; return; }
        f->wait = f->timer;
        f->erupting = 1;
        f->height = FOUNT_HEIGHT0 * 0x10000;
        f->bob = FOUNT_BOB;
        f->vy = f->speed * FOUNT_SPEED_UNIT;
        A_CULL(a) = CULL_NEVER;
        flink_sound(a, SND_FOUNTAIN);
        flog("fountain at %d erupts", A_X(a));
    } else {                                         /* 0x26D5E */
        f->vy -= FOUNT_GRAVITY;
        if (f->vy < 0 && f->height < 0 && !f->bob) f->bob = -FOUNT_BOB;
        f->height += f->vy;
        fount_platform(a, f);
    }
    /* the draw's state (0x26DE4): the bob, the hurt box */
    f->anim += FOUNT_HEAD_SPEED;
    f->off = fount_bob(f);
    bottom = fount_rows(f, rows, &n);
    if (lava_touches(a, A_X(a) - FOUNT_HALF_W, f->y - (f->height >> 16), A_X(a) + FOUNT_HALF_W, bottom)) {
        flog("fountain at (%d,%d) hurts Lomax at (%d,%d)", A_X(a), f->y, *G_PLAYERX, *G_PLAYERY);
        lava_hurt_by(0, 0);
    }
    if (!f->erupting) {
        place_y(a, f->y);
        A_CULL(a) = f->cull;
    }
}

/* the foot, the top, the column (Flink's order: the first queued in front) */
static void __cdecl fount_draw(BYTE *a)
{
    Fountain *f = fount_of(a);
    int x = A_X(a), y = A_Y(a), rows[FOUNT_ROWS_MAX], n = 0, k, fr = (f ? f->frame >> 2 : 0) & 1, on;
    WORD hide = A_HIDE(a);
    if (!f || !g_active || !f->ready) return;
    on = f->erupting || f->off;                      /* (shown from the eruption to the bob's end) */
    if (on) fount_rows(f, rows, &n);
    lava_piece(a, "EINS", fr, x, f->y + f->off, on);
    set_anim(a, "DREI");
    lava_piece(a, "DREI", (f->anim >> 8) % (A_NFRAMES(a) > 0 ? A_NFRAMES(a) : 1), x, fount_top(f),
               on && (f->height >> 16) - FOUNT_TOP >= FOUNT_SHOWN);
    for (k = 0; k < FOUNT_ROWS_MAX; k++)
        lava_piece(a, "ZWEI", (fr + k) & 1, x, k < n ? rows[k] + FOUNT_ROW : y, k < n);
    A_HIDE(a) = hide;
    A_X(a) = x;
    A_Y(a) = y;
    set_anim(a, "DREI");                             /* (the platform's hitbox) */
}

/* (from spik_spawn) params: speed-y, timer, wait */
void fountain_spawn(BYTE *a, BYTE *def)
{
    int i = slot_of(a);
    Fountain *f;
    if (i < 0 || !def || T_NPARAMS(A_TYPE(a)) < 3 || !record_of(a, FOURCC("EINS")) || !record_of(a, FOURCC("ZWEI")) ||
        !record_of(a, FOURCC("DREI"))) return;
    f = &g_fount[i];
    memset(f, 0, sizeof(*f));
    f->speed = D_PARAM(def, 0);
    f->timer = D_PARAM(def, 1);
    f->waits = D_PARAM(def, 2) & 1;
    f->wait = FOUNT_FIRST;
    f->type = A_TYPE(a);
    f->cull = A_CULL(a);
    A_FLAGS(a) &= ~(0x200000u | AF_CONTACT);
    A_LAYER(a) = (short)FLINK_LAYER(3);          /* (a5) = 0x7D70: list 3 */
    A_UPDATE(a) = (void *)fount_update;
    A_DRAW(a) = (void *)fount_draw;
    set_anim(a, "DREI");
}
