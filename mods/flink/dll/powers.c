/*
 * powers.c - Lomax's helmet powers are out of reach in Flink's maps.
 *
 * Flink's levels have nothing for the powers (grapple, helicopter, bomb, digger, platform, fire
 * helmet), so in Flink's maps (g_active) the three ways to them are closed; the powers' code and
 * counters stay as they are, and Lomax's own levels keep them all.
 *
 * - Ability does nothing: Level_Start copies the control scheme's {Jump, Helm, Ability, DropHelmet}
 *   row into the live masks (0x40FB12..0x40FB3B). At 0x40FB1E it loads the Ability column,
 *   mov edx, [esi + 0x464590] (g_ControlSchemeMasks_Ability), stored at 0x40FB35 into
 *   g_AbilityButtonMask (0x52C57C), which every power reads (Player_Update, Rope_UpdateClimbing,
 *   Wall_DiggableDirt_Update; Cont_ContinueScreen_Update masks it out). In Flink's maps that load is
 *   xor edx, edx (powers_set, from world_load, which runs before Level_Start reaches it): the mask is
 *   0 and the button does nothing. The cheat's fly mode (Ability + a low button with the helicopter
 *   chosen) reads that mask too; grab.c's fly_cheat does it in Flink's maps instead.
 * - Select opens the wheel of Flink's spells instead: Player_Update calls PowerWheel_Open (0x430BA0)
 *   at 0x40FCD6 when Select is pressed; wheel_open calls spell_wheel_open (spells.c) in its place.
 *   On Flink's title screen (g_title, title.c) Select opens no wheel at all.
 * - The HUD shows no power: Hud_Draw (0x42A390) ends with the chosen power's ammo (ones at
 *   0x42A6C1, tens at 0x42A6EF) and its icon (0x42A754 with ammo, 0x42A773 greyed without), all
 *   through Actor_Draw; power_hud_draw skips them, and at the ones' call (made every tick) draws the
 *   magic glass and the spirits held there instead (spirits.c), and right of them the chosen spell
 *   and its cost (spells.c). Lomax+ moves these sprites' x (the mov [esi+0x3C] before each call), not
 *   the calls.
 */
#include "flink.h"

#define ABILITY_LOAD       0x0040FB1Eu   /* Level_Start: mov edx, [esi + 0x464590] */
#define CALL_WHEEL_OPEN    0x0040FCD6u   /* Player_Update: call PowerWheel_Open */
#define PowerWheel_Open    0x00430BA0u   /* void PowerWheel_Open(void) */

static const BYTE abilityLomax[6] = { 0x8B, 0x96, 0x90, 0x45, 0x46, 0x00 };
static const BYTE abilityFlink[6] = { 0x33, 0xD2, 0x90, 0x90, 0x90, 0x90 };   /* xor edx, edx */

/* Hud_Draw: call Actor_Draw for the power's ammo digits and icon (the first: the ones, spirits.c) */
static const DWORD HUD_POWER_DRAWS[] = { 0x0042A6C1u, 0x0042A6EFu, 0x0042A754u, 0x0042A773u };

static void __cdecl wheel_open(void)
{
    if (g_title) return;                           /* Flink's title: no wheel */
    if (!g_active) ((void (*)(void))PowerWheel_Open)();
    else spell_wheel_open();
}

static void __cdecl power_hud_draw(BYTE *a, int scale, int angle, int alpha)
{
    if (!g_active) Actor_Draw(a, scale, angle, alpha);
}

static void __cdecl ammo_ones_draw(BYTE *a, int scale, int angle, int alpha)
{
    if (!g_active) Actor_Draw(a, scale, angle, alpha);
    else spirits_hud_draw(a);
}

int powers_retail(void)
{
    size_t i;
    if (!is_bytes(ABILITY_LOAD, abilityLomax, sizeof(abilityLomax)) || !is_call(CALL_WHEEL_OPEN, PowerWheel_Open)) return 0;
    for (i = 0; i < COUNT(HUD_POWER_DRAWS); i++)
        if (!is_call(HUD_POWER_DRAWS[i], (DWORD)Actor_Draw)) return 0;
    return 1;
}

void powers_install(void)
{
    size_t i;
    patch_call(CALL_WHEEL_OPEN, (const void *)wheel_open);
    for (i = 0; i < COUNT(HUD_POWER_DRAWS); i++)
        patch_call(HUD_POWER_DRAWS[i], i ? (const void *)power_hud_draw : (const void *)ammo_ones_draw);
}

void powers_set(int flink)
{
    patch(ABILITY_LOAD, flink ? abilityFlink : abilityLomax, sizeof(abilityLomax));
}
