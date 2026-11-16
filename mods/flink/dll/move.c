/*
 * move.c - Lomax's movement in Flink's maps (README "How FLINK.DLL works", Movement): his walking and
 * running speed, his jump height and, with Swim=2, walking under water. The values are the defines
 * below (in the code, not in a settings file):
 *
 *   MOVE_SPEED          walk and run speed, % of Lomax's (3.0 walking, 4.0 running, 3.0 in the air)
 *   JUMP_HEIGHT         a full jump's height in px; 0 = Lomax's own (81 px). Flink's is 68 (see below)
 *   HEAVY_JUMP_HEIGHT   the same carrying something heavy (Flink's 37)
 *   WATER_SPEED         Swim=2, under water: speed, % of the speed on land
 *   WATER_GRAVITY       Swim=2, under water: gravity and the fastest fall, % of Lomax's (0.375, 6.0)
 *   WATER_JUMP          Swim=2, under water: a jump's or stroke's speed and push, % of the jump on land
 *   SWIM_GAP            Swim=2: ticks since the last Jump press before a press is a stroke, not a spin
 *
 * Speed. Player_Update (0x40FB70) takes the walk / run speeds from g_PlayerMoveTable (0x464450): 10
 * rows of {left limit, rest speed, right limit, acceleration, brake over the limit, friction}, 16.16
 * px a tick, the row by g_PlayerGroundType + 1 (0 in the air: 1 flat, 2 gentle slope, 3 steep slope),
 * + 4 with Run held; row 8 pushing a block, 9 dying. Rows 3 and 7 are the forced slides down a steep
 * slope (4.0, 5.0 both ways), rows 8 and 9 are no walk: those keep Lomax's speeds on land. The table
 * is data; move_set (from world_load) writes MOVE_SPEED's rows in Flink's maps and the retail ones
 * back for Lomax's levels, and move_before switches between the land and the water rows (WATER_SPEED,
 * all of rows 0-7) as Lomax goes in and out. Other caps on Lomax's speed (carrying something heavy,
 * mud: grab.c, groundtype.c) scale with it through move_speed.
 *
 * Jump. Player_Update starts a jump with vy -4.0 and g_JumpHoldForce (0x59E94C) 0x9800, then holds
 * him on the ground for g_JumpTakeoffTimer (0x59E764: 10 ticks standing, 5 moving); after that, each
 * tick while Jump is held, the force drops by 0x800 and is taken off vy (up), then gravity adds 0x6000
 * (vy at most 6.0). Let go, the force is -1. A full jump is 81 px high, a tap 19 px. Flink's
 * (0x1CE2E, 0x1C160, 0x1C23E): vy 4.0 up and boost 0x9000, dropping by 0x800 and added from the next
 * frame on, gravity 0x6000: 68 px, a tap 19 px; carrying something heavy, boost 0x6000: 37 px.
 * Flink's boost starts one frame later than Lomax's force, so the same 0x9000 would give Lomax 73 px:
 * the start force for a given height comes from simulating Player_Update's jump (hold_for). It is set
 * on the first tick after the takeoff (move_before), while the force is still Lomax's 0x9800, so the
 * air spin (Jump pressed again with the force under 0x9000) can't start any earlier than in Lomax's
 * levels. A force that isn't a multiple of 0x800 is held at 0x800 for its last step, so that it ends
 * at 0 as Lomax's does (below 0 with Jump still held, landing would jump again). A tap stays 19 px
 * (Flink's too). Other jumps (bounce pad 0xA000, out of the water 0xB000) are left alone.
 *
 * Under water (Swim=2, water.c): Lomax doesn't swim. Water_Update's player part is skipped (water.c),
 * so he never floats, drowns or gets the input lock; he walks on the bottom as on land. Under water
 * (his feet WATER_DEPTH or more under the surface, g_WaterLevelY, tide included) he moves at
 * WATER_SPEED, falls with WATER_GRAVITY and jumps at WATER_JUMP. Gravity: before Player_Update adds
 * its 0x6000, vy is lowered so that the sum is WATER_GRAVITY's share, and kept so that it ends no
 * faster than the share of 6.0.
 *  - Strokes. Much of Flink's water has no bottom (2-1's runs out of the map, a pit), where Flink
 *    swims up with Jump (0x1C0B6: a boost of 0x6000 a press). Here a Jump press under water in the
 *    air, SWIM_GAP ticks or more after the last Jump press, is a stroke: the same jump as on the
 *    ground (vy -4.0 and the start force, both at WATER_JUMP; Lomax's jump sound 0x16), with no
 *    takeoff; the press is taken out of g_InputPressed for Player_Update, which would start an air
 *    spin with it. A quicker press is left to Player_Update: Lomax's air spin, which lifts him no
 *    higher, and the count starts again. So mashing Jump spins him and he sinks; to swim up the
 *    presses must be timed.
 *  - Out of the water. Rising past WATER_DEPTH (under water last tick, not now) in the air with Jump
 *    held, he leaves the water with a full land jump from there (vy -4.0 and the start force, sound
 *    0x16), as Flink jumps out near the surface (0x1C0F4) and Lomax's own water throws him out with
 *    vy -4.0 (Water_Update 0x431A20). Without it a stroke at a low WATER_JUMP took him only just over
 *    that depth, and he fell back in at the surface. Above that depth everything is as on land.
 */
#include "flink.h"

#define MOVE_SPEED         80
#define JUMP_HEIGHT        68
#define HEAVY_JUMP_HEIGHT  37
#define WATER_SPEED        50
#define WATER_GRAVITY      30
#define WATER_JUMP         50
#define SWIM_GAP           20

#define MOVE_TABLE         0x00464450u   /* g_PlayerMoveTable: int [10][6] */
#define MOVE_ROWS          10
#define WATER_ROWS         8             /* rows 0-7 change under water */
#define JUMP_HOLD_LOMAX    0x9800        /* Player_Update's start force (0x40FF87) */
#define JUMP_VY            0x40000       /* and speed up */
#define GRAVITY            0x6000
#define FALL_MAX           0x60000
#define SND_JUMP           0x16          /* Player_Update's jump sound */
#define WATER_DEPTH        32            /* feet this far under the surface: under water (Water_Update's
                                            own catch depth) */

static const int moveOrig[MOVE_ROWS][6] = {
    { -0x30000, 0,       0x30000, 0x2000, 0x0800, 0x0800 },   /* 0 air */
    { -0x30000, 0,       0x30000, 0x2000, 0x0200, 0x1800 },   /* 1 flat */
    { -0x20000, 0,       0x38000, 0x2000, 0x0200, 0x1800 },   /* 2 gentle slope */
    {  0x40000, 0x40000, 0x40000, 0x2000, 0x1000, 0x1800 },   /* 3 steep slope: slide */
    { -0x30000, 0,       0x30000, 0x2000, 0x1000, 0x1000 },   /* 4 air, Run */
    { -0x40000, 0,       0x40000, 0x2000, 0x0200, 0x2000 },   /* 5 flat, Run */
    { -0x40000, 0,       0x40000, 0x2000, 0x0200, 0x2000 },   /* 6 gentle slope, Run */
    {  0x50000, 0x50000, 0x50000, 0x2000, 0x1000, 0x2000 },   /* 7 steep slope, Run: slide */
    { -0x10000, 0,       0x10000, 0x2000, 0x1000, 0x1800 },   /* 8 pushing a block */
    { -0x10000, 0,       0x10000, 0x2000, 0x0200, 0x1800 },   /* 9 dying */
};

static int g_hold = JUMP_HOLD_LOMAX, g_holdHeavy = JUMP_HOLD_LOMAX;   /* start forces from the heights */

static int g_table = -1;                 /* what MOVE_TABLE holds: -1 retail, 0 land, 1 water */
static int g_under;                      /* Lomax is under water this tick */
static int g_hold0;                      /* the force before Player_Update */
static int g_pending;                    /* a jump began: its force is set after the takeoff */
static int g_pendingWater;               /* it began under water */
static int g_sincePress;                 /* ticks since the last Jump press (strokes) */

static int pct(int v, int p) { return (int)((long long)v * p / 100); }

/* Player_Update's jump with start force hold: its height in 16.16 px */
static int jump_apex(int hold)
{
    int vy = -JUMP_VY, y = 0, top = 0;
    do {
        if (hold > 0) { if (hold < 0x800) hold = 0x800; hold -= 0x800; vy -= hold; }   /* (move_before) */
        vy += GRAVITY;
        if (vy > FALL_MAX) vy = FALL_MAX;
        y += vy;
        if (y < top) top = y;
    } while (vy < 0);
    return -top;
}

/* the start force whose full jump is px high (px 0: Lomax's own) */
static int hold_for(int px)
{
    int h, best = 0, err = 0x7FFFFFFF;
    if (px <= 0) return JUMP_HOLD_LOMAX;
    for (h = 0; h <= 0x10000; h += 0x40) {
        int e = abs(jump_apex(h) - (px << 16));
        if (e < err) { err = e; best = h; }
    }
    return best;
}

void move_init(void)
{
    g_hold = hold_for(JUMP_HEIGHT);
    g_holdHeavy = hold_for(HEAVY_JUMP_HEIGHT);
    flog("movement: speed %d%%, jump %d px (force 0x%X), heavy %d px (0x%X), under water speed %d%%, gravity %d%%, jump %d%%",
         MOVE_SPEED, JUMP_HEIGHT, g_hold, HEAVY_JUMP_HEIGHT, g_holdHeavy, WATER_SPEED, WATER_GRAVITY, WATER_JUMP);
}

int move_retail(void)
{
    return is_bytes(MOVE_TABLE, moveOrig, sizeof(moveOrig));
}

/* t: -1 Lomax's table, 0 Flink's maps on land, 1 under water */
static void table_set(int t)
{
    int rows[MOVE_ROWS][6], r, c;
    if (t == g_table) return;
    memcpy(rows, moveOrig, sizeof(rows));
    for (r = 0; t >= 0 && r < WATER_ROWS; r++) {
        int p = r == 3 || r == 7 ? 100 : MOVE_SPEED;        /* the slides keep their speed on land */
        if (t == 1) p = p * WATER_SPEED / 100;
        for (c = 0; c < 3; c++) rows[r][c] = pct(moveOrig[r][c], p);
    }
    patch(MOVE_TABLE, rows, sizeof(rows));
    g_table = t;
}

/* from world_load: Flink's speeds in Flink's maps, Lomax's in his */
void move_set(int flink)
{
    g_under = g_pending = 0;
    g_sincePress = SWIM_GAP;
    table_set(flink ? 0 : -1);
}

/* a speed limit (16.16) scaled as Lomax's speed is now */
int move_speed(int v)
{
    if (!g_active) return v;
    v = pct(v, MOVE_SPEED);
    return g_under ? pct(v, WATER_SPEED) : v;
}

static int under_water(void)
{
    DWORD s = *G_PLAYERSTATE;
    return SWIM_MODE == 2 && water_present() && !(s & (PS_ROPE | PS_FLY)) &&
           *G_PLAYERY >= *G_WATERLEVELY + WATER_DEPTH;
}

/* a jump in the air with no takeoff: vy up at pct of JUMP_VY, the start force at pct (strokes, out of
   the water) */
static void air_jump(int p)
{
    int h = grab_carrying_heavy() ? g_holdHeavy : g_hold;
    *G_PLAYERVY = -pct(JUMP_VY, p);
    *G_JUMPHOLDFORCE = g_hold0 = pct(h, p);
    *G_BAIRSPIN = 0;
    Sound_PlayEffect(NULL, SND_JUMP);
}

/* Flink's jump off the Chopper Bike (0x30810): Player_StartJumpWithSfx (4.0 up and the boost), then
   VelY halved (0x30816): half the start speed, the full hold force */
void move_jump_off(void)
{
    *G_PLAYERVY = -JUMP_VY / 2;
    *G_JUMPHOLDFORCE = g_hold0 = move_jump_force();
    *G_BAIRSPIN = 0;
    Sound_PlayEffect(NULL, SND_JUMP);
}

/* the start force of a full jump as Lomax is now (stomp.c's high bounce) */
int move_jump_force(void)
{
    int h = grab_carrying_heavy() ? g_holdHeavy : g_hold;
    return g_under ? pct(h, WATER_JUMP) : h;
}

int move_under_water(void) { return g_under; }

/* before Player_Update (player_tick, grab.c) */
void move_before(void)
{
    int was, pressed, free;
    if (!g_active || *G_BPAUSED || *G_PLAYERSTATE & PS_FROZEN) return;
    was = g_under;
    g_under = under_water();
    table_set(g_under);
    /* a force that isn't a multiple of 0x800 (ours) would end below 0, and Player_Update takes a force
       below 0 with Jump held on the ground as a new jump: holding Jump through a landing would jump
       again. Its last step ends at 0, as Lomax's own. */
    if (*G_JUMPHOLDFORCE > 0 && *G_JUMPHOLDFORCE < 0x800) *G_JUMPHOLDFORCE = 0x800;
    g_hold0 = *G_JUMPHOLDFORCE;
    if (g_pending) {                         /* the takeoff is over: the jump's own force */
        if (*G_JUMPHOLDFORCE != JUMP_HOLD_LOMAX) g_pending = 0;     /* let go meanwhile */
        else if (!*G_JUMPTAKEOFFTIMER) {
            int h = grab_carrying_heavy() ? g_holdHeavy : g_hold;
            *G_JUMPHOLDFORCE = g_pendingWater ? pct(h, WATER_JUMP) : h;
            g_hold0 = *G_JUMPHOLDFORCE;
            g_pending = 0;
        }
    }
    pressed = (*G_INPUTPRESSED & *G_JUMPMASK) != 0;
    free = *G_PLAYERSTATE & PS_AIR && !*G_JUMPTAKEOFFTIMER && !*G_PLAYERDEATHTIMER &&
           *G_PLAYERHURTTIMER <= 0 && !*G_BINPUTLOCKED;
    if (g_sincePress < SWIM_GAP) g_sincePress++;
    /* under water, a timed Jump press is a stroke; a quicker one Lomax's spin (see the top of the file) */
    if (g_under && free && pressed && *G_JUMPHOLDFORCE < 0 && g_sincePress >= SWIM_GAP) {
        air_jump(WATER_JUMP);
        *G_INPUTPRESSED &= (WORD)~*G_JUMPMASK;
    }
    if (pressed) g_sincePress = 0;
    /* rising out of the water with Jump held: a full land jump out of it */
    if (was && !g_under && free && *G_PLAYERVY < 0 && *G_INPUTHELD & *G_JUMPMASK && water_present()) {
        air_jump(100);
        flog("Lomax jumps out of the water at (%d,%d)", *G_PLAYERX, *G_PLAYERY);
    }
    /* under water: gravity's share, and no faster than its share of 6.0 (Player_Update adds 0x6000
       and caps at 6.0 only in the air, after the takeoff, and with no death timer) */
    if (g_under && *G_PLAYERSTATE & PS_AIR && !*G_JUMPTAKEOFFTIMER && !*G_PLAYERDEATHTIMER) {
        int vy = *G_PLAYERVY - (GRAVITY - pct(GRAVITY, WATER_GRAVITY)), cap = pct(FALL_MAX, WATER_GRAVITY);
        if (vy + GRAVITY > cap) vy = cap - GRAVITY;
        *G_PLAYERVY = vy;
    }
}

/* after Player_Update: a jump that began this tick */
void move_after(void)
{
    if (!g_active || *G_BPAUSED || *G_PLAYERSTATE & PS_FROZEN) return;
    water_tick();
    if (g_hold0 <= 0 && *G_JUMPHOLDFORCE == JUMP_HOLD_LOMAX && *G_PLAYERVY < 0) {
        g_pending = 1;
        g_pendingWater = g_under;
        if (g_under) *G_PLAYERVY = pct(*G_PLAYERVY, WATER_JUMP);
    }
}
