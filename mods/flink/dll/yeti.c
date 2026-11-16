/*
 * yeti.c - MOUNTAIN's Yeti, Flink's 'Yeti' (init 0x30044, think 0x30096, its dust 0x30382;
 * 18-1 on): a 'Muta Yeti n' type with Flink's records STAY (asleep), WARN (waking), SPIN, WAIT (tired)
 * and DEAD, and MAIN.WLD's clods ERDE (README "Enemies"). Params: facing left (always 0: Flink's init
 * reads none and its placements face right). Flink's behaviour:
 * - Not spinning (+0x2C = 0): a hit (+3 bit 6) or a stomp (Actor_CheckStompedByPlayer 0x1A352) kills
 *   it (Enemy_Die 0x1B12E); touching it hurts (0x1A462). Asleep (STAY), with Flink within 32 px up or
 *   down and 128 px across (0x30102) it turns to him and wakes (WARN) for 20 frames (+0x4C), then
 *   spins: +0x4C = 180 + (random & 255) frames, speed (+0x48) 0.5, the walk anim (+0x2C) SPIN, sound
 *   0x1A, and its dust starts (the draw hook +0x42 = 0x30382).
 * - Spinning (0x301DE): Actor_Walk (0x1D0C4) with flags 0xD000 (a wall turns it at once, a 45 degree
 *   slope turns it either way, it walks off ledges: in the air it falls by 0x2A00 a frame, or 0x2400
 *   below speed 1.0, at most 4.0, and its vx goes by 0x800 toward 1.5, or 2.0 below speed 1.0). Its
 *   speed grows by 0x200 a frame up to 1.75. With Flink behind it (by x) for 40 frames (+0x56) it
 *   turns to him if he still is (flags bit 10: it stops, by |vx| / 6 a frame, and turns), its speed
 *   back to 0.5. It hurts on touch with SPIN's box (0x1A46E), can't be stomped, and hits the first
 *   enemy its box touches (Actor_FindTouchingActor 0x1A5A0: the hit flag, as a thrown object sets it).
 *   When +0x4C runs out its speed is 0: it slows down, and stopped on the ground it is tired: facing
 *   right, WAIT for 60 + (random & 127) frames (0x301F8), then asleep or, Flink near, waking again.
 * - Hit while spinning (0x302B8): its vx is 0; Flink sliding into it or his spells (thinks 0x31D7C,
 *   0x30E06, 0x31142) kill it; anything else (a thrown object, another Yeti) is turned back (the
 *   hitter's vx negated, mirrored), sound 0x29, and for 14 frames (+0x4E) hits don't count.
 * - Its dust (0x30382, in its draw): 4 clods (MAIN.WLD's ERDE, frame 1 for clods 3 and 2, 0 for 1 and
 *   0: the sprites at 0x8546), each moving by Actor_FallAndLand's numbers (0x1B0E2: 0x2000 a frame
 *   down, at most 4.0, vx braked by 0x400), no landing. Every 16 frames (+0x50, 0..63) clod
 *   (+0x50 >> 4) is put out and, while it spins on the ground, thrown again from its feet: 2.5 up,
 *   (random & 15) / 8 px across, odd clods to the left. Drawn in front of it.
 * Here Lomax's stomp, spin, roll and helmet kill it when it isn't spinning, as Flink's slide and spells;
 * spinning, only the thrown helmet reaches it, and leaves it tired (WAIT) as when
 * its spin runs out; his spin and roll do nothing to it, and landing on it hurts him (enemy_spiked,
 * contact.c). Thrown objects reach it through grab.c's strike (yeti_deflects, before enemy_struck). The
 * clods move in the update, drawn by yeti_draw (Lomax+'s FPS Override may draw several times a tick).
 * Sounds: Flink's 0x1A at the spin's start (sfx.c); HELMHIT1 (17) for the FM sound 0x29, a hit
 * turned back.
 */
#include "enemy.h"

#define WAKE_DX            0x80
#define WAKE_DY            0x20
#define WAKE_TICKS         0x14
#define SPIN_SPEED         0x8000
#define SPIN_MAX           0x1C000
#define SPIN_ACCEL         0x200
#define TURN_WAIT          0x28
#define COOL_TICKS         0x0E
#define DUSTS              4
#define DUST_VY            0x28000
#define DUST_GRAVITY       0x2000
#define DUST_MAXFALL       0x40000
#define DUST_FRICTION      0x400
#define SND_TURN_BACK      17            /* HELMHIT1 (Flink's 0x29) */

typedef struct { int fx, fy, vx, vy; } Dust;
typedef struct {
    int spin, speed, t, cool, turn_wait, turning, air, dusty, ph, mask;
    Dust d[DUSTS];
} Yeti;
static Yeti g_yeti[MAX_ACTORS];

static int yeti_rand(void) { static unsigned r = 0x1F0D3B57u; r = r * 1103515245u + 12345u; return (int)(r >> 16) & 0x7FFF; }

static int is_yeti(int i) { return i >= 0 && g_enemy[i].yeti && !g_enemy[i].dead; }

static DWORD anim_tag(const BYTE *a)
{
    const BYTE *r = A_RECORD(a);
    return r ? *(const DWORD *)(r + 4) : 0;
}

/* 0x30338: Lomax on the other side from the way it faces */
static int lomax_behind(const BYTE *a)
{
    return (*G_PLAYERX < A_X(a)) != ((A_FLAGS(a) & AF_LEFT) != 0);
}

/* Actor_Walk (0x1D0C4) with flags 0xD000 at w->speed, w->turning its flags bit 10, w->air bit 8 */
static void yeti_walk(BYTE *a, Yeti *w)
{
    static const int SNAP[8] = { 3, 4, 6, 7, 10, 10, 10, 10 };   /* 0x1AEDA */
    int left = (A_FLAGS(a) & AF_LEFT) != 0, x = A_X(a), y = A_Y(a), target = w->speed, step, speed, g;
    if (w->air) {                            /* (0x1D10C) */
        int slow = w->speed < 0x10000;
        target = slow ? 0x20000 : 0x18000;
        A_VY(a) += slow ? 0x2400 : 0x2A00;
        if (A_VY(a) > 0x40000) A_VY(a) = 0x40000;
        A_FY(a) += A_VY(a);
        step = 0x800;
    } else step = 0x1000;
    if (wall_at(a, left ? x - 8 : x + 8, !left)) { A_FLAGS(a) ^= AF_LEFT; left = !left; w->turning = 0; }   /* (0x1D196) */
    speed = abs(A_VX(a));
    if (w->turning) {                        /* (0x1D2EA: divu.w #6, as enemy_walk's) */
        step = speed < 6 * 0x10000 ? (speed % 6) << 16 | speed / 6 : speed;
        target = 0;
        if (!A_VX(a)) { w->turning = 0; A_FLAGS(a) ^= AF_LEFT; left = !left; }
    }
    if (speed < target) speed = speed + step > target ? target : speed + step;
    else if (speed > target) speed = speed - step < target ? target : speed - step;
    A_VX(a) = left ? -speed : speed;
    A_FX(a) += A_VX(a);
    set_pos(a);
    if (below_map(a)) return;
    g = ground_y(a, A_X(a), w->air ? A_Y(a) : y);
    if (g == NO_GROUND || abs(g - A_Y(a)) > SNAP[speed >> 16 < 7 ? speed >> 16 : 7]) { w->air = 1; return; }   /* (0x1D412) */
    place_y(a, g);
    A_VY(a) = 0;
    if (*G_GROUNDSLOPECLASS == 2) {          /* 45 degrees: turns either way (bits 15, 12) */
        if (w->air) A_VX(a) /= 2;
        w->air = 0;
        A_FLAGS(a) ^= AF_LEFT;
        w->turning = 0;
        return;
    }
    w->air = 0;
    set_anim(a, "SPIN");                     /* (+0x2C) */
}

/* tired (0x301F8): WAIT, facing right */
static void yeti_tire(BYTE *a, Yeti *w)
{
    w->spin = 0;
    w->speed = 0;
    w->turning = w->turn_wait = 0;
    A_VX(a) = 0;
    A_FLAGS(a) &= ~AF_LEFT;
    w->t = 60 + (yeti_rand() & 127);
    flog("yeti at (%d,%d) tired", A_X(a), A_Y(a));
}

/* 0x30174: it spins */
static void yeti_start(BYTE *a, Yeti *w)
{
    w->dusty = 1;
    w->t = 180 + (yeti_rand() & 255);
    w->turn_wait = 0;
    w->turning = 0;
    w->speed = SPIN_SPEED;
    w->spin = 1;
    flink_sound(a, 0x1A);
    flog("yeti at (%d,%d) spins for %d frames", A_X(a), A_Y(a), w->t);
}

/* 0x30382's moves and throws, once a tick */
static void yeti_dust(BYTE *a, Yeti *w)
{
    int k;
    if (!w->dusty) return;
    for (k = 0; k < DUSTS; k++) {
        Dust *d = &w->d[k];
        if (!(w->mask >> k & 1)) continue;
        d->vy += DUST_GRAVITY;
        if (d->vy > DUST_MAXFALL) d->vy = DUST_MAXFALL;
        d->fy += d->vy;
        d->fx += d->vx;
        d->vx = d->vx > 0 ? (d->vx > DUST_FRICTION ? d->vx - DUST_FRICTION : 0)
                          : (d->vx < -DUST_FRICTION ? d->vx + DUST_FRICTION : 0);
    }
    w->ph = (w->ph + 1) & 63;
    if (w->ph & 15) return;
    k = w->ph >> 4;
    w->mask &= ~(1 << k);
    if (!w->spin || w->air) return;
    w->mask |= 1 << k;
    w->d[k].fx = A_FX(a);
    w->d[k].fy = A_FY(a);
    w->d[k].vx = ((yeti_rand() & 15) << 16) >> 3;
    if (k & 1) w->d[k].vx = -w->d[k].vx;
    w->d[k].vy = -DUST_VY;
}

/* the first other Flink enemy its box touches is hit (0x1A5A0, from 0x302A6) */
static void yeti_touch(BYTE *a, int self)
{
    int x1, y1, x2, y2, k;
    if (!actor_box(a, &x1, &y1, &x2, &y2)) return;
    for (k = 0; k < MAX_ACTORS; k++) {
        BYTE *t = actor_at(k);
        int e, tx1, ty1, tx2, ty2;
        if (k == self || !actor_active(k) || !A_TYPE(t) || A_LAYER(t) == KILLED_LAYER || A_PLANE(t) != A_PLANE(a)) continue;
        if ((e = enemy_slot(t)) < 0 || g_enemy[e].dead || !(A_FLAGS(t) & AF_HITTABLE) || grab_held(t)) continue;
        if (!actor_box(t, &tx1, &ty1, &tx2, &ty2) || x1 > tx2 || tx1 > x2 || y1 > ty2 || ty1 > y2) continue;
        flog("spinning yeti at (%d,%d) hits an enemy at (%d,%d)", A_X(a), A_Y(a), A_X(t), A_Y(t));
        if (!yeti_deflects(t, a)) enemy_struck(t, 0);
        return;
    }
}

/* A thrown object (or a Yeti) o hit it (grab.c's strike, yeti_touch): 1 = a Yeti
   took it (0x302B8: turned back, or within the 14 frames after one); 0 = it is hit as any enemy */
int yeti_deflects(BYTE *t, BYTE *o)
{
    int i = t ? enemy_slot(t) : -1;
    Yeti *w;
    if (!is_yeti(i) || !g_yeti[i].spin) return 0;
    w = &g_yeti[i];
    if (w->cool) return 1;
    A_VX(t) = 0;
    w->cool = COOL_TICKS;
    A_FLAGS(o) ^= AF_LEFT;
    A_VX(o) = -A_VX(o);
    Sound_PlayEffect(t, SND_TURN_BACK);
    flog("spinning yeti at (%d,%d) turns a hit back", A_X(t), A_Y(t));
    return 1;
}

/* a Yeti: landing on it hurts Lomax (contact.c), no stomp */
int yeti_spiked(BYTE *a)
{
    int i = a ? enemy_slot(a) : -1;
    return is_yeti(i) && g_yeti[i].spin;
}

/* not spinning (0x300C8): asleep, waking or tired */
static void yeti_rest(BYTE *a, Yeti *w)
{
    DWORD tag = anim_tag(a);
    if (tag != FOURCC("WARN")) {
        if (tag != FOURCC("STAY")) {         /* tired (0x301C8) */
            set_anim(a, "WAIT");
            if (--w->t != 0) { Actor_AdvanceAnim(a); return; }
        }
        if (abs(*G_PLAYERY - A_Y(a)) > WAKE_DY || abs(*G_PLAYERX - A_X(a)) > WAKE_DX) {   /* (0x30102) */
            set_anim(a, "STAY");
            Actor_AdvanceAnim(a);
            return;
        }
        if (*G_PLAYERX < A_X(a)) A_FLAGS(a) |= AF_LEFT;
        else A_FLAGS(a) &= ~AF_LEFT;
        w->t = WAKE_TICKS;
        flog("yeti at (%d,%d) wakes", A_X(a), A_Y(a));
    }
    set_anim(a, "WARN");                     /* (0x30146) */
    if (--w->t == 0) yeti_start(a, w);
    Actor_AdvanceAnim(a);
}

static void __cdecl yeti_update(BYTE *a)
{
    int i = enemy_slot(a);
    Yeti *w;
    if (i < 0) return;
    if (g_enemy[i].dead) { enemy_dead_update(a, i); return; }
    w = &g_yeti[i];
    if (!w->spin) {
        if (enemy_attacked(a, i)) { enemy_die(a, i, 1); return; }   /* (as Flink's slide and spells) */
        yeti_rest(a, w);
        yeti_dust(a, w);
        return;
    }
    if (A_FLAGS(a) & 0x20000) {              /* the thrown helmet: tired */
        enemy_attacked(a, i);                /* (takes the flag; its guard keeps the next hits off) */
        Sound_PlayEffect(a, SND_HIT);
        flog("yeti at (%d,%d) hit by the helmet", A_X(a), A_Y(a));
        if (w->air) w->speed = 0;            /* (tired once it lands) */
        else {
            yeti_tire(a, w);
            set_anim(a, "WAIT");
            --w->t;
            Actor_AdvanceAnim(a);
            yeti_dust(a, w);
            return;
        }
    } else
        g_enemy[i].hit = Actor_CheckHitByPlayer(a) != 0;   /* his spin and roll: nothing */
    if (!w->speed && !A_VX(a) && !w->air) {  /* stopped: tired (0x301DE) */
        yeti_tire(a, w);
        set_anim(a, "WAIT");
        --w->t;
        Actor_AdvanceAnim(a);
        yeti_dust(a, w);
        return;
    }
    yeti_walk(a, w);
    if (below_map(a)) { g_enemy[i].type = NULL; Actor_Kill(a); return; }   /* (fell off: 0x2E770) */
    if (w->speed) { w->speed += SPIN_ACCEL; if (w->speed > SPIN_MAX) w->speed = SPIN_MAX; }
    if (w->turning) { if (w->speed) w->speed = SPIN_SPEED; }
    else if (w->turn_wait) { if (!--w->turn_wait && lomax_behind(a)) w->turning = 1; }
    else if (lomax_behind(a)) w->turn_wait = TURN_WAIT;
    yeti_touch(a, i);
    if (--w->t == 0) w->speed = 0;
    if (w->cool) w->cool--;
    Actor_AdvanceAnim(a);
    yeti_dust(a, w);
}

/* one clod: ERDE's frame (one piece a frame), one bucket in front of it */
static void dust_draw(BYTE *a, int frame, int x, int y)
{
    const BYTE *e, *bank, *type = A_TYPE(a);
    if (!(e = record_of(a, FOURCC("ERDE")))) return;
    e += frame * 8;
    if (!(bank = G_ANIMBANKS[e[0] | (T_BANKS(type) & 0x100)])) return;
    Sprite_QueueWorld(PLANE(A_PLANE(a)), bank + e[1] * 0x38 + 0x10, x + *(const short *)(e + 2),
                      y + *(const short *)(e + 4), (unsigned)(Actor_GetDrawDepth(a) + 1) << 24 | (e[6] & 0x18), 20000);
}

static void __cdecl yeti_draw(BYTE *a)
{
    int i = enemy_slot(a), k;
    draw_actor(a);
    if (i < 0 || !g_enemy[i].yeti) return;
    for (k = 0; k < DUSTS; k++)
        if (g_yeti[i].mask >> k & 1) dust_draw(a, k >= 2, g_yeti[i].d[k].fx >> 16, g_yeti[i].d[k].fy >> 16);
}

/* (from enemy_spawn) asleep (0x30044: STAY) */
void yeti_spawn(BYTE *a, int i)
{
    memset(&g_yeti[i], 0, sizeof(g_yeti[i]));
    g_enemy[i].yeti = 1;
    A_UPDATE(a) = (void *)yeti_update;
    A_DRAW(a) = (void *)yeti_draw;
    Actor_SetAnim(a, FOURCC("STAY"));
}
