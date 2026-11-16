/*
 * cyclindude.c - VILLAGE's Cyclin' Dude (9-1 on; README "Enemies"): the Cyclin' Dude (Flink 0x3045A,
 * think 0x304F0), who hangs from his Chopper Bike (0x30724) and drops Little Dudes (0x3099C); knocked
 * off, he walks (0x305A4), and Lomax can take the bike and fly it (0x307B2). The converter's types:
 * 'Muta Cycl n' (HANG, DEAD, WALK, SL45; params 'move left', hook 1 and hook 2), 'Muta Bike n' (CYCL,
 * DEAD) and 'Muta Litt n' (WALK, STUN, STUN's effects, DEAD, the small shot SHSM). Flink spawns the
 * bike and the Little Dudes by type name (0x32BD4), so their maps place one of each never to spawn,
 * only to load the type; here they are found by name in the level's types (type_named).
 *
 * The Cyclin' Dude:
 * - Spawned (always by a Generator in Flink's maps), he spawns his bike (held: +3 bit 1, its think
 *   does nothing) and picks a place of his own over Lomax: x + (random & 63) - 32, y - 100 +
 *   (random & 31) - 16. Without a bike he goes (0x32CCE).
 * - Flying, every frame he moves toward that place (Actor_MoveToward 0x1B008: per axis 0x800 a frame,
 *   twice that while moving away, up to 2.0; no turning) and touching him hurts (0x1A462). He can't
 *   be stomped (no Actor_CheckStompedByPlayer: Lomax's stomp hurts him there). His HANG frame's hooks
 *   (table 0x3071C): hook 1 (0x30692) puts the bike at its point and draws it; hook 2 (0x306AC) drops
 *   a Little Dude from its point every 240 frames, the first at once (sound 0x1A on screen; here
 *   Lomax's throw sound): thrown 2.0 up and (random & 15 + 4) / 8 px across, left and right in turn,
 *   the first left, in the air (+0x2C = 0).
 * - Hit by a thrown object (+3 bit 6, the thrower at +0x34): a small one (+6 bit 1: a Little Dude or
 *   a small stone) knocks him off the bike (0x30508): the bike is let go, 1.5 down; he is thrown 3.0
 *   up with his flying speed across, DEAD while in the air, and walks once he lands. A big one (a
 *   dummy, a big stone; 0x30540): the bike dies thrown 2.5 up and 1.0 the way he faces, and he dies
 *   (Enemy_Die 0x1B12E). Here Lomax's attacks (spin, tumble, helmet; Flink's spells) knock him off,
 *   as a small object (hit, he falls off and the bike can be taken).
 * - Knocked off (think 0x305A4): for 30 frames hits don't count (+0x50); then a hit or a thrown
 *   object kills him, and a stomp does at any time. He falls (0.25 a frame, friction 0x2AA), lands
 *   and walks as the Forest Dummy (Actor_Walk flags 0x1000: enemy_walk) at 1.0 a frame, and with
 *   Lomax behind him, 30 frames later (+0x52) he turns to him if Lomax is still behind (flags bit 10:
 *   enemy_walk's turning).
 *
 * The Chopper Bike:
 * - Let go, it falls at 1.5 slowing by 0x800 a frame, and then rises at 0.25 (0x30754); Lomax picks it
 *   up as any light object (Actor_CheckPickedUp 0x1ABB4: the grab button held and armed, his grab
 *   box on its hitbox, in the air too) and rides it. Here Lomax takes hold of it by touching it, as of
 *   his ropes (grab_touch, as for the lianas), not for REGRAB ticks after jumping off.
 * - Ridden (0x307B2), Flink hangs from it (player flags 4: his own speeds aren't stored, 5: HANG) and
 *   it is drawn at his hand, HANG's carry hook (0, -45) from his feet, mirrored with him (0x1B5D6).
 *   Every frame: his vx is braked by 0x800, then Left / Right add 0x1000 up to 1.0 (0 within 20 px of
 *   the map's sides); his vy (up) goes toward the bike's lift by 0x1000, Up adds 0x2000 up to the lift
 *   + 1.0, Down takes 0x2000 down to the lift - 1.0; the lift starts at 0.25 (+0x46) and drops by
 *   0x100 a frame of riding, so the bike gets heavier and heavier. Flink's movement then moves him by
 *   vx through his air control (0.125 a frame toward 2.0 with a direction held, twice against the way
 *   he faces, which doesn't change while he hangs; else braked by 0x800), and by vy, as in the air.
 *   He stays below y 30. Jump and Start are taken out of the buttons; Ability does nothing.
 * - Jump, pressed after it was released (g_JumpBoost < 0), jumps off (Player_StartJumpWithSfx, then
 *   VelY halved): the bike flies on as let go. Being hurt or landing ends the ride and the bike dies
 *   (Enemy_Die: 4.0 up, falling through everything).
 * Here Lomax hangs in his rope pose (player state 0x1010, as title.c's logo), held around
 * Player_Update (ride_before / ride_after, from player_tick), his position moved here with Flink's
 * numbers; walls (x +- 8), ceilings and the ground are tested here, as Flink's player code tests them.
 *
 * The Little Dude:
 * - In the air (+0x2C = 0) it falls at 0.125 a frame (friction 0x2AA) as DEAD and, landing, walks
 *   (WALK) facing Lomax (0x30A5C). Stomped in the air or walking it is stunned for 300 frames as the
 *   Forest Dummy (dummy_stomped: 3.0 up, STUN; stun sound 0x48), can then be picked up (light, small,
 *   held at +4, +4; its stun is the fuse) and hops back up (3.0) into the air when it ends; stomped
 *   again, or hit, it dies. Touching it hurts.
 * - Walking (Actor_Walk flags 0xD000, speed 1.0): it turns at a wall at once, turns on a 45 degree
 *   slope either way (bits 15 and 12), and walks off ledges into the air (bit 14: no look ahead).
 */
#include "enemy.h"

#define HOOK_DY            45            /* the bike from Flink's feet in HANG (the light carry hook) */
#define TARGET_DY          100           /* the Cyclin' Dude's place over Lomax */
#define FLY_MAX            0x20000
#define FLY_ACCEL          0x800
#define DROP_EVERY         240
#define DROP_VY            0x20000
#define KNOCK_VY           0x30000
#define KNOCK_GUARD        30
#define CHASE_TURN         30
#define BIKE_LET_GO_VY     0x18000       /* down */
#define BIKE_RISE          0x4000        /* at most this up, 0x800 a frame */
#define BIKE_LIFT          0x4000
#define LIFT_DROP          0x100
#define RIDE_BRAKE         0x800
#define RIDE_ACCEL_X       0x1000
#define RIDE_MAX_X         0x10000
#define RIDE_TO_LIFT       0x1000
#define RIDE_ACCEL_Y       0x2000
#define RIDE_SIDE          20
#define RIDE_TOP           30
#define AIR_ACCEL          0x2000        /* Flink's air row (0x1CFD4): 0.125 toward 2.0 */
#define AIR_MAX            0x20000
#define AIR_BRAKE          0x800
#define HANG_TOP           41            /* Flink's HANG hitbox top */
#define LITT_SPEED         0x10000
#define LITT_GRAVITY       0x2000
#define DEAD_TICKS         150
#define REGRAB             30            /* ticks after a jump off before touching it takes it again */

#define IN_LEFT            0x8000
#define IN_RIGHT           0x2000
#define IN_UP              0x1000
#define IN_DOWN            0x4000
#define IN_LOOK            0x0005        /* LookDown, LookUp */

enum { B_NONE, B_HELD, B_FREE, B_RIDDEN, B_DEAD };

/* per actor slot: the riders, the bikes, the Little Dudes in the air */
static struct {
    BYTE *bike;
    int ox, oy, h1x, h1y, h2x, h2y, drop, side, off, guard, chase, air;
} g_cyc[MAX_ACTORS];
static struct { BYTE *type, *rider; int state, vy, lift, t; } g_bike[MAX_ACTORS];
static int g_lair[MAX_ACTORS];

/* the ride: the bike's slot, Lomax's speeds (vy up, Flink's), Jump released since, the hurt timer,
   the buttons taken out for Player_Update */
static struct { int slot, vx, vy, jumpok, hurt, masked; WORD held, pressed; } R = { -1, 0, 0, 0, 0, 0, 0, 0 };

static int cyc_rand(void) { static unsigned r = 0x2545F491u; r = r * 1103515245u + 12345u; return (int)(r >> 16) & 0x7FFF; }

void cyclindudes_reset(void)
{
    memset(g_cyc, 0, sizeof(g_cyc));
    memset(g_bike, 0, sizeof(g_bike));
    memset(g_lair, 0, sizeof(g_lair));
    R.slot = -1;
    R.masked = 0;
}

/* (a Rope slide's ride too, ropeslide.c: no grab, throw, rope or other ride meanwhile) */
int ride_active(void) { return R.slot >= 0 || ropeslide_active(); }

/* a level type whose name starts with prefix (Flink's 0x32BD4 finds it by its 4-letter code) */
static BYTE *type_named(const char *prefix)
{
    int n = *G_NUMOBJTYPES, k;
    for (k = 0; k < n && k < 0x100; k++) {
        BYTE *t = G_OBJTYPES[k];
        if (t && !strncmp(T_NAME(t), prefix, strlen(prefix))) return t;
    }
    return NULL;
}

static int brake(int v, int d) { return v > 0 ? (v > d ? v - d : 0) : (v < -d ? v + d : 0); }

/* ---- The bike --------------------------------------------------------------------------------- */

static void __cdecl bike_update(BYTE *b);

static int bike_ok(const BYTE *b, int k)
{
    return b && k >= 0 && g_bike[k].state != B_NONE && g_bike[k].type == A_TYPE(b) && A_UPDATE(b) == (void *)bike_update;
}

/* Enemy_Die (0x1B12E) on the bike: DEAD, thrown, falling through everything; not a defeated enemy */
static void bike_die(BYTE *b, int k, int vx, int vy)
{
    g_bike[k].state = B_DEAD;
    g_bike[k].t = 0;
    A_FLAGS(b) &= ~AF_CONTACT;
    A_CULL(b) = CULL_NEVER;
    A_VX(b) = vx;
    A_VY(b) = vy;
    set_anim(b, "DEAD");
}

static int rider_holds(BYTE *r, const BYTE *b)
{
    int i = r ? enemy_slot(r) : -1;
    return i >= 0 && actor_active(i) && g_enemy[i].cycl && !g_enemy[i].dead && !g_cyc[i].off && g_cyc[i].bike == b;
}

static void ride_start(BYTE *b, int k);

static void __cdecl bike_update(BYTE *b)
{
    int k = slot_of(b);
    if (!bike_ok(b, k)) return;
    switch (g_bike[k].state) {
    case B_HELD:                                 /* (the Cyclin' Dude puts it at his hook) */
        if (!rider_holds(g_bike[k].rider, b)) { g_bike[k].state = B_NONE; Actor_Kill(b); return; }
        break;
    case B_FREE:                                 /* 0x30754 */
        g_bike[k].vy += 0x800;
        if (g_bike[k].vy > BIKE_RISE) g_bike[k].vy = BIKE_RISE;
        A_FY(b) -= g_bike[k].vy;
        set_pos(b);
        if (g_bike[k].t > 0) g_bike[k].t--;     /* (just jumped off: REGRAB) */
        else if (grab_touch(b)) { ride_start(b, k); break; }
        if (A_Y(b) < -64 || below_map(b) || abs(A_X(b) - *G_PLAYERX) > 1000) { g_bike[k].state = B_NONE; Actor_Kill(b); return; }
        break;
    case B_RIDDEN:                               /* (ride_after puts it at Lomax's hand) */
        break;
    case B_DEAD:
        fall_and_land(b, 0x2AA, 0);
        if (++g_bike[k].t > DEAD_TICKS || below_map(b)) { g_bike[k].state = B_NONE; Actor_Kill(b); return; }
        break;
    }
    Actor_AdvanceAnim(b);
}

static void bike_init(BYTE *b, BYTE *rider, int state)
{
    int k = slot_of(b);
    if (k < 0) return;
    g_enemy[k].type = NULL;                      /* (not an enemy) */
    memset(&g_bike[k], 0, sizeof(g_bike[k]));
    g_bike[k].type = A_TYPE(b);
    g_bike[k].rider = rider;
    g_bike[k].state = state;
    g_bike[k].lift = BIKE_LIFT;
    A_UPDATE(b) = (void *)bike_update;
    A_FLAGS(b) &= ~AF_CONTACT;                   /* (+6 bit 3 clear: nothing stomps or strikes it) */
    A_CULL(b) = CULL_NEVER;
    A_VX(b) = A_VY(b) = 0;
    Actor_SetAnim(b, FOURCC("CYCL"));
}

/* a 'Muta Bike' placement (only ever placed never to spawn): a bike let go */
void bike_spawn(BYTE *a, int i)
{
    (void)i;
    bike_init(a, NULL, B_FREE);
}

/* ---- The ride ----------------------------------------------------------------------------------- */

static void hang(void)
{
    *G_PLAYERSTATE = (*G_PLAYERSTATE & ~(ROPE_CLEAR | ROPE_CLIMB)) | ROPE_STATE | PS_AIR;
    *G_BAIRSPIN = 0;
    *G_JUMPHOLDFORCE = -1;
    *G_PLAYERVX = *G_PLAYERVY = 0;
}

static void bike_at_hand(BYTE *b)
{
    A_X(b) = *G_PLAYERX;
    A_Y(b) = *G_PLAYERY - HOOK_DY;
    A_FX(b) = A_X(b) << 16;
    A_FY(b) = A_Y(b) << 16;
    A_PLANE(b) = *G_PLAYERPLANE;
    if (*G_PLAYERFLAGS & AF_LEFT) A_FLAGS(b) |= AF_LEFT;
    else A_FLAGS(b) &= ~AF_LEFT;
}

static void ride_start(BYTE *b, int k)
{
    g_bike[k].state = B_RIDDEN;
    R.slot = k;
    R.vx = *G_PLAYERVX;
    R.vy = -*G_PLAYERVY;
    R.jumpok = 0;
    R.hurt = *G_PLAYERHURTTIMER;
    A_LAYER(b) = 2;                              /* (as a carried object) */
    hang();
    bike_at_hand(b);
    flog("Lomax rides the Chopper Bike from (%d,%d), lift %d", *G_PLAYERX, *G_PLAYERY, g_bike[k].lift);
}

enum { END_JUMP, END_HURT, END_LAND };

static void ride_end(int how)
{
    BYTE *b = actor_at(R.slot);
    int k = R.slot;
    R.slot = -1;
    *G_PLAYERSTATE &= ~(ROPE_STATE | ROPE_CLIMB);
    if (how == END_LAND) *G_PLAYERSTATE &= ~PS_AIR;
    else *G_PLAYERSTATE |= PS_AIR;
    if (how != END_HURT) *G_PLAYERVX = R.vx;
    if (how == END_LAND) *G_PLAYERVY = 0;
    if (how == END_JUMP) move_jump_off();
    if (bike_ok(b, k)) {
        A_LAYER(b) = 1;
        if (how == END_JUMP) { g_bike[k].state = B_FREE; g_bike[k].t = REGRAB; }   /* flies on (think 0x30754 again) */
        else bike_die(b, k, 0, -0x40000);
    }
    flog("Lomax %s the Chopper Bike at (%d,%d) (hurt %d, dying %d)", how == END_JUMP ? "jumps off" : how == END_HURT ? "is hurt and loses" : "lands with",
         *G_PLAYERX, *G_PLAYERY, *G_PLAYERHURTTIMER, *G_PLAYERDEATHTIMER);
}

/* a solid cell that blocks from below (0x80 | 0x08) at x, y on Lomax's plane */
static int ceiling_at(int x, int y)
{
    const BYTE *pl = PLANE(*G_PLAYERPLANE), *coll = P_COLL(pl);
    int w = P_W(pl), h = P_H(pl);
    if (P_EMPTY(pl) || !coll || x < 0 || y < 0 || x >= w * 16 || y >= h * 16) return 0;
    return (coll[(x >> 4) + (y >> 4) * w] & 0x88) == 0x88;
}

/* hurt since the ride began or last tick (Player_TakeHit sets the timer to 180 or 1000; it then counts
   down, and after the landing from the knock-back up from below 0, which is no new hit), or dying */
static int new_hit(void)
{
    int h = *G_PLAYERHURTTIMER;
    return (h > 0 && h > R.hurt) || *G_PLAYERDEATHTIMER;
}

/* before Player_Update: the bike's think (0x307B2) and Flink's movement with it */
void ride_before(void)
{
    BYTE *b = actor_at(R.slot), *pl;
    int k = R.slot, held, left, right, x, fx, fy, oy, step, vy, lift, acc, w, g;
    if (R.slot < 0) return;
    if (!g_active || !bike_ok(b, k) || g_bike[k].state != B_RIDDEN) { ride_end(END_HURT); return; }
    if (*G_BPAUSED || *G_PLAYERSTATE & PS_FROZEN) return;
    if (new_hit()) { ride_end(END_HURT); return; }   /* (0x307B2: 0x82E0 > 0) */
    R.hurt = *G_PLAYERHURTTIMER;
    held = *G_INPUTHELD;
    if (!(held & *G_JUMPMASK)) R.jumpok = 1;     /* (0x1C1A4: g_JumpBoost = -1) */
    else if (R.jumpok) { ride_end(END_JUMP); return; }
    pl = PLANE(*G_PLAYERPLANE);
    w = P_W(pl) * 16;
    x = *G_PLAYERX;
    left = (held & IN_LEFT) != 0;
    right = (held & IN_RIGHT) != 0;
    /* the bike: vx */
    R.vx = brake(R.vx, RIDE_BRAKE);
    if (left) {
        if (x <= RIDE_SIDE) R.vx = 0;
        else if (R.vx > -RIDE_MAX_X && (R.vx -= RIDE_ACCEL_X) < -RIDE_MAX_X) R.vx = -RIDE_MAX_X;
    }
    if (right) {
        if (x >= w - RIDE_SIDE) R.vx = 0;
        else if (R.vx < RIDE_MAX_X && (R.vx += RIDE_ACCEL_X) > RIDE_MAX_X) R.vx = RIDE_MAX_X;
    }
    /* vy, toward the lift */
    vy = R.vy;
    lift = g_bike[k].lift;
    if (vy > lift) { vy -= RIDE_TO_LIFT; if (vy < lift) vy = lift; }
    else if (vy < lift) { vy += RIDE_TO_LIFT; if (vy > lift) vy = lift; }
    if (held & IN_UP && vy < lift + 0x10000 && (vy += RIDE_ACCEL_Y) > lift + 0x10000) vy = lift + 0x10000;
    if (held & IN_DOWN && vy > lift - 0x10000 && (vy -= RIDE_ACCEL_Y) < lift - 0x10000) vy = lift - 0x10000;
    R.vy = vy;
    g_bike[k].lift -= LIFT_DROP;
    /* Flink's movement: his air control on top of vx (not stored), then vx, vy */
    acc = AIR_ACCEL;
    step = R.vx;
    if (left) {
        if (!(*G_PLAYERFLAGS & AF_LEFT)) acc *= 2;
        if (step > -AIR_MAX && (step -= acc) < -AIR_MAX) step = -AIR_MAX;
    } else if (right) {
        if (*G_PLAYERFLAGS & AF_LEFT) acc *= 2;
        if (step < AIR_MAX && (step += acc) > AIR_MAX) step = AIR_MAX;
    } else step = brake(step, AIR_BRAKE);
    fx = *G_PLAYERXFIXED + step;
    if (step && wall_at(G_PLAYERACTOR, (fx >> 16) + (step > 0 ? 8 : -8), step > 0)) { fx = *G_PLAYERXFIXED; R.vx = 0; }
    oy = *G_PLAYERY;
    fy = *G_PLAYERYFIXED - R.vy;
    if (fy >> 16 < RIDE_TOP) { fy = RIDE_TOP << 16; if (R.vy >= 0) R.vy = 0; }
    if (R.vy > 0 && ceiling_at(fx >> 16, (fy >> 16) - HANG_TOP)) { fy = *G_PLAYERYFIXED; R.vy = -0x20000; }
    *G_PLAYERXFIXED = fx; *G_PLAYERX = fx >> 16;
    *G_PLAYERYFIXED = fy; *G_PLAYERY = fy >> 16;
    if (R.vy < 0 && (g = ground_y(G_PLAYERACTOR, *G_PLAYERX, oy)) != NO_GROUND && *G_PLAYERY >= g) {
        *G_PLAYERY = g; *G_PLAYERYFIXED = g << 16;
        ride_end(END_LAND);
        return;
    }
    if (below_map(G_PLAYERACTOR)) { ride_end(END_HURT); return; }
    /* hanging, the buttons out of Player_Update's reach */
    hang();
    {
        WORD mask = (WORD)(IN_LEFT | IN_RIGHT | IN_UP | IN_DOWN | IN_LOOK | *G_JUMPMASK | *G_HELMMASK);
        R.held = *G_INPUTHELD & mask; R.pressed = *G_INPUTPRESSED & mask;
        *G_INPUTHELD &= (WORD)~mask; *G_INPUTPRESSED &= (WORD)~mask;
        R.masked = 1;
    }
    bike_at_hand(b);
}

void ride_after(void)
{
    BYTE *b = actor_at(R.slot);
    if (R.masked) { *G_INPUTHELD |= R.held; *G_INPUTPRESSED |= R.pressed; R.masked = 0; }
    if (R.slot < 0) return;
    if (!bike_ok(b, R.slot)) { ride_end(END_HURT); return; }
    if (new_hit()) { ride_end(END_HURT); return; }   /* (his knock-back stays) */
    *G_PLAYERSTATE &= ~ROPE_CLIMB;
    *G_PLAYERVX = *G_PLAYERVY = 0;
    bike_at_hand(b);
}

/* ---- The Little Dude ------------------------------------------------------------------------------ */

/* Actor_Walk (0x1D0C4) with flags 0xD000 at 1.0: a wall turns it at once, no look ahead (it walks
   off ledges: into the air), a 45 degree slope turns it either way */
static void litt_walk(BYTE *a, int i)
{
    static const int SNAP[8] = { 3, 4, 6, 7, 10, 10, 10, 10 };   /* 0x1AEDA */
    int left = (A_FLAGS(a) & AF_LEFT) != 0, x = A_X(a), y = A_Y(a), speed, g;
    if (wall_at(a, left ? x - 8 : x + 8, !left)) { A_FLAGS(a) ^= AF_LEFT; left = !left; }   /* (0x1D196) */
    speed = abs(A_VX(a));
    if (speed < LITT_SPEED) speed = speed + 0x1000 > LITT_SPEED ? LITT_SPEED : speed + 0x1000;
    else if (speed > LITT_SPEED) speed = speed - 0x1000 < LITT_SPEED ? LITT_SPEED : speed - 0x1000;
    A_VX(a) = left ? -speed : speed;
    A_FX(a) += A_VX(a);
    set_pos(a);
    g = ground_y(a, A_X(a), y);
    if (g == NO_GROUND || abs(g - y) > SNAP[speed >> 16 < 7 ? speed >> 16 : 7]) { g_lair[i] = 1; return; }   /* (bit 8) */
    place_y(a, g);
    A_VY(a) = 0;
    if (*G_GROUNDSLOPECLASS == 2) A_FLAGS(a) ^= AF_LEFT;   /* (bits 15, 12) */
    set_anim(a, "WALK");
}

/* in the air (0x30A04): 0.125 a frame, DEAD; landed, it walks facing Lomax */
static void litt_fall(BYTE *a, int i)
{
    if (fall_and_land_g(a, LITT_GRAVITY, 0x2AA, 1)) {
        g_lair[i] = 0;
        set_anim(a, "WALK");
        if (A_X(a) < *G_PLAYERX) A_FLAGS(a) &= ~AF_LEFT;
        else A_FLAGS(a) |= AF_LEFT;
    } else set_anim(a, "DEAD");
}

static void __cdecl litt_update(BYTE *a)
{
    int i = enemy_slot(a);
    Enemy *e;
    if (i < 0) return;
    e = &g_enemy[i];
    if (e->dead) { enemy_dead_update(a, i); return; }
    switch (enemy_attacked(a, i)) {
    case 2: enemy_die(a, i, 1); return;
    case 1: dummy_stomped(a, i, 1); if (e->dead) return;
    }
    if (e->timer > 0) {                          /* stunned (0x1D074) */
        fall_and_land(a, 0x1800, 1);
        if (--e->timer > 0) stun_frame(a, i);
        else { A_VY(a) = -0x30000; g_lair[i] = 1; e->hop = 0; }   /* hops up */
        Actor_AdvanceAnim(a);
        return;
    }
    if (e->hop) { e->hop = 0; g_lair[i] = 1; }   /* (dummy_stomped, or landed from a throw) */
    if (g_lair[i]) litt_fall(a, i);
    else litt_walk(a, i);
    Actor_AdvanceAnim(a);
}

static void litt_init(BYTE *a, int i, int air)
{
    g_enemy[i].litt = 1;
    g_lair[i] = air;
    A_UPDATE(a) = (void *)litt_update;
    Actor_SetAnim(a, FOURCC(air ? "DEAD" : "WALK"));
}

/* a placed one walks (0x3099C: +0x2C = WALK) */
void litt_spawn(BYTE *a, int i) { litt_init(a, i, 0); }

/* hook 2 (0x306AC): one dropped from the Cyclin' Dude */
static void litt_drop(BYTE *r, int i)
{
    BYTE *t = type_named("Muta Litt"), *d = t ? Actor_Alloc(r, T_NAME(t)) : NULL;
    int k = d ? slot_of(d) : -1, vx;
    if (k < 0) return;
    memset(&g_enemy[k], 0, sizeof(g_enemy[k]));
    g_enemy[k].type = A_TYPE(d);
    A_DRAW(d) = A_DRAW(r);
    A_FLAGS(d) = AF_HURTS | AF_HITTABLE;
    A_LAYER(d) = 1;
    A_CULL(d) = 1;                               /* (gone off the screen) */
    A_X(d) = A_X(r) + (A_FLAGS(r) & AF_LEFT ? -g_cyc[i].h2x : g_cyc[i].h2x);
    A_Y(d) = A_Y(r) + g_cyc[i].h2y;
    A_FX(d) = A_X(d) << 16;
    A_FY(d) = A_Y(d) << 16;
    vx = ((cyc_rand() & 15) + 4) << 13;
    g_cyc[i].side = !g_cyc[i].side;
    A_VX(d) = g_cyc[i].side ? -vx : vx;
    A_VY(d) = -DROP_VY;
    if (A_VX(d) < 0) A_FLAGS(d) |= AF_LEFT;
    litt_init(d, k, 1);
    flink_sound(r, 0x1A);
    flog("cyclin' dude at (%d,%d) drops a little dude", A_X(r), A_Y(r));
}

/* ---- The Cyclin' Dude --------------------------------------------------------------------------- */

/* Actor_MoveToward (0x1B008), one axis */
static void move_axis(int *v, int pos, int target)
{
    if (target == pos && abs(*v) <= FLY_ACCEL) { *v = 0; return; }
    if (target >= pos) { if (FLY_MAX > *v) *v += *v < 0 ? 2 * FLY_ACCEL : FLY_ACCEL; }
    else if (FLY_MAX > -*v) *v -= *v > 0 ? 2 * FLY_ACCEL : FLY_ACCEL;
}

/* knocked off his bike (0x30508) */
static void knock_off(BYTE *a, int i)
{
    BYTE *b = g_cyc[i].bike;
    int k = b ? slot_of(b) : -1;
    if (bike_ok(b, k) && g_bike[k].state == B_HELD) {
        g_bike[k].state = B_FREE;
        g_bike[k].vy = -BIKE_LET_GO_VY;
        g_bike[k].rider = NULL;
    }
    g_cyc[i].bike = NULL;
    g_cyc[i].off = 1;
    g_cyc[i].air = 1;
    g_cyc[i].guard = KNOCK_GUARD;
    A_VY(a) = -KNOCK_VY;
    A_FLAGS(a) |= AF_HURTS | AF_HITTABLE;        /* (now stompable) */
    set_anim(a, "DEAD");
    flog("cyclin' dude at (%d,%d) knocked off his bike", A_X(a), A_Y(a));
}

/* hit by a thrown object (small: +6 bit 1) */
void cycl_struck(BYTE *a, int i, int small)
{
    BYTE *b = g_cyc[i].bike;
    int k = b ? slot_of(b) : -1;
    if (g_cyc[i].off) { if (!g_cyc[i].guard) enemy_die(a, i, 1); return; }
    if (small) { knock_off(a, i); return; }
    if (bike_ok(b, k)) bike_die(b, k, A_FLAGS(a) & AF_LEFT ? -0x10000 : 0x10000, -0x28000);   /* (0x30540) */
    g_cyc[i].bike = NULL;
    enemy_die(a, i, 1);
}

void cycl_stomped(BYTE *a, int i)
{
    if (g_cyc[i].off) enemy_die(a, i, 0);        /* (flying he can't be stomped: not hittable) */
}

static void __cdecl cycl_update(BYTE *a)
{
    int i = enemy_slot(a), left;
    Enemy *e;
    BYTE *b;
    if (i < 0) return;
    e = &g_enemy[i];
    if (e->dead) { enemy_dead_update(a, i); return; }
    left = (A_FLAGS(a) & AF_LEFT) != 0;
    if (!g_cyc[i].off) {                         /* flying (0x304F0) */
        b = g_cyc[i].bike;
        if (!bike_ok(b, slot_of(b))) { e->type = NULL; Actor_Despawn(a); return; }   /* (0x32CCE) */
        A_DRAW(b) = A_DRAW(a);                   /* (drawn as he is; set after his spawn) */
        if (enemy_attacked(a, i)) { knock_off(a, i); Actor_AdvanceAnim(a); return; }
        move_axis(&A_VX(a), A_X(a), *G_PLAYERX + g_cyc[i].ox);
        move_axis(&A_VY(a), A_Y(a), *G_PLAYERY - TARGET_DY + g_cyc[i].oy);
        A_FX(a) += A_VX(a);
        A_FY(a) += A_VY(a);
        set_pos(a);
        Actor_AdvanceAnim(a);
        A_X(b) = A_X(a) + (left ? -g_cyc[i].h1x : g_cyc[i].h1x);   /* hook 1 (0x30692) */
        A_Y(b) = A_Y(a) + g_cyc[i].h1y;
        A_FX(b) = A_X(b) << 16;
        A_FY(b) = A_Y(b) << 16;
        A_PLANE(b) = A_PLANE(a);
        if (--g_cyc[i].drop < 0) { litt_drop(a, i); g_cyc[i].drop = DROP_EVERY; }   /* hook 2 (0x306AC) */
        return;
    }
    /* knocked off (0x305A4) */
    if (g_cyc[i].guard > 0) g_cyc[i].guard--;
    if (enemy_attacked(a, i) && !g_cyc[i].guard) { enemy_die(a, i, 1); return; }
    if (g_cyc[i].air) {
        if (fall_and_land(a, 0x2AA, 1)) g_cyc[i].air = 0;
    } else {
        enemy_walk(a, i);
        if (!e->turning) {                       /* Lomax behind it: turns to him 30 frames later (0x30616) */
            int behind = (*G_PLAYERX < A_X(a)) != ((A_FLAGS(a) & AF_LEFT) != 0);
            if (g_cyc[i].chase) { if (!--g_cyc[i].chase && behind) e->turning = 1; }
            else if (behind) g_cyc[i].chase = CHASE_TURN;
        }
    }
    Actor_AdvanceAnim(a);
}

void cycl_spawn(BYTE *a, int i, const BYTE *def)
{
    BYTE *t = type_named("Muta Bike"), *b = t ? Actor_Alloc(a, T_NAME(t)) : NULL;
    memset(&g_cyc[i], 0, sizeof(g_cyc[i]));
    g_enemy[i].cycl = 1;
    g_cyc[i].h1x = (short)def_param(a, def, 1, 0);
    g_cyc[i].h1y = (short)def_param(a, def, 2, 0);
    g_cyc[i].h2x = (short)def_param(a, def, 3, 0);
    g_cyc[i].h2y = (short)def_param(a, def, 4, 0);
    g_cyc[i].ox = (cyc_rand() & 63) - 32;
    g_cyc[i].oy = (cyc_rand() & 31) - 16;
    A_UPDATE(a) = (void *)cycl_update;
    A_FLAGS(a) &= ~AF_HITTABLE;                  /* flying: touching him hurts, no stomp */
    Actor_SetAnim(a, FOURCC("HANG"));
    if (b && slot_of(b) >= 0) {
        A_DRAW(b) = A_DRAW(a);
        bike_init(b, a, B_HELD);
        A_X(b) = A_X(a) + g_cyc[i].h1x;
        A_Y(b) = A_Y(a) + g_cyc[i].h1y;
        A_FX(b) = A_X(b) << 16;
        A_FY(b) = A_Y(b) << 16;
        g_cyc[i].bike = b;
    }
    flog("cyclin' dude at (%d,%d)%s", A_X(a), A_Y(a), g_cyc[i].bike ? "" : ": no bike");
}
