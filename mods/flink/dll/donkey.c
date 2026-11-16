/*
 * donkey.c - Flink's Donkey monkey (actor 'Donk', init 0x220FC, think 0x22176), TREE's boss, at the
 * top of the tree in 28-1 (TREE map 7; README "Enemies" (The Donkey monkey)). The converter makes it
 * a 'Muta Donk n' type with Flink's STAY, JUMP, LAND and OUCH (STAY keeping its hide selectors 1 and
 * 2); params 0, the fight's music and the music at its defeat (Lomax Music params). Its placement
 * links to the arena's Finish (placement +4), and the converter adds two placements at its x, y 0x3C,
 * never spawned by proximity: a 'Spik Apple' (distance 1000) and a 'Spik App2' (start speed 0), the
 * apples it shakes out of the canopy.
 *
 * Flink's monkey:
 * - Init: hits 0x8284 (8 at each level start, 0x14776; 1 if 0), draw list 1, window 0x18, +6 bits 3
 *   (thrown objects and the slide reach it) and 0, STAY; +2 bit 0 set (Actor_DrawFrame 0x328C8 skips
 *   a piece with hide selector n while +2's bit n - 1 is set: STAY has one piece of each, over its
 *   face), the timers +0x46 100 and +0x48 10, home y +0x4A its y, the wait
 *   +0x4C 100; it faces away from the map's right end (0x22700: left from the map's width - 160 on).
 * - Each frame (0x22176):
 *   - defeated and not hurt (+0x52 0): a hop (+0x52 1, JUMP frame 4, vy -2.0), the first one facing
 *     (0x22700) with 3 jumps across;
 *   - hit (+3 bit 6, the hitter at +0x34), unless Flink is dying, it is guarded (+0x54) or hurt: a
 *     spell (think 0x31A1E) or a spirit (0x31772) does nothing; Flink's Shot (0x32630) is turned back
 *     (mirrored, vx negated, x + 4 vx) and hits; anything else hits: a spirit at y - 70, +0x54 30,
 *     sound 0x2B, hits - 1; at 0 it faces (0x22700), +0x54 161, Flink can't be hurt (hurt timer -2)
 *     and the music changes (index 21, 0x29A7C). Then +0x52 1, standing (vy 0) -1 with a hop in place
 *     (JUMP frame 4, vy -2.0);
 *   - standing (vy 0, +0x4E 0): +0x4C - 1; still >= 0, STAY, its selector 1 piece shown for 8 frames
 *     every 60..180 (+0x46, the table 0x22376: a blink) and its selector 2 piece shown 11 frames,
 *     hidden 7..25 (+0x48, 0x223B4).
 *     Run out, it jumps if on screen (+6 bit 4), Flink no lower than its home y and the camera's x
 *     not moving (0x8738), starting the boss music (index 9) if another plays and Flink isn't dying;
 *     otherwise +0x4C 100 and STAY. The jump: JUMP, vy -5.0, +0x4E 0; a new series (+0x50 0) faces
 *     (0x22700) and is 3 jumps across (+0x50 3) one time in 4 (Rand & 3 = 0) and after 3 series in
 *     place in a row (+0x56), otherwise 2 jumps in place (+0x50 -2);
 *   - otherwise JUMP, at +0x4E's frame (8.8): squatting, +0x80 a frame from 0 to frame 4 while vy's
 *     whole part isn't 0, and getting up, -0x80 a frame back to 0 once it is (0x224E0). At frame 4 in
 *     the air: 1 px a frame its way when jumping across and not hurt in place, vy + 0x2000 (at most
 *     4.0), y + vy, and falling in place LAND; at its home y it lands: on screen the shake 0x13
 *     (0x874E) and sound 0x35; vy 0; a hop in place ends there (+0x52 0); else +0x52 0 and across,
 *     +0x50 - 1 (at 0 the wait +0x4C is 60 + Rand & 63); in place, on screen the shake 0x23 and sound
 *     0x35 again, +0x50 + 1: at 0 the wait, else the apples (0x22614);
 *   - then (0x224F2) its record's hitbox: the touch test on the other actors (0x1A5A0: App2s break,
 *     Falling apples burst), and unless +0x54 is set the touch test on Flink (0x1A46E: Player_TakeHit,
 *     or with Flink sliding into it the hit flag); OUCH while +0x52 is set; while +0x54 runs it
 *     counts down, hidden on its odd values (it blinks), and defeated, every 32 frames a spirit
 *     (y - 70 - Rand & 31), a flash and sound 0x1A, and at 0 its home y goes 400 px below the map, +0x54
 *     -2000 (it hops off the floor and falls out of the map, blinking) and it drops item 0x19
 *     (Item_Spawn at y - 80; 0x63AE set).
 * - The apples (0x22614): of 6 places across Flink's screen, x 90 + 40 k (320 - that facing left),
 *   k = 0..5 from the monkey's side on, one is left out (1..4 counted from the far side, Rand) and
 *   another (0..5, Rand) is an App2; the others are Falling apples. Each is spawned by name (0x32BD4)
 *   at y 0x3C with its think 0x226CC: down 1 px a frame to y 0x50, then waiting until the shake is 0x20
 *   or more (the next in-place landing's), then 5 + 10 k frames more (0x226F2), and then its own
 *   think: the Falling apple, whose 'distance' is 1000, shakes and falls; the App2 drops, bounces
 *   and lies there to be picked up.
 * - Flink stays 26..294 px inside its 320-px screen while the camera's x scrolls (0x1CCCC): 28-1's
 *   Scroll Point moves it right to the map's end, and the fight is on that last screen.
 *
 * Here: the same, but nothing of Lomax's hits it (Flink's slide does): only thrown objects (shots
 * turned back) and rolling or falling App2s; only HELMET from its first jump on (boss_fighting,
 * forestboss.c); the spirits spirits.c's, the shake Flink's
 * counter (razorback.c's flink_quake), and the apples spawned from the converter's two placements (moved
 * there for Object_TrySpawn, then unlinked from them: forgotten or freed, they don't touch the
 * placement). Flink's screen is the map's last 320 px, where the scroll stops it (Lomax+'s view
 * stops sooner, at the map's width - its own): the apples' places are on it, and once the fight has
 * started Lomax is held 26..294 px inside it (donk_after), an unseen wall where Lomax's wider view
 * shows more of the floor to the left, so the fight is Flink's at any width; it starts only with him
 * inside. While it dies Lomax can't be hurt (boss_untouchable, forestboss.c). Not ported: the flashes
 * and item 0x19: once it has
 * fallen out of sight (DONK_FINISH_WAIT) the arena's Finish appears in the middle of that screen, as
 * after the Forest Boss (boss_finish_later). The fanfare is played once (music_play_once). A defeated
 * monkey stays gone for the level visit (gone.c); the arena then has the Finish from the start.
 * Sounds: Flink's 0x35, 0x2B and 0x1A (sfx.c).
 */
#include "enemy.h"

#define DONK_HITS          8             /* 0x14776: 0x8284 */
#define DONK_WAIT          100           /* +0x4C, 0x2214C */
#define DONK_BLINK         100           /* +0x46, 0x2213A */
#define DONK_MOUTH         10            /* +0x48, 0x22140 */
#define DONK_SHUT          7             /* 0x22350: selector 1 shown 8 frames */
#define DONK_JUMP          (-0x50100)    /* 0xFFFAFF00 */
#define DONK_HOP           (-0x20100)    /* 0xFFFDFF00 */
#define DONK_GRAVITY       0x2000
#define DONK_FALL          0x40000
#define DONK_FRAME4        0x400         /* JUMP's frame 4 in +0x4E (8.8): in the air */
#define DONK_SQUAT         0x80
#define DONK_ACROSS        3
#define DONK_IN_PLACE      (-2)
#define DONK_SERIES        3             /* 0x2232A: in place at most 3 series in a row */
#define DONK_QUAKE         0x13          /* 0x2245A */
#define DONK_QUAKE_PLACE   0x23          /* 0x224AE */
#define DONK_OUCH          30            /* 0x2222A */
#define DONK_DYING         161           /* 0x2224A */
#define DONK_GONE          (-2000)       /* 0x225DE */
#define DONK_BELOW         400           /* 0x225D6: the home y under the map's height */
#define DONK_SPIRIT_DY     70            /* 0x22214 */
#define DONK_TURN          160           /* 0x22706: faces left from the map's width - 160 on */
#define DONK_SCREEN        320           /* Flink's screen; the fight is on the map's last one */
#define DONK_EDGE_L        26            /* 0x1CCCC: Flink kept 26..294 px inside it */
#define DONK_EDGE_R        294
#define DONK_APPLE_Y       0x3C          /* 0x22698: the apples' y (the converter's DONK_APPLE_Y) */
#define DONK_APPLE_DOWN    0x50          /* 0x226CC: they come down to here */
#define DONK_APPLE_QUAKE   0x20          /* 0x226DC: and wait for this much shake */
#define DONK_APPLE_X       0x5A          /* 0x2263E */
#define DONK_APPLE_DX      0x28
#define DONK_APPLE_DELAY   5             /* 0x22640, + 10 each */
#define DONK_APPLE_PLACES  6
#define DONK_FINISH_WAIT   100           /* ticks from its fall to the Finish */

static const short BLINK_TIMES[8] = { 180, 60, 120, 10, 120, 60, 120, 180 };   /* 0x22376 */
static const short MOUTH_TIMES[4] = { 24, 18, 12, 6 };                         /* 0x223B4 */

typedef struct {
    BYTE *type;
    BYTE *finish, *fall, *app2;          /* the Finish's placement, the apples' */
    int music, win, music_on;
    int hits;                            /* 0x8284 */
    int vy;                              /* +0x20, 16.16 down */
    int blink, mouth;                    /* +0x46, +0x48 */
    int hide1, hide2;                    /* +2 bits 0 and 1: hide selectors 1 and 2 */
    int home;                            /* +0x4A */
    int wait;                            /* +0x4C */
    int fr;                              /* +0x4E, JUMP's frame (8.8) */
    int jumps;                           /* +0x50: > 0 across, < 0 in place */
    int hurt;                            /* +0x52 */
    int guard;                           /* +0x54 */
    int series;                          /* +0x56 */
    int left;                            /* +3 bit 3 */
    int hit, hit_shot;                   /* +3 bit 6; the hitter (+0x34) and whether it is a shot */
    BYTE *hitter;
    int started, gone;                   /* its first jump (Lomax inside); fallen away */
} Donk;
static Donk g_donk[MAX_ACTORS];

/* the spawned apples coming down (0x226CC, 0x226F2): their own update, delay and stage */
static struct { BYTE *type; void *update; int delay, armed; } g_drop[MAX_ACTORS];

/* the wall as the last monkey update left it: for donk_after (g_wall_fresh: set this tick) */
static int g_wall_fresh, g_wall_plane = -1, g_wall_lo, g_wall_hi;

void donkeys_reset(void)
{
    memset(g_donk, 0, sizeof(g_donk));
    memset(g_drop, 0, sizeof(g_drop));
    g_wall_fresh = 0;
    g_wall_plane = -1;
}

static int donk_rand(void) { static unsigned r = 0x2E5D1F03u; r = r * 1103515245u + 12345u; return (int)(r >> 16) & 0x7FFF; }

static Donk *donk_of(const BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_donk[i].type && g_donk[i].type == A_TYPE(a) && g_enemy[i].donk ? &g_donk[i] : NULL;
}

/* Flink's last screen in the map: where the fight is */
static int arena_x(const BYTE *a)
{
    int x = P_W(PLANE(A_PLANE(a))) * 16 - DONK_SCREEN;
    return x > 0 ? x : 0;
}

/* 0x22700: facing away from the map's right end */
static void face(BYTE *a, Donk *m)
{
    m->left = A_X(a) >= P_W(PLANE(A_PLANE(a))) * 16 - DONK_TURN;
    if (m->left) A_FLAGS(a) |= AF_LEFT;
    else A_FLAGS(a) &= ~AF_LEFT;
}

static void show_frame(BYTE *a, const char *tag, int frame)
{
    set_anim(a, tag);
    hold_frame(a, frame);
    Actor_AdvanceAnim(a);
}

/* ---- The apples -------------------------------------------------------------------------------- */

static int dropping(const BYTE *d, int k) { return k >= 0 && g_drop[k].type && g_drop[k].type == A_TYPE(d); }

/* 0x226CC, 0x226F2: down to y 0x50, then the shake, then the delay; then its own think */
static void __cdecl drop_update(BYTE *d)
{
    int k = slot_of(d);
    if (!dropping(d, k)) return;
    if (A_Y(d) != DONK_APPLE_DOWN) place_y(d, A_Y(d) + 1);
    else if (!g_drop[k].armed) g_drop[k].armed = flink_quake() >= DONK_APPLE_QUAKE;
    else if (--g_drop[k].delay < 0) {
        A_UPDATE(d) = g_drop[k].update;
        g_drop[k].type = NULL;
        flog("donkey monkey's %s at (%d,%d) falls", T_NAME(A_TYPE(d)), A_X(d), A_Y(d));
    }
    Actor_AdvanceAnim(d);
}

/* Flink's 0x32BD4 (a type spawned by name at x, y): the placement spawned there, then no longer its */
static void apple_drop(BYTE *def, int x, int delay)
{
    short ox, oy;
    BYTE *d;
    int k;
    if (!def) return;
    ox = D_X(def); oy = D_Y(def);
    D_X(def) = (short)x; D_Y(def) = DONK_APPLE_Y;
    D_STATE(def) &= ~DS_SPAWNED;
    d = Object_TrySpawn(def);
    D_X(def) = ox; D_Y(def) = oy;
    if (!d || (k = slot_of(d)) < 0 || A_UPDATE(d) == NULL) return;
    A_DEF(d) = NULL;                         /* (its burst or throw forgets nothing) */
    A_CULL(d) = CULL_NEVER;
    g_drop[k].type = A_TYPE(d);
    g_drop[k].update = A_UPDATE(d);
    g_drop[k].delay = delay;
    g_drop[k].armed = 0;
    A_UPDATE(d) = (void *)drop_update;
}

/* 0x22614 */
static void apples(BYTE *a, Donk *m)
{
    int none, app2, k, x0 = arena_x(a);
    do none = (donk_rand() & 7) + 1; while (none >= 5);
    do app2 = donk_rand() & 7; while (app2 >= 6 || app2 == none);
    flog("donkey monkey shakes the apples down (%s, none at %d, the app2 at %d)", m->left ? "from the right" : "from the left",
         none, app2);
    for (k = 0; k < DONK_APPLE_PLACES; k++) {
        int n = DONK_APPLE_PLACES - 1 - k, dx = DONK_APPLE_X + k * DONK_APPLE_DX;
        if (n == none) continue;
        apple_drop(n == app2 ? m->app2 : m->fall, x0 + (m->left ? DONK_SCREEN - dx : dx), DONK_APPLE_DELAY + 10 * k);
    }
}

/* ---- The fight --------------------------------------------------------------------------------- */

/* a thrown object or rolling apple o (its box x1..y2) touches the monkey t (Actor_FindTouchingActor
   0x1A5A0, +6 bit 3): its hit flag, for its next think. shot: Flink's Shot (0x32630), turned back
   there. 1 = it touched (a shot only when it can hit: guarded, the monkey lets it fly on) */
int donk_struck(BYTE *t, BYTE *o, int x1, int y1, int x2, int y2, int shot)
{
    Donk *m = t ? donk_of(t) : NULL;
    int tx1, ty1, tx2, ty2;
    if (!m || m->gone || !actor_box(t, &tx1, &ty1, &tx2, &ty2) || x2 < tx1 || x1 > tx2 || y2 < ty1 || y1 > ty2) return 0;
    if (shot && (m->guard || m->hurt)) return 0;
    if (!m->hit) {
        m->hit = 1;
        m->hitter = o;
        m->hit_shot = shot;
    }
    return 1;
}

/* the hit (0x221AE) */
static void donk_hit(BYTE *a, int i, Donk *m)
{
    BYTE *o = m->hitter;
    m->hit = 0;
    if (*G_PLAYERDEATHTIMER || m->guard || m->hurt) return;
    if (m->hit_shot && o && slot_of(o) >= 0 && grab_held(o)) {   /* turned back (0x221FA) */
        A_FLAGS(o) ^= AF_LEFT;
        A_VX(o) = -A_VX(o);
        A_FX(o) += A_VX(o) * 4;
        set_pos(o);
    }
    spirit_release(a, A_X(a), A_Y(a) - DONK_SPIRIT_DY);
    m->guard = DONK_OUCH;
    flink_sound(a, 0x2B);
    if (--m->hits <= 0) {
        face(a, m);
        m->guard = DONK_DYING;
        music_play_once(m->win);             /* (a fanfare: Lomax would loop it) */
    }
    flog("donkey monkey hit%s, %d left", m->hit_shot ? " by a shot, turned back" : "", m->hits);
    m->hurt = 1;
    if (!m->vy) {
        m->hurt = -1;
        m->fr = DONK_FRAME4;
        m->vy = DONK_HOP;
    }
    (void)i;
}

/* 0x1A46E: its box against Lomax's (inclusive): Player_TakeHit. Flink's slide toward it (+6 bit 3)
   sets its hit flag instead; Lomax's roll doesn't: only what is thrown or rolls at it hits it */
static void touch_lomax(BYTE *a, Donk *m, int x1, int y1, int x2, int y2)
{
    const BYTE *pa = *G_PLAYERANIM;
    int px = *G_PLAYERX, py = *G_PLAYERY;
    (void)m;
    if (!pa || A_PLANE(a) != *G_PLAYERPLANE || *G_PLAYERDEATHTIMER) return;
    if (x2 < px + HB_X1(pa) || x1 > px + HB_X2(pa) || y2 < py + HB_Y1(pa) || y1 > py + HB_Y2(pa)) return;
    if (*G_PLAYERHURTTIMER) return;
    flog("donkey monkey hurts Lomax");
    Player_TakeHit(A_X(a), 0, 0);
}

/* 0x1A5A0: the first actor its box touches among this frame's slots (by parity) is hit */
static void touch_others(BYTE *a, int x1, int y1, int x2, int y2)
{
    int k, self = slot_of(a);
    for (k = *G_TICKCOUNTER & 1; k < MAX_ACTORS; k += 2) {
        BYTE *t = actor_at(k);
        if (k == self || !actor_active(k) || !A_TYPE(t) || A_LAYER(t) == KILLED_LAYER || A_PLANE(t) != A_PLANE(a)) continue;
        if (bigapple_struck(t, x1, y1, x2, y2) || apple_struck(t, x1, y1, x2, y2)) return;
    }
}

/* 0x224F2 on: its box, the touch tests, OUCH, the guard and its blinking, the defeat */
static void donk_after_think(BYTE *a, int i, Donk *m)
{
    int x1, y1, x2, y2;
    if (actor_box(a, &x1, &y1, &x2, &y2)) {
        touch_others(a, x1, y1, x2, y2);
        if (!m->guard) touch_lomax(a, m, x1, y1, x2, y2);
    }
    if (m->hurt) show_frame(a, "OUCH", 0);
    A_FLAGS(a) &= ~AF_HIDDEN;
    if (!m->guard) return;
    if (m->guard > 0 && m->hits <= 0) boss_untouchable();   /* 0x22542: Flink can't be hurt */
    if (m->guard > 0 && m->hits <= 0 && !(m->guard & 31)) {
        spirit_release(a, A_X(a), A_Y(a) - DONK_SPIRIT_DY - (donk_rand() & 31));
        flink_sound(a, 0x1A);
    }
    if (--m->guard == 0 && m->hits <= 0) {   /* 0x225C6: it falls out of the map */
        m->home = P_H(PLANE(A_PLANE(a))) * 16 + DONK_BELOW;
        m->guard = DONK_GONE;
        m->gone = 1;
        forget_placement(A_DEF(a));
        enemy_defeated(a);
        boss_finish_later(a, m->finish, DONK_FINISH_WAIT);
        flog("donkey monkey defeated, falls away from (%d,%d)", A_X(a), A_Y(a));
    }
    if (m->guard & 1) A_FLAGS(a) |= AF_HIDDEN;
    (void)i;
}

/* 0x2233C: STAY and its two selectors */
static void stay(BYTE *a, Donk *m)
{
    set_anim(a, "STAY");
    if (--m->blink < 0) {
        m->blink = DONK_SHUT;
        m->hide1 = !m->hide1;
        if (m->hide1) m->blink = BLINK_TIMES[(donk_rand() & 0xE) >> 1];
    }
    if (--m->mouth < 0) {
        m->mouth = DONK_MOUTH;
        m->hide2 = !m->hide2;
        if (m->hide2) m->mouth = MOUTH_TIMES[(donk_rand() & 6) >> 1];
    }
    A_HIDE(a) = (WORD)((A_HIDE(a) & ~6) | (m->hide1 ? 2 : 0) | (m->hide2 ? 4 : 0));
    hold_frame(a, 0);
    Actor_AdvanceAnim(a);
}

/* 0x22298: may it jump? On screen, Lomax no lower than its home and inside Flink's screen, the
   camera's x not moving */
static int may_jump(BYTE *a, Donk *m)
{
    int x0 = arena_x(a);
    if (!enemy_on_screen(a) || *G_PLAYERY > m->home || scroll_x_moving() || A_PLANE(a) != *G_PLAYERPLANE) return 0;
    return m->started || (*G_PLAYERX >= x0 + DONK_EDGE_L && *G_PLAYERX <= x0 + DONK_EDGE_R);
}

/* 0x222E6 */
static void jump(BYTE *a, Donk *m)
{
    if (!m->started) flog("donkey monkey's fight starts: Lomax at (%d,%d)", *G_PLAYERX, *G_PLAYERY);
    m->started = 1;
    if (!m->music_on && !*G_PLAYERDEATHTIMER) { music_play(m->music); m->music_on = 1; }
    Actor_SetAnim(a, FOURCC("JUMP"));
    m->vy = DONK_JUMP;
    m->fr = 0;
    if (m->jumps) return;
    face(a, m);
    if (!(donk_rand() & 3) || ++m->series > DONK_SERIES) { m->series = 0; m->jumps = DONK_ACROSS; }
    else m->jumps = DONK_IN_PLACE;
}

/* 0x22444: on its home y */
static void land(BYTE *a, Donk *m)
{
    int seen = enemy_on_screen(a);
    if (seen) { flink_quake_set(DONK_QUAKE); flink_sound(a, 0x35); }
    m->vy = 0;
    place_y(a, m->home);
    if (m->hurt < 0) { m->hurt = 0; return; }
    m->hurt = 0;
    if (m->jumps > 0) {
        if (--m->jumps == 0) m->wait = 60 + (donk_rand() & 63);
        return;
    }
    if (seen) { flink_quake_set(DONK_QUAKE_PLACE); flink_sound(a, 0x35); }
    if (++m->jumps != 0) apples(a, m);
    else m->wait = 60 + (donk_rand() & 63);
}

static void __cdecl donk_update(BYTE *a)
{
    int i = enemy_slot(a), standing, landing = 0;
    Donk *m = donk_of(a);
    if (i < 0 || !m) return;
    if (m->started && !m->gone) boss_fighting();   /* (only HELMET: spells.c) */
    if (m->hits <= 0 && !m->hurt) {          /* defeated: it hops (0x22176) */
        if (m->jumps <= 0) { face(a, m); m->jumps = DONK_ACROSS; }
        m->hurt = 1;
        m->fr = DONK_FRAME4;
        m->vy = DONK_HOP;
    }
    if (m->hit) donk_hit(a, i, m);
    standing = !m->vy && !m->fr;
    if (standing) {                          /* 0x22280 */
        if (--m->wait >= 0) stay(a, m);
        else if (may_jump(a, m)) jump(a, m);
        else { m->wait = DONK_WAIT; stay(a, m); }
    } else if ((m->fr >> 8) == 4 && m->vy) { /* in the air (0x223D2) */
        if (m->jumps > 0 && m->hurt >= 0) { A_FX(a) += m->left ? -0x10000 : 0x10000; set_pos(a); }
        m->vy += DONK_GRAVITY;
        if (m->vy > DONK_FALL) m->vy = DONK_FALL;
        A_FY(a) += m->vy;
        set_pos(a);
        landing = m->vy >= 0 && m->jumps < 0;
        if (A_Y(a) >= m->home) land(a, m);
    } else {                                 /* squatting or getting up (0x224E0) */
        m->fr += m->vy >> 16 ? DONK_SQUAT : -DONK_SQUAT;
        if (m->fr < 0) m->fr = 0;
    }
    if (landing) show_frame(a, "LAND", 0);   /* (STAY is stay's) */
    else if (!standing || m->vy) show_frame(a, "JUMP", m->fr >> 8 < 4 ? m->fr >> 8 : 4);
    donk_after_think(a, i, m);
    if (m->gone && below_map(a)) {
        flog("donkey monkey out of the map");
        g_enemy[i].type = NULL;
        m->type = NULL;
        Actor_Kill(a);
        return;
    }
    if (m->started && !m->gone) {            /* the wall (donk_after) */
        int x0 = arena_x(a);
        g_wall_fresh = 1;
        g_wall_plane = A_PLANE(a);
        g_wall_lo = x0 + DONK_EDGE_L;
        g_wall_hi = x0 + DONK_EDGE_R;
    }
}

/* after Player_Update (player_tick): Lomax held 26..294 px inside Flink's screen while the fight is
   on (0x1CCCC), his speed into its edge taken off */
void donk_after(void)
{
    int px;
    if (!g_wall_fresh) { g_wall_plane = -1; return; }
    g_wall_fresh = 0;
    if (!g_active || g_wall_plane != *G_PLAYERPLANE || *G_PLAYERSTATE & PS_FLY) return;
    px = *G_PLAYERX;
    if (px < g_wall_lo) { px = g_wall_lo; if (*G_PLAYERVX < 0) *G_PLAYERVX = 0; }
    if (px > g_wall_hi) { px = g_wall_hi; if (*G_PLAYERVX > 0) *G_PLAYERVX = 0; }
    if (px != *G_PLAYERX) { *G_PLAYERX = px; *G_PLAYERXFIXED = px << 16; }
}

/* the plane's placement of a type named so at x, y (its list: Plane +0, placement +0 next, +8 type) */
static BYTE *placement_at(int plane, const char *prefix, int x, int y)
{
    BYTE *d;
    for (d = *(BYTE **)PLANE(plane); d; d = *(BYTE **)d) {
        BYTE *t = *(BYTE **)(d + 8);
        if (t && !strncmp(T_NAME(t), prefix, strlen(prefix)) && D_X(d) == x && D_Y(d) == y) return d;
    }
    return NULL;
}

/* (from enemy_spawn) defeated already in this visit: only its Finish */
void donk_gone(BYTE *a, int i, const BYTE *def)
{
    g_donk[i].type = NULL;
    g_enemy[i].donk = 0;
    boss_gone(a, def);
}

/* (from enemy_spawn) params 1 and 2: the music of the fight and of its defeat (0x220FC) */
void donk_spawn(BYTE *a, int i, const BYTE *def)
{
    Donk *m = &g_donk[i];
    memset(m, 0, sizeof(*m));
    g_enemy[i].donk = 1;
    m->type = A_TYPE(a);
    m->finish = def && D_FLAGS(def) & 0x8000 ? D_NEXT(def) : NULL;
    m->music = def_param(a, def, 1, 0);
    m->win = def_param(a, def, 2, 0);
    if (def) {
        m->fall = placement_at(A_PLANE(a), "Spik Apple", D_X(def), DONK_APPLE_Y);
        m->app2 = placement_at(A_PLANE(a), "Spik App2", D_X(def), DONK_APPLE_Y);
    }
    m->hits = DONK_HITS;
    m->blink = DONK_BLINK;
    m->mouth = DONK_MOUTH;
    m->hide1 = 1;
    m->home = A_Y(a);
    m->wait = DONK_WAIT;
    A_UPDATE(a) = (void *)donk_update;
    A_CULL(a) = CULL_NEVER;
    A_LAYER(a) = (short)FLINK_LAYER(1);      /* (a5) = 0x7D6C: list 1, in front of Lomax */
    A_FLAGS(a) &= ~AF_CONTACT;               /* the actor touches nothing itself: Flink's tests */
    A_VX(a) = A_VY(a) = 0;
    A_FX(a) = A_X(a) << 16;
    place_y(a, m->home);
    face(a, m);
    Actor_SetAnim(a, FOURCC("STAY"));
    A_HIDE(a) = 2;
    flog("donkey monkey at (%d,%d): apples %s, finish %s", A_X(a), A_Y(a), m->fall && m->app2 ? "found" : "MISSING",
         m->finish ? "linked" : "none");
}
