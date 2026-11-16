/*
 * burst.c - Flink's Burst of lava ('Burst of lava', type 54, init 0x26EF4, thinks 0x26F76 / 0x26FA8,
 * draws 0x27006 / 0x2703C; MOUNTAIN 14 = 23-1; README "Lava"): a column of lava shooting up out of the
 * lava and off the top of the screen. Flink's:
 * - Init: draw list 1; its y the lava's surface (no + 10); params 'length' (+0x46) and 'timer'
 *   (+0x48); the wait (+0x4A) 30. ZWEI's two frames (+0x4C, +0x50: the body), DREI's (+0x54, +0x58:
 *   the ends); it shows EINS (the bubble).
 * - Waiting (0x26F76): the wait runs down, nothing drawn (0x3333E); at 0 it is 'timer' again, the
 *   rise (+0x5C) -80, the bob (+0x5E) 7, and it bursts (think 0x26FA8, draw 0x27006, drawn at once).
 * - Bursting (0x26FA8): once the bubble is gone (draw 0x2703C) and nothing of it was drawn on the
 *   screen (+6 bit 4, set by Actors_DrawAll 0x334FE when the draw queued a sprite) it waits again
 *   (this frame still runs). At a rise of 0 sound 0x4A (on screen). The rise + 4; at ('length' + 4)
 *   * 16 the bob is -7.
 * - Its draw while the bubble shows (0x27006): EINS at y + 2 * the bob's step: a bob of n > 0 goes
 *   to n - 1 (14 px down coming up to the surface over 7 frames); from -7 it goes back down (0, 2,
 *   4, 6, 8, 10 px) and then the bubble is gone (draw 0x2703C).
 * - The column (0x2703C): from the top, y - the rise, down to the lava's surface - 10 (only the
 *   16 px rows whose top is above that): DREI (an end), 'length' rows of ZWEI, DREI flipped upside
 *   down; each row's frame 0 or 1, (the frame counter & 4) alternating row by row. Each row is two
 *   halves, the left one at x - 24 and the right one, flipped, at x: the records' frames. Its hurt
 *   box (0x1A46E): x +- 16, from the top to the top of the first row not drawn.
 * Here: the converter's 'Spik Burst' (Lomax's decoration, object 51) with Flink's EINS (2 frames),
 * ZWEI, DREI and DRUV (DREI flipped upside down), each piece at Flink's offsets (EINS at (-32, -24)
 * and (0, -24); ZWEI, DREI at (-24, -16) and (0, -16): a row's anchor is its top + 16); params length,
 * timer. "Drawn on the screen" is a drawn row's or the bubble's sprite inside Flink's box on Lomax's
 * view (lava_seen). The draw is one Actor_Draw a piece in a fixed order (lava.c's lava_piece).
 */
#include "flink.h"
#include "enemy.h"

#define BURST_FIRST      30              /* the first wait (0x26F1C) */
#define BURST_RISE0      (-80)           /* (0x26F9A) */
#define BURST_BOB        7               /* (0x26FA0) */
#define BURST_STEP       4               /* the rise a frame (0x26FEE) */
#define BURST_ROW        16
#define BURST_LIMIT      10              /* rows above the surface - 10 (0x27052) */
#define BURST_HALF_W     16              /* the hurt box (0x2711C) */
#define BURST_ROWS_MAX   16              /* rows of ZWEI drawn at most ('length' is 8 and 12 in 23-1) */
#define BURST_HEAD_SPEED 51              /* EINS's record speed (8.8 frames a tick) */
#define BURST_HEAD_DY    24              /* EINS's top: y - 24 */
#define SND_BURST        0x4A            /* (0x26FDA) */

enum { BURST_WAIT, BURST_BUBBLE, BURST_COLUMN };

typedef struct {
    BYTE *type;
    int ready, y, length, timer;         /* the surface (+0x18), +0x46, +0x48 */
    int state, wait, rise, bob, off;     /* +0x4A, +0x5C, +0x5E, the bubble's offset this tick */
    int frame, anim, seen;               /* the frame counter, EINS's position (8.8), drawn on the screen */
} Burst;
static Burst g_burst[MAX_ACTORS];

void burst_reset(void) { memset(g_burst, 0, sizeof(g_burst)); }

static Burst *burst_of(const BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_burst[i].type && g_burst[i].type == A_TYPE(a) ? &g_burst[i] : NULL;
}

/* the bubble's bob (0x27006): its offset this frame; the bubble gone: BURST_COLUMN */
static int bubble_bob(Burst *b)
{
    int d = b->bob;
    if (!d) return 0;
    if (d < 0) {
        d += BURST_BOB;
        b->bob += 2;
        if (b->bob >= 0) b->state = BURST_COLUMN;
    }
    b->bob--;
    return d * 2;
}

/* the column's rows (0x2703C): calls row(k, kind, frame, top) for each (kind 0 the top end, 1 the
   body, 2 the bottom end), returns the top of the first row not drawn */
static int column_rows(const BYTE *a, const Burst *b, void (*row)(BYTE *, int, int, int, int))
{
    int limit = (lava_surface() == NO_GROUND ? b->y : lava_surface()) - BURST_LIMIT;
    int t = b->y - b->rise, f = (b->frame >> 2) & 1, k = 0, n;
    if (t >= limit) return t;
    if (row) row((BYTE *)a, k, 0, f, t);
    k++; f ^= 1;
    for (n = 0; n < b->length; n++) {
        t += BURST_ROW;
        if (t >= limit) return t;
        if (row) row((BYTE *)a, k, 1, f, t);
        k++; f ^= 1;
    }
    t += BURST_ROW;
    if (t < limit && row) row((BYTE *)a, k, 2, f, t);
    return t;
}

static int g_seen;                       /* (column_seen) */
static void column_seen(BYTE *a, int k, int kind, int f, int t)
{
    (void)k; (void)kind; (void)f;
    g_seen |= lava_seen(a, A_X(a) - 24, t);
}

static void __cdecl burst_update(BYTE *a)
{
    Burst *b = burst_of(a);
    int top, bottom;
    if (!b || !g_active) return;
    if (!b->ready) {
        if (lava_surface() == NO_GROUND) return;
        b->ready = 1;
        b->y = lava_surface();
        place_y(a, b->y);
        flog("burst of lava at (%d,%d): length %d, timer %d", A_X(a), b->y, b->length, b->timer);
    }
    b->frame++;
    if (b->state == BURST_WAIT) {                    /* 0x26F76 */
        if (--b->wait > 0) return;
        b->wait = b->timer;
        b->state = BURST_BUBBLE;
        b->rise = BURST_RISE0;
        b->bob = BURST_BOB;
    } else {                                         /* 0x26FA8 */
        if (!b->seen && b->state == BURST_COLUMN) b->state = BURST_WAIT;
        if (!b->rise && b->seen) flink_sound(a, SND_BURST);
        b->rise += BURST_STEP;
        if (b->rise == (b->length + 4) * BURST_ROW) b->bob = -BURST_BOB;
    }
    /* the draw's state (0x27006 / 0x2703C): this frame's bubble, what shows, the hurt box */
    b->off = 0;
    g_seen = 0;
    if (b->state == BURST_BUBBLE) {
        b->anim += BURST_HEAD_SPEED;
        b->off = bubble_bob(b);
        g_seen = lava_seen(a, A_X(a) - 32, b->y + b->off - BURST_HEAD_DY);
    }
    top = b->y - b->rise;
    bottom = column_rows(a, b, column_seen);
    b->seen = g_seen;
    if (b->state != BURST_WAIT && lava_touches(a, A_X(a) - BURST_HALF_W, top, A_X(a) + BURST_HALF_W, bottom)) {
        flog("burst of lava at (%d,%d) hurts Lomax at (%d,%d)", A_X(a), b->y, *G_PLAYERX, *G_PLAYERY);
        lava_hurt_by(0, 0);
    }
}

/* the rows drawn this tick: kind (-1 none) and frame, by row number (0 the top end) */
static int g_kind[BURST_ROWS_MAX + 2], g_frame[BURST_ROWS_MAX + 2], g_top[BURST_ROWS_MAX + 2];
static void column_note(BYTE *a, int k, int kind, int f, int t)
{
    (void)a;
    if (k < BURST_ROWS_MAX + 2) { g_kind[k] = kind; g_frame[k] = f; g_top[k] = t; }
}

/* the bubble, then the rows from the top (Flink's order: the first queued in front) */
static void __cdecl burst_draw(BYTE *a)
{
    static const char *const TAGS[3] = { "DREI", "ZWEI", "DRUV" };
    Burst *b = burst_of(a);
    int x = A_X(a), y = A_Y(a), k;
    WORD hide = A_HIDE(a);
    if (!b || !g_active || !b->ready) return;
    for (k = 0; k < BURST_ROWS_MAX + 2; k++) g_kind[k] = -1;
    if (b->state != BURST_WAIT) column_rows(a, b, column_note);
    lava_piece(a, "EINS", (b->anim >> 8) & 1, x, b->y + b->off, b->state == BURST_BUBBLE);
    for (k = 0; k < BURST_ROWS_MAX + 2; k++) {
        int on = g_kind[k] >= 0;
        lava_piece(a, on ? TAGS[g_kind[k]] : "ZWEI", on ? g_frame[k] : 0, x, on ? g_top[k] + BURST_ROW : y, on);
    }
    A_HIDE(a) = hide;
    A_X(a) = x;
    A_Y(a) = y;
}

/* (from spik_spawn) params: length, timer */
void burst_spawn(BYTE *a, BYTE *def)
{
    int i = slot_of(a);
    Burst *b;
    if (i < 0 || !def || T_NPARAMS(A_TYPE(a)) < 2 || !record_of(a, FOURCC("EINS")) || !record_of(a, FOURCC("ZWEI")) ||
        !record_of(a, FOURCC("DREI")) || !record_of(a, FOURCC("DRUV"))) return;
    b = &g_burst[i];
    memset(b, 0, sizeof(*b));
    b->length = D_PARAM(def, 0);
    if (b->length > BURST_ROWS_MAX) b->length = BURST_ROWS_MAX;
    b->timer = D_PARAM(def, 1);
    b->wait = BURST_FIRST;
    b->type = A_TYPE(a);
    A_FLAGS(a) &= ~(0x200000u | AF_CONTACT);
    A_LAYER(a) = (short)FLINK_LAYER(1);          /* (a5) = 0x7D6C: list 1 */
    A_UPDATE(a) = (void *)burst_update;
    A_DRAW(a) = (void *)burst_draw;
    set_anim(a, "EINS");
}
