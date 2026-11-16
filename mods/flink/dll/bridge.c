/*
 * bridge.c - Flink's rope bridges, whose planks fall (README "Bridges").
 *
 * Flink's Bridge (init 0x27C7E, think 0x27D1C, draw 0x27E5E; no params; VILLAGE, 10-1): 12 planks
 * (record WOOD) hung between two posts that are background. The map has no collision there: the
 * bridge actor is the floor. Per plank a byte pair at +0x4E: a timer (0xFF untouched) and a drop
 * (0 = in place). Every frame:
 * - each plank: falling (drop != 0), its timer counts up to 16 and, until the drop reaches 0xF0, the
 *   drop grows by timer / 4 (an accelerating fall to 4 px a frame); in place with a timer (>= 0),
 *   the timer counts down, and at 0 the plank starts falling (drop 1) with sound 0x29;
 * - the standing surface (Platform_Stand 0x1A29A; its box +0x46: x - 80 .. x + 80) is
 *   y + 2 + SAG[k], k = (player x - x + 64) * 10 / 45 (muls.w, divs.w; the table at 0x27E1C), and
 *   plank k / 2 - 1 (k 2..25) is the one under him: no floor where it has fallen. Stood on, a plank
 *   still untouched gets its timer, 30 frames: it falls half a second after he stepped on it.
 * - drawn: plank i (0..11) is WOOD's first hardware sprite with its top-left at
 *   (x - 54 + 9 i, one more from i = 6, y - 16 + SAG[2 + 2 i] + drop); not drawn from drop 0xF0.
 * Left of x - 64 (k < 0) Flink reads the code before the table, giving no floor there; the hitbox
 * starts at x - 64 for that.
 * Here: the converter's 'Mega Bridge' (a static Mega, object 64, whose spawn platforms.c wraps):
 * record MAIN = one plank at the actor's point, hitbox Flink's box (x1 - 65, y1 2, x2 81: Lomax's
 * test is strict); bridge_update does the planks, then Platform_MoveAndCarryPlayer (0x413510) with
 * bridge_surface's sag, with player state 0x20000 set around it (no platform: as hand.c) while he is
 * over a fallen plank; bridge_draw draws each plank with one Actor_Draw, the actor moved onto it
 * (smooth under Lomax+'s FPS Override). Flink's 0x29 is Lomax's WOODBNC1 here (effect 54). Enemies and thrown objects fall through the bridge, as
 * in Flink (they stand on map collision only). The planks come back when Lomax's cull despawns the
 * bridge and its placement spawns it again (as the falling platforms do).
 */
#include "flink.h"

#define PLANKS          12
#define PLANK_WAIT      30          /* frames from a step to the fall */
#define PLANK_GONE      0xF0        /* drop at which the plank is no longer drawn or moved */
#define PLANK_TIMER_MAX 16
#define SND_PLANK       54          /* WOODBNC1 for Flink's 0x29 */
#define PS_NO_PLATFORMS 0x20000u    /* Platform_MoveAndCarryPlayer returns 0 at once */

/* FLINK.PRG 0x27E1C: the sag under the player, by k (33 words) */
static const signed char SAG[33] = { -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 9, 9, 9, 9, 8, 8, 7, 6,
                                     5, 4, 3, 2, 1, 0, -1, -2, 0, 0, 0, 0, 0 };

/* per actor slot: its type, each plank's timer (-1 untouched) and drop */
static struct { BYTE *type; signed char timer[PLANKS]; BYTE drop[PLANKS]; } g_bridge[MAX_ACTORS];

void bridges_reset(void) { memset(g_bridge, 0, sizeof(g_bridge)); }

static int bridge_slot(const BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_bridge[i].type && g_bridge[i].type == A_TYPE(a) ? i : -1;
}

/* the sag index under relx (the player's x - the bridge's x), clamped to the table */
static int sag_index(int relx)
{
    int k = (short)(relx + 64) * 10 / 45;
    return k < 0 ? 0 : k > 32 ? 32 : k;
}

/* the plank under relx, -1 at the ends (Flink's d6 = (k & ~1) - 2 checked < 0x18 unsigned) */
static int plank_under(int relx)
{
    int k = (short)(relx + 64) * 10 / 45, d6 = (k & ~1) - 2;
    return d6 >= 0 && d6 < 2 * PLANKS ? d6 / 2 : -1;
}

static int __cdecl bridge_surface(BYTE *a, int relx)
{
    (void)a;
    return SAG[sag_index((short)relx)];
}

static void planks_step(BYTE *a, int i)
{
    int p;
    for (p = 0; p < PLANKS; p++) {
        if (g_bridge[i].drop[p]) {
            if (g_bridge[i].timer[p] != PLANK_TIMER_MAX) g_bridge[i].timer[p]++;
            if (g_bridge[i].drop[p] < PLANK_GONE) g_bridge[i].drop[p] += g_bridge[i].timer[p] >> 2;
        } else if (g_bridge[i].timer[p] >= 0 && --g_bridge[i].timer[p] == 0) {
            g_bridge[i].drop[p] = 1;
            Sound_PlayEffect(a, SND_PLANK);
            flog("bridge at (%d,%d): plank %d falls", A_X(a), A_Y(a), p);
        }
    }
}

static void __cdecl bridge_update(BYTE *a)
{
    int i = bridge_slot(a), p, on;
    if (i < 0) return;
    planks_step(a, i);
    p = plank_under(*G_PLAYERX - A_X(a));
    if (p >= 0 && g_bridge[i].drop[p]) {
        DWORD st = *G_PLAYERSTATE;
        *G_PLAYERSTATE = st | PS_NO_PLATFORMS;
        on = Platform_MoveAndCarryPlayer(a, bridge_surface);
        *G_PLAYERSTATE = (*G_PLAYERSTATE & ~PS_NO_PLATFORMS) | (st & PS_NO_PLATFORMS);
    } else
        on = Platform_MoveAndCarryPlayer(a, bridge_surface);
    if (on && p >= 0 && g_bridge[i].timer[p] < 0) g_bridge[i].timer[p] = PLANK_WAIT;
}

static void __cdecl bridge_draw(BYTE *a)
{
    int i = bridge_slot(a), p, x0 = A_X(a), y0 = A_Y(a);
    if (i < 0) return;
    for (p = 0; p < PLANKS; p++) {
        if (g_bridge[i].drop[p] >= PLANK_GONE) continue;
        A_X(a) = x0 - 54 + 9 * p + (p >= 6);
        A_Y(a) = y0 - 16 + SAG[2 + 2 * p] + g_bridge[i].drop[p];
        draw_actor(a);
    }
    A_X(a) = x0; A_Y(a) = y0;
}

void bridge_spawn(BYTE *a, int i)
{
    memset(&g_bridge[i], 0, sizeof(g_bridge[i]));
    g_bridge[i].type = A_TYPE(a);
    memset(g_bridge[i].timer, 0xFF, sizeof(g_bridge[i].timer));
    A_VX(a) = A_VY(a) = 0;
    hold_frame(a, 0);
    A_UPDATE(a) = (void *)bridge_update;
    A_DRAW(a) = (void *)bridge_draw;
}
