/*
 * plant.c - Flink's Plant that grows (README "Plant"; FOREST, 6 in all: 3-3,
 * 3-4, 3-5, 8-1, two in 7-1).
 *
 * Flink (init 0x2E3B0, think 0x2E412, draw 0x2E4AC; param 0 'height'; records LEAF, ROOT):
 * - Init: home y (+0x46) = its y, top (+0x48) = y - height; the stalk's foot (+0x52) =
 *   Map_GetGroundHeight (0x1AEEA) from its x, y (within 100 px) + 2; ROOT's two frames are kept
 *   (+0x4A, +0x4E), the actor shows LEAF.
 * - Think, every frame: 0x1A28A moves it by its y speed (+0x20) and carries the player; then
 *   - not stood on, or stood on while sinking (speed > 0): speed 1 px a frame down, 0 once at home
 *     (left alone, a grown plant sinks back);
 *   - stood on while growing (speed < 0): 0 once at the top;
 *   - stood on at rest: when the Quick Grow spell (4) is cast on it (cast timer 0x830C at -30, the
 *     player's platform 0x82FE this one), it costs 4 magic (with 4 or less the magic gauge only
 *     flashes, 0x830E = 0x40) and it starts growing at 1 px a frame up (sparkles, 0x2ECA6).
 * - Draw: the leaf, then the stalk: ROOT's frames alternating (starting with the one the leaf's
 *   frame isn't) as single hardware sprites, top-left at (x - 8, f - 16), (x - 8, f - 32), ... from
 *   the foot f up while that y is below the leaf's y + 12.
 * Here: a static 'Mega Plant' (Lomax's MovingPlatform,
 * object 64, whose spawn platforms.c wraps), MAIN = LEAF, ROOT = ROOT; params: the stalk's foot
 * below the placement (the X speed; the converter measures the ground), the height (the Y speed),
 * both with an amplitude of 0. Lomax standing on it at rest with QUICK GROW chosen on the spell wheel
 * (spells.c) casts it by pressing Ability (grab.c's grab_before, before a pick-up): it costs 4
 * spirits (spell_pay; Flink's 4 magic), and it grows; with fewer the cost flashes on the
 * HUD. With another spell chosen the press is a pick-up. Lomax has no casting gesture (Flink's MAG1 / MAG2), so it grows at once. Unlike Flink's, a plant at its top doesn't
 * take the press (Flink's would grow on past it). No sparkles. ROOT's frame 1 is frame 0 turned over in place
 * (flags 0x18) in a 32-px-wide image whose left half is the sprite, so its record offset is 16 px
 * further left (Flink's -24 and -7): drawn at x - 1 and x, both land at x - 8 as Flink's.
 */
#include "flink.h"

#define PLANT_SPEED   0x10000            /* 1 px a tick, both ways */
#define LEAF_SPEED    10                 /* FOREST's LEAF record (8.8 frames a tick) */
#define ROOT_STEP     16
#define ROOT_ABOVE    12                 /* the stalk stops at the leaf's y + 12 */

/* per actor slot: their type, home and top y, the stalk's foot, y speed (16.16), the leaf's frame
   phase (8.8 frames) */
static struct { BYTE *type; int home, top, foot, vy, phase; } g_plant[MAX_ACTORS];

void plants_reset(void) { memset(g_plant, 0, sizeof(g_plant)); }

static int plant_slot(const BYTE *a)
{
    int i = a ? slot_of(a) : -1;
    return i >= 0 && g_plant[i].type && g_plant[i].type == A_TYPE(a) ? i : -1;
}

/* the leaf's frame, 0 or 1 */
static int leaf_frame(int i) { return (g_plant[i].phase >> 8) & 1; }

static void __cdecl plant_update(BYTE *a)
{
    int i = plant_slot(a), on;
    if (i < 0) return;
    g_plant[i].phase += LEAF_SPEED;
    A_VX(a) = 0;
    A_VY(a) = g_plant[i].vy;
    on = Platform_MoveAndCarryPlayer(a, NULL);
    A_VX(a) = A_VY(a) = 0;
    if (!on || g_plant[i].vy > 0) {
        g_plant[i].vy = A_Y(a) >= g_plant[i].home ? 0 : PLANT_SPEED;
        if (A_Y(a) > g_plant[i].home) place_y(a, g_plant[i].home);
    } else if (g_plant[i].vy < 0 && A_Y(a) <= g_plant[i].top) {
        g_plant[i].vy = 0;
        place_y(a, g_plant[i].top);
        flog("plant at x %d is grown (y %d)", A_X(a), A_Y(a));
    }
}

/* Lomax pressed Ability: 1 if it was cast on the plant he stands on (Spell4_GrowPlant 0x315C2: on a
   Plant, QUICK GROW chosen; it grows if paid, else the cost flashes) */
int plant_grow(void)
{
    BYTE *a = *G_PRIDINGPLATFORM;
    int i = plant_slot(a);
    if (i < 0 || g_plant[i].vy || A_Y(a) <= g_plant[i].top || !spell_chosen(SPELL_QUICK_GROW) || !spell_can_cast())
        return 0;
    if (!spell_pay(SPELL_QUICK_GROW)) return 1;
    g_plant[i].vy = -PLANT_SPEED;
    flog("plant at (%d,%d) grows to y %d", A_X(a), A_Y(a), g_plant[i].top);
    return 1;
}

/* the leaf, then the stalk (each through Actor_Draw at its own point, for Lomax+'s FPS Override) */
static void __cdecl plant_draw(BYTE *a)
{
    int i = plant_slot(a), x = A_X(a), y = A_Y(a), f, k;
    if (i < 0) return;
    Actor_SetAnim(a, FOURCC("MAIN"));
    hold_frame(a, leaf_frame(i));
    Actor_AdvanceAnim(a);
    draw_actor(a);
    if (record_of(a, FOURCC("ROOT"))) {
        Actor_SetAnim(a, FOURCC("ROOT"));
        for (f = g_plant[i].foot - ROOT_STEP, k = leaf_frame(i) ^ 1; ; f -= ROOT_STEP, k ^= 1) {
            hold_frame(a, k);
            Actor_AdvanceAnim(a);
            A_X(a) = k ? x : x - 1;
            A_Y(a) = f + ROOT_STEP;
            draw_actor(a);
            if (f <= y + ROOT_ABOVE) break;
        }
        A_X(a) = x; A_Y(a) = y;
        Actor_SetAnim(a, FOURCC("MAIN"));
    }
}

void plant_spawn(BYTE *a, BYTE *def, int i)
{
    memset(&g_plant[i], 0, sizeof(g_plant[i]));
    g_plant[i].type = A_TYPE(a);
    g_plant[i].home = A_Y(a);
    g_plant[i].foot = A_Y(a) + def_param(a, def, 2, 0);
    g_plant[i].top = A_Y(a) - def_param(a, def, 3, 0);
    A_FX(a) = A_X(a) << 16;
    A_FY(a) = A_Y(a) << 16;
    A_VX(a) = A_VY(a) = 0;
    A_UPDATE(a) = (void *)plant_update;
    A_DRAW(a) = (void *)plant_draw;
    flog("plant at (%d,%d): height %d, stalk foot %d", A_X(a), A_Y(a), A_Y(a) - g_plant[i].top, g_plant[i].foot);
}
