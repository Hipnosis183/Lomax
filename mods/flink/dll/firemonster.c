/*
 * firemonster.c - the Firemonster (Flink 0x2F226, think 0x2F2D8), FOREST (README "Enemies"). The
 * converter makes it a 'Muta Fire n' type with Flink's WALK, its flames FIRE and FADE, MAIN.WLD's
 * smoke DUST and BOOM (see the converter's ENEMIES); params: facing left, the mouth's offset (the
 * attach point with flags 6 in WALK's second frame).
 * - Walking: Actor_Walk with flags 0x9800 at 0x5555 (1/3 px) a frame (enemy_walk); where the
 *   ground under it ends it falls (0x1D10C: gravity 0x2400 a frame, at most 4.0, while vx goes to
 *   2.0 by 0x800 a frame) and lands where it finds ground. A timer of 120 + (random & 127) frames
 *   counts down; once it is out the Firemonster stops on WALK's second frame, mouth open.
 * - Breathing, on that frame (the mouth's hook 0x2F376, run as it is drawn): a counter t goes 1..159;
 *   at 30 the breath sound (Flink's 0x32). For t < 32 and t >= 96 a smoke puff rises from the mouth
 *   (DUST frame (t & 15) / 2, 4 px left of and 6 above it, rising 1 px a frame and drifting 1 px every
 *   2 forward, mirrored when it faces left). For 32 <= t < 96 (u = t - 32, w = u & 3) the flame
 *   (0x2F3B6): from 8 px left of and 10 above the mouth, moved w px forward and 2w down, min(u / 4, 4)
 *   + 1 segments, each 8 px further forward and 8 px lower than the last but never less than 16 px
 *   above the ground under its next spot, FIRE frames alternating (starting on frame (u / 2) & 1);
 *   segment k from the tip (0 = the last) is drawn only while k < (63 - u) / 4, so the flame leaves
 *   the mouth first. Once the flame is 5 segments long a FADE tip (frame w, none at w = 3) rises at
 *   its end, 3w px above it. The point past segment k with 4 - k = w hurts Flink (one segment a frame,
 *   cycling); here only while that segment is drawn (Flink's undrawn segments hurt too, unseen).
 *   At 160 it walks again with a new timer.
 * - One stomp, or Flink's slide or a spell, destroys it (0x1B1E8): forgotten for good, its current
 *   frame breaks up into its sprites, which fly up and fall (sound 0x2A). Here Lomax's stomp and
 *   attacks, and Actor_ShatterIntoPieces with BOOM (the mouth-open frame). Touching it hurts.
 */
#include "enemy.h"

#define FIRE_BREATH 160
/* WALK's attack face (the open-mouthed head) is a piece with hide selector 1: Flink's Actor_DrawFrame
   skips it while bit 8 of the state word is set (Firemonster init 0x2F240 sets it, the breath clears
   it, 0x2F340, and its end sets it again, 0x2F2F4). Lomax's Actor_Draw hides slot n while bit n of
   +0x70 is set; the converter keeps the slot. */
#define FIRE_FACE   2

static int fire_rand(void) { static unsigned r = 0x2545F491u; r = r * 1103515245u + 12345u; return (int)(r >> 16) & 0x7FFF; }

void fire_die(BYTE *a, int i)
{
    BYTE *coin;
    forget_placement(A_DEF(a));              /* gone for good, as the dummy */
    g_enemy[i].type = NULL;
    flink_sound(a, 0x2A);
    coin = enemy_defeated(a);
    Actor_ShatterIntoPieces(a, 0xC00);       /* pieces up at 2.3..3.8 px, as Flink's 1..4.9 */
    flog("firemonster at (%d,%d) destroyed%s", A_X(a), A_Y(a), coin ? ", drops a coin" : "");
}

/* the breath at counter t, piece by piece: fn(actor, record (NULL: not drawn), its frame, top-left
   x, y, mirror, the point that hurts or 0) */
typedef void (*FlameFn)(BYTE *a, const char *tag, int frame, int x, int y, int mirror, int hx, int hy);
static void fire_flames(BYTE *a, int i, FlameFn fn)
{
    int left = A_FLAGS(a) & AF_LEFT, t = g_enemy[i].t;
    int mx = A_X(a) + (left ? -g_enemy[i].mdx : g_enemy[i].mdx), my = A_Y(a) + g_enemy[i].mdy;
    if (t < 32 || t >= 96) {
        int k = (t & 15) >> 1;
        fn(a, "DUST", k, mx - 4 + (left ? -(k >> 1) : k >> 1), my - 6 - k, left ? 8 : 0, 0, 0);
    } else {
        int u = t - 32, w = u & 3, n = u >> 2, cap = n > 4, d6 = (63 - u) >> 2, f = (u >> 1) & 1, k, g;
        int x = mx - 8 + (left ? -w : w), y = my - 10 + 2 * w, step = left ? -8 : 8;
        if (cap) n = 4;
        for (k = n; k >= 0; k--, f ^= 1) {
            int shown = k < d6, px = x, py = y, hurt;
            x += step;
            y += 8;
            hurt = shown && 4 - k == w;
            fn(a, shown ? "FIRE" : NULL, f, px, py, 0, hurt ? x : 0, hurt ? y : 0);
            g = ground_y(a, x + 8, y);
            if (g != NO_GROUND && y > g - 16) y = g - 16;
        }
        if (cap && w < 3) fn(a, "FADE", w, x, y - 3 * w, 0, 0, 0);
    }
}

static void flame_hurt(BYTE *a, const char *tag, int frame, int x, int y, int mirror, int hx, int hy)
{
    const BYTE *pa = *G_PLAYERANIM;
    int px = *G_PLAYERX, py = *G_PLAYERY;
    (void)tag; (void)frame; (void)x; (void)y; (void)mirror;
    if (!hx || !pa || A_PLANE(a) != *G_PLAYERPLANE) return;
    if (hx >= px + HB_X1(pa) && hx <= px + HB_X2(pa) && hy >= py + HB_Y1(pa) && hy <= py + HB_Y2(pa)) {
        flog("firemonster's flame at (%d,%d) hurts Lomax", hx, hy);
        Player_TakeHit(hx, 0, 0);
    }
}

/* one flame sprite, one bucket over the Firemonster (Sprite_QueueWorld at its top-left) */
static void flame_draw(BYTE *a, const char *tag, int frame, int x, int y, int mirror, int hx, int hy)
{
    const BYTE *e, *bank, *type = A_TYPE(a);
    (void)hx; (void)hy;
    if (!tag || !(e = record_of(a, FOURCC(tag)))) return;
    e += frame * 8;                          /* (one piece a frame) */
    if (!(bank = G_ANIMBANKS[e[0] | (T_BANKS(type) & 0x100)])) return;
    Sprite_QueueWorld(PLANE(A_PLANE(a)), bank + e[1] * 0x38 + 0x10, x, y,
                      (unsigned)(Actor_GetDrawDepth(a) + 1) << 24 | ((e[6] ^ mirror) & 0x18), 20000);
}

static void __cdecl fire_draw(BYTE *a)
{
    int i = enemy_slot(a);
    if (i < 0) return;                       /* (gone for good: spawn_gone removes it) */
    draw_actor(a);
    if (g_enemy[i].breath) fire_flames(a, i, flame_draw);
}

/* falling where the ground ended (0x1D10C); 1 while it is in the air */
static int fire_fall(BYTE *a, int i)
{
    int *vx = &A_VX(a), *vy = &A_VY(a), to = A_FLAGS(a) & AF_LEFT ? -0x20000 : 0x20000, g, y;
    *vy += 0x2400;
    if (*vy > 0x40000) *vy = 0x40000;
    *vx = *vx < to ? (*vx + 0x800 > to ? to : *vx + 0x800) : (*vx - 0x800 < to ? to : *vx - 0x800);
    A_FX(a) += *vx;
    A_FY(a) += *vy;
    set_pos(a);
    y = A_Y(a);
    g = ground_y(a, A_X(a), y);
    if (g != NO_GROUND && y >= g && y - g <= 3 + (*vy >> 16)) {
        place_y(a, g);
        *vy = 0;
        return 0;
    }
    if (below_map(a)) {
        g_enemy[i].type = NULL;              /* out of the map */
        Actor_Kill(a);
    }
    return 1;
}

static void __cdecl fire_update(BYTE *a)
{
    int i = enemy_slot(a);
    if (i < 0) return;
    if (Actor_CheckHitByPlayer(a)) { fire_die(a, i); return; }
    if (g_enemy[i].breath) {
        if (++g_enemy[i].t == 30) flink_sound(a, 0x32);
        if (g_enemy[i].t >= FIRE_BREATH) {
            g_enemy[i].breath = 0;
            g_enemy[i].t = 120 + (fire_rand() & 127);
            A_HIDE(a) = FIRE_FACE;
        }
        else fire_flames(a, i, flame_hurt);
        return;
    }
    if (g_enemy[i].air) {
        g_enemy[i].air = fire_fall(a, i);
        return;
    }
    enemy_walk(a, i);
    set_anim(a, "WALK");
    Actor_AdvanceAnim(a);
    if (--g_enemy[i].t < 0 && A_FRAME(a) == 1) {
        A_HIDE(a) = 0;                       /* the attack face shows (0x2F340 clears state bit 8) */
        g_enemy[i].breath = 1;
        g_enemy[i].t = 0;
        flog("firemonster at (%d,%d) breathes", A_X(a), A_Y(a));
    }
}

/* (from enemy_spawn) params 1 and 2: the mouth's offset */
void fire_spawn(BYTE *a, int i, const BYTE *def)
{
    g_enemy[i].fire = 1;
    g_enemy[i].t = 120 + (fire_rand() & 127);
    if (def && T_NPARAMS(A_TYPE(a)) >= 3) {
        g_enemy[i].mdx = D_PARAM(def, 1);
        g_enemy[i].mdy = D_PARAM(def, 2);
    }
    A_UPDATE(a) = (void *)fire_update;
    A_HIDE(a) = FIRE_FACE;
    A_DRAW(a) = (void *)fire_draw;
}
