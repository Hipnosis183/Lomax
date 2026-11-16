/*
 * circ.c - META's ring of heads, Flink's 'Circling suckers' (init 0x237D8, think 0x238A6, draw 0x23944;
 * 30-1, each one spawned by a Generator at its x): a 'Muta Circ n' type with Flink's records FLY (as
 * Flink's draw puts it: see the converter's circ_records) and DEAD (README "Enemies"). Params: 0, then
 * Flink's 'how many', 'radius', 'turn-speed' and 'shrink-speed'.
 * Flink's init (the placement's actor): FLY's two frames' hardware sprites (0x34132), then 'how many'
 * children (Actor_Alloc 0x32CD8), evenly spaced round the circle (0x400 / how many apart, from 0), each
 * with think 0x238A6, draw 0x23944, draw list 1 (0x7D6C), activation window 8, stompable (+6 bit 3),
 * FLY, the centre (+0x4E, +0x50: the placement's point), its angle (+0x52), the radius * 4 (+0x54, 16.16),
 * the turn speed (+0x58) and the shrink speed * 0x28F (+0x5A: radius / 100 a frame, in quarter px) and
 * an animation speed of 0x10..0x17 (+0x32, Rand_GetNext 0x19C8A); then it is freed (Actor_Free 0x32D9E:
 * its pending bit stays clear, so it never spawns again until a life is lost). The children's pending
 * bit is Actor_Alloc's spare one (+0x0C = 0x7F00), so Enemy_Die forgets nothing.
 * The think, every frame:
 * - hit (+3 bit 6: a thrown object, Flink's slide or a spell) or stomped (Actor_CheckStompedByPlayer
 *   0x1A352): Enemy_Die (0x1B12E). Otherwise touching it hurts (0x1A462, hidden or not).
 * - the frame (+0x30, 8.8) goes on by the animation speed, between FLY's two frames;
 * - the radius goes down by the shrink speed, the angle on by the turn speed (1024 a turn);
 * - x = centre x + sin(angle) * radius, y = centre y + cos(angle) * radius (Flink's table 0x1491E,
 *   0x4000 = 1.0; the product's high word);
 * - under 200 (50 px) it blinks (+3 bit 1 toggled every frame), under 80 (20 px) it is freed.
 * Its draw puts the frame's hardware sprite (Sprite_DrawScreen 0x3430E) with its top-left at x - 12,
 * y - 24 (frame 1: x - 15, y - 23). Off the screen and outside Flink's mid window (Actors_DrawAll
 * 0x3351E: camera - (128, 112), 576 x 480) it is despawned, and gone (no placement to bring it back).
 * Here: Lomax's stomp, spin, roll and thrown helmet kill it at once (as Flink's slide and spells),
 * thrown objects and spells through enemy_struck; enemy_die as every Flink enemy's. The placement's
 * actor makes the ring on its first tick and is removed with Actor_Kill, which keeps its placement
 * spawned (the Generator's chain spawns only placements that aren't), until the sub-level restarts.
 * The heads are Actor_Alloc children (no placement), never culled by Lomax: off Lomax's view and
 * outside Flink's mid window (its margins around Lomax's view, as wide as Lomax+ makes it) they are
 * removed. Blinking, a head is not drawn but still touches Lomax.
 */
#include <math.h>
#include "enemy.h"

#define CIRCLE          0x400
#define RADIUS_SCALE    4               /* +0x54 is the radius in quarter px */
#define SHRINK_SCALE    0x28F
#define ANIM_RAND       7               /* + ANIM_BASE: the animation speed (8.8 frames a frame) */
#define ANIM_BASE       0x10
#define BLINK_UNDER     0xC8
#define GONE_UNDER      0x50
#define WIN_LEFT        128             /* Flink's mid window around its 320 x 224 screen */
#define WIN_RIGHT       128
#define WIN_ABOVE       112
#define WIN_BELOW       144
#define FLINK_VIEW_W    320
#define FLINK_VIEW_H    224

/* a head (by its slot): the centre, angle, radius (16.16 quarter px), turn and shrink speeds, its
   8.8 frame and animation speed, blinking (+3 bit 1: hidden this frame). The ring's placement actor
   (by its slot): n, how many, and the rest for its heads */
typedef struct {
    int cx, cy, angle, radius, turn, shrink, t, speed, blink, n;
} Circ;
static Circ g_circ[MAX_ACTORS];
static short g_sine[1024];              /* Flink's table 0x1491E: round(0x4000 * sin) */

static int circ_rand(void) { static unsigned r = 0x3C91D5A7u; r = r * 1103515245u + 12345u; return (int)(r >> 16) & 0x7FFF; }

void circ_reset(void) { memset(g_circ, 0, sizeof(g_circ)); }

/* inside Flink's mid window: its margins around Lomax's view */
static int in_window(const BYTE *a)
{
    const BYTE *pl = PLANE(A_PLANE(a));
    int w = P_W(pl) * 16 - P_MAXX(pl), x = A_X(a) - P_CAMX(pl), y = A_Y(a) - P_CAMY(pl);
    if (P_MAXX(pl) <= 0 || w < FLINK_VIEW_W) w = FLINK_VIEW_W;
    return x >= -WIN_LEFT && x <= w + WIN_RIGHT && y >= -WIN_ABOVE && y <= FLINK_VIEW_H + WIN_BELOW;
}

/* its point on the circle (0x238E4) */
static void circ_place(BYTE *a, const Circ *c)
{
    int r = (short)(c->radius >> 16);
    A_FX(a) = (((g_sine[c->angle & 0x3FF] * r) >> 16) + c->cx) << 16;
    A_FY(a) = (((g_sine[(c->angle + 0x100) & 0x3FF] * r) >> 16) + c->cy) << 16;
    set_pos(a);
}

static void circ_gone(BYTE *a, int i)
{
    g_enemy[i].type = NULL;
    Actor_Kill(a);
}

/* 0x238A6 */
static void __cdecl circ_update(BYTE *a)
{
    int i = enemy_slot(a);
    Circ *c;
    if (i < 0 || g_enemy[i].circ != 2) return;
    if (g_enemy[i].dead) { enemy_dead_update(a, i); return; }
    c = &g_circ[i];
    if (Actor_CheckHitByPlayer(a)) { enemy_die(a, i, 1); return; }   /* (as Flink's slide and spells) */
    c->t = (c->t + c->speed) & 0x1FF;
    hold_frame(a, c->t >> 8);
    c->radius -= c->shrink;
    c->angle = (c->angle + c->turn) & 0x3FF;
    circ_place(a, c);
    if ((short)(c->radius >> 16) < BLINK_UNDER) {
        c->blink ^= 1;
        if ((short)(c->radius >> 16) < GONE_UNDER) { circ_gone(a, i); return; }
    }
    if (!c->blink && !in_window(a)) { circ_gone(a, i); return; }    /* (Actors_DrawAll: drawn ones only) */
    Actor_AdvanceAnim(a);
}

/* 0x23944: not drawn while blinking */
static void __cdecl circ_draw(BYTE *a)
{
    int i = slot_of(a);
    if (i >= 0 && g_enemy[i].circ == 2 && !g_enemy[i].dead && g_circ[i].blink) return;
    draw_actor(a);
}

/* 0x237D8's loop: the heads */
static void __cdecl ring_update(BYTE *a)
{
    int i = enemy_slot(a), n, k, step;
    Circ c;
    if (i < 0 || g_enemy[i].circ != 1) return;
    c = g_circ[i];
    n = c.n;
    c.cx = A_X(a);
    c.cy = A_Y(a);
    step = n > 0 ? CIRCLE / n : 0;
    for (k = 0; k < n; k++) {
        BYTE *d = Actor_Alloc(a, NULL);
        int j;
        if (!d || (j = slot_of(d)) < 0) break;
        memset(&g_enemy[j], 0, sizeof(g_enemy[j]));
        g_enemy[j].type = A_TYPE(d);
        g_enemy[j].circ = 2;
        g_circ[j] = c;
        g_circ[j].angle = k * step;
        g_circ[j].speed = (circ_rand() & ANIM_RAND) + ANIM_BASE;
        A_FLAGS(d) = AF_HURTS | AF_HITTABLE;
        A_CULL(d) = CULL_NEVER;
        A_LAYER(d) = FLINK_LAYER(1);
        A_VX(d) = A_VY(d) = 0;
        A_UPDATE(d) = (void *)circ_update;
        A_DRAW(d) = (void *)circ_draw;
        A_RECORD(d) = NULL;                  /* (Actor_Alloc leaves the slot's last one) */
        Actor_SetAnim(d, FOURCC("FLY "));
        hold_frame(d, 0);
        circ_place(d, &g_circ[j]);
        Actor_AdvanceAnim(d);
    }
    flog("circling suckers at (%d,%d): %d of %d, radius %d", A_X(a), A_Y(a), k, n, (short)(c.radius >> 16) / RADIUS_SCALE);
    g_enemy[i].type = NULL;
    Actor_Kill(a);                           /* (Actor_Free: its placement stays spawned) */
}

/* (from enemy_spawn) 0x237D8: the placement's actor, unseen; it makes the ring on its first tick */
void circ_spawn(BYTE *a, int i, const BYTE *def)
{
    Circ *r = &g_circ[i];
    int k;
    if (!g_sine[256])
        for (k = 0; k < 1024; k++) g_sine[k] = (short)floor(16384.0 * sin(k * 3.14159265358979 / 512) + 0.5);
    memset(r, 0, sizeof(*r));
    g_enemy[i].circ = 1;
    r->n = def_param(a, def, 1, 0);
    r->radius = def_param(a, def, 2, 0) * RADIUS_SCALE << 16;
    r->turn = (short)def_param(a, def, 3, 0);
    r->shrink = (short)def_param(a, def, 4, 0) * SHRINK_SCALE;
    A_FLAGS(a) = (A_FLAGS(a) | AF_HIDDEN) & ~AF_CONTACT;
    A_UPDATE(a) = (void *)ring_update;
    Actor_SetAnim(a, FOURCC("FLY "));
}
