/*
 * groundtype.c - Flink's Ground type (README "Ground type"): Lomax sinks into it, slowly, and dies at 35 px.
 *
 * Flink. The mud is ordinary ground; what makes it mud is the ground type g_GroundType (0x82C2),
 * set by 'Ground type' actors (type 81, spawn 0x2905E, think 0x29082) standing at its edges, with
 * params {type left of it, type right of it}. While the player is within x +- 8 of one and his feet
 * are y - 200 .. y + 2, the type becomes the one of his side (x < the actor's: param 0, else param 1).
 * Player_Update (0x1BDB6..0x1BE90), with ground type != 0, on the ground (not airborne, flag bit 0),
 * not on a platform (0x82FE) and not in the water (bit 3):
 *   - walks at 0.25 px/frame at most (PlayerSpeedTable's ground type 1 rows, 0x1CF84: -0.25/0/0.25,
 *     accel 0.0625; Flink walks at 2.0);
 *   - sinks: g_MudSinkDepth (0x82F0) += 0.25 px a frame;
 *   - from 4 px deep, Jump pressed starts a pull-out instead of a jump (g_JumpBoost cleared): 10
 *     frames (g_MudPullOutTimer 0x82F4) of -1 px each, no sinking, no jump, down to 0;
 *   - elsewhere (in the air, on a platform, on ground type 0) the depth is 0 again.
 * At 35 px (0x1BB1E) and not already dying, Player_Die. The depth moves only the drawing: the
 * player is drawn that much lower (0x3356E), into the mud's tiles in front of him. When the type turns
 * to 1, the Ground type actor spawns a 'MUD ' child (0x290FA: think 0x29136, draw 0x2914E): its 2
 * frames (the sprite and its mirror image, speed 12) drawn at the player's feet while he stands on
 * the ground, until the type is no longer 1.
 *
 * Here. The converter's 'Spik Ground' (a Lomax decoration, object 51, with Flink's MUD record after a
 * blank MAIN) is the Ground type actor, params as Flink's: groundtype_spawn (from spik_spawn) gives it
 * bound_update, Flink's think, and no drawing. Before Player_Update (player_tick, grab.c) groundtype_before
 * sinks, pulls out and kills; after it, groundtype_after holds his move to GROUNDTYPE_SPEED. The player's draw
 * call in Actors_DrawAndCull (0x40F1C0, Actor_Draw(&g_Player, ...)) goes through player_draw, which
 * draws him depth px lower (Lomax+'s frame interpolation reads the actor's y during the call, so it
 * follows). It also draws him shrinking as he walks into a door (exits.c, door_walk_scale): that
 * scale, with g_PlaneTransitionScale cleared for the call so that Actor_Draw takes its plain path,
 * which scales the parts around his feet (0x40C365 would draw him through the plane-door path,
 * lifted and toward the plane behind); instead he rises door_walk_lift px as he shrinks. SHRINK draws
 * him the same way at spell_scale (spells.c). The overlay is an Actor_Alloc'd copy of the Spik Ground actor, never culled, in front of him.
 */
#include "flink.h"

#define CALL_PLAYER_DRAW   0x0040F1C0u   /* Actors_DrawAndCull: call Actor_Draw(&g_Player, scale, 0, 0) */

#define GROUNDTYPE_RANGE          8             /* Flink's box: x +- 8, */
#define GROUNDTYPE_ABOVE          200           /*   feet y - 200 .. */
#define GROUNDTYPE_BELOW          2             /*   y + 2 (0x29082) */
#define SINK_STEP          0x4000        /* 0.25 px a tick */
#define PULL_STEP          0x10000       /* 1 px a tick */
#define PULL_TICKS         10
#define PULL_FROM          4             /* px deep before Jump pulls out */
#define SINK_DEADLY        35            /* px deep: dead */
#define GROUNDTYPE_SPEED          move_speed(0x6000)   /* Flink's 0.25 of 2.0, at Lomax's 3.0 walk, scaled
                                            with his speed (move.c) */
#define OVERLAY_LAYER      3             /* bucket + 10, in front of Lomax's + 6 (layer 6) */

static int g_ground;                     /* Flink's g_GroundType */
static int g_depth;                      /* g_MudSinkDepth, 16.16 */
static int g_pull;                       /* g_MudPullOutTimer */
static int g_overlay = -1;               /* the MUD actor's slot */
static BYTE *g_overlayType;
static int g_sinking, g_x0;              /* this tick, groundtype_before -> groundtype_after */

void groundtype_reset(void)
{
    g_ground = 0;
    g_depth = g_pull = 0;
    g_overlay = -1;
    g_overlayType = NULL;
    g_sinking = 0;
}

static int on_ground(void)
{
    return !(*G_PLAYERSTATE & (PS_AIR | PS_WATER | PS_ROPE | PS_FLY)) && !*G_PRIDINGPLATFORM;
}

static BYTE *overlay(void)
{
    BYTE *o = actor_at(g_overlay);
    return o && A_TYPE(o) == g_overlayType && A_LAYER(o) != KILLED_LAYER ? o : NULL;
}

/* the MUD child (0x29136): its animation while the type is 1, then it goes */
static void __cdecl overlay_update(BYTE *a)
{
    if (g_ground != 1 || !g_active) { Actor_Kill(a); if (slot_of(a) == g_overlay) g_overlay = -1; return; }
    Actor_AdvanceAnim(a);
}

/* (0x2914E) at his feet, while he stands on the ground */
static void __cdecl overlay_draw(BYTE *a)
{
    if (!on_ground() || A_PLANE(a) != *G_PLAYERPLANE) return;
    A_X(a) = *G_PLAYERX; A_FX(a) = A_X(a) << 16;
    A_Y(a) = *G_PLAYERY; A_FY(a) = A_Y(a) << 16;
    draw_actor(a);
}

static void __cdecl draw_none(BYTE *a) { (void)a; }

/* Flink's Ground type think (0x29082) */
static void __cdecl bound_update(BYTE *a)
{
    int x = A_X(a), y = A_Y(a), px = *G_PLAYERX, py = *G_PLAYERY, was = g_ground;
    BYTE *o;
    if (A_PLANE(a) != *G_PLAYERPLANE || px < x - GROUNDTYPE_RANGE || px > x + GROUNDTYPE_RANGE ||
        py < y - GROUNDTYPE_ABOVE || py > y + GROUNDTYPE_BELOW) return;
    g_ground = px < x ? *(int *)(a + 0x80) : *(int *)(a + 0x84);
    if (g_ground == was) return;
    flog("ground type %d at (%d,%d) (Lomax at %d,%d)", g_ground, x, y, px, py);
    if (g_ground != 1 || overlay() || !(o = Actor_Alloc(a, NULL))) return;
    g_overlay = slot_of(o);
    g_overlayType = A_TYPE(o);
    A_FLAGS(o) &= ~(0x200000u | AF_HURTS | AF_HIDDEN);
    A_CULL(o) = CULL_NEVER;
    A_LAYER(o) = OVERLAY_LAYER;
    A_UPDATE(o) = (void *)overlay_update;
    A_DRAW(o) = (void *)overlay_draw;
    Actor_SetAnim(o, FOURCC("MUD "));
}

/* 'Spik Ground' (from spik_spawn, after Deco_Spawn): params type left, type right */
void groundtype_spawn(BYTE *a, BYTE *def)
{
    *(int *)(a + 0x80) = def_param(a, def, 0, 0);
    *(int *)(a + 0x84) = def_param(a, def, 1, 0);
    A_FLAGS(a) &= ~(0x200000u | AF_HURTS);       /* harmless */
    A_UPDATE(a) = (void *)bound_update;
    A_DRAW(a) = (void *)draw_none;
}

/* Lomax dies whatever he wears (Player_TakeHit without the helmet: state 3, death timer 300, vy -4.0,
   OHNO), straight up; also scroll.c's */
void player_kill(void)
{
    *G_BHASHELMET = 0;
    *G_BHELMETINFLIGHT = 0;
    *G_PLAYERHURTTIMER = 0;
    Player_TakeHit(*G_PLAYERX, 0, 0);
    *G_PLAYERVX = 0;
}

static void groundtype_kill(void)
{
    player_kill();
    flog("Lomax sank into the mud at (%d,%d)", *G_PLAYERX, *G_PLAYERY);
}

/* before Player_Update (0x1BE16..0x1BE8A, 0x1BB1E) */
void groundtype_before(void)
{
    WORD jump = (WORD)*G_JUMPMASK;
    g_sinking = 0;
    if (!g_active || *G_BPAUSED || *G_PLAYERSTATE & PS_FROZEN) return;
    if (*G_PLAYERDEATHTIMER) return;             /* dying: drawn where he sank */
    if (!g_ground || !on_ground()) { g_depth = g_pull = 0; return; }
    g_sinking = 1;
    g_x0 = *G_PLAYERXFIXED;
    if (g_pull) {
        g_pull--;
        if ((g_depth -= PULL_STEP) < 0) g_depth = 0;
    } else {
        g_depth += SINK_STEP;
        if (g_depth >> 16 >= PULL_FROM && *G_INPUTPRESSED & jump) {
            g_pull = PULL_TICKS;
            flog("Lomax pulls out of the mud (%d px deep)", g_depth >> 16);
        }
    }
    if (g_pull || g_depth >> 16 >= PULL_FROM) {   /* no jump */
        *G_INPUTPRESSED &= (WORD)~jump;
        *G_INPUTHELD &= (WORD)~jump;
    }
    if (g_depth >> 16 >= SINK_DEADLY) groundtype_kill();
}

/* after Player_Update: no more than GROUNDTYPE_SPEED a tick on the mud */
void groundtype_after(void)
{
    int dx;
    if (!g_sinking || !g_active) return;
    g_sinking = 0;
    if (!on_ground()) return;
    dx = *G_PLAYERXFIXED - g_x0;
    if (dx > GROUNDTYPE_SPEED || dx < -GROUNDTYPE_SPEED) {
        *G_PLAYERXFIXED = g_x0 + (dx > 0 ? GROUNDTYPE_SPEED : -GROUNDTYPE_SPEED);
        *G_PLAYERX = *G_PLAYERXFIXED >> 16;
    }
    if (*G_PLAYERVX > GROUNDTYPE_SPEED) *G_PLAYERVX = GROUNDTYPE_SPEED;
    if (*G_PLAYERVX < -GROUNDTYPE_SPEED) *G_PLAYERVX = -GROUNDTYPE_SPEED;
}

int groundtype_depth(void) { return g_active ? g_depth >> 16 : 0; }
int groundtype_type(void) { return g_active ? g_ground : 0; }

/* the player drawn depth px lower */
static void __cdecl player_draw(BYTE *a, int scale, int angle, int alpha)
{
    int d = a == G_PLAYERACTOR ? groundtype_depth() - door_walk_lift() : 0;
    int s = a == G_PLAYERACTOR ? door_walk_scale() : 0;
    if (a == G_PLAYERACTOR && scene_hides_player()) return;   /* Flink's intro and ending (scenes.c) */
    if (!s && a == G_PLAYERACTOR) s = spell_scale();
    A_Y(a) += d;
    if (s) { *G_PLANETRANSITIONSCALE = 0; scale = s; }
    Actor_Draw(a, scale, angle, alpha);
    if (s) *G_PLANETRANSITIONSCALE = s;
    A_Y(a) -= d;
}

int groundtype_retail(void) { return is_call(CALL_PLAYER_DRAW, (DWORD)Actor_Draw); }

void groundtype_install(void) { patch_call(CALL_PLAYER_DRAW, (const void *)player_draw); }
