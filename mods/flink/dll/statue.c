/*
 * statue.c - the Statue (Flink 0x2829E, think 0x28358), FOREST: a stone head that shoots fireballs
 * (README "Enemies"). The converter makes it a 'Muta Statue n' type with Flink's STAY and SHOT, its
 * DEAD as BOOM and MAIN.WLD's clods ERDE; params: the checkboxes ('move left' bit 0) | 2 when a
 * Generator spawns it (Flink's spawn flag bit 5), 'period', 'speed', and the mouth's offset (STAY's
 * attach point with hook 1). Flink's behaviour:
 * - Rising (0x28316, Generator-spawned ones only): it starts 40 px under its placement, hidden behind
 *   the ground (draw hook 0x283B8 redraws the ground's cells over it), throws the clods
 *   (Fx_SpawnDebris 0x2E0BC, sound 0x49) and rises 2 px a frame. Its first shot comes 30 frames after
 *   it appears; for the others (never hidden) after 'period' frames. Here the ground hides it through
 *   the clip line (+0x7C), as the chests, and the clods are the chests' (ground_clods).
 * - Shooting: the mouth's hook (0x283F4, run as STAY is drawn) counts 'period' frames, then spawns a
 *   copy of the statue at the mouth (0x32CD8) as the shot: SHOT, think 0x28448. It waits 30 frames at
 *   the mouth (animating), then (0x28484, sound 0x2F) flies straight on at 'speed' * 0x28F (speed / 100
 *   px) a frame, left with 'move left', through walls, until it leaves the screen. Touching it hurts
 *   (0x1A462). Here the shot is an Actor_Alloc child of the statue, flagged to hurt only (not
 *   hittable), despawned 64 px off the view (it inherits the statue's cull phase, which is 3, never,
 *   for a Generator's); the count runs only while the statue is near the view, as Flink's while it
 *   is drawn.
 * - Touching the statue doesn't hurt. A stomp only clanks (Actor_CheckStompedByPlayer 0x1A352, sound
 *   0x18, as a locked chest). Flink's slide, its spells and thrown objects (+3 bit 6) break it
 *   (0x28392): a shot still waiting at its mouth goes out, DEAD breaks up (0x1B1E8, forgotten for
 *   good). Here Lomax's roll down a slope (TUMB, Flink's slide) and thrown objects (enemy_struck)
 *   break it with Actor_ShatterIntoPieces (BOOM = DEAD); his spin and the helmet only clank, as a
 *   stomp (enemy_attacked). It counts as defeated and drops a coin as the other enemies.
 */
#include "enemy.h"

#define STATUE_LEFT       1
#define STATUE_RISES      2              /* (the converter's STATUE_RISES) */
#define STATUE_SINK       40             /* px under its placement when it starts rising (0x28338) */
#define STATUE_RISE       2              /* px a frame */
#define STATUE_FIRST      30             /* the first shot of a risen one (0x2833E) */
#define SHOT_WAIT         30             /* frames at the mouth (0x2842E) */
#define SHOT_SPEED_UNIT   0x28F          /* speed 1 = 0.01 px a frame (0x282F6) */
#define SND_CLANK         38             /* ROCKHITX for Flink's 0x18 */

/* per actor slot, the shots: their statue, frames left at the mouth, vx */
static struct { BYTE *parent; int wait, vx; } g_shot[MAX_ACTORS];

#define VIEW_W            512            /* Lomax+'s widest view; (Lomax's own is narrower) */
#define VIEW_H            240

static void __cdecl shot_update(BYTE *d);

/* within margin px of the view (the widest one) on its plane */
static int near_view(const BYTE *a, int margin)
{
    const BYTE *pl = PLANE(A_PLANE(a));
    int x = A_X(a) - P_CAMX(pl), y = A_Y(a) - P_CAMY(pl);
    return x >= -margin && x < VIEW_W + margin && y >= -margin && y < VIEW_H + margin;
}

static int is_shot(BYTE *d, int *i)
{
    *i = d ? slot_of(d) : -1;
    return *i >= 0 && A_UPDATE(d) == (void *)shot_update && A_LAYER(d) != KILLED_LAYER;
}

static void __cdecl shot_update(BYTE *d)
{
    int i = slot_of(d);
    if (i < 0) return;
    if (g_shot[i].wait > 0) {                /* at the mouth (0x28456) */
        if (--g_shot[i].wait == 0) flink_sound(d, 0x2F);
    } else {                                 /* flying (0x28484) */
        A_FX(d) += g_shot[i].vx;
        set_pos(d);
        if (!near_view(d, 64)) { Actor_Despawn(d); return; }   /* (it inherits the statue's cull, often 3) */
    }
    Actor_AdvanceAnim(d);
}

static void __cdecl shot_draw(BYTE *d) { draw_actor(d); }

/* the mouth's hook (0x283F4): a shot at the mouth */
static void statue_shoot(BYTE *a, int i)
{
    int left = A_FLAGS(a) & AF_LEFT, k;
    BYTE *d = Actor_Alloc(a, NULL);
    if (!d || (k = slot_of(d)) < 0) return;
    g_enemy[k].type = NULL;                  /* (not an enemy, whatever was in its slot before) */
    g_shot[k].parent = a;
    g_shot[k].wait = SHOT_WAIT;
    g_shot[k].vx = g_enemy[i].speed;
    A_FX(d) = (A_X(a) + (left ? -g_enemy[i].mdx : g_enemy[i].mdx)) << 16;
    A_FY(d) = (A_Y(a) + g_enemy[i].mdy) << 16;
    set_pos(d);
    A_FLAGS(d) = AF_HURTS | (left ? AF_LEFT : 0);   /* hurts; not stomped nor hit */
    A_UPDATE(d) = (void *)shot_update;
    A_DRAW(d) = (void *)shot_draw;
    Actor_SetAnim(d, FOURCC("SHOT"));
    g_enemy[i].shot = d;
}

/* broken (0x28392): its shot still at the mouth goes out (0x283A2), DEAD breaks up (0x1B1E8) */
void statue_die(BYTE *a, int i)
{
    BYTE *d = g_enemy[i].shot, *coin;
    int k;
    if (is_shot(d, &k) && g_shot[k].parent == a && g_shot[k].wait > 0) Actor_Despawn(d);
    forget_placement(A_DEF(a));
    g_enemy[i].type = NULL;
    flink_sound(a, 0x2A);
    coin = enemy_defeated(a);
    Actor_ShatterIntoPieces(a, 0xC00);
    flog("statue at (%d,%d) broken%s", A_X(a), A_Y(a), coin ? ", drops a coin" : "");
}

/* a stomp, or Lomax's spin or helmet: it only clanks */
void statue_stomped(BYTE *a)
{
    Sound_PlayEffect(a, SND_CLANK);
}

static void __cdecl statue_update(BYTE *a)
{
    int i = enemy_slot(a), hit;
    if (i < 0) return;
    if ((hit = enemy_attacked(a, i)) == 2) { statue_die(a, i); return; }
    if (hit) statue_stomped(a);
    if (A_Y(a) > g_enemy[i].home) {          /* rising (0x28358) */
        A_FY(a) -= STATUE_RISE << 16;
        set_pos(a);
        if (A_Y(a) < g_enemy[i].home) place_y(a, g_enemy[i].home);
    }
    if (near_view(a, 16) && --g_enemy[i].t <= 0) {     /* (Flink counts as it draws the mouth) */
        g_enemy[i].t = g_enemy[i].every;
        statue_shoot(a, i);
    }
    Actor_AdvanceAnim(a);
}

/* (from enemy_spawn) params: checkboxes, period, speed, the mouth's offset */
void statue_spawn(BYTE *a, int i, const BYTE *def)
{
    int chk = def_param(a, def, 0, 0), y = A_Y(a);
    g_enemy[i].statue = 1;
    g_enemy[i].every = (WORD)def_param(a, def, 1, 0);
    if (!g_enemy[i].every) g_enemy[i].every = 0x10000;            /* (subq.w from 0: 65536 frames) */
    g_enemy[i].t = g_enemy[i].every;
    g_enemy[i].speed = (WORD)def_param(a, def, 2, 0) * SHOT_SPEED_UNIT;
    if (chk & STATUE_LEFT) g_enemy[i].speed = -g_enemy[i].speed;
    g_enemy[i].mdx = def_param(a, def, 3, 0);
    g_enemy[i].mdy = def_param(a, def, 4, 0);
    g_enemy[i].home = y;
    A_UPDATE(a) = (void *)statue_update;
    Actor_SetAnim(a, FOURCC("STAY"));
    if (chk & STATUE_RISES && !placement_gone(def)) {
        A_CLIPY(a) = (short)y;               /* the ground hides what is below its placement */
        place_y(a, y + STATUE_SINK);
        g_enemy[i].t = STATUE_FIRST;
        ground_clods(a, A_X(a), y);
        flink_sound(a, 0x49);
    }
}
