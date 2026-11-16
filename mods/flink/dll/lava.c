/*
 * lava.c - Flink's lava (MOUNTAIN maps 14, 15 = 23-1, 23-2; README "Lava"): the Lava column ('Lava',
 * type 55, init 0x2712A, think 0x271F8, draw 0x273BE) and the lava water's touch (Flink's Water with
 * checkbox 1 'lava', think 0x2E976).
 *
 * The Lava column, Flink's:
 * - Init: two blocks of its own (0x32DB4): 4 droplets (+0x56, 0x12 bytes each: timer, x, y, vx, vy,
 *   16.16; the timers -1) and 45 tail words (+0x58, cleared). Draw list 1 (0x7D6C), window 0x18. The
 *   head starts at the base, g_WaterSurfaceY (0x8718) + 10 (+0x18; the base kept in +0x48), going up
 *   at 2.0 (+0x20, up is positive); the burst toggle (+0x46) 0, the tail timer (+0x5A) 2. It keeps
 *   TAIL's three frames' hardware sprites (0x34132) and shows HEAD (2 frames at speed 64).
 * - Each frame (0x271F8): vy - 1/16 down to -4.0, y - vy. Falling below the base (the live surface
 *   + 10), the toggle flips: on, a burst, vy ((rand & 7) + 30) / 8 (3.75 .. 4.625: about 140 px up)
 *   and sound 0x4A; off, a hop, vy 2.0 (32 px) and sound 0x30 (both only on screen, +6 bit 4). Every
 *   8 frames a droplet (slot (n >> 3) & 3) at the base, its sprite's top-left at (x - 16, base - 27),
 *   timer 23, vx ((rand & 7) + 3) / 8, vy ((rand & 7) + 23) / 8 (each + 4 and + 12 during a hop going
 *   up), vx negated while bit 4 of the frame count is set. When the tail timer runs out (it then is
 *   4 - the integer part of vy: a blob every frame while the head flies up fast, every 9 at -4.0) the
 *   first free tail word takes the head's height over the base + 8. The hurt box (0x1A46E): x +- 8,
 *   from the head's y down to the base.
 * - Its draw (0x273BE): each droplet with a timer left falls (vy - 0.1875 a frame) and is drawn as
 *   TAIL frame 2 - timer / 8 (shrinking); each tail word counts its age (high byte) up and its height
 *   (low byte) 4 px down, freed below 0, and is drawn at (x - 16, base - 28 - height) as TAIL frame
 *   {2, 1, 0, 2}[(age >> 2) & 3]; then the head (0x328C4). Flink queues them in that order, the
 *   first in front.
 * Here: the converter's 'Spik Lava' (Lomax's decoration, object 51) with Flink's HEAD and TAIL (each
 * piece at Flink's (-16, -32), so Sprite_DrawScreen's top-left (X, Y) is the anchor (X + 16, Y + 32)).
 * The motion is all in the update; the draw is one Actor_Draw per head, tail word and droplet (50
 * every tick, in the same order, empty ones with their piece hidden through +0x70), the actor moved
 * onto each: Lomax+'s FPS Override pairs each call with the previous tick's call of the same number.
 * The base is read at the first tick the map's water runs (water_present), since the Lava may spawn
 * before the water's first update; without water the column waits. The hurt is Flink's box against
 * Lomax's hitbox, then lava_hurt.
 *
 * The lava's touch (Flink's Water think 0x2E976 with checkbox 1): with the player 25 px under the
 * surface (g_PlayerFlags bit 3, "in water": his y - 0x19 below g_WaterSurfaceY) he dies (0x1A8D0);
 * otherwise below the surface and not in the air (bit 0 clear) he is hurt (Player_Hurt 0x1A7DC):
 * thrown up at 5.0 and back at 2.0 against the way he faces (the water has no x speed, 0x1A828).
 * So a lava pit kills, and lava with a floor less than 25 px under the surface (23-1's at x 1200-1696
 * and 1824-2144) hurts whoever walks into it. Here (lava_player, from water.c's water_update):
 * player_kill, and Lomax's Player_TakeHit, which throws him up at 5.0 (with his helmet: he loses it;
 * without, he dies) and back at 2.5 from the x it is given; Lomax swims, floats or drowns in no lava.
 *
 * Shared with the other lava kinds (protuberance.c, greatballs.c, burst.c, fountain.c): the
 * droplets (Lava's, Protuberance's and Great balls of fire's code is the same, 0x272CA / 0x2673A /
 * 0x26AD8 and their draws), Flink's hurt box (0x1A46E) and Player_Hurt's direction (0x1A80C), the
 * pieces drawn one Actor_Draw each, and Flink's on-screen box for a sprite (Sprite_DrawScreen
 * 0x3430E, as butterfly.c's).
 */
#include "flink.h"
#include "enemy.h"

#define LAVA_TAIL         45             /* (+0x58: dbra from 0x2C) */
#define LAVA_BASE         10             /* the base: the surface + 10 (0x2719A) */
#define LAVA_GRAVITY      0x1000         /* vy's step a frame (0x2724C) */
#define LAVA_VY_MIN       (-0x40000)     /* (0x27242) */
#define LAVA_HOP          0x20000        /* (0x2727A) */
#define LAVA_DROP_EVERY   8              /* frames between droplets (0x272D4) */
#define LAVA_DROP_LIFE    0x17           /* (0x272EE) */
#define LAVA_DROP_GRAVITY 0x3000         /* (0x273D4) */
#define LAVA_DROP_X       16             /* its top-left: x - 16, base - 0x1B (0x272F6, 0x27302) */
#define LAVA_DROP_Y       0x1B
#define FLINK_EDGE        32             /* Sprite_DrawScreen's box: -32 .. the screen, exclusive (0x34340) */
#define FLINK_VIEW_W      320
#define FLINK_VIEW_H      224
#define LAVA_TAIL_Y       0x1C           /* a blob's top-left's y: base - 0x1C - height (0x27432) */
#define LAVA_TAIL_FALL    4              /* px a frame (0x2744A) */
#define LAVA_HALF_W       8              /* the hurt box's half width (0x273AC) */
#define LAVA_SPRITE_X     16             /* the pieces' offset in TAIL and HEAD (-16, -32) */
#define LAVA_SPRITE_Y     32
#define LAVA_HEAD_SPEED   0x40           /* HEAD's record speed (8.8 frames a tick) */
#define LAVA_DEEP         0x19           /* the player's y this far under the surface: dead (0x828E bit 3) */
#define SND_LAVA_BURST    0x4A           /* (0x272C0) */
#define SND_LAVA_HOP      0x30           /* (0x2728E) */

static const int TAIL_FRAME[4] = { 2, 1, 0, 2 };   /* 0x2747E: +0x4A, +0x4E, +0x52 = TAIL frames 2, 1, 0 */

typedef struct {
    BYTE *type;
    int ready, base;                     /* the base (+0x48), read once the water runs */
    int y, vy;                           /* the head's 16.16 y (+0x18) and speed (+0x20, up positive) */
    int burst, count, timer, anim;       /* +0x46, +0x5C, +0x5A, HEAD's position (8.8) */
    LavaDrop drop[LAVA_DROPS];
    struct { int age, h; } tail[LAVA_TAIL];             /* h 0 and age 0: free (the word 0) */
} Lava;
static Lava g_lava[MAX_ACTORS];

void lava_reset(void) { memset(g_lava, 0, sizeof(g_lava)); }

int lava_rand(void) { static unsigned r = 0x5A17C0DEu; r = r * 1103515245u + 12345u; return (int)(r >> 16) & 0x7FFF; }

/* the lava's surface (g_WaterSurfaceY), or NO_GROUND while the map's water hasn't run yet */
int lava_surface(void) { return water_present() ? *G_WATERLEVELY : NO_GROUND; }

/* a droplet in slot (n >> 3) & 3 from the top-left x, y (0x272DC): up, the faster one of a hop
   going up (the Lava column's only) */
void lava_drop_add(LavaDrop *d, int n, int x, int y, int up)
{
    LavaDrop *o = &d[(n >> 3) & 3];
    int v;
    o->t = LAVA_DROP_LIFE;
    o->x = x << 16;
    o->y = y << 16;
    v = ((lava_rand() & 7) + 3 + (up ? 4 : 0)) << 13;
    o->vx = n & 0x10 ? -v : v;
    o->vy = ((lava_rand() & 7) + 23 + (up ? 12 : 0)) << 13;
}

/* the droplets' fall (0x273BE's first loop); off: -1 */
void lava_drops_move(LavaDrop *d)
{
    int k;
    for (k = 0; k < LAVA_DROPS; k++)
        if (--d[k].t >= 0) {
            d[k].vy -= LAVA_DROP_GRAVITY;
            d[k].y -= d[k].vy;
            d[k].x += d[k].vx;
        } else
            d[k].t = -1;
}

/* the droplets as tag's frame 2 - timer / 8 (shrinking), each its top-left + (ax, ay) */
void lava_drops_draw(BYTE *a, const LavaDrop *d, const char *tag, int ax, int ay)
{
    int k;
    for (k = 0; k < LAVA_DROPS; k++) {
        int on = d[k].t >= 0, t = on ? d[k].t >> 3 : 0;
        lava_piece(a, tag, 2 - (t > 2 ? 2 : t), (d[k].x >> 16) + ax, (d[k].y >> 16) + ay, on);
    }
}

/* Flink's hurt box (0x1A46E: inclusive) against Lomax's hitbox, when he can be hurt */
int lava_touches(const BYTE *a, int x1, int y1, int x2, int y2)
{
    const BYTE *pa = *G_PLAYERANIM;
    int px = *G_PLAYERX, py = *G_PLAYERY;
    if (!pa || A_PLANE(a) != *G_PLAYERPLANE || *G_PLAYERHURTTIMER || *G_PLAYERDEATHTIMER) return 0;
    return x2 >= px + HB_X1(pa) && x1 <= px + HB_X2(pa) && y2 >= py + HB_Y1(pa) && y1 <= py + HB_Y2(pa);
}

/* a sprite's top-left inside Flink's box on Lomax's view (Sprite_DrawScreen queues it) */
int lava_seen(const BYTE *a, int x, int y)
{
    const BYTE *pl = PLANE(A_PLANE(a));
    int w = P_W(pl) * 16 - P_MAXX(pl), sx = x - P_CAMX(pl), sy = y - P_CAMY(pl);
    if (P_MAXX(pl) <= 0 || w < FLINK_VIEW_W) w = FLINK_VIEW_W;
    return sx > -FLINK_EDGE && sx < w && sy > -FLINK_EDGE && sy < FLINK_VIEW_H;
}

static Lava *lava_of(const BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_lava[i].type && g_lava[i].type == A_TYPE(a) ? &g_lava[i] : NULL;
}

/* Player_Hurt (0x1A7DC), thrown back at 2.0 (Lomax's hit: 2.5): the way the hurting actor faces
   (left: its +3 bit 3) when its x speed is faster than his, else against the way he faces */
void lava_hurt_by(int vx, int left)
{
    int px = *G_PLAYERX, right;
    if (abs(vx) > abs(*G_PLAYERVX)) right = !left;
    else right = (*G_PLAYERFLAGS & AF_LEFT) != 0;
    Player_TakeHit(right ? px - 1 : px + 1, 0, 0);
}
static void lava_hurt(void) { lava_hurt_by(0, 0); }

/* the lava water's touch (0x2E976), surface: its y (g_WaterLevelY) */
void lava_player(const BYTE *water)
{
    int s = A_Y(water), feet = *G_PLAYERY;
    if (!g_active || *G_PLAYERDEATHTIMER || *G_PLAYERSTATE & (PS_FLY | PS_FROZEN) || *G_PLAYERPLANE != A_PLANE(water)) return;
    if (feet - LAVA_DEEP > s) {
        flog("Lomax sank into the lava at (%d,%d), surface %d", *G_PLAYERX, feet, s);
        player_kill();
    } else if (feet > s && !(*G_PLAYERSTATE & PS_AIR) && !*G_PLAYERHURTTIMER) {
        flog("the lava hurts Lomax at (%d,%d), surface %d", *G_PLAYERX, feet, s);
        lava_hurt();
    }
}

/* the think (0x271F8) and the draw's motion (0x273BE) */
static void __cdecl lava_update(BYTE *a)
{
    Lava *l = lava_of(a);
    int k, x = A_X(a), y;
    if (!l || !g_active) return;
    if (!l->ready) {
        if (lava_surface() == NO_GROUND) return;
        l->ready = 1;
        l->base = lava_surface() + LAVA_BASE;
        l->y = l->base << 16;
        flog("lava column at (%d,%d): base %d", x, A_Y(a), l->base);
    }
    if (l->vy > LAVA_VY_MIN) l->vy -= LAVA_GRAVITY;
    l->y -= l->vy;
    if (l->vy < 0 && (l->y >> 16) > *G_WATERLEVELY + LAVA_BASE) {
        l->burst = !l->burst;
        if (l->burst) {
            l->vy = ((lava_rand() & 7) + 30) << 13;
            if (enemy_on_screen(a)) flink_sound(a, SND_LAVA_BURST);
        } else {
            l->vy = LAVA_HOP;
            if (enemy_on_screen(a)) flink_sound(a, SND_LAVA_HOP);
        }
    }
    y = l->y >> 16;
    if (!(++l->count & (LAVA_DROP_EVERY - 1)))       /* a droplet */
        lava_drop_add(l->drop, l->count, x - LAVA_DROP_X, l->base - LAVA_DROP_Y, !l->burst && l->vy >= 0);
    if (--l->timer < 0) {                            /* a tail blob */
        l->timer = 4 - (l->vy >> 16);
        for (k = 0; k < LAVA_TAIL && (l->tail[k].age || l->tail[k].h); k++) ;
        if (k < LAVA_TAIL) {
            l->tail[k].age = 0;
            l->tail[k].h = (l->base - y + 8) & 0xFF;
        }
    }
    l->anim += LAVA_HEAD_SPEED;
    place_y(a, y);
    if (lava_touches(a, x - LAVA_HALF_W, y, x + LAVA_HALF_W, l->base)) {   /* 0x1A46E */
        flog("lava column at (%d,%d) hurts Lomax at (%d,%d)", x, y, *G_PLAYERX, *G_PLAYERY);
        lava_hurt();
    }
    lava_drops_move(l->drop);                        /* the draw's motion */
    for (k = 0; k < LAVA_TAIL; k++)
        if (l->tail[k].age || l->tail[k].h) {
            l->tail[k].age = (l->tail[k].age + 1) & 0xFF;
            if ((l->tail[k].h -= LAVA_TAIL_FALL) < 0) l->tail[k].age = l->tail[k].h = 0;
        }
}

/* one piece: the record's frame at the anchor, or nothing (its pieces hidden: the Actor_Draw call
   still made, so that Lomax+'s FPS Override pairs the calls tick by tick) */
void lava_piece(BYTE *a, const char *tag, int frame, int x, int y, int shown)
{
    set_anim(a, tag);
    hold_frame(a, frame);
    Actor_AdvanceAnim(a);
    A_X(a) = x;
    A_Y(a) = y;
    A_HIDE(a) = shown ? 0 : 0xFFFF;
    draw_actor(a);
}

/* 0x273BE's sprites in Flink's order, the droplets, the tail words, the head: the first queued in
   front, in Flink's sprite list as within a Lomax draw bucket (chests.c) */
static void __cdecl lava_draw(BYTE *a)
{
    Lava *l = lava_of(a);
    int x = A_X(a), y = A_Y(a), k;
    WORD hide = A_HIDE(a);
    if (!l || !g_active || !l->ready) return;
    lava_drops_draw(a, l->drop, "TAIL", LAVA_SPRITE_X, LAVA_SPRITE_Y);
    for (k = 0; k < LAVA_TAIL; k++) {
        int on = l->tail[k].age || l->tail[k].h;
        lava_piece(a, "TAIL", TAIL_FRAME[(l->tail[k].age >> 2) & 3], x,
                   l->base - LAVA_TAIL_Y - l->tail[k].h + LAVA_SPRITE_Y, on);
    }
    lava_piece(a, "HEAD", (l->anim >> 8) & 1, x, y, 1);
    A_HIDE(a) = hide;
    A_X(a) = x;
    A_Y(a) = y;
    set_anim(a, "HEAD");
}

/* (from spik_spawn) no params */
void lava_spawn(BYTE *a, BYTE *def)
{
    int i = slot_of(a), k;
    Lava *l;
    (void)def;
    if (i < 0 || !record_of(a, FOURCC("HEAD")) || !record_of(a, FOURCC("TAIL"))) return;
    l = &g_lava[i];
    memset(l, 0, sizeof(*l));
    for (k = 0; k < LAVA_DROPS; k++) l->drop[k].t = -1;
    l->vy = LAVA_HOP;
    l->timer = 2;
    l->type = A_TYPE(a);
    A_FLAGS(a) &= ~(0x200000u | AF_CONTACT);     /* the hurt is Flink's box (lava_update) */
    A_LAYER(a) = (short)FLINK_LAYER(1);          /* (a5) = 0x7D6C: list 1 */
    A_UPDATE(a) = (void *)lava_update;
    A_DRAW(a) = (void *)lava_draw;
    set_anim(a, "HEAD");
}
