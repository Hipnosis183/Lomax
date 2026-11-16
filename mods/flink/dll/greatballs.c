/*
 * greatballs.c - Flink's Great balls of fire ('Great balls of fire', type 52, init 0x26924, think
 * 0x269E4, draw 0x26B50; each ball's think 0x26BA6 and draw 0x26C16; MOUNTAIN 15 = 23-2; README
 * "Lava"): fireballs thrown up out of the lava, four at a time. Flink's:
 * - Init (the spawner): 4 droplets (+0x46); draw list 1, window 0x18; 'timer' (+0x58), the countdown
 *   (+0x56) 100; its y the lava's surface + 10 (the base). TAIL's three frames' hardware sprites go to
 *   0x84A4 (the balls' trail), LAVA's to +0x52, +0x4E, +0x4A (the droplets).
 * - Each frame (0x269E4): the countdown runs down; past 0 it is 'timer' again. Under 64 and a multiple
 *   of 16 (48, 32, 16, 0): sound 0x1A (on screen) and a ball (0x32CD8, a child at its x and base;
 *   list 1): vy ((rand & 7) + 20) / 8 up (2.5 .. 3.375), vx ((rand & 7) + 1) / 8, to the left when
 *   the countdown's bit 4 is set (16, 48), HEAD; its trail (0x2EE0C: three sparks, phases 0x400,
 *   0x2AB, 0x156, unplaced). Every 8th frame of its count (+0x48) a droplet as the Lava column's.
 * - Its draw (0x26B50): the droplets, as LAVA frame 2 - timer / 8.
 * - A ball (0x26BA6): each spark's phase + 0x30; at a high byte of 3 it is 0 again and the spark is
 *   put at the ball's x - 8, y - 16 (the first, from 0x400, never gets there before the ball ends:
 *   two sparks show). Touching its HEAD hitbox hurts (0x1A462). It moves (0x1B0E2): vy - 1/32 a
 *   frame (down to -4.0), y - vy, x + vx. Going down through the base it splashes (0x2E770: the
 *   Water's SPLA at the surface, sound 0x25) and is gone (0x32D8E); otherwise HEAD animates.
 * - A ball's draw (0x26C16): each placed spark as TAIL frame (its phase's high byte), its sprite's
 *   top-left at its point; then the ball.
 * Here: the converter's 'Spik Great' (Lomax's decoration, object 51) with Flink's HEAD (3 frames, its
 * hitbox), TAIL (the sparks) and LAVA (the droplets), each piece at Flink's offsets (HEAD and TAIL at
 * (-7, -16), LAVA at (-15, -32)); param timer. The balls are Actor_Alloc children, never culled
 * (they always come down), their splash Lomax's Water_SpawnSplash (Flink's SPLA, Lomax's sound). The
 * spawner's and balls' draws are one Actor_Draw a piece in a fixed order (lava.c's lava_piece).
 */
#include "flink.h"
#include "enemy.h"

#define GREAT_BASE       10              /* the surface + 10 (0x26976) */
#define GREAT_FIRST      100             /* the first countdown (0x2696A) */
#define GREAT_THROWS     64              /* balls while the countdown is under this ... (0x26A32) */
#define GREAT_EVERY      16              /* ... and a multiple of this (0x26A3A) */
#define GREAT_GRAVITY    0x800           /* (0x26BE0) */
#define GREAT_VY_MIN     (-0x40000)      /* (0x1B0E6) */
#define GREAT_SPARKS     3
#define GREAT_SPARK_STEP 0x30            /* (0x26BAC) */
#define GREAT_SPARK_X    8               /* the spark's top-left: x - 8, y - 16 (0x26BBE) */
#define GREAT_SPARK_Y    16
#define GREAT_HEAD_SPEED 24              /* HEAD's record speed (8.8 frames a tick) */
#define GREAT_HEAD_X     7               /* HEAD's and TAIL's pieces at (-7, -16) */
#define GREAT_HEAD_Y     16
#define GREAT_DROP_X     15              /* LAVA's pieces at (-15, -32) */
#define GREAT_DROP_Y     32
#define SND_GREAT_THROW  0x1A            /* (0x26A4E) */

typedef struct {
    BYTE *type;
    void *update;                        /* great_update or ball_update: which this slot is */
    int ready, base, timer, countdown, count;   /* the spawner: +0x58, +0x56, +0x48 */
    LavaDrop drop[LAVA_DROPS];
    int y, vy, x, vx, anim;              /* a ball: 16.16, up positive; HEAD's position (8.8) */
    struct { int phase, x, y; } spark[GREAT_SPARKS];
} Great;
static Great g_great[MAX_ACTORS];

void great_reset(void) { memset(g_great, 0, sizeof(g_great)); }

static void __cdecl great_update(BYTE *a);
static void __cdecl ball_update(BYTE *a);

static Great *great_of(const BYTE *a, void *update)
{
    int i = slot_of(a);
    return i >= 0 && g_great[i].type && g_great[i].type == A_TYPE(a) && g_great[i].update == update &&
           A_UPDATE(a) == update ? &g_great[i] : NULL;
}

/* a ball (0x26BA6) */
static void __cdecl ball_update(BYTE *a)
{
    Great *b = great_of(a, (void *)ball_update);
    int k, was, y, base;
    if (!b || !g_active) return;
    for (k = 0; k < GREAT_SPARKS; k++) {
        b->spark[k].phase = (b->spark[k].phase + GREAT_SPARK_STEP) & 0xFFFF;
        if ((b->spark[k].phase >> 8) == 3) {
            b->spark[k].phase &= 0xFF;
            b->spark[k].x = A_X(a) - GREAT_SPARK_X;
            b->spark[k].y = A_Y(a) - GREAT_SPARK_Y;
        }
    }
    {
        int x1, y1, x2, y2;
        if (actor_box(a, &x1, &y1, &x2, &y2) && lava_touches(a, x1, y1, x2, y2)) {   /* 0x1A462 */
            flog("great ball of fire at (%d,%d) hurts Lomax", A_X(a), A_Y(a));
            lava_hurt_by(b->vx, 0);
        }
    }
    was = A_Y(a);
    b->vy -= GREAT_GRAVITY;
    if (b->vy < GREAT_VY_MIN) b->vy = GREAT_VY_MIN;
    b->y -= b->vy;
    b->x += b->vx;
    A_X(a) = b->x >> 16;
    A_Y(a) = y = b->y >> 16;
    base = lava_surface() == NO_GROUND ? b->base : lava_surface() + GREAT_BASE;
    if (base <= y && base > was) {
        Water_SpawnSplash(a);
        b->type = NULL;
        Actor_Despawn(a);
        return;
    }
    b->anim += GREAT_HEAD_SPEED;
}

/* a ball's draw (0x26C16): the sparks, then the ball (Flink's order: the first queued in front) */
static void __cdecl ball_draw(BYTE *a)
{
    Great *b = great_of(a, (void *)ball_update);
    int x = A_X(a), y = A_Y(a), k, n;
    WORD hide = A_HIDE(a);
    if (!b || !g_active) return;
    set_anim(a, "HEAD");
    n = A_NFRAMES(a) > 0 ? A_NFRAMES(a) : 1;
    for (k = 0; k < GREAT_SPARKS; k++) {
        int f = b->spark[k].phase >> 8, on = b->spark[k].y >= 0 && f < 3;
        lava_piece(a, "TAIL", on ? f : 0, b->spark[k].x + GREAT_HEAD_X, b->spark[k].y + GREAT_HEAD_Y, on);
    }
    lava_piece(a, "HEAD", (b->anim >> 8) % n, x, y, 1);
    A_HIDE(a) = hide;
    A_X(a) = x;
    A_Y(a) = y;
    set_anim(a, "HEAD");
}

/* a ball (0x26A58) */
static void throw_ball(BYTE *a, Great *s)
{
    BYTE *d = Actor_Alloc(a, NULL);
    Great *b;
    int k, v;
    static const int SPARK0[GREAT_SPARKS] = { 0x400, 0x2AB, 0x156 };   /* 0x2EE0C */
    if (!d || (k = slot_of(d)) < 0) return;
    b = &g_great[k];
    memset(b, 0, sizeof(*b));
    b->base = s->base;
    b->x = A_X(a) << 16;
    b->y = s->base << 16;
    b->vy = ((lava_rand() & 7) + 20) << 13;
    v = ((lava_rand() & 7) + 1) << 13;
    b->vx = s->countdown & 0x10 ? -v : v;
    for (k = 0; k < GREAT_SPARKS; k++) {
        b->spark[k].phase = SPARK0[k];
        b->spark[k].x = b->spark[k].y = -1;
    }
    b->type = A_TYPE(d);
    b->update = (void *)ball_update;
    A_X(d) = A_X(a);
    place_y(d, s->base);
    A_VX(d) = A_VY(d) = 0;
    A_FLAGS(d) = 0;                                  /* touches nothing: the hurt is Flink's (ball_update) */
    A_CULL(d) = CULL_NEVER;
    A_LAYER(d) = (short)FLINK_LAYER(1);              /* (a5) = 0x7D6C: list 1 */
    A_UPDATE(d) = (void *)ball_update;
    A_DRAW(d) = (void *)ball_draw;
    A_RECORD(d) = NULL;                              /* (Actor_Alloc leaves the slot's last one) */
    Actor_SetAnim(d, FOURCC("HEAD"));
    if (!A_RECORD(d)) { b->type = NULL; Actor_Despawn(d); }
}

/* the spawner (0x269E4) */
static void __cdecl great_update(BYTE *a)
{
    Great *s = great_of(a, (void *)great_update);
    if (!s || !g_active) return;
    if (!s->ready) {
        if (lava_surface() == NO_GROUND) return;
        s->ready = 1;
        s->base = lava_surface() + GREAT_BASE;
        place_y(a, s->base);
        flog("great balls of fire at (%d,%d): timer %d", A_X(a), s->base, s->timer);
    }
    if (--s->countdown < 0) s->countdown = s->timer;
    if (s->countdown < GREAT_THROWS && !(s->countdown & (GREAT_EVERY - 1))) {
        if (enemy_on_screen(a)) flink_sound(a, SND_GREAT_THROW);
        throw_ball(a, s);
    }
    if (!(++s->count & 7)) lava_drop_add(s->drop, s->count, A_X(a) - 16, s->base - 0x1B, 0);
    lava_drops_move(s->drop);
}

static void __cdecl great_draw(BYTE *a)
{
    Great *s = great_of(a, (void *)great_update);
    int x = A_X(a), y = A_Y(a);
    WORD hide = A_HIDE(a);
    if (!s || !g_active || !s->ready) return;
    lava_drops_draw(a, s->drop, "LAVA", GREAT_DROP_X, GREAT_DROP_Y);
    A_HIDE(a) = hide;
    A_X(a) = x;
    A_Y(a) = y;
}

/* (from spik_spawn) param: timer */
void great_spawn(BYTE *a, BYTE *def)
{
    int i = slot_of(a), k;
    Great *s;
    if (i < 0 || !def || T_NPARAMS(A_TYPE(a)) < 1 || !record_of(a, FOURCC("HEAD")) ||
        !record_of(a, FOURCC("TAIL")) || !record_of(a, FOURCC("LAVA"))) return;
    s = &g_great[i];
    memset(s, 0, sizeof(*s));
    for (k = 0; k < LAVA_DROPS; k++) s->drop[k].t = -1;
    s->timer = D_PARAM(def, 0);
    s->countdown = GREAT_FIRST;
    s->type = A_TYPE(a);
    s->update = (void *)great_update;
    A_FLAGS(a) &= ~(0x200000u | AF_CONTACT);
    A_LAYER(a) = (short)FLINK_LAYER(1);
    A_UPDATE(a) = (void *)great_update;
    A_DRAW(a) = (void *)great_draw;
    set_anim(a, "LAVA");
}
