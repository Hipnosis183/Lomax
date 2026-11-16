/*
 * dustcloud.c - Flink's landing dust (README "Landing dust"; 'Spik Dustcloud').
 *
 * Flink. Landing on the ground (Player_Update 0x1C612: falling, his feet crossing g_PlayerGroundY
 * 0x82BE) plays sound 0x50 and calls Fx_SpawnDust (0x33688): while his feet are above the water's
 * surface (0x8718) and the ground type (0x82C2) is 0, one of 8 slots (mask 0x8504) gets a puff at
 * his x - 4, feet y - 6, mirrored when he faces left. 0x336F0 draws each: MAIN.WLD's Dustcloud DUST
 * frame (counter >> 8, the counter + 0x40 a frame: a frame every 4 ticks), the puff's top-left at its
 * point, queued before the hero (in front of him); after frame 7 the slot is free. Flink has no
 * dust while walking.
 *
 * Lomax. Player_Update's landing (0x410DE2: SND_LAND, the water escape timer, then off a platform
 * three Fx_SpawnGroundDebris calls, 0x410E06) throws grass bits (material 0) or coins (2, world 5)
 * off his feet; his walk's step hook, g_Player +0x24 (Level_Start 0x40F95C stores
 * Effect_SpawnDustPuff there), puts a grass puff at each step on material 0.
 *
 * Here, in Flink's maps and title (g_active, g_title): the landing's call goes through land_debris,
 * which spawns one Dustcloud puff a tick (the loop calls it three times) instead of the grass, and
 * the step hook stored by Level_Start is step_puff, which does nothing. Elsewhere both call Lomax's.
 * The puff is an Actor_Alloc'd 'Spik Dustcloud' (the converter's dustcloud_type) at Lomax's layer,
 * never culled, drawn one bucket over his (Sprite_QueueWorld at its top-left); no type in the map,
 * no puff.
 */
#include "flink.h"

#define CALL_LAND_DEBRIS   0x00410E06u   /* Player_Update: call Fx_SpawnGroundDebris (the landing's loop of 3) */
#define STORE_STEP_HOOK    0x0040F95Cu   /* Level_Start: mov [0x59F224], Effect_SpawnDustPuff */
static const BYTE STEP_HOOK_RETAIL[10] = { 0xC7, 0x05, 0x24, 0xF2, 0x59, 0x00, 0xA0, 0xFC, 0x42, 0x00 };

#define DUST_TYPE          "Spik Dustcloud"
#define DUST_FRAMES        8
#define DUST_FRAME_TICKS   4             /* + 0x40 a frame on the counter's high byte */
#define DUST_DX            (-4)          /* its top-left: x - 4, feet y - 6 */
#define DUST_DY            (-6)
#define D_TICKS(a)         (*(int *)((BYTE *)(a) + 0x80))
#define D_MIRROR(a)        (*(int *)((BYTE *)(a) + 0x84))

static int flink_screen(void) { return g_active || g_title; }

static void __cdecl dust_update(BYTE *a)
{
    if (++D_TICKS(a) >= DUST_FRAMES * DUST_FRAME_TICKS) Actor_Kill(a);
}

static void __cdecl dust_draw(BYTE *a)
{
    const BYTE *e, *bank, *type = A_TYPE(a);
    int f = D_TICKS(a) / DUST_FRAME_TICKS;
    if (f >= DUST_FRAMES || !(e = record_of(a, FOURCC("DUST")))) return;
    e += f * 8;                              /* (one piece a frame) */
    if (!(bank = G_ANIMBANKS[e[0] | (T_BANKS(type) & 0x100)])) return;
    Sprite_QueueWorld(PLANE(A_PLANE(a)), bank + e[1] * 0x38 + 0x10, A_X(a), A_Y(a),
                      (unsigned)(Actor_GetDrawDepth(a) + 1) << 24 | ((e[6] ^ D_MIRROR(a)) & 0x18), 20000);
}

/* Fx_SpawnDust (0x33688) at Lomax's feet */
static void dust_spawn(void)
{
    int x = *G_PLAYERX, y = *G_PLAYERY;
    BYTE *a;
    if (water_present() && y >= *G_WATERLEVELY) return;   /* (under the surface) */
    if (groundtype_type()) return;                        /* (the mud) */
    if (!ObjType_FindByName(DUST_TYPE) || !(a = Actor_Alloc(NULL, DUST_TYPE))) return;
    A_PLANE(a) = *G_PLAYERPLANE;
    A_FLAGS(a) = 0;
    A_CULL(a) = CULL_NEVER;
    A_LAYER(a) = A_LAYER(G_PLAYERACTOR);
    A_FX(a) = (x + DUST_DX) << 16;
    A_FY(a) = (y + DUST_DY) << 16;
    A_VX(a) = A_VY(a) = 0;
    set_pos(a);
    D_TICKS(a) = 0;
    D_MIRROR(a) = A_FLAGS(G_PLAYERACTOR) & AF_LEFT ? 8 : 0;
    A_UPDATE(a) = (void *)dust_update;
    A_DRAW(a) = (void *)dust_draw;
}

/* the landing's Fx_SpawnGroundDebris calls: one puff a landing */
static void __cdecl land_debris(void)
{
    static int tick = -1;
    if (!flink_screen()) { Fx_SpawnGroundDebris(); return; }
    if (tick == *G_TICKCOUNTER) return;
    tick = *G_TICKCOUNTER;
    dust_spawn();
}

/* g_Player's step hook: no grass puffs */
static void __cdecl step_puff(BYTE *a, short x, short y)
{
    if (!flink_screen()) Effect_SpawnDustPuff(a, x, y);
}

int dustcloud_retail(void)
{
    return is_call(CALL_LAND_DEBRIS, (DWORD)Fx_SpawnGroundDebris) &&
           is_bytes(STORE_STEP_HOOK, STEP_HOOK_RETAIL, sizeof(STEP_HOOK_RETAIL));
}

void dustcloud_install(void)
{
    patch_call(CALL_LAND_DEBRIS, (const void *)land_debris);
    patch_dword(STORE_STEP_HOOK + 6, (DWORD)step_puff);
}
