/*
 * razorback.c - MOUNTAIN's stampede of Razorbacks, Flink's 'Razorback' (init 0x27486, think 0x274BE; the
 * Razorbacks' think 0x275BA; only in 21-1, MOUNTAIN map 10, four placed) (README "Enemies").
 *
 * The placement is no Razorback: it is an unseen spawner (+3 bit 1) that sends Razorbacks at Flink. Params:
 * 'time' (+0x46), checkbox 'move left' (+3 bit 3: the Razorbacks come from the right, running left).
 * Init: +0x48 = 10 (the first Razorback), +0x4A = 0 (the Razorbacks alive, counted by them every frame).
 * The spawner's think (0x274BE), every frame:
 * - the shake: d = the Razorbacks that ran last frame + 1; when the camera's shake counter (0x874E) & 3 is
 *   0, d = min(d, 8) / 2, and if d > the counter / 8 the counter is d * 4. So the screen shakes while
 *   a spawner runs, more with more Razorbacks: the counter cycles 4, 3, 2, 1 with one Razorback or none, up to
 *   16 .. 13 with 7 or more.
 * - time - 1; below 0 it waits for its Razorbacks to be gone (+0x4A 0 on a frame), then frees itself
 *   (Actor_Free 0x32D9E: its placement doesn't spawn it again before a lost life);
 * - else +0x48 - 1, and at 0 (+0x48 = 30: a Razorback every 30 frames) a new actor (0x32CD8) as a Razorback:
 *   draw list 2 (0x7D6E), stompable (+6 bit 3, the hit test's), window 0x18 (never culled), its grunt
 *   period +0x56 = 50 + (rand & 15), its speed (20 + (rand & 15)) / 8 px a frame (2.5 .. 4.375),
 *   the spawner's y, x = the camera's x - 100 (running right) or + 420 (left: 100 px right of
 *   Flink's 320-px screen), +0x4A = the spawner's counter, RUN.
 * The camera (Camera_Update, 0x19F84): while the counter isn't 0, - 1 a frame and the camera's y +
 * the word at 0x19FB4 + (the counter before & ~1): 0, 0, -1, 1, -2, 2, -3, 3, -2, 2, -3, 3, -4, 4,
 * -3, 3, -4, 4.
 * A Razorback's think (0x275BA), every frame:
 * - +0x54 + 1, and each +0x56 frames sound 0x18 when on the screen (+6 bit 4: drawn last frame);
 * - + 1 to its spawner's counter;
 * - hit (+3 bit 6, cleared): by anything but a Razorback (+0x34's think 0x275BA) it dies (Enemy_Die
 *   0x1B12E); a running Razorback's hit is let be. A Razorback whose hitter has died since (an edge, a rock,
 *   in its own think after the hit) dies of it: so Razorbacks that run into a dying one die with it.
 * - 0x1A462: touching Flink hurts him (0x1A7DC), and Flink sliding (SL45) into its front hits it
 *   (not here: Lomax can't kill a Razorback);
 * - Actor_FindTouchingActor (0x1A5A0, its hitbox against those of the other actors with +6 bit 3,
 *   in slot order, every other slot: the even ones on even frames, the odd ones on odd frames) hits
 *   the first one touched (0x1A6D2: its +3 bit 6, +0x34 = the Razorback); that one a Rock (think
 *   0x291EC): the Razorback dies (and the rock counts it: rock.c rock_hit);
 * - x + vx (+0x1C = +0x46); Actor_SnapToGround (0x1AE96: from y - 16 down 3 cells, within the
 *   table 0x1AECA by vx's whole px): on it, y = the ground and RUN animates; no ground (a ledge, a
 *   pit, the map's end): it dies.
 * Enemy_Die (0x1B12E): DEAD, sound 0x22 on the screen, vy 4.0 up, vx kept (its running speed), draw
 * list 1 (in front of Flink), falls through everything (0x1B1AA: gravity 0.25, at most 4.0, vx braked
 * by 0x2AA) and is freed off the screen.
 * Here: the converter's 'Muta Razo n' (object 74, enemy_spawn) with RUN and DEAD; the spawner waits
 * for Flink's window as the other enemies (enemy_wait); the Razorbacks are children (Actor_Alloc) with the
 * spawner's type. Lomax's view is wider than Flink's: a Razorback running left starts 100 px right of
 * Lomax's view (within the map: at its last column at most). The hit test runs over Lomax's actor
 * slots with Flink's parity; what a Razorback finds is another running Razorback (it records the hit), a rock
 * (rock.c) or another Flink enemy (enemy_struck, as yeti.c's spin). Not Flink's: Lomax can't kill a
 * Razorback (the level is about avoiding them), so his spin, roll and helmet (Flink's
 * slide) do nothing to it and thrown objects go through it (enemy_struck leaves it alone); none of
 * the Razorbacks counts as defeated (no spirit, coin or HUD count). Landing on one
 * hurts Lomax: Flink's Razorback has no stomp (contact.c, razorback_spiked). The shake is Flink's counter in
 * g_quake, its offset added to the camera's y after Player_Update (razorback_after) and taken off before
 * the next (razorback_before). Sounds: Lomax's ROCKHITX (38) for 0x18 (as statue.c's), the enemy hit for
 * 0x22, the rock's 0x29 in rock.c.
 */
#include "enemy.h"

#define FIRST_WAIT      10              /* 0x274A2 */
#define EVERY           30              /* 0x27520 */
#define SPAWN_BEHIND    100             /* 0x27580: camera x - 100 */
#define FLINK_VIEW_W    320             /*   + 0x208 = 420: 100 px past Flink's screen */
#define GRUNT_BASE      50              /* 0x27558 */
#define SPEED_BASE      20              /* 0x2756A: (20 + rand & 15) << 13 */
#define GROUND_CELLS    3               /* 0x1AE9E */
#define DEAD_FRICTION   0x2AA           /* 0x1B1B0 */
#define DEAD_VY         0x40000         /* 0x1B164 */
#define SND_GRUNT       38              /* ROCKHITX for Flink's 0x18 (statue.c) */
#define G_TICKCOUNTER   ((volatile int *)0x0052C564u)

/* 0x1AECA: the ground's reach by vx's whole px, -8 .. 7 (wagon.c) */
static const BYTE STICK[16] = { 10, 10, 10, 10, 9, 7, 6, 4, 3, 4, 6, 7, 10, 10, 10, 10 };
/* 0x19FB4: the camera's y offset by the shake counter / 2 */
static const signed char QUAKE[18] = { 0, 0, -1, 1, -2, 2, -3, 3, -2, 2, -3, 3, -4, 4, -3, 3, -4, 4 };

typedef struct {
    int serial;                          /* this spawner or Razorback (0: none) */
    int spawner;                         /* 1: the placement, 0: a Razorback */
    int time, wait, count;               /* the spawner's +0x46, +0x48, +0x4A */
    int parent, pserial;                 /* a Razorback's spawner (its counter, +0x4A) */
    int vx, t, every, seen;              /* +0x46, +0x54, +0x56, +6 bit 4 */
    int hitter, hserial;                 /* +3 bit 6 / +0x34 from another Razorback, -1: none */
} Razo;
static Razo g_razo[MAX_ACTORS];
static int g_serial;
static int g_quake;                      /* Flink's shake counter, 0x874E */
static BYTE *g_qplane;                   /* the plane and camera y razorback_after left, its offset */
static int g_qy, g_qdy;

void razorbacks_reset(void)
{
    memset(g_razo, 0, sizeof(g_razo));
    g_quake = 0;
    g_qplane = NULL;
}

static int is_razo(int i) { return i >= 0 && g_enemy[i].razo && g_razo[i].serial; }

/* Flink's shake counter (0x874E), for the others that shake the screen (bigapple.c) */
int flink_quake(void) { return g_quake; }
void flink_quake_set(int n) { g_quake = n; }

/* a Razorback still running (its think 0x275BA): not dying, not freed */
static int running(int k, int serial)
{
    BYTE *b = actor_at(k);
    return k >= 0 && serial && g_razo[k].serial == serial && !g_razo[k].spawner && g_enemy[k].razo &&
           !g_enemy[k].dead && actor_active(k) && A_TYPE(b) == g_enemy[k].type && A_LAYER(b) != KILLED_LAYER;
}

static int view_w(const BYTE *pl)
{
    int w = P_W(pl) * 16 - P_MAXX(pl);
    return P_MAXX(pl) <= 0 || w < FLINK_VIEW_W ? FLINK_VIEW_W : w;
}

/* Enemy_Die (0x1B12E) */
static void razorback_die(BYTE *a, int i, const char *why)
{
    g_enemy[i].dead = 1;
    A_FLAGS(a) &= ~AF_CONTACT;
    A_VX(a) = g_razo[i].vx;
    A_VY(a) = -DEAD_VY;
    A_LAYER(a) = FLINK_LAYER(1);
    set_anim(a, "DEAD");
    if (g_razo[i].seen) flink_sound(a, 0x22);
    flog("razorback at (%d,%d) dies: %s", A_X(a), A_Y(a), why);
}


/* landing on a Razorback hurts Lomax (no stomp in its think) */
int razorback_spiked(BYTE *a)
{
    int i = a ? enemy_slot(a) : -1;
    return is_razo(i) && !g_razo[i].spawner && !g_enemy[i].dead;
}

/* 0x1A5A0: the first actor touched among the slots of this frame's parity, hit */
static BYTE *razorback_touch(BYTE *a, int self)
{
    int x1, y1, x2, y2, k;
    if (!actor_box(a, &x1, &y1, &x2, &y2)) return NULL;
    for (k = *G_TICKCOUNTER & 1; k < MAX_ACTORS; k += 2) {
        BYTE *t = actor_at(k);
        int e, tx1, ty1, tx2, ty2, razorback, rock;
        if (k == self || !actor_active(k) || !A_TYPE(t) || A_LAYER(t) == KILLED_LAYER || A_PLANE(t) != A_PLANE(a)) continue;
        razorback = running(k, g_razo[k].serial);
        rock = !razorback && rock_is(t);
        e = razorback || rock ? -1 : enemy_slot(t);
        if (!razorback && !rock && (e < 0 || g_enemy[e].dead || !(A_FLAGS(t) & AF_HITTABLE) || grab_held(t))) continue;
        if (!actor_box(t, &tx1, &ty1, &tx2, &ty2) || x1 > tx2 || tx1 > x2 || y1 > ty2 || ty1 > y2) continue;
        if (razorback) { g_razo[k].hitter = self; g_razo[k].hserial = g_razo[self].serial; }
        else if (!rock) {
            flog("razorback at (%d,%d) hits an enemy at (%d,%d)", A_X(a), A_Y(a), A_X(t), A_Y(t));
            if (!yeti_deflects(t, a)) enemy_struck(t, 0);
        }
        return t;
    }
    return NULL;
}

/* 0x1B1AA: falls through everything, freed off the screen */
static void razorback_dead(BYTE *a, int i)
{
    fall_and_land(a, DEAD_FRICTION, 0);
    Actor_AdvanceAnim(a);
    if (!enemy_on_screen(a) || below_map(a)) {
        g_enemy[i].type = NULL;
        g_razo[i].serial = 0;
        Actor_Kill(a);
    }
}

/* 0x275BA */
static void razorback_run(BYTE *a, int i)
{
    Razo *b = &g_razo[i];
    BYTE *t;
    int g, k;
    if (++b->t % b->every == 0 && b->seen) Sound_PlayEffect(a, SND_GRUNT);
    if (b->parent >= 0 && g_razo[b->parent].serial == b->pserial) g_razo[b->parent].count++;
    if (b->hitter >= 0) {                                     /* (0x275EE) */
        int h = b->hitter, s = b->hserial;
        b->hitter = -1;
        if (!running(h, s)) { razorback_die(a, i, "it ran into a dying razorback"); return; }
    }
    if ((t = razorback_touch(a, i)) && rock_is(t)) {               /* (0x27628) */
        razorback_die(a, i, "it ran into a rock");
        rock_hit(t, a);
        return;
    }
    A_VX(a) = b->vx;
    A_FX(a) += b->vx;
    set_pos(a);
    g = (int)Plane_FindGroundY(PLANE(A_PLANE(a)), A_X(a), A_Y(a), GROUND_CELLS);
    k = b->vx >> 16;
    if (g == NO_GROUND || abs(g - A_Y(a)) > STICK[(k < -8 ? -8 : k > 7 ? 7 : k) + 8]) {
        razorback_die(a, i, "no ground");
        return;
    }
    place_y(a, g);
    Actor_AdvanceAnim(a);
}

/* 0x27520: a Razorback from spawner s */
static void razorback_new(BYTE *s, int si)
{
    BYTE *b = Actor_Alloc(s, NULL);
    const BYTE *pl = PLANE(A_PLANE(s));
    int k = b ? slot_of(b) : -1, x, mapw = P_W(pl) * 16;
    Razo *r;
    if (k < 0) return;
    memset(&g_enemy[k], 0, sizeof(g_enemy[k]));
    g_enemy[k].type = A_TYPE(b);
    g_enemy[k].razo = 1;
    r = &g_razo[k];
    memset(r, 0, sizeof(*r));
    r->serial = ++g_serial;
    r->parent = si;
    r->pserial = g_razo[si].serial;
    r->hitter = -1;
    r->every = GRUNT_BASE + (rand() & 15);
    r->vx = (SPEED_BASE + (rand() & 15)) << 13;
    x = P_CAMX(pl) - SPAWN_BEHIND;
    A_FLAGS(b) = AF_HURTS | AF_HITTABLE;
    if (A_FLAGS(s) & AF_LEFT) {
        r->vx = -r->vx;
        x = P_CAMX(pl) + view_w(pl) + SPAWN_BEHIND;
        if (x > mapw - 1) x = mapw - 1;
        A_FLAGS(b) |= AF_LEFT;
    }
    A_DEF(b) = NULL;
    A_DRAW(b) = A_DRAW(s);
    A_UPDATE(b) = A_UPDATE(s);
    A_LAYER(b) = FLINK_LAYER(2);
    A_CULL(b) = CULL_NEVER;                  /* (window 0x18) */
    A_X(b) = x;
    A_Y(b) = A_Y(s);
    A_FX(b) = x << 16;
    A_FY(b) = A_Y(b) << 16;
    A_VX(b) = A_VY(b) = 0;
    Actor_SetAnim(b, FOURCC("RUN "));
    flog("razorback at (%d,%d) sends a razorback from x %d at %d/65536", A_X(s), A_Y(s), x, r->vx);
}

/* 0x274BE */
static void spawner_run(BYTE *a, int i)
{
    Razo *s = &g_razo[i];
    int d = s->count + 1;
    if (!(g_quake & 3)) {
        if (d > 8) d = 8;
        d >>= 1;
        if (d > g_quake >> 3) g_quake = d * 4;
    }
    if (--s->time < 0) {
        if (s->count) { s->count = 0; return; }
        flog("razorback at (%d,%d) done", A_X(a), A_Y(a));
        g_enemy[i].type = NULL;
        s->serial = 0;
        Actor_Kill(a);                       /* (Actor_Free: its placement stays spawned) */
        return;
    }
    s->count = 0;
    if (--s->wait) return;
    s->wait = EVERY;
    razorback_new(a, i);
}

static void __cdecl razo_update(BYTE *a)
{
    int i = enemy_slot(a);
    if (!is_razo(i)) return;
    if (g_razo[i].spawner) { spawner_run(a, i); return; }
    if (g_enemy[i].dead) { razorback_dead(a, i); return; }
    razorback_run(a, i);
    g_razo[i].seen = enemy_on_screen(a);     /* (+6 bit 4: Flink draws after its think) */
}

/* (from enemy_spawn) 0x27486: the spawner, unseen and untouchable; params facing left, time */
void razo_spawn(BYTE *a, int i, const BYTE *def)
{
    Razo *s = &g_razo[i];
    memset(s, 0, sizeof(*s));
    s->serial = ++g_serial;
    s->spawner = 1;
    s->time = def_param(a, def, 1, 0);
    s->wait = FIRST_WAIT;
    s->hitter = -1;
    g_enemy[i].razo = 1;
    A_FLAGS(a) = (A_FLAGS(a) & ~AF_CONTACT) | AF_HIDDEN;
    A_UPDATE(a) = (void *)razo_update;
    Actor_SetAnim(a, FOURCC("RUN "));
    flog("razorback at (%d,%d): %d frames, %s", A_X(a), A_Y(a), s->time, A_FLAGS(a) & AF_LEFT ? "from the right" : "from the left");
}

/* before Player_Update (player_tick): the shake's offset off the camera again */
void razorback_before(void)
{
    BYTE *pl = *G_PCURPLANE;
    if (g_qplane && pl == g_qplane && P_CAMY(pl) == g_qy) P_CAMY(pl) -= g_qdy;
    g_qplane = NULL;
}

/* after Player_Update (player_tick), last: Camera_Update's shake (0x19F84) */
void razorback_after(void)
{
    BYTE *pl = *G_PCURPLANE;
    int d;
    if (!g_active || !g_quake || !pl) return;
    d = QUAKE[g_quake >> 1 < 17 ? g_quake >> 1 : 17];
    g_quake--;
    if (!d) return;
    P_CAMY(pl) += d;
    g_qplane = pl;
    g_qy = P_CAMY(pl);
    g_qdy = d;
}
