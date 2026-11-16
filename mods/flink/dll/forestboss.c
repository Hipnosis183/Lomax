/*
 * forestboss.c - Flink's Forest Boss (actor 'Boss', init 0x2948C -> 0x294B0 for
 * FOREST, think 0x295D0 then 0x29602), in 8-2 (FOREST map 17) (README "Enemies" (The Forest Boss)). The converter makes it
 * a 'Muta Boss n' type (boss_records): WALK, JUMP and OUCH with its stilts' spiked tops drawn in, the
 * stilts' points LGWK / LGJP / LGOU, DEAD and BOOM (DEAD's frame 0); its spirits are spirits.c's;
 * params 0, the fight's music and the music at its defeat (Lomax Music params). Its placement links
 * to the arena's Finish (placement +4), which never spawns by proximity.
 *
 * Flink's boss:
 * - It waits 320 px above its placement (0x29588) until Flink is within 50 px of its x (0x295D0),
 *   then drops in at 4.0 px a frame and the boss music starts (music index 8, 0x295FA).
 * - In the air it falls 0x3000 a frame faster, up to 4.0 (0x29754); landing on its placement's y it
 *   shakes the screen for 35 frames (0x874E = 0x23), sound 0x35, and gets up: JUMP's frames 32..0.
 * - Then it walks at 1 px a frame, right first, turning 160 px from the map's ends (0x2981A); WALK
 *   plays forward walking left and backward walking right (+0x32 = +-0x100). Each time it passes its
 *   placement's x it squats (JUMP 0..32), jumps straight up at 9.0 (0xFFF7FFFF) and lands as above.
 * - Hit (0x296CA): stomped on its head (Actor_CheckStompedByPlayer, its hitbox), or by a thrown
 *   object or Flink's slide (+3 bit 6, set for actors with +6 bit 3). It stops, OUCH for 40 frames,
 *   then 60 more frames in which it is neither hit nor hurts; a spirit flies out of it (0x2EDAA at
 *   y - 70), sound 0x2B. Touching its hitbox hurts otherwise (0x1A462). 8 hits (0x14776: 0x8280).
 * - Its stilts' tops are two child actors (NODE) placed at its frames' attach points (0x29A46); each
 *   hurts in x +- 6 by y - 54 .. y - 30 (the spike) and y + 74 .. y + 98 (the stilt where Flink
 *   stands) (0x299F2), blocks Flink as a pushed object (0x1AA56) and throws thrown objects back
 *   (0x299B4). Here the same, following Flink's code, except: Lomax walking into
 *   a trunk stops at its side instead of pushing it, with no push pose (trunks_after), and the lower
 *   end hurts only while the trunk is raised (in its step, or with the boss in the air), not on the
 *   ground. Thrown objects come back with Lomax's stomp sound.
 * - Defeated (0x29724): Flink can't be hurt (hurt timer -2), the screen shakes, the music changes
 *   (index 21, 0x29A7C), and for 161 frames it is hit (OUCH), a spirit flying out every 32 with a
 *   flash and sound 0x1A (0x2961C). Then (0x29898) it drops Flink's crystal (Item_Spawn 0x17), its
 *   body (DEAD frame 0) breaks into its sprites (0x1B1F8) and frames 1-4 fly off as 4 clusters
 *   (0x29906: vx (rand & 31) - 15 eighths, up at 1.0 + (rand & 31) / 8, falling 0x4000 a frame),
 *   sound 0x53; then Flink's end-of-level sequence.
 * - A spirit (0x2EDAA, think 0x2EE2C, MAIN.WLD): starts up at 2.0, homes in on Flink's x, y - 15
 *   (Actor_MoveToward 0x1B008: 0x800 a frame up to 2.0), drops a trail of 3 TAIL puffs, lives 600
 *   frames (blinking the last 60); touching Flink collects it (sound 0x1D, a spirit counted), and
 *   then, or when its time is up, it plays TAIL once and goes.
 *
 * Here: the same, with Lomax's spin and helmet as a stomp, no flashes and no crystal; Lomax can't be
 * hurt while it dies (boss_untouchable); the shake is
 * Lomax's g_ScreenShake (10, as his falling tree's), kept up while it dies and again at the break-up
 * (shake_after makes it show in a map one screen tall); the defeat's fanfare is
 * played once (music_play_once: Lomax loops every track); the spirits are spirits.c's, collected
 * into the magic glass, plus one as it breaks up (enemy_defeated). When the clusters have fallen away (BOSS_FINISH_WAIT), the Finish appears where the
 * converter placed it, in the middle of the arena, with a poof (no end
 * sequence). A defeated boss stays gone for the level visit (gone.c); its arena then has the
 * Finish from the start.
 */
#include "enemy.h"

#define BOSS_HITS          8             /* 0x14776 */
#define BOSS_DROP          320           /* 0x29588 */
#define BOSS_NEAR          50            /* 0x295E0 */
#define BOSS_FALL          0x40000
#define BOSS_GRAVITY       0x3000
#define BOSS_JUMP          (-0x90001)    /* 0xFFF7FFFF */
#define BOSS_SQUAT         33            /* JUMP frames 0..32 squatting or getting up, 33 in the air */
#define BOSS_TURN          160           /* 0xA0 from the map's ends */
#define BOSS_SPEED         0x10000
#define BOSS_OUCH          40
#define BOSS_GUARD         60
#define BOSS_DYING         161           /* 0xA1 */
#define BOSS_SPIRIT_DY     70            /* 0x29646 */
#define BOSS_SHAKE         10            /* g_ScreenShake as Lomax's falling tree sets it (~40 ticks; Flink's 35 frames) */
#define BOSS_SHAKE_KEEP    8             /* while it is dying, kept at least this */
#define BOSS_FINISH_WAIT   100           /* ticks from the break-up to the Finish */
#define STILT_HALF         6
#define STILT_REST         (-52)         /* a planted trunk's point: attach y - 52 from the boss's feet on the ground */
#define STILT_RAISED       4             /* raised this far above that, a trunk hurts */
#define TRUNK_X1           (-9)          /* a trunk's box from its point: NODE's hitbox with 0x29A2E's y + 40 */
#define TRUNK_X2           8
#define TRUNK_Y1           (-14)
#define TRUNK_Y2           115
#define TRUNK_EDGE         16            /* 0x1AA56: Flink held with the point 16 px in front of him at its side */
#define TRUNK_PUSH_OUT     4             /* 0x299DE: a thrown object turned back moves 4 x its new vx */
#define SND_FINISH         69            /* TDOROPEN */

typedef struct {
    BYTE *finish;                        /* the Finish's placement */
    int music, win, hits, homex, homey, dir, ouch, guard, squat, wf;
} Boss;
static Boss g_boss[MAX_ACTORS];

/* the trunks as the last boss update put them (one boss at a time): for Lomax after Player_Update and
   for the thrown objects. g_trunk_fresh: set this tick (cleared by trunks_after) */
static struct { int x, y, x1, x2; } g_trunk[2];   /* point, box across */
static int g_trunk_plane = -1, g_trunk_fresh, g_trunk_px0;   /* (Lomax's x before Player_Update) */

static int boss_rand(void) { static unsigned r = 0x6C8E9CF5u; r = r * 1103515245u + 12345u; return (int)(r >> 16) & 0x7FFF; }

static int overlap(int ax1, int ay1, int ax2, int ay2, int bx1, int by1, int bx2, int by2)
{
    return ax1 < bx2 && bx1 < ax2 && ay1 < by2 && by1 < ay2;
}

static int player_box(int *x1, int *y1, int *x2, int *y2)
{
    const BYTE *pa = *G_PLAYERANIM;
    if (!pa) return 0;
    *x1 = *G_PLAYERX + HB_X1(pa); *x2 = *G_PLAYERX + HB_X2(pa);
    *y1 = *G_PLAYERY + HB_Y1(pa); *y2 = *G_PLAYERY + HB_Y2(pa);
    return 1;
}

/* a child actor (no placement, never culled) of the boss, not an enemy */
static BYTE *child(BYTE *a, ActorFn update, ActorFn draw)
{
    BYTE *d = Actor_Alloc(a, NULL);
    int k;
    if (!d || (k = slot_of(d)) < 0) return NULL;
    g_enemy[k].type = NULL;
    A_UPDATE(d) = (void *)update;
    A_DRAW(d) = (void *)draw;
    A_CULL(d) = CULL_NEVER;
    A_FLAGS(d) = 0;
    return d;
}

static void __cdecl plain_draw(BYTE *d) { draw_actor(d); }

/* ---- Its end ----------------------------------------------------------------------------------- */

/* a cluster of DEAD (0x2997A): thrown up, falling 0x4000 a frame, gone below the map */
static void __cdecl cluster_update(BYTE *d)
{
    A_VY(d) += 0x4000;
    A_FX(d) += A_VX(d);
    A_FY(d) += A_VY(d);
    set_pos(d);
    Actor_AdvanceAnim(d);
    if (below_map(d)) Actor_Despawn(d);
}

/* the Finish appears (once: not while one is out) */
static void finish_show(BYTE *def)
{
    BYTE *f;
    int k;
    if (!def) return;
    for (k = 0; k < MAX_ACTORS; k++) {
        BYTE *t = actor_at(k);
        if (A_DEF(t) == def && A_LAYER(t) != KILLED_LAYER && actor_active(k)) return;
    }
    D_STATE(def) &= ~DS_SPAWNED;
    if (!(f = Object_TrySpawn(def))) return;
    A_CULL(f) = CULL_NEVER;                  /* (despawned, it would never come back: 0x2000) */
    Effect_SpawnPoof(D_X(def), D_Y(def));
    Sound_PlayEffect(f, SND_FINISH);
    flog("boss defeated: the Finish appears at (%d,%d)", D_X(def), D_Y(def));
}

/* the countdown to the Finish (+0x80 its placement, +0x84 ticks) */
static void __cdecl finish_wait(BYTE *w)
{
    if (--*(int *)(w + 0x84) > 0) return;
    finish_show(*(BYTE **)(w + 0x80));
    Actor_Despawn(w);
}

static void finish_later(BYTE *a, BYTE *def, int ticks)
{
    BYTE *w = child(a, finish_wait, plain_draw);
    if (!w) { finish_show(def); return; }
    A_FLAGS(w) = AF_HIDDEN;
    *(BYTE **)(w + 0x80) = def;
    *(int *)(w + 0x84) = ticks;
}

/* (the Village Boss too: villageboss.c) */
void boss_finish_later(BYTE *a, BYTE *def, int ticks) { finish_later(a, def, ticks); }

/* 0x29898: the body breaks up, 4 clusters fly off, then the Finish */
static void boss_die(BYTE *a, int i)
{
    int n;
    forget_placement(A_DEF(a));
    for (n = 1; n <= 4; n++) {
        BYTE *d = child(a, cluster_update, plain_draw);
        if (!d) break;
        Actor_SetAnim(d, FOURCC("DEAD"));
        hold_frame(d, n);
        Actor_AdvanceAnim(d);
        A_FX(d) = A_X(a) << 16;
        A_FY(d) = A_Y(a) << 16;
        A_VX(d) = (((boss_rand() & 31) - 15) << 16) >> 3;
        A_VY(d) = -(0x10000 + ((boss_rand() & 31) << 13));
    }
    finish_later(a, g_boss[i].finish, BOSS_FINISH_WAIT);
    g_trunk_plane = -1;
    *G_SCREENSHAKE = BOSS_SHAKE;
    enemy_defeated(a);
    flink_sound(a, 0x53);
    g_enemy[i].type = NULL;
    Actor_ShatterIntoPieces(a, 0xC00);
    flog("boss at (%d,%d) breaks up", A_X(a), A_Y(a));
}

/* ---- The fight --------------------------------------------------------------------------------- */

static void boss_hit(BYTE *a, int i)
{
    Boss *b = &g_boss[i];
    if (b->ouch || b->guard || b->hits <= 0) return;
    A_VX(a) = 0;
    A_FLAGS(a) &= ~AF_CONTACT;
    b->ouch = BOSS_OUCH;
    b->guard = BOSS_GUARD;
    spirit_release(a, A_X(a), A_Y(a) - BOSS_SPIRIT_DY);
    flink_sound(a, 0x2B);
    if (--b->hits <= 0) {
        b->ouch = BOSS_DYING;
        b->guard = 0;
        *G_SCREENSHAKE = BOSS_SHAKE;
        music_play_once(b->win);             /* (a fanfare: Lomax would loop it) */
    }
    flog("boss hit, %d left", b->hits);
}

void boss_stomped(BYTE *a, int i) { boss_hit(a, i); }

/* Flink's 0x1A46E: the box x1..y2 (inclusive) touches Lomax's hitbox */
static int touches_lomax(int x1, int y1, int x2, int y2, int px1, int py1, int px2, int py2)
{
    return x2 >= px1 && x1 <= px2 && y2 >= py1 && y1 <= py2;
}

/* the trunks at the current frame's points (LGWK / LGJP / LGOU, 2 a frame), as the NODE's think
   0x299F2 after 0x29A46 put them there: its two edges hurt, x +- 6 by y - 54 .. y - 30 (the spiked
   top) and y + 74 .. y + 98 (the lower end) (0x1A46E: Player_TakeHit, which a hurt Lomax ignores).
   The lower end hurts only while the trunk is raised (STILT_RAISED px above STILT_REST: lifted in its
   step, or the boss in the air): on the ground it doesn't. Their boxes are kept
   for trunks_after and boss_trunk_bounce: the NODE's hitbox at y + 40 (0x29A2E), mirrored for the
   left one (the NODE at +0x54, +3 bit 3: 0x1AAF4). */
static void trunks_update(BYTE *a, Boss *b)
{
    const BYTE *r = A_RECORD(a), *e;
    DWORD cc;
    int k, px1 = 0, py1 = 0, px2 = 0, py2 = 0, hurt;
    if (!r) return;
    cc = *(const DWORD *)(r + 4);
    e = record_of(a, cc == FOURCC("WALK") ? FOURCC("LGWK") : cc == FOURCC("JUMP") ? FOURCC("LGJP") :
                     cc == FOURCC("OUCH") ? FOURCC("LGOU") : 0);
    if (!e) return;
    e += A_FRAME(a) * 16;
    g_trunk_plane = A_PLANE(a);
    g_trunk_fresh = 1;
    g_trunk_px0 = *G_PLAYERX;
    hurt = b->hits > 0 && A_PLANE(a) == *G_PLAYERPLANE && !*G_PLAYERHURTTIMER && !*G_PLAYERDEATHTIMER &&
           player_box(&px1, &py1, &px2, &py2);
    for (k = 0; k < 2; k++) {
        int x = A_X(a) + *(const short *)(e + k * 8 + 2), y = A_Y(a) + *(const short *)(e + k * 8 + 4);
        int raised = y < b->homey + STILT_REST - STILT_RAISED, left = x < A_X(a);
        g_trunk[k].x = x;
        g_trunk[k].y = y;
        g_trunk[k].x1 = x + (left ? -TRUNK_X2 : TRUNK_X1);
        g_trunk[k].x2 = x + (left ? -TRUNK_X1 : TRUNK_X2);
        if (hurt && (touches_lomax(x - STILT_HALF, y - 54, x + STILT_HALF, y - 30, px1, py1, px2, py2) ||
                     (raised && touches_lomax(x - STILT_HALF, y + 74, x + STILT_HALF, y + 98, px1, py1, px2, py2)))) {
            flog("boss's %s trunk at (%d,%d) hurts Lomax", raised ? "raised" : "planted", x, y);
            Player_TakeHit(x, 0, 0);
            hurt = 0;
        }
    }
}

/* after Player_Update (player_tick): the trunks are solid, as Flink's NODE, a pushed object that can't
   move (0x1AA56 from 0x29A3E, for both every frame, raised or not; not while Flink is hurt, 0x29A34,
   or dying). The side is the trunk's x against Lomax's: the point 16 px in front of him toward it
   inside its box across, his feet below its top and his feet - 32 not below its bottom. On the
   ground with the pad held away from it, there is no check (0x1AA74 / 0x1AA9C). Flink then pushes
   it (Player_Update keeps it in front of him and plays PUSH), and the NODE goes back to the boss's
   point next frame; here (solid, no push pose) Lomax goes no further in: back to
   where the point in front of him is at its edge, or where he was at the tick's start if already
   in, and his speed into it is taken off. A trunk that comes onto him doesn't push him out. */
void trunks_after(void)
{
    int k, px, py;
    if (!g_trunk_fresh) { g_trunk_plane = -1; return; }     /* (no boss update this tick: no trunks) */
    g_trunk_fresh = 0;
    if (!g_active) return;
    if (g_trunk_plane != *G_PLAYERPLANE || *G_PLAYERDEATHTIMER || *G_PLAYERHURTTIMER || *G_PLAYERSTATE & PS_FLY) return;
    px = *G_PLAYERX; py = *G_PLAYERY;
    for (k = 0; k < 2; k++) {
        int x = g_trunk[k].x, y = g_trunk[k].y, x1 = g_trunk[k].x1, x2 = g_trunk[k].x2, lim;
        int ground = !(*G_PLAYERSTATE & PS_AIR);
        if (py < y + TRUNK_Y1 + 1 || py - 32 > y + TRUNK_Y2) continue;
        if (x > px) {                                    /* to his right */
            if (ground && *G_PLAYERVX < 0) continue;     /* (walking away: Flink's Left held) */
            if (px + TRUNK_EDGE < x1 || px + TRUNK_EDGE > x2) continue;
            lim = x1 - TRUNK_EDGE;
            if (lim < g_trunk_px0) lim = g_trunk_px0;
            if (px > lim) px = lim;
            if (*G_PLAYERVX > 0) *G_PLAYERVX = 0;
        } else {
            if (ground && *G_PLAYERVX > 0) continue;
            if (px - TRUNK_EDGE < x1 || px - TRUNK_EDGE > x2) continue;
            lim = x2 + TRUNK_EDGE;
            if (lim > g_trunk_px0) lim = g_trunk_px0;
            if (px < lim) px = lim;
            if (*G_PLAYERVX < 0) *G_PLAYERVX = 0;
        }
    }
    if (px != *G_PLAYERX) { *G_PLAYERX = px; *G_PLAYERXFIXED = px << 16; }
}

/* 0x299B4: a thrown object (or shot) whose box x1..y2 touches a trunk, raised or not, while moving
   toward it is turned back: mirrored, vx negated and moved 4 x the new vx, with Lomax's stomp sound
   (Flink's 0x29). 1 = it was (grab.c's strike: then its puff, and one with a fuse breaks up, as on a hit). */
int boss_trunk_bounce(BYTE *o, int x1, int y1, int x2, int y2)
{
    int k, vx = A_VX(o), cx = (x1 + x2) / 2;
    if (!g_active || g_trunk_plane < 0 || A_PLANE(o) != g_trunk_plane || !vx) return 0;
    for (k = 0; k < 2; k++) {
        int x = g_trunk[k].x, y = g_trunk[k].y;
        if (!overlap(x1, y1, x2, y2, g_trunk[k].x1, y + TRUNK_Y1, g_trunk[k].x2, y + TRUNK_Y2)) continue;
        if ((vx > 0) != (cx < x)) continue;              /* (already going away from it) */
        A_VX(o) = vx = -vx;
        A_FLAGS(o) ^= AF_LEFT;
        A_FX(o) += vx * TRUNK_PUSH_OUT;
        set_pos(o);
        Sound_PlayEffect(o, SND_STOMP);
        flog("thrown object turned back by the boss's trunk at (%d,%d)", x, y);
        return 1;
    }
    return 0;
}

/* g_ScreenShake in a map one screen tall: Player_Update adds the shake to the camera's y and then
   clamps it to 16 .. nMaxScrollY (0x41226B), which takes it all off where the map is no taller than
   the view (8-2). In Flink's maps it goes back on after the clamp; the next Player_Update puts the
   camera back first (g_CameraYBeforeShake, 0x4120B7), as for any shake. */
void shake_after(void)
{
    BYTE *pl = *G_PCURPLANE;
    int y = *G_CAMERAYBEFORESHAKE;
    if (!g_active || !pl || !*G_SCREENSHAKE || y == SHAKE_NONE) return;
    P_CAMY(pl) = y + *G_SCREENSHAKEOFFSETY;
}

/* Flink's bosses, defeated, set the hurt timer to -2 every frame while they die (0x29724, 0x29CD6,
   0x22542): Flink can't be hurt then. Lomax's Player_TakeHit (0x412E30) returns while
   g_PlayerHurtTimer isn't 0, and his contact loop hurts only through it, so -2 does the same here; a
   negative timer counts up to 0 by itself (after a knock-back's landing). Player_Update gives Lomax his
   invulnerability blink while it is negative (his actor's +0x78 dwBlinkTimer 0x2000); Flink doesn't
   blink, so boss_quiet_after clears it after Player_Update while this timer is the boss's (until a few
   ticks after the last call, the timer back at 0). A running (positive) timer is left alone. */
static int g_boss_quiet;

void boss_untouchable(void)
{
    if (*G_PLAYERHURTTIMER <= 0 && !*G_PLAYERDEATHTIMER) { *G_PLAYERHURTTIMER = -2; g_boss_quiet = 4; }
}

/* after Player_Update (player_tick) */
/* A boss fight runs: each boss's update calls boss_fighting every tick from its fight's start (the
   Forest Boss dropping in, the Village Boss running in, the Meta Boss rising, the final boss's first
   fireball, the Donkey monkey's first jump) until it is gone; boss_fight is true until two ticks
   after the last call (the boss updates and Player_Update, which casts, may come in either order).
   Meanwhile the wheel opens but only HELMET is cast (spell_can_cast, spells.c) and the spells already
   out hit nothing (spell_strike, grab.c). */
static int g_fight, g_fight_tick;

void boss_fighting(void) { g_fight = 1; g_fight_tick = *G_TICKCOUNTER; }
int  boss_fight(void) { return g_fight && (unsigned)(*G_TICKCOUNTER - g_fight_tick) <= 2; }
void boss_fight_reset(void) { g_fight = 0; }

void boss_quiet_after(void)
{
    if (g_boss_quiet <= 0) return;
    g_boss_quiet--;
    if (*G_PLAYERHURTTIMER <= 0) *(DWORD *)(G_PLAYERACTOR + 0x78) = 0;
}

static void show_frame(BYTE *a, const char *tag, int frame)
{
    set_anim(a, tag);
    hold_frame(a, frame);
    Actor_AdvanceAnim(a);
}

static void boss_move(BYTE *a, int i)
{
    Boss *b = &g_boss[i];
    int x, mapw;
    if (b->ouch) {                           /* 0x29602 */
        b->squat = 0;
        set_anim(a, "OUCH");
        if (b->hits <= 0) {
            boss_untouchable();
            if (*G_SCREENSHAKE < BOSS_SHAKE_KEEP) *G_SCREENSHAKE = BOSS_SHAKE;
            if (!(b->ouch & 31)) {
                spirit_release(a, A_X(a), A_Y(a) - BOSS_SPIRIT_DY - (boss_rand() & 31));
                flink_sound(a, 0x1A);
            }
        }
        if (--b->ouch == 0 && b->hits <= 0) { boss_die(a, i); return; }
    } else if (b->guard) {
        if (--b->guard == 0) A_FLAGS(a) |= AF_HURTS | AF_HITTABLE;
    } else if (enemy_attacked(a, i)) {       /* Lomax's spin or helmet, as a stomp */
        boss_hit(a, i);
    }
    if (A_VY(a)) {                           /* in the air (0x29754) */
        int vy = A_VY(a) + BOSS_GRAVITY;
        if (vy > BOSS_FALL) vy = BOSS_FALL;
        A_VY(a) = vy;
        A_FY(a) += vy;
        set_pos(a);
        if (A_Y(a) >= b->homey) {
            place_y(a, b->homey);
            A_VY(a) = 0;
            *G_SCREENSHAKE = BOSS_SHAKE;
            b->squat = BOSS_SQUAT;
            flink_sound(a, 0x35);
        }
        if (b->ouch) Actor_AdvanceAnim(a);
        else show_frame(a, "JUMP", BOSS_SQUAT);
        return;
    }
    if (b->squat < 0) {                      /* squatting (0x297D6) */
        show_frame(a, "JUMP", BOSS_SQUAT + b->squat);
        if (++b->squat == 0) A_VY(a) = BOSS_JUMP;
        return;
    }
    if (b->squat > 0) {                      /* getting up (0x29802) */
        show_frame(a, "JUMP", --b->squat);
        return;
    }
    if (b->ouch) { Actor_AdvanceAnim(a); return; }
    x = A_X(a);                              /* walking (0x2981A) */
    mapw = P_W(PLANE(A_PLANE(a))) * 16;
    if (b->dir) { A_VX(a) = -BOSS_SPEED; b->wf++; if (x < BOSS_TURN) b->dir = 0; }
    else { A_VX(a) = BOSS_SPEED; b->wf--; if (x > mapw - BOSS_TURN) b->dir = 1; }
    A_FX(a) += A_VX(a);
    set_pos(a);
    if (A_X(a) == b->homex) b->squat = -BOSS_SQUAT;
    set_anim(a, "WALK");
    if (A_NFRAMES(a) > 0) b->wf = (b->wf % A_NFRAMES(a) + A_NFRAMES(a)) % A_NFRAMES(a);
    hold_frame(a, b->wf);
    Actor_AdvanceAnim(a);
}

static void __cdecl boss_update(BYTE *a)
{
    int i = enemy_slot(a);
    if (i < 0) return;
    boss_fighting();
    boss_move(a, i);
    if (enemy_slot(a) == i) trunks_update(a, &g_boss[i]);   /* (not once it broke up) */
}

/* 0x295D0: up out of sight until Lomax is near its x, then it drops in */
static void __cdecl boss_wait(BYTE *a)
{
    int i = enemy_slot(a);
    if (i < 0) return;
    if (abs(A_X(a) - *G_PLAYERX) >= BOSS_NEAR || A_PLANE(a) != *G_PLAYERPLANE) return;
    A_UPDATE(a) = (void *)boss_update;
    A_VY(a) = BOSS_FALL;
    A_FLAGS(a) |= AF_HURTS | AF_HITTABLE;
    music_play(g_boss[i].music);
    flog("boss drops in at x %d", A_X(a));
}

/* (from enemy_spawn) params 1 and 2: the music of the fight and of its defeat */
void boss_spawn(BYTE *a, int i, const BYTE *def)
{
    Boss *b = &g_boss[i];
    memset(b, 0, sizeof(*b));
    g_enemy[i].boss = 1;
    b->finish = def && D_FLAGS(def) & 0x8000 ? D_NEXT(def) : NULL;
    b->music = def_param(a, def, 1, 0);
    b->win = def_param(a, def, 2, 0);
    b->hits = BOSS_HITS;
    b->homex = A_X(a);
    b->homey = A_Y(a);
    A_UPDATE(a) = (void *)boss_wait;
    A_CULL(a) = CULL_NEVER;
    A_LAYER(a) = 0;                          /* behind the thrown objects and stones (layer 1, as enemies) */
    A_FLAGS(a) &= ~AF_CONTACT;
    A_VX(a) = A_VY(a) = 0;
    place_y(a, b->homey - BOSS_DROP);
    A_FX(a) = A_X(a) << 16;
}

/* (from enemy_spawn) defeated already in this visit: only its Finish */
void boss_gone(BYTE *a, const BYTE *def)
{
    if (def && D_FLAGS(def) & 0x8000) finish_later(a, D_NEXT(def), 1);
}

/* a thrown object hit its head */
void boss_struck(BYTE *a, int i) { boss_hit(a, i); }
