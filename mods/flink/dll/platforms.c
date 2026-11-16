/*
 * platforms.c - Flink's platforms on Lomax's Mega (object 64, MovingPlatform_Spawn 0x429A90) and
 * boat (object 65, DriftingBoat_Init 0x432450), whose spawns are wrapped (README "Platforms"):
 *  - 'Mega Float' (platforms on the water) and boats ride the tide (water.c: float_track);
 *  - 'Mega Dip' is Flink's Normalplatform: it dips when landed on (dip_update);
 *  - 'Mega Fall' is Flink's Tra2 / Trap platform: once landed on it falls for good (fall_update);
 *  - 'Mega Jumppad' is Flink's Jumppad (jumppad.c);
 *  - 'Mega Bouncy' is Flink's Bouncy platform (jumppad.c);
 *  - 'Mega Throw' is Flink's Throw platform (throwplat.c);
 *  - 'Mega Seesaw' is Flink's Seesaw, the seesaw (seesaw.c);
 *  - 'Mega Floating' is Flink's Floating platform, the spawner of the leaves blowing across
 *    (floating.c);
 *  - 'Mega Leaf' and 'Mega Leaves' are Flink's Leaveplatform, the falling leaves and their spawner
 *    (leaves.c);
 *  - 'Mega Steer' is Flink's Steer platform, ridden and steered down (steer.c);
 *  - 'Mega Hand' is Flink's Hand, a platform closing into a fist on a timer (hand.c);
 *  - 'Mega Wall' is Flink's Wall platform, a brick sliding in and out of the wall on timers (wall.c);
 *  - 'Mega Plant' is Flink's Plant that grows (plant.c);
 *  - 'Mega Bridge' is Flink's Bridge, the rope bridge whose planks fall (bridge.c);
 *  - 'Mega Rock' is Flink's Rock, the block Lomax pushes (rock.c);
 *  - 'Mega Wagon' is Flink's Wagon Lomax rides on the rails (wagon.c);
 *  - 'Mega Explode' is Flink's Exploding platform, going and breaking up once stood on (explode.c);
 *  - 'Mega Wolky' is Flink's Wolky platform, a cloud rising off the top (wolky.c);
 *  - a 'Mega Flink' in the sinking mode is Flink's Mega platform with 'dont move': it also sinks
 *    under a heavy thrown object (sink_update, grab_land_on in grab.c).
 * The converter writes the other kinds as plain Megas.
 *
 * Chains: Lomax's swing (Swin_Swing_Draw) and wheel (Rota_RotatingPlatforms_DrawSpokes) draw their
 * links with Draw_RopeSegments (0x425D40), one every `step` px from the pivot, step pushed as 8 by
 * every call. Flink's (0x2B752, for both) draws one every 16 px (0x40 of its 1/4 px units) from 16 px
 * out to the platform. In Flink's maps the six pushes become 16 (chains_set, from world_load), so the
 * links are as far apart as Flink's (from the pivot instead of from 16 px out: the same count).
 */
#include "flink.h"

/* dipping leaves, per actor slot: their type, rest y, ticks into the dip or -1, ridden last tick */
static struct { BYTE *type; int base, t, riding; } g_dip[MAX_ACTORS];

/* falling platforms, per actor slot: their type, rest y, drop (16.16), fall speed (16.16, 0 = not
   falling), countdown after a landing (0 = none) and the placement's delay */
static struct { BYTE *type; int base, drop, vy, timer, delay; } g_fall[MAX_ACTORS];

void platforms_reset(void) { memset(g_dip, 0, sizeof(g_dip)); memset(g_fall, 0, sizeof(g_fall)); }

/* Draw_RopeSegments' step, pushed by Swin_Swing_Draw (5 calls) and Rota_RotatingPlatforms_DrawSpokes:
   push 8 (6A 08) at each */
static const DWORD CHAIN_STEP_PUSH[] = { 0x00425C53u, 0x00425C88u, 0x00425CB9u, 0x00425CEAu, 0x00425D1Bu, 0x0041F672u };
#define CHAIN_STEP_LOMAX   8
#define CHAIN_STEP_FLINK   16

int platforms_retail(void)
{
    static const BYTE push8[2] = { 0x6A, CHAIN_STEP_LOMAX };
    size_t i;
    for (i = 0; i < COUNT(CHAIN_STEP_PUSH); i++)
        if (!is_bytes(CHAIN_STEP_PUSH[i], push8, 2) || !is_call(CHAIN_STEP_PUSH[i] + (i < 5 ? 7 : 0x17), (DWORD)Draw_RopeSegments)) return 0;
    return 1;
}

void chains_set(int flink)
{
    BYTE step = flink ? CHAIN_STEP_FLINK : CHAIN_STEP_LOMAX;
    size_t i;
    for (i = 0; i < COUNT(CHAIN_STEP_PUSH); i++) patch(CHAIN_STEP_PUSH[i] + 1, &step, 1);
}

/* Flink's Normalplatform (0x293F2), when landed on, sinks 0.5 px a frame for 15 frames and rises
   back: its base y (+0x9C) follows that, MovingPlatform_Update moving it (and Lomax). */
static void __cdecl dip_update(BYTE *a)
{
    int i = slot_of(a), riding;
    MovingPlatform_Update(a);
    if (i < 0 || g_dip[i].type != A_TYPE(a)) return;
    riding = *G_PRIDINGPLATFORM == a;
    if (riding && !g_dip[i].riding && g_dip[i].t < 0) g_dip[i].t = 0;
    g_dip[i].riding = riding;
    if (g_dip[i].t >= 0 && ++g_dip[i].t >= 30) g_dip[i].t = -1;
    *(int *)(a + 0x9C) = g_dip[i].base + (g_dip[i].t < 0 ? 0 : (g_dip[i].t <= 15 ? g_dip[i].t : 30 - g_dip[i].t) / 2);
}

static void dip_spawn(BYTE *a, int i)
{
    g_dip[i].type = A_TYPE(a);
    g_dip[i].base = *(int *)(a + 0x9C);
    g_dip[i].t = -1;
    g_dip[i].riding = 0;
    A_UPDATE(a) = (void *)dip_update;
}

/* Flink's Tra2 / Trap platform (spawn 0x2765C, think 0x2768E): param 0 is a delay. Landing on it
   (0x1A28A reports him standing) starts a countdown of that many frames; at 0 it starts to fall,
   then its speed grows 0x2000 (1/8 px) a frame up to 5 px a frame (0x50000), and it never stops,
   ridden or not, until it is gone below the map. A negative delay (MOUNTAIN, level 41) starts it
   instead when the player's point is within 20 px of its sides and 70 px above to 10 px below its
   top. Delay 0 would never fall, as in Flink. Its base y (+0x9C) follows the drop, so
   MovingPlatform_Update moves it and carries Lomax. */
#define FALL_ACCEL   0x2000
#define FALL_MAX     0x50000
static void __cdecl fall_update(BYTE *a)
{
    int i = slot_of(a);
    if (i >= 0 && g_fall[i].type == A_TYPE(a)) {
        const BYTE *r = A_RECORD(a);
        if (g_fall[i].delay < 0 && !g_fall[i].vy && r && !*G_PLAYERDEATHTIMER && *G_PLAYERPLANE == A_PLANE(a)) {
            int top = A_Y(a) + HB_Y1(r), px = *G_PLAYERX, py = *G_PLAYERY;
            if (px >= A_X(a) + HB_X1(r) - 20 && px <= A_X(a) + HB_X2(r) + 20 && py >= top - 70 && py <= top + 10)
                g_fall[i].vy = 1;
        }
        if (g_fall[i].timer && !--g_fall[i].timer) g_fall[i].vy += 1;
        if (g_fall[i].vy && g_fall[i].vy < FALL_MAX) g_fall[i].vy += FALL_ACCEL;
        g_fall[i].drop += g_fall[i].vy;
        *(int *)(a + 0x9C) = g_fall[i].base + (g_fall[i].drop >> 16);
    }
    MovingPlatform_Update(a);
    if (i >= 0 && g_fall[i].type == A_TYPE(a) && *G_PRIDINGPLATFORM == a && !g_fall[i].timer && g_fall[i].delay > 0)
        g_fall[i].timer = g_fall[i].delay;
}

static void fall_spawn(BYTE *a, BYTE *def, int i)
{
    memset(&g_fall[i], 0, sizeof(g_fall[i]));
    g_fall[i].type = A_TYPE(a);
    g_fall[i].base = *(int *)(a + 0x9C);
    g_fall[i].delay = def_param(a, def, 3, 1);     /* the converter puts the delay in the Y speed */
    A_UPDATE(a) = (void *)fall_update;
}

/* Flink's Mega platform with 'dont move' (init 0x286B2, think 0x2876A; converter plat_mega: a plain
   Mega in MovingPlatform's sinking mode, +0xA8, that doesn't rise back, +0xA4 0). Flink's sinks at
   speed * 0x28F a frame while Flink stands on it (0x1A28A) or a heavy thrown object lies on it
   (Platform_CarryThrownObjects 0x1A1B2, 0x288D0): 10-1's stone is thrown onto one to bring it
   down. MovingPlatform_Update (0x429BB0) moves it by its vy and sets the next one from Lomax alone
   (+0x90 * 0x28F stood on, else 0); then the objects on its new top are put on it (grab_land_on)
   and, if any, it sinks too, as Flink's order: move, the objects, the next speed. */
#define SINK_REACH  4                    /* 0x1A1B2's d3: the object within 4 px of the top */
static void __cdecl sink_update(BYTE *a)
{
    MovingPlatform_Update(a);
    if (grab_land_on(a, SINK_REACH)) A_VY(a) = *(int *)(a + 0x90) * 0x28F;
}

void __cdecl mega_spawn(BYTE *a, BYTE *def)
{
    int i;
    MovingPlatform_Spawn(a, def);
    if (!g_active) return;
    if (is_type(a, "Mega Jumppad") && (i = slot_of(a)) >= 0) jumppad_spawn(a, i);
    else if (is_type(a, "Mega Bouncy") && (i = slot_of(a)) >= 0) bouncy_spawn(a, def, i);
    else if (is_type(a, "Mega Floating") && (i = slot_of(a)) >= 0) floating_spawn(a, def, i);
    else if (is_type(a, "Mega Float ")) float_track(a, 1);
    else if (is_type(a, "Mega Dip") && (i = slot_of(a)) >= 0) dip_spawn(a, i);
    else if (is_type(a, "Mega Fall") && (i = slot_of(a)) >= 0) fall_spawn(a, def, i);
    else if (is_type(a, "Mega Throw") && (i = slot_of(a)) >= 0) throwplat_spawn(a, def, i);
    else if (is_type(a, "Mega Seesaw") && (i = slot_of(a)) >= 0) seesaw_spawn(a, i);
    else if (is_type(a, "Mega Leaves") && (i = slot_of(a)) >= 0) leaf_spawn(a, def, i, 1);
    else if (is_type(a, "Mega Leaf ") && (i = slot_of(a)) >= 0) leaf_spawn(a, def, i, 0);
    else if (is_type(a, "Mega Steer") && (i = slot_of(a)) >= 0) steer_spawn(a, i);
    else if (is_type(a, "Mega Hand") && (i = slot_of(a)) >= 0) hand_spawn(a, def, i);
    else if (is_type(a, "Mega Plant") && (i = slot_of(a)) >= 0) plant_spawn(a, def, i);
    else if (is_type(a, "Mega Wall") && (i = slot_of(a)) >= 0) wall_spawn(a, def, i);
    else if (is_type(a, "Mega Bridge") && (i = slot_of(a)) >= 0) bridge_spawn(a, i);
    else if (is_type(a, "Mega Rock") && (i = slot_of(a)) >= 0) rock_spawn(a, def, i);
    else if (is_type(a, "Mega Wagon") && (i = slot_of(a)) >= 0) wagon_spawn(a, def, i);
    else if (is_type(a, "Mega Explode") && (i = slot_of(a)) >= 0) explode_spawn(a, def, i);
    else if (is_type(a, "Mega Wolky") && (i = slot_of(a)) >= 0) wolky_spawn(a, def, i);
    else if (is_type(a, "Mega Flink") && *(int *)(a + 0xA8) && !*(int *)(a + 0xA4) &&
             A_UPDATE(a) == (void *)MovingPlatform_Update) A_UPDATE(a) = (void *)sink_update;
}

void __cdecl boat_spawn(BYTE *a, BYTE *def)
{
    DriftingBoat_Init(a, def);
    if (g_active) float_track(a, 0);
}
