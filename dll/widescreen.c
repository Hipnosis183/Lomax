/*
 * widescreen.c - the 320-pixel internal screen widened, and demo playback kept in sync.
 */
#include "lomax.h"

int g_wsWidth;             /* widescreen width (0 = off) */
static int g_demo;         /* demo recording loaded: spawning follows the original 320 layout */
/* ---- Widescreen: widen the 320-pixel internal screen ---------------------------------------
   The game hardcodes the screen width (320 / half 160 and derived values) as instruction
   immediates. The table below lists every site: the original instruction bytes (verified before
   anything is written) and where the immediate is.
   New value:  den > 0   integer  W * num / den + add
               den < 0   integer  round W*num up to a multiple of -den, + add   (fade tiles)
               den == 0  float    num > 0: W / num      num < 0: -num / W
   repl (optional): replacement bytes written over orig before the immediate (instruction rewrite). */
typedef struct {
    DWORD       addr;      /* instruction (or data) address */
    const char *orig;      /* original bytes at addr, hex */
    BYTE        immOff;    /* offset of the immediate inside those bytes */
    BYTE        immSize;   /* 1, 2 or 4 */
    int         num, den, add;
    const char *what;
    const char *repl;      /* NULL = keep the instruction, only change the immediate */
} WsPatch;

/* Widescreen patch sites for the retail LOMAX.EXE.
   Columns: address, original bytes, immediate offset, immediate size, num, den, add, description
   [, replacement bytes]. New value = W*num/den + add (den<0: round up to a multiple, den==0: float,
   see above). W = internal width (432 by default), Wa = W rounded up to 32 (fade tiles). */
#define P_(a, b, o, s, n, d, ad, w)     { a, b, o, s, n, d, ad, w, NULL }
#define R_(a, b, o, s, n, d, ad, w, r)  { a, b, o, s, n, d, ad, w, r }
#define P(...) P_(__VA_ARGS__)          /* extra level so W_/W2_/WA_ expand first */
#define R(...) R_(__VA_ARGS__)
#define W_       1, 1, 0
#define W2_      1, 2, 0
#define WA_      1, -32, 0

static const WsPatch kWsPatches[] = {
    /* ===== display plumbing ===== */
    P(0x00403FB1, "b840010000",       1, 4, W_,        "Display_SetVideoMode: mode 0 client width"),
    P(0x004040DA, "b880020000",       1, 4, 2, 1, 0,   "Display_SetVideoMode: mode 1 (2x) client width"),
    P(0x00461CE0, "40010000",         0, 4, W_,        "g_WinClientW default"),
    P(0x0040E36B, "3d40010000",       1, 4, W_,        "Menu_OnProperties: detect 1x window"),
    P(0x0040E386, "3d80020000",       1, 4, 2, 1, 0,   "Menu_OnProperties: detect 2x window"),
    P(0x00404D7B, "c744242040010000", 4, 4, W_,        "Game_Frame: viewport clear rect right"),
    P(0x00404F4F, "6a00",             1, 1, 0, 1, 0,   "Game_Frame: debug FPS text x (0; ws_inset_update)"),
    P(0x00404F9C, "6822010000",       1, 4, 1, 1, -30, "Game_Frame: debug world.level text x"),
    P(0x00405B2A, "3d40010000",       1, 4, W_,        "DDraw_Present: client width == screen width"),
    P(0x00405C01, "3d40010000",       1, 4, W_,        "DDraw_Present: client width == screen width"),
    P(0x00462588, "40010000",         0, 4, W_,        "present source rect right (0x462580)"),
    P(0x00462598, "40010000",         0, 4, W_,        "present dest rect right initial (0x462590)"),
    /* surfaces are Wa wide (multiple of 32 for the dissolve-fade tiles); only W is ever shown */
    P(0x00407B0E, "c744246c40010000", 4, 4, WA_,       "DDraw_InitSurfaces: back/stretch surface width"),
    P(0x00407CE9, "c744243840010000", 4, 4, W_,        "DDraw_InitSurfaces: overlay source rect right"),
    P(0x00407D14, "c744243090010000", 4, 4, 5, 4, 0,   "DDraw_InitSurfaces: overlay dest rect right"),
    P(0x00406C76, "c744244840010000", 4, 4, WA_,       "D3D_InitDevice: z-buffer width (= render target)"),
    P(0x00406F6E, "c744241c40010000", 4, 4, W_,        "D3D_InitDevice: viewport width"),
    P(0x00406F9F, "c744242400002043", 4, 4, 2, 0, 0,   "D3D_InitDevice: viewport dvScaleX = W/2 (float)"),
    P(0x00461BE0, "0000803f",         0, 4, -320, 0, 0, "D3D view matrix _11 = 320/W (float)"),

    /* ===== fades ===== */
    P(0x0040574B, "81fb40010000",     2, 4, WA_,       "Render_DissolveFade: tile column loop"),
    P(0x00405799, "81fe40010000",     2, 4, WA_,       "Render_DissolveFade: tile column loop (last row)"),
    P(0x004058E0, "81fb40010000",     2, 4, W_,        "Render_ScreenFade: D3D quad column loop"),

    /* ===== D3D quads: screen x relative to the centre ===== */
    P(0x0040507C, "2da0000000",       1, 4, W2_,       "D3D_EmitTexturedQuad: x - centre"),
    P(0x00405107, "2da0000000",       1, 4, W2_,       "D3D_EmitTexturedQuad: x - centre"),
    P(0x0040516C, "2da0000000",       1, 4, W2_,       "D3D_EmitTexturedQuad: x - centre"),
    P(0x004051D4, "2da0000000",       1, 4, W2_,       "D3D_EmitTexturedQuad: x - centre"),
    P(0x00405240, "2da0000000",       1, 4, W2_,       "D3D_EmitTexturedQuad: x - centre (mirrored)"),
    P(0x004052A7, "2da0000000",       1, 4, W2_,       "D3D_EmitTexturedQuad: x - centre (mirrored)"),
    P(0x00405310, "2da0000000",       1, 4, W2_,       "D3D_EmitTexturedQuad: x - centre (mirrored)"),
    P(0x0040537B, "2da0000000",       1, 4, W2_,       "D3D_EmitTexturedQuad: x - centre (mirrored)"),
    P(0x0040C0FE, "6681c7a000",       3, 2, W2_,       "Sprite_Queue3DQuad: mesh x -> screen x"),

    /* ===== sprite clipping to the right screen edge ===== */
    P(0x0040A985, "3d40010000",       1, 4, W_,        "Sprite_QueueScreen: right clip (mirrored)"),
    P(0x0040A98C, "2d40010000",       1, 4, W_,        "Sprite_QueueScreen: right clip (mirrored)"),
    P(0x0040A997, "c7410840010000",   3, 4, W_,        "Sprite_QueueScreen: right clip (mirrored)"),
    P(0x0040A9B0, "3d40010000",       1, 4, W_,        "Sprite_QueueScreen: right clip"),
    P(0x0040A9B7, "ba40010000",       1, 4, W_,        "Sprite_QueueScreen: right clip"),
    P(0x0040A9C6, "c7410840010000",   3, 4, W_,        "Sprite_QueueScreen: right clip"),
    P(0x0040ABEF, "6681ff6001",       3, 2, 1, 1, 32,  "Sprite_QueueWorldScaled: on-screen test (scaled)"),
    P(0x0040AD65, "6681ff6001",       3, 2, 1, 1, 32,  "Sprite_QueueWorldScaled: on-screen test (1:1)"),
    P(0x0040ADE4, "3d40010000",       1, 4, W_,        "Sprite_QueueWorldScaled: right clip (mirrored)"),
    P(0x0040ADEB, "2d40010000",       1, 4, W_,        "Sprite_QueueWorldScaled: right clip (mirrored)"),
    P(0x0040ADF6, "c7410840010000",   3, 4, W_,        "Sprite_QueueWorldScaled: right clip (mirrored)"),
    P(0x0040AE0F, "3d40010000",       1, 4, W_,        "Sprite_QueueWorldScaled: right clip"),
    P(0x0040AE16, "ba40010000",       1, 4, W_,        "Sprite_QueueWorldScaled: right clip"),
    P(0x0040AE25, "c7410840010000",   3, 4, W_,        "Sprite_QueueWorldScaled: right clip"),
    P(0x0040B129, "3d40010000",       1, 4, W_,        "Sprite_QueueScaledCentered: right clip (mirrored)"),
    P(0x0040B130, "8d8480c0f9ffff",   3, 4, -5, 1, 0,  "Sprite_QueueScaledCentered: -5*W"),
    P(0x0040B14B, "c7410840010000",   3, 4, W_,        "Sprite_QueueScaledCentered: right clip (mirrored)"),
    P(0x0040B175, "3d40010000",       1, 4, W_,        "Sprite_QueueScaledCentered: right clip"),
    P(0x0040B17C, "ba40010000",       1, 4, W_,        "Sprite_QueueScaledCentered: right clip"),
    P(0x0040B1A0, "c7410840010000",   3, 4, W_,        "Sprite_QueueScaledCentered: right clip"),
    P(0x0040B495, "3d40010000",       1, 4, W_,        "Sprite_QueueScaled: right clip (mirrored)"),
    P(0x0040B49C, "8d8480c0f9ffff",   3, 4, -5, 1, 0,  "Sprite_QueueScaled: -5*W"),
    P(0x0040B4B7, "c7410840010000",   3, 4, W_,        "Sprite_QueueScaled: right clip (mirrored)"),
    P(0x0040B4E1, "3d40010000",       1, 4, W_,        "Sprite_QueueScaled: right clip"),
    P(0x0040B4E8, "ba40010000",       1, 4, W_,        "Sprite_QueueScaled: right clip"),
    P(0x0040B50C, "c7410840010000",   3, 4, W_,        "Sprite_QueueScaled: right clip"),

    /* ===== Actor_Draw ===== */
    R(0x0040C3CF, "8d0480c1e006",     2, 4, W_,        "Actor_Draw: player zoom x*320 -> imul x*W", "69c000000000"),
    P(0x0040C3DA, "0540010000",       1, 4, W_,        "Actor_Draw: player zoom centring"),
    P(0x0040C5CE, "66817c241c0802",   5, 2, 1, 1, 200, "Actor_Draw: coarse x cull (W+200)"),
    P(0x0040C8B6, "3d40010000",       1, 4, W_,        "Actor_Draw: right clip (mirrored)"),
    P(0x0040C8BD, "8d8480c0f9ffff",   3, 4, -5, 1, 0,  "Actor_Draw: mirrored clip -5*W"),
    P(0x0040C8D8, "c7450840010000",   3, 4, W_,        "Actor_Draw: right clip (mirrored)"),
    P(0x0040C903, "3d40010000",       1, 4, W_,        "Actor_Draw: right clip"),
    P(0x0040C90A, "ba40010000",       1, 4, W_,        "Actor_Draw: right clip"),
    P(0x0040C92E, "c7450840010000",   3, 4, W_,        "Actor_Draw: right clip"),

    /* ===== TileLayer_Draw ===== */
    P(0x0040D13F, "81f940010000",     2, 4, W_,        "TileLayer_Draw: zoom path column loop entry"),
    P(0x0040D225, "3d40010000",       1, 4, W_,        "TileLayer_Draw: zoom path right clip"),
    P(0x0040D231, "ba40010000",       1, 4, W_,        "TileLayer_Draw: zoom path right clip"),
    P(0x0040D256, "c7430840010000",   3, 4, W_,        "TileLayer_Draw: zoom path right clip"),
    P(0x0040D28C, "81f940010000",     2, 4, W_,        "TileLayer_Draw: zoom path column loop end"),
    P(0x0040D3DD, "3d40010000",       1, 4, W_,        "TileLayer_Draw: column loop entry"),
    P(0x0040D4F1, "3d40010000",       1, 4, W_,        "TileLayer_Draw: right clip"),
    P(0x0040D4F8, "bf40010000",       1, 4, W_,        "TileLayer_Draw: right clip"),
    P(0x0040D507, "c7460840010000",   3, 4, W_,        "TileLayer_Draw: right clip"),
    P(0x0040D53B, "81f940010000",     2, 4, W_,        "TileLayer_Draw: column loop end"),

    /* ===== pseudo-3D meshes: x relative to the screen centre ===== */
    P(0x0041DDE3, "6681eaa000",       3, 2, W2_,       "Hang_PlaneDoor_Draw: centre"),
    P(0x0041E12A, "6681eea000",       3, 2, W2_,       "Flue_RotatingMesh_Draw: centre"),
    P(0x00432B6C, "6681eaa000",       3, 2, W2_,       "FallingMesh_Draw: centre"),
    P(0x00433143, "6681eaa000",       3, 2, W2_,       "PlaneDoor_Draw: centre"),
    P(0x0043376A, "662da000",         2, 2, W2_,       "PlaneLadder_Draw: centre"),
    P(0x004338CB, "6681eaa000",       3, 2, W2_,       "StaticMesh_Draw: centre"),
    R(0x0043275F, "662b148584b84900a178c55200" "6683ea6e" "668910", 15, 2, 1, 2, -50,
      "MeshProp_Draw: centre - 50 (imm8 -> imm16 rewrite)",
      "2b148584b84900" "a178c55200" "6681eaa600" "668910"),

    /* ===== camera & level width ===== */
    P(0x0040ECBA, "6683ea14",         3, 1, 1, 16, 0,  "Level_InitPlanes: maxScrollX = (wTiles - W/16)*16"),
    P(0x004120FC, "81e9a0000000",     2, 4, W2_,       "Player_Update: camera x = player.x - W/2"),
    /* camera clamp: planes narrower than W (maxScrollX < 0) are centred instead of snapping */
    R(0x00412105, "8b151c8f4900" "8b4a04" "83c204" "85c9" "7d08" "c70200000000" "eb0f" "a11c8f4900" "0fbf4030" "3bc1" "7d02" "8902",
      0, 0, W_, "Player_Update: camera clamp (centre narrow planes)",
      "8b151c8f4900" "0fbf4230" "8b4a04" "85c0" "7d06" "d1f8" "8bc8" "eb0c" "3bc8" "7e02" "8bc8" "85c9" "7d02" "33c9" "894a04" "90"),
    R(0x00412330, "8d04b6c1e006",     2, 4, W_,        "Player_Update: zoom centring x*320 -> imul x*W", "69c600000000"),
    P(0x00412339, "2d40010000",       1, 4, W_,        "Player_Update: zoom centring"),
    /* the left limit stays 16; listed so that ws_inset_update (narrowed view) writes only checked bytes */
    P(0x004113C4, "83f910",           2, 1, 0, 1, 16,  "Player_Update: keep player within screen (16)"),
    P(0x004113C9, "83c010",           2, 1, 0, 1, 16,  "Player_Update: keep player within screen (16)"),
    P(0x00411401, "81f930010000",     2, 4, 1, 1, -16, "Player_Update: keep player within screen (W-16)"),
    P(0x00411409, "0530010000",       1, 4, 1, 1, -16, "Player_Update: keep player within screen (W-16)"),
    P(0x00413106, "8db040010000",     2, 4, W_,        "Actor_CheckWallCollision: map right edge"),
    P(0x00413116, "0540010000",       1, 4, W_,        "Actor_CheckWallCollision: map right edge"),
    P(0x0041C392, "81f930010000",     2, 4, 1, 1, -16, "ForceScroll_Update: player right-edge clamp"),
    P(0x0041C39A, "0530010000",       1, 4, 1, 1, -16, "ForceScroll_Update: player right-edge clamp"),
    P(0x0042AA3A, "2da0000000",       1, 4, W2_,       "Trapdoor_Init: initial camera (exit replay)"),
    P(0x0042AB36, "2da0000000",       1, 4, W2_,       "Trapdoor_Init: initial camera (start)"),
    P(0x0042AC7C, "2da0000000",       1, 4, W2_,       "Trapdoor_Init: initial camera (checkpoint)"),

    /* ===== zoomed planes / parallax ===== */
    R(0x0042B8A7, "8d04bfc1e006",     2, 4, W_,        "ParallaxPlane_ApplyCamera: x*320 -> imul x*W", "69c700000000"),
    P(0x0042B8B0, "2d40010000",       1, 4, W_,        "ParallaxPlane_ApplyCamera: zoom centring"),
    R(0x0042401E, "8d04bfc1e006",     2, 4, W_,        "Mapf_MapParallaxProp_Draw: x*320 -> imul x*W", "69c700000000"),
    P(0x00424029, "0540010000",       1, 4, W_,        "Mapf_MapParallaxProp_Draw: zoom centring"),
    P(0x0042D7B8, "66b97201",         2, 2, 1, 1, 50,  "ParallaxSprite_Draw: right cull (rotating plane)"),
    P(0x0042D7D6, "66b94a01",         2, 2, 1, 1, 10,  "ParallaxSprite_Draw: right cull"),

    /* ===== spawning / despawning windows ===== */
    P(0x004124D0, "8d841840010000",   3, 4, W_,        "Objects_SpawnNearCamera: spawn window right (rotating)"),
    P(0x00412507, "6681c39001",       3, 2, 1, 1, 80,  "Objects_SpawnNearCamera: no-pop-in zone right (rotating)"),
    P(0x00412535, "8d841840010000",   3, 4, W_,        "Objects_SpawnNearCamera: spawn window right"),
    P(0x00412564, "6681c36801",       3, 2, 1, 1, 40,  "Objects_SpawnNearCamera: no-pop-in zone right"),
    P(0x0040F1E1, "8d841040010000",   3, 4, W_,        "Actors_DrawAndCull: despawn window right"),
    P(0x0040F228, "8d841040010000",   3, 4, W_,        "Actors_DrawAndCull: despawn window right"),
    P(0x0042D2EE, "8d842840010000",   3, 4, W_,        "TriggerGroup_UpdateDespawn: despawn window right"),
    P(0x0042D32F, "8d842840010000",   3, 4, W_,        "TriggerGroup_UpdateDespawn: despawn window right"),

    /* ===== sound panning ===== */
    P(0x0040D6B6, "2da0000000",       1, 4, W2_,       "Sound_PlayEffect: pan relative to screen centre"),
    P(0x0040D6C3, "83f80c",           2, 1, 1, 16, -8, "Sound_PlayEffect: full-volume zone (8 px units)"),
    P(0x0040D6CA, "83ef0c",           2, 1, 1, 16, -8, "Sound_PlayEffect: full-volume zone (8 px units)"),

    /* ===== objects positioned relative to the screen ===== */
    P(0x0042758B, "054a010000",       1, 4, 1, 1, 10,  "ParticleEmitter_Update: enter from right edge"),
    P(0x004274A1, "b903000000",       1, 4, 0, 1, 9,   "ParticleEmitter_Update: top-entry x spread /9 (tuned for W=432)"),
    P(0x004274A6, "c1e002",           2, 1, 0, 1, 4,   "ParticleEmitter_Update: top-entry x spread shl 4 (tuned for W=432)"),
    P(0x00428C28, "0540010000",       1, 4, W_,        "Rockslide_Update: trigger near right edge"),
    P(0x00428C99, "81c15e010000",     2, 4, 1, 1, 30,  "Rockslide_Update: rocks spawn off right edge"),
    P(0x00428D8D, "0518010000",       1, 4, 1, 1, -40, "Rockslide_DrawWarning: WARN sign at right edge"),
    P(0x00428F72, "2da0000000",       1, 4, W2_,       "RockslideRocks_Update: shake distance from centre"),
    P(0x00428FE0, "81c190010000",     2, 4, 1, 1, 80,  "RockslideRocks_Update: despawn past right edge"),
    P(0x00431168, "3da4010000",       1, 4, 1, 1, 100, "AmbientFlyer_Update: respawn when far right"),
    P(0x004311F3, "0540010000",       1, 4, W_,        "AmbientFlyer_Update: respawn off right edge"),
    P(0x0041CD17, "81c390010000",     2, 4, 1, 1, 80,  "Flum_FloaterSpawner_Update: spawn off right edge"),
    P(0x0042DB32, "8d9c01a0000000",   3, 4, W2_,       "Confetti_WaitForPlayer: from screen centre"),
    P(0x0042E716, "05a0000000",       1, 4, W2_,       "Helmet_DropSpare: drop at screen centre"),
    P(0x00432B07, "2da0000000",       1, 4, W2_,       "FallingMesh_Update: falls at screen centre"),
    P(0x00431E12, "66c744241006 00",  5, 2, 1, 64, 2,  "Water_Draw: wave strips (64 px)"),
    P(0x00431F79, "66c744241006 00",  5, 2, 1, 64, 2,  "Water_Draw: reflection strips"),
    P(0x00432024, "66bd0600",         2, 2, 1, 64, 2,  "Water_Draw: surface strip"),
    P(0x00432098, "66bf0600",         2, 2, 1, 64, 2,  "Water_Draw: back layer strip"),

    /* world map camera / trapdoor: handled by the hooks in widescreen.c (ws_map_*) */

    /* ===== HUD: the 320-wide bar is shifted as a block to stay centred (+ (W-320)/2) ===== */
    /* centred position = W/2 + (orig - 160): num 1, den 2, add orig-160 */
    P(0x0042A3EC, "c7463c8a000000",   3, 4, 1, 2, -22,  "Hud_Draw: lemmings ones"),
    P(0x0042A414, "c7463c85000000",   3, 4, 1, 2, -27,  "Hud_Draw: lemmings tens"),
    P(0x0042A466, "c7463cb8000000",   3, 4, 1, 2, 24,   "Hud_Draw: coins ones"),
    P(0x0042A48E, "c7463cb3000000",   3, 4, 1, 2, 19,   "Hud_Draw: coins tens"),
    P(0x0042A4CE, "c7463c1d010000",   3, 4, 1, 2, 125,  "Hud_Draw: lives ones"),
    P(0x0042A503, "c7463c16010000",   3, 4, 1, 2, 118,  "Hud_Draw: lives tens"),
    P(0x0042A568, "c7463ca0000000",   3, 4, W2_,        "Hud_Draw: centre bar"),
    P(0x0042A59E, "c7463ca0000000",   3, 4, W2_,        "Hud_Draw: spare helmet icon"),
    P(0x0042A5E8, "c7463c82000000",   3, 4, 1, 2, -30,  "Hud_Draw: lemming icon"),
    P(0x0042A615, "c7463cbe000000",   3, 4, 1, 2, 30,   "Hud_Draw: coin icon"),
    P(0x0042A647, "c7463c27010000",   3, 4, 1, 2, 135,  "Hud_Draw: lives icon"),
    P(0x0042A6B4, "c7463c23000000",   3, 4, 1, 2, -125, "Hud_Draw: ammo ones"),
    P(0x0042A6CD, "c7463c1e000000",   3, 4, 1, 2, -130, "Hud_Draw: ammo tens"),
    P(0x0042A730, "c7463c1b000000",   3, 4, 1, 2, -133, "Hud_Draw: power icon"),
    P(0x0042C891, "c7473cbe000000",   3, 4, 1, 2, 30,   "Item_UpdateCoinOrHelmet: 1UP starts at HUD coin icon"),
    P(0x0042CAAC, "3d27010000",       1, 4, 1, 2, 135,  "Item_UpdateCoinLifeToHud: 1UP reaches HUD lives icon"),

    /* ===== banners, timer, hints, text screens ===== */
    P(0x004279C8, "c7463ca0000000",   3, 4, W2_,        "Insert_Show: message banners"),
    P(0x00428030, "c7463cac000000",   3, 4, 1, 2, 12,   "LevelTimer_Draw: 3 digits"),
    P(0x00428039, "c7463ca6000000",   3, 4, 1, 2, 6,    "LevelTimer_Draw: 2 digits"),
    P(0x00428045, "c7463ca0000000",   3, 4, W2_,        "LevelTimer_Draw: 1 digit"),
    P(0x00433BB5, "c7403c40000000",   3, 4, 1, 2, -96,  "HintGuy_UpdateAppear: hint box x"),
    P(0x0041CF78, "b8a0000000",       1, 4, W2_,        "Message_Init: PRESS BUTTON TO START"),
    P(0x0041CB22, "c7473ca0000000",   3, 4, W2_,        "Credits_Update: The End"),
    P(0x004245D5, "c7463c09010000",   3, 4, 1, 2, 105,  "Cont_ContinueScreen_Init: credits counter"),

    /* ===== logo & title screen ===== */
    P(0x0041D022, "b8a0000000",       1, 4, W2_,        "Logo_Init: logo x"),
    P(0x0041D226, "8d048d9c000000",   3, 4, 1, 2, -4,   "Logo_Update: fly-in x"),
    P(0x0041D2A1, "8d0c453a000000",   3, 4, 1, 2, -102, "Logo_Update: shrink-to-corner x"),
    P(0x0041D775, "83c07b",           2, 1, -1, 2, 283, "TitleScreen_Update: title pan start (keeps framing centred)"),
    P(0x0041D7C9, "0540010000",       1, 4, W_,         "TitleScreen_CheckStartOrExit: exit past maxScrollX + W"),
    /* level-placed screen-space objects: add (W-320)/2 to x in their init (code caves in int3 padding) */
    R(0x0041C453, "83c408c3cccccccccccccccc", 10, 1, 1, 2, -160,
      "Copyright_Init: x += (W-320)/2", "83c4088b44240483403c00c3"),
    R(0x00417EE8, "5e5bc3", 0, 0, W_, "Obj_LangSelect_Init: jump to the x adjust", "eb0b90"),
    R(0x00417EF5, "5e5bc3cccccccc", 3, 1, 1, 2, -160,
      "Obj_LangSelect_Init: x += (W-320)/2", "83463c005e5bc3"),

    /* ===== world map password box (right-anchored) ===== */
    P(0x0042414A, "c7473cbc000000",   3, 4, 1, 1, -132, "Map_WorldMap_Init: password box x"),

    /* ===== Airship boss (auto-scroll) ===== */
    P(0x0041A854, "81c1c2010000",     2, 4, 1, 1, 130,  "Boss_Airship_NextAttackPattern: pass starts off the right edge"),
    P(0x0041B135, "05dc000000",       1, 4, 1, 2, 60,   "Boss_Airship_UpdateBossPass: hover target"),
    P(0x0041B241, "2da0000000",       1, 4, W2_,        "Boss_Airship_UpdateBossPass: distance from screen centre"),
    P(0x0041ACAA, "055e010000",       1, 4, 1, 1, 30,   "Boss_Airship_SpawnScrollingEnemies: coffin spawn x"),
    P(0x0041ADC8, "055e010000",       1, 4, 1, 1, 30,   "Boss_Airship_SpawnScrollingEnemies: clay pot spawn x"),
    P(0x0041AED1, "055e010000",       1, 4, 1, 1, 30,   "Boss_Airship_SpawnScrollingEnemies: zombie spawn x"),
    P(0x0041AFBE, "055e010000",       1, 4, 1, 1, 30,   "Boss_Airship_SpawnScrollingEnemies: cowboy spawn x"),
    P(0x0041B0AE, "055e010000",       1, 4, 1, 1, 30,   "Boss_Airship_UpdateBossPass: obstacle spawn x"),
    P(0x0041BBAC, "055e010000",       1, 4, 1, 1, 30,   "Boss_Airship_UpdateCrusher: spikes spawn x"),
    P(0x0041BC62, "055e010000",       1, 4, 1, 1, 30,   "Boss_Airship_UpdateCrusher: end spikes spawn x"),
    P(0x0041BD98, "b903000000",       1, 4, 0, 1, 2,    "Boss_Airship_UpdateCrusher: debris x spread"),
    P(0x0041C227, "bb05000000",       1, 4, 0, 1, 6,    "Boss_Airship_DrawCrusherTiled: one more 144-px tile"),
    /* the scroll-trigger keys (camX % w + 350) are shifted by W-320 in DLL stubs, see widescreen.c */

    /* ===== credits text centre x table (0x464a78, {x,y} shorts): + (W-320)/2 ===== */
    P(0x00464A78, "6400", 0, 2, 1, 2, -60, "credits text x"), P(0x00464A7C, "dc00", 0, 2, 1, 2, 60, "credits text x"),
    P(0x00464A80, "8200", 0, 2, 1, 2, -30, "credits text x"), P(0x00464A84, "c800", 0, 2, 1, 2, 40, "credits text x"),
    P(0x00464A88, "e600", 0, 2, 1, 2, 70, "credits text x"),  P(0x00464A8C, "6e00", 0, 2, 1, 2, -50, "credits text x"),
    P(0x00464A90, "be00", 0, 2, 1, 2, 30, "credits text x"),  P(0x00464A94, "7800", 0, 2, 1, 2, -40, "credits text x"),
    P(0x00464A98, "8c00", 0, 2, 1, 2, -20, "credits text x"), P(0x00464A9C, "b400", 0, 2, 1, 2, 20, "credits text x"),
    P(0x00464AA0, "a000", 0, 2, 1, 2, 0, "credits text x"),   P(0x00464AA4, "aa00", 0, 2, 1, 2, 10, "credits text x"),
    P(0x00464AA8, "6e00", 0, 2, 1, 2, -50, "credits text x"), P(0x00464AAC, "e600", 0, 2, 1, 2, 70, "credits text x"),
    P(0x00464AB0, "8c00", 0, 2, 1, 2, -20, "credits text x"), P(0x00464AB4, "c800", 0, 2, 1, 2, 40, "credits text x"),
    P(0x00464AB8, "7800", 0, 2, 1, 2, -40, "credits text x"), P(0x00464ABC, "dc00", 0, 2, 1, 2, 60, "credits text x"),
    P(0x00464AC0, "6e00", 0, 2, 1, 2, -50, "credits text x"), P(0x00464AC4, "a000", 0, 2, 1, 2, 0, "credits text x"),
    P(0x00464AC8, "a000", 0, 2, 1, 2, 0, "credits text x"),   P(0x00464ACC, "b400", 0, 2, 1, 2, 20, "credits text x"),

    { 0, "", 0, 0, 0, 1, 0, "end", NULL },
};

int g_wsDelta;             /* W - 320, read by the asm stubs below */

/* Airship boss: spawn triggers compare (camX % planeW + 350) with tables tuned for a 320 screen.
   Objects now spawn 112 px further right (W+30), so the key is taken from camX + (W-320) to keep
   the choreography. The original instructions are replaced by a call to these stubs. */
__asm__(".globl _ws_airship_key_a\n"
        "_ws_airship_key_a:\n"          /* replaces: mov eax,[eax+4]; cdq; idiv ecx */
        "  movl 4(%eax), %eax\n"
        "  addl _g_wsDelta, %eax\n"
        "  cltd\n"
        "  idivl %ecx\n"
        "  ret\n"
        ".globl _ws_airship_key_c\n"
        "_ws_airship_key_c:\n"          /* replaces: mov eax,[edx+4]; shl ecx,4 */
        "  movl 4(%edx), %eax\n"
        "  addl _g_wsDelta, %eax\n"
        "  shll $4, %ecx\n"
        "  ret\n"
        /* Actors_DrawAndCull despawn window: the original-game camera in demo mode (ws_demo_cam) */
        ".globl _ws_cull_scroll\n"
        "_ws_cull_scroll:\n"            /* replaces: mov dx,[eax+4]; mov ebp,[esp+0x1c] */
        "  movl 0x20(%esp), %ebp\n"
        "  movw 4(%eax), %dx\n"
        "  cmpl $0, _g_demoCull\n"
        "  je 1f\n"
        "  movw _g_demoCamOrig, %dx\n"
        "1:\n"
        "  ret\n");
extern void ws_airship_key_a(void), ws_airship_key_c(void), ws_cull_scroll(void);

static const struct { DWORD addr; const char *orig; void (*stub)(void); } kWsCalls[] = {
    { 0x0041AC2F, "8b400499f7f9", ws_airship_key_a },   /* Boss_Airship_SpawnScrollingEnemies */
    { 0x0041AFEF, "8b400499f7f9", ws_airship_key_a },   /* Boss_Airship_UpdateBossPass */
    { 0x0041BAC5, "8b4204c1e104", ws_airship_key_c },   /* Boss_Airship_UpdateCrusher */
    { 0x0040F1D0, "668b50048b6c241c", ws_cull_scroll }, /* Actors_DrawAndCull */
};

/* Logic sites seen with the original camera in demo mode (spawning and actor updates): put back to
   the original 320-based bytes while a demo plays. */
static const DWORD kWsDemoSites[] = {
    0x004124D0, 0x00412507, 0x00412535, 0x00412564,                       /* Objects_SpawnNearCamera */
    0x0040F1E1, 0x0040F228,                                               /* Actors_DrawAndCull */
    0x0042D2EE, 0x0042D32F,                                               /* TriggerGroup_UpdateDespawn */
    0x0041C392, 0x0041C39A,                                               /* forced scroll */
    0x0042758B, 0x004274A1, 0x004274A6, 0x00428C28, 0x00428C99, 0x00428F72, 0x00428FE0,
    0x00431168, 0x004311F3, 0x0041CD17, 0x0042DB32, 0x0042E716, 0x00432B07, /* screen-edge objects */
    0x0041A854, 0x0041B135, 0x0041B241, 0x0041ACAA,                       /* airship */
};

/* Widescreen HUD at the edges: the power icon and its ammo keep their distance from the left edge and
   the lives counter its distance from the right edge, instead of moving with the centred bar.
   New value W * num + add (the table entry's num/den/add are replaced). The 1UP that flies
   from the coin icon adds the life once it passes the lives icon, so its target moves too. */
static const struct { DWORD addr; int num, add; } kWsHudEdges[] = {
    { 0x0042A6B4, 0, 0x23 },        /* ammo ones */
    { 0x0042A6CD, 0, 0x1E },        /* ammo tens */
    { 0x0042A730, 0, 0x1B },        /* power icon */
    { 0x0042A4CE, 1, 0x11D - 320 }, /* lives ones */
    { 0x0042A503, 1, 0x116 - 320 }, /* lives tens */
    { 0x0042A647, 1, 0x127 - 320 }, /* lives icon */
    { 0x0042CAAC, 1, 0x127 - 320 }, /* 1UP reaches the lives icon */
};

/* World map: Litt_MapHeroMarker_Init sets the map camera to max(0, marker - 160). Everything else is
   placed on screen from it: the trapdoor Lomax drops from and lands under (MapT_MapTrapdoor_Update,
   camera + 50, 1x) and the props on the 2x parallax plane (Mapf_MapParallaxProp_Update, drawn at
   a[0x21] - 2 * scroll before the zoom): the sun rays by the trapdoor (kind 1, 2*camera + 50) and
   Evil Ed (kind 3) with the light behind him (kind 2) at 2*camera + 280. So in the original the
   trapdoor is always 50 px from the left edge and Ed 40 px from the right one.
   In widescreen the map scrolls to max(0, camera - (W-320)/2), which centres the marker, but no
   further right than the map's end, past which only the black that the original showed is allowed.
   The framing is kept a little inside the edges, k = (W-320)/4 px (28 at W 432): the trapdoor gets
   scroll + k in place of the camera, so it (and Lomax's landing spot) is 50 + k px from the left
   edge, the rays are moved with it, and Ed and his light are put k px in from W - 40. */
#define G_MAPCAMX            ((volatile int *)0x0049C4B8u)
#define FN_MAP_WORLDMAP_UPDATE 0x004241C0u
#define FN_MAPT_TRAPDOOR_UPDATE 0x00423CB0u
#define FN_MAPF_PROP_UPDATE    0x00423F30u
static void (__cdecl *oMapUpdate)(int *);
static void (__cdecl *oMapTrapdoorUpdate)(int *);
static void (__cdecl *oMapfUpdate)(int *);

static int ws_map_scroll_target(void)
{
    const BYTE *plane = (const BYTE *)*G_PCURPLANE;
    int cam = *G_MAPCAMX, w = 320 + g_wsDelta, t = cam - g_wsDelta / 2;
    if (plane) {
        int mapW = *(const int *)(plane + 0x10) * 16;          /* nWidthTiles */
        int past = cam + 320 - mapW;                           /* black the original showed */
        int maxT = mapW - w + (past > 0 ? past : 0);
        if (t > maxT) t = maxT;
    }
    return t < 0 ? 0 : t;
}

/* runs fn with the map camera swapped for the widescreen scroll target + add */
static void ws_map_call(void (__cdecl *fn)(int *), int *a, int add)
{
    int cam = *G_MAPCAMX;
    *G_MAPCAMX = ws_map_scroll_target() + add;
    fn(a);
    *G_MAPCAMX = cam;
}

static void __cdecl hkMapUpdate(int *a)         { ws_map_call(oMapUpdate, a, 0); }
static void __cdecl hkMapTrapdoorUpdate(int *a) { ws_map_call(oMapTrapdoorUpdate, a, g_wsDelta / 4); }

static void __cdecl hkMapfUpdate(int *a)
{
    short kind;
    int shift = 2 * (*G_MAPCAMX - ws_map_scroll_target()), k = g_wsDelta / 4;
    oMapfUpdate(a);
    kind = *(short *)(a + 0x18);              /* kinds 1..3 are re-placed from the camera */
    if (kind == 1)                            /* sun rays: screen 50 + k, with the trapdoor */
        a[0x21] += k - shift;
    else if (kind >= 2)                       /* Ed and his light: screen W - 40 - k */
        a[0x21] += g_wsDelta - k - shift;
}

/* ---- demo playback ---------------------------------------------------------------------------
   Demos replay recorded input, so objects that spawn or despawn at different moments than in the
   original (the wider screen reaches them earlier) make the recording desync. While a demo is loaded
   (title screen and attract demos, demo data pointer 0x464628) the camera and drawing stay
   widescreen, but the game logic that reads the camera sees the one the original 320-wide game would
   have (ws_demo_cam), with the original window widths (kWsDemoSites):
     Objects_SpawnNearCamera, Actors_UpdateAll   the planes' scroll is swapped for the call
                                                 (ws_demo_enter / ws_demo_leave)
     Actors_DrawAndCull   its despawn test reads the camera through the ws_cull_scroll stub
   Player_Update, which sets the camera, keeps the wide one. */
#define G_DEMODATA             ((void *volatile *)0x00464628u)
#define G_CURSECTION           ((volatile int *)0x004645C4u)
#define G_PLAYERX              ((volatile int *)0x0059F23Cu)
#define G_PLAYERSTATE          ((volatile unsigned *)0x0049C4C4u)
#define FN_LEVEL_LOADWORLD     0x0040EB80u
#define FN_LEVEL_START         0x0040F670u
#define FN_ACTORS_DRAWANDCULL  0x0040F040u
#define FN_OBJECTS_SPAWN       0x00412450u
#define FN_ACTORS_UPDATEALL    0x0040EE50u

int g_demoCull;            /* Actors_DrawAndCull running in demo mode: the stub reads g_demoCamOrig */
int g_demoCamOrig;         /* original-game camera x for that pass */
static const BYTE *g_demoCamPlane;
static int  g_demoCamLast, g_demoScrollLast;
/* Wide scroll -> original-camera scroll pairs. Planes keep their scroll across level loads, so at a
   level start the parallax planes still hold values from the previous level, computed from the wide
   camera; these pairs turn them back into what the original had (kept across loads on purpose). */
static int  g_demoPlaneX[MAX_PLANES];     /* parallax plane scroll from the original camera */
static int  g_demoPlaneWide[MAX_PLANES];  /* ... and the wide value written at the same time */
static BYTE g_demoPlaneSet[MAX_PLANES];
static int  g_demoPrevWide, g_demoPrevLogic, g_demoPrevSet;   /* previous level's last camera */

/* Camera x of the original game. When the wide camera is the one Player_Update sets from the player
   (x - W/2 clamped, or centred on narrow planes), the original is x - 160 clamped to the original
   limit. Otherwise other code placed it (level start, trapdoors, title pan, forced scrolling): the
   original camera then moves by the same amount as the wide one, or starts (W-320)/2 further right
   (those placements use x - W/2 where the original used x - 160). */
static int ws_demo_cam(const BYTE *cur)
{
    int scroll = *(const int *)(cur + 4), cam, maxW = ws_max_scroll(cur), follow;
    follow = *G_PLAYERX - g_wsWidth / 2;
    if (maxW < 0) follow = maxW / 2;
    else if (follow < 0) follow = 0;
    else if (follow > maxW) follow = maxW;
    if (!(*G_PLAYERSTATE & 0xA0) && scroll == follow) {
        int maxO = maxW + g_wsWidth - 320;
        cam = *G_PLAYERX - 160;
        if (cam < 0) cam = 0;
        else if (cam > maxO) cam = maxO;
    } else if (g_demoCamPlane == cur) {
        cam = g_demoCamLast + scroll - g_demoScrollLast;
    } else {
        cam = scroll + (g_wsWidth - 320) / 2;      /* e.g. the title pan, started (W-320)/2 earlier */
    }
    g_demoCamPlane = cur;
    g_demoCamLast = cam;
    g_demoScrollLast = scroll;
    return cam;
}

static void ws_demo_reset(void)
{
    if (g_demoCamPlane) {
        g_demoPrevWide = g_demoScrollLast;
        g_demoPrevLogic = g_demoCamLast;
        g_demoPrevSet = 1;
    }
    g_demoCamPlane = NULL;
}

static void (__cdecl *oActorsDrawAndCull)(void);
static void (__cdecl *oObjectsSpawn)(void);
static void (__cdecl *oActorsUpdateAll)(void);
static void (__cdecl *oLevelStart)(void);
static int  (__cdecl *oLevelLoadWorld)(int);
static void ws_set_demo_mode(int on);

/* Swap the active planes' scroll to the original camera for a logic call: the current plane gets
   ws_demo_cam, the others the value it gives for the scroll they hold (g_demoPlaneX pairs, the
   previous level's camera, or the current camera when they share it); unknown values stay. On the
   way back each plane keeps whatever the game changed meanwhile (forced scrolling, the airship's
   wrap-around), on top of its wide value. */
static int  g_demoInside, g_demoFirst, g_demoLast;
static int  g_demoSaved[MAX_PLANES], g_demoSwapVal[MAX_PLANES];

static int ws_demo_enter(void)
{
    BYTE *cur = (BYTE *)*G_PCURPLANE;
    int i, cam, curWide;
    if (!g_demo || !cur || g_demoInside || *G_CURSECTION == 23) return 0;   /* title sets its camera itself */
    g_demoFirst = *(volatile short *)0x004699FCu;                            /* g_FirstPlane */
    g_demoLast = *(volatile int *)0x0046A844u;                               /* g_LastPlane */
    if (g_demoFirst < 0) g_demoFirst = 0;
    if (g_demoLast >= MAX_PLANES) g_demoLast = MAX_PLANES - 1;
    cam = ws_demo_cam(cur);
    curWide = *(int *)(cur + 4);
    for (i = g_demoFirst; i <= g_demoLast; i++) {
        BYTE *p = (BYTE *)(G_PLANES + i * PLANE_STRIDE);
        int w = *(int *)(p + 4), v = w;
        if (p == cur)                                          v = cam;
        else if (g_demoPlaneSet[i] && w == g_demoPlaneWide[i]) v = g_demoPlaneX[i];
        else if (g_demoPrevSet && w == g_demoPrevWide)         v = g_demoPrevLogic;
        else if (w == curWide)                                 v = cam;
        g_demoSaved[i] = w;
        g_demoSwapVal[i] = v;
        *(int *)(p + 4) = v;
    }
    g_demoInside = 1;
    return 1;
}

static void ws_demo_leave(void)
{
    int i;
    for (i = g_demoLast; i >= g_demoFirst; i--) {
        int *sx = (int *)(G_PLANES + i * PLANE_STRIDE + 4);
        *sx = g_demoSaved[i] + (*sx - g_demoSwapVal[i]);
    }
    g_demoInside = 0;
}

/* DemoTrace (testing aid): spawn / despawn / player trace in LOADER.LOG, to compare a demo between
   Width=320 (original) and widescreen runs. */
#define G_TICK                 ((volatile unsigned *)0x0052C564u)
#define G_PLAYERY              ((volatile int *)0x0059F240u)
#define FN_OBJECT_TRYSPAWN     0x004126A0u
#define FN_ACTOR_DESPAWN       0x00412B80u
#define FN_ACTOR_KILL          0x00412C50u
static int *(__cdecl *oTrySpawn)(BYTE *);
static void (__cdecl *oActorDespawn)(BYTE *);
static void (__cdecl *oActorKill)(BYTE *);

static int *__cdecl hkTrySpawn(BYTE *rec)
{
    int *a = oTrySpawn(rec);
    if (a) log_msg("T%u spawn %d,%d pl%d", *G_TICK, *(short *)(rec + 0xC), *(short *)(rec + 0xE),
                   *(short *)((BYTE *)a + 0x5E));
    return a;
}

static void __cdecl hkActorDespawn(BYTE *a)
{
    log_msg("T%u despawn %d,%d pl%d", *G_TICK, *(int *)(a + 0x3C), *(int *)(a + 0x40), *(short *)(a + 0x5E));
    oActorDespawn(a);
}

static void __cdecl hkActorKill(BYTE *a)
{
    log_msg("T%u kill %d,%d pl%d", *G_TICK, *(int *)(a + 0x3C), *(int *)(a + 0x40), *(short *)(a + 0x5E));
    oActorKill(a);
}

static void __cdecl hkObjectsSpawn(void)
{
    BYTE *cur = (BYTE *)*G_PCURPLANE;
    int i;
    if (cfg.dbgTrace && cur && *G_TICK % 30 == 0)
        log_msg("T%u player %d,%d pl%d state %X scroll %d,%d", *G_TICK, *G_PLAYERX, *G_PLAYERY,
                (int)((cur - (BYTE *)G_PLANES) / PLANE_STRIDE), *G_PLAYERSTATE, *(int *)(cur + 4), *(int *)(cur + 8));
    if (cfg.dbgTrace && cur && *G_TICK == 0) {
        int first = *(volatile short *)0x004699FCu, last = *(volatile int *)0x0046A844u;
        for (i = first; i <= last && i < MAX_PLANES; i++) {
            const BYTE *p = (const BYTE *)(G_PLANES + i * PLANE_STRIDE);
            log_msg("T0 plane %d scroll %d,%d max %d rot %d%s", i, *(const int *)(p + 4), *(const int *)(p + 8),
                    *(const short *)(p + 0x30), *(const int *)(p + 0x3C), p == cur ? " (cur)" : "");
        }
    }
    i = ws_demo_enter();
    oObjectsSpawn();
    if (i) ws_demo_leave();
}

static void __cdecl hkActorsUpdateAll(void)
{
    int swapped = ws_demo_enter();
    oActorsUpdateAll();
    if (swapped) ws_demo_leave();
}

/* Trapdoor_Init places the camera on the player (start, checkpoint, exit replay). It runs with the
   real wide camera even inside a swap, so it sets the wide camera the normal way; the original
   camera is then derived again from it. */
#define FN_TRAPDOOR_INIT       0x0042A820u
static void (__cdecl *oTrapdoorInit)(void *, void *);

static void __cdecl hkTrapdoorInit(void *a, void *rec)
{
    int inside = g_demoInside;
    if (inside) ws_demo_leave();
    oTrapdoorInit(a, rec);
    if (inside) ws_demo_enter();
}

static void __cdecl hkActorsDrawAndCull(void)
{
    const BYTE *cur = (const BYTE *)*G_PCURPLANE;
    if (g_demo && cur) {
        g_demoCamOrig = ws_demo_cam(cur);
        g_demoCull = 1;
    }
    oActorsDrawAndCull();
    g_demoCull = 0;
}

static void ws_layers_reset(void);
static void __cdecl hkLevelStart(void) { ws_demo_reset(); ws_layers_reset(); oLevelStart(); }


static int __cdecl hkLevelLoadWorld(int a)
{
    ws_demo_reset();
    ws_layers_reset();
    if (cfg.dbgTrace) log_msg("T%u LoadWorld section %d demo %d", *G_TICK, *G_CURSECTION, *G_DEMODATA != NULL);
    ws_set_demo_mode(*G_DEMODATA != NULL);         /* before the planes are set up */
    return oLevelLoadWorld(a);
}

/* Narrow areas: the camera clamp centres planes narrower than W (scroll = maxScroll/2 < 0) where the
   original had scroll 0. ParallaxPlane_ApplyCamera sets a parallax plane's scroll to
   camX * factor/100 + offset, so layers with factor < 100 lag behind that centring. Their offset
   (+0x82) is shifted for the call by the part of the centring the factor leaves out, so they line up
   as with scroll 0 in the original. */
#define FN_PARALLAX_APPLY_CAMERA 0x0042B7E0u
static void (__cdecl *oParallaxApplyCamera)(BYTE *);

/* Which background layers the game repeats on purpose (ws_layer_mirror mirrors only the others): a
   parallax layer whose scroll can leave the part of its map a 320x240 screen shows (the original
   camera's range times the factor, plus the offset), or whose offset moves (drifting clouds), was
   seen repeated in the original. Bit 1 = across, 2 = down; cleared when a level loads. */
BYTE  g_wsLayerRepeat[MAX_PLANES];
static BYTE g_wsLayerSeen[MAX_PLANES];
static short g_wsLayerOff[MAX_PLANES][2];

static void ws_layers_reset(void)
{
    memset(g_wsLayerRepeat, 0, sizeof(g_wsLayerRepeat));
    memset(g_wsLayerSeen, 0, sizeof(g_wsLayerSeen));
}

static int ws_axis_repeats(int camMax, int factor, int off, int mapPx, int view)
{
    int a = off, b = off + camMax * factor / 100;
    return (a < b ? a : b) < 0 || (a > b ? a : b) + view > mapPx;
}

static void ws_layer_note(const BYTE *a, int plane, const BYTE *cur)
{
    const BYTE *l = (const BYTE *)(G_PLANES + plane * PLANE_STRIDE);
    short ox = *(const short *)(a + 0x82), oy = *(const short *)(a + 0x8E);
    int cw = *(const int *)(cur + 0x10) * 16, ch = *(const int *)(cur + 0x14) * 16, r = 0;
    if (g_wsLayerSeen[plane]) {
        if (ox != g_wsLayerOff[plane][0]) r |= 1;
        if (oy != g_wsLayerOff[plane][1]) r |= 2;
    }
    g_wsLayerSeen[plane] = 1;
    g_wsLayerOff[plane][0] = ox;
    g_wsLayerOff[plane][1] = oy;
    if (ws_axis_repeats(cw > 320 ? cw - 320 : 0, *(const int *)(a + 0x88), ox, *(const int *)(l + 0x10) * 16, 320))
        r |= 1;
    if (ws_axis_repeats(ch > 240 ? ch - 240 : 0, *(const int *)(a + 0x94), oy, *(const int *)(l + 0x14) * 16, 240))
        r |= 2;
    if (r & ~g_wsLayerRepeat[plane])
        log_msg("widescreen: background plane %d repeats%s%s, as in the original (factor %d/%d, offset %d/%d, "
                "camera range %d/%d, map %dx%d px)", plane, r & 1 ? " across" : "", r & 2 ? " down" : "",
                *(const int *)(a + 0x88), *(const int *)(a + 0x94), ox, oy, cw > 320 ? cw - 320 : 0,
                ch > 240 ? ch - 240 : 0, *(const int *)(l + 0x10) * 16, *(const int *)(l + 0x14) * 16);
    g_wsLayerRepeat[plane] |= (BYTE)r;
}

static void __cdecl hkParallaxApplyCamera(BYTE *a)
{
    const BYTE *cur = (const BYTE *)*G_PCURPLANE;
    short *off = (short *)(a + 0x82), save = *off;
    int plane = *(short *)(a + 0x5E);
    if (cur && ws_max_scroll(cur) < 0) {
        int s = *(const int *)(cur + 4);
        *off = (short)(save + s - s * *(const int *)(a + 0x88) / 100);
    }
    oParallaxApplyCamera(a);
    *off = save;
    if (cur && plane >= 0 && plane < MAX_PLANES && (BYTE *)(G_PLANES + plane * PLANE_STRIDE) != cur)
        ws_layer_note(a, plane, cur);
    /* demo: the scroll the original camera gives (same formula), for Objects_SpawnNearCamera */
    if (g_demoCull && plane >= 0 && plane < MAX_PLANES && (BYTE *)(G_PLANES + plane * PLANE_STRIDE) != cur) {
        g_demoPlaneX[plane] = g_demoCamOrig * *(const int *)(a + 0x88) / 100 + save;
        g_demoPlaneWide[plane] = *(const int *)(G_PLANES + plane * PLANE_STRIDE + 4);
        g_demoPlaneSet[plane] = 1;
    }
}

/* Same for screen-space parallax sprites: ParallaxSprite_Draw places them at
   base x (+0x8E) - planeScroll * factor (+0x94) / 100, so with factor 0 (the continue screen's lit
   floor patches) they ignore the centring. Their base is shifted for the call by the missing part.
   Screen-fixed backdrops (no repeat, factor 0, no auto-scroll, about a screen wide: the WEST and
   HAUNTED horizon pictures) only cover the 320 px they were drawn for and the layer behind shows
   through beside them. They get a mirrored copy on each side that has room, so the edge continues
   seamlessly. */
#define FN_PARALLAX_SPRITE_DRAW 0x0042D6B0u
#define ACTOR_MIRROR_X          0x8
static void (__cdecl *oParallaxSpriteDraw)(int *);
typedef void (__cdecl *ActorDraw_t)(int *, int, unsigned short, int);

static void ws_backdrop_mirror(int *a, int axis2)   /* axis2 = 2 * mirror axis (screen px) */
{
    int x = a[0xF];
    a[0xF] = axis2 - 1 - x;                          /* pixel p -> axis2-1-p, see Actor_Draw */
    a[0x16] ^= ACTOR_MIRROR_X;
    ((ActorDraw_t)FN_ACTOR_DRAW)(a, 1000, (unsigned short)(a[0x2A] / 10), a[0x20]);
    a[0x16] ^= ACTOR_MIRROR_X;
    a[0xF] = x;
}

static void __cdecl hkParallaxSpriteDraw(int *a)
{
    const BYTE *cur = (const BYTE *)*G_PCURPLANE;
    short *base = (short *)((BYTE *)a + 0x8E), save = *base;
    int shift = 0;
    if (cur && ws_max_scroll(cur) < 0) {
        int s = *(const int *)(cur + 4);
        shift = -s * (100 - a[0x25]) / 100;
        *base = (short)(save + shift);
    }
    oParallaxSpriteDraw(a);
    *base = save;

    /* +0x88 repeat x, +0x98 repeat y, +0x94 factor x, +0x90 speed x, +0xB8/+0xBC frame left/right
       (Actor_GetFrameBounds) */
    if (!a[0x22] && !a[0x26] && !a[0x25] && !a[0x24] && a[0x2F] - a[0x2E] >= 288) {
        /* mirror at the frame edge, or at the 320-px screen edge the picture was drawn for when the
           frame reaches past it (some frames have a few transparent columns there) */
        int left = a[0xF] + a[0x2E], right = a[0xF] + a[0x2F];   /* right is exclusive */
        if (right > shift + 320) right = shift + 320;
        if (left < shift)        left = shift;
        if (right < g_wsWidth) ws_backdrop_mirror(a, 2 * right);
        if (left > 0)          ws_backdrop_mirror(a, 2 * left);
    }
}

/* Narrow maps without black bars (WS_REPEAT_TILES: the 26-tile Evil Ed arena) show their
   tile map repeated on both sides, but collision ended at the map: the player, kept only within the
   screen (16 px from each edge), walked off the floor at Width 512 (48 px repeated on each side). The
   collision code already has a horizontal wrap, unused by the game: with bit 0x20000000 of the player
   state flags, Actor_CheckWallCollision and Plane_FindGroundY read the collision map at x modulo the map
   width. The bit is set for the duration of those calls on such maps, so the repeated floor is solid
   like what is drawn. Actor_CheckWallCollision also stops actors at the map's right end (maxScrollX + W,
   0x413106); that limit is lifted during the call, leaving the screen clamp. Off while a demo plays. */
#define FN_ACTOR_WALL_COLLISION 0x00412F50u   /* int (Actor *, int) */
#define FN_PLANE_FIND_GROUND_Y  0x00413B90u   /* int (Plane *, int x, int y, int rows) */
#define G_PLAYERSTATEFLAGS      ((volatile DWORD *)0x0049C4C4u)
#define PS_WRAP_X               0x20000000u
static int (__cdecl *oWallCollision)(int *, int);
static int (__cdecl *oFindGroundY)(BYTE *, int, int, int);

static int ws_wraps(const BYTE *plane)
{
    int maxScroll;
    if (!g_wsWidth || g_demo || !plane) return 0;
    maxScroll = ws_max_scroll(plane);
    return maxScroll < 0 && !ws_narrowed(plane, g_wsWidth);
}

/* The narrowed view (see ws_narrowed in lomax.h): the camera centres the plane (scroll =
   maxScroll/2), and the present covers the wrapped tiles beside it with black bars. */
void ws_bars(int *l, int *r)
{
    const BYTE *plane = (const BYTE *)*G_PCURPLANE;
    *l = *r = 0;
    if (g_wsWidth && plane && ws_narrowed(plane, g_wsWidth)) {
        int scroll = *(const int *)(plane + 4);
        *l = -scroll;                                  /* plane width = maxScroll + W */
        *r = scroll - ws_max_scroll(plane);
        if (*l < 0) *l = 0;
        if (*r < 0) *r = 0;
    }
}

/* Inside the bars the screen is the plane's own width, so what keeps to the screen's edges keeps to
   the bars' edges: Player_Update's limit, 16 px from each side (0x4113C4 / 0x411401: the player's x
   is set to scroll + 16 or scroll + W - 16), and the HUD's edge counters (kWsHudEdges: the power
   icon and its ammo from the left, the lives and the 1UP's target from the right; cheat mode's FPS
   text at the bottom left, 0x404F4F, and world.level at the bottom right, 0x404F9C). Their immediates
   are rewritten by the bars' widths whenever those change, before Player_Update (the hook) and so
   before Hud_Draw. Bars of 0 put back what apply_widescreen wrote. Not during demos: their logic
   follows the original layout. */
#define FN_PLAYER_UPDATE        0x0040FB70u
static void (__cdecl *oPlayerUpdate)(void);
static int  g_insetL, g_insetR;

static void ws_write_value(DWORD addr, int v)
{
    size_t i;
    for (i = 0; i < sizeof(kWsPatches) / sizeof(kWsPatches[0]); i++)
        if (kWsPatches[i].addr == addr) {
            const WsPatch *p = &kWsPatches[i];
            BYTE *imm = (BYTE *)addr + p->immOff;
            DWORD old;
            VirtualProtect(imm, p->immSize, PAGE_EXECUTE_READWRITE, &old);
            if (p->immSize == 1)      *(signed char *)imm = (signed char)v;
            else if (p->immSize == 2) *(short *)imm = (short)v;
            else                      *(int *)imm = v;
            VirtualProtect(imm, p->immSize, old, &old);
            return;
        }
}

static void ws_inset_update(void)
{
    int l = 0, r = 0;
    size_t j;
    if (!g_demo) ws_bars(&l, &r);
    if (l > 127 - 16) l = 127 - 16;                    /* the left limit is an imm8 */
    if (l == g_insetL && r == g_insetR) return;
    g_insetL = l;
    g_insetR = r;
    ws_write_value(0x004113C4u, 16 + l);
    ws_write_value(0x004113C9u, 16 + l);
    ws_write_value(0x00411401u, g_wsWidth - 16 - r);
    ws_write_value(0x00411409u, g_wsWidth - 16 - r);
    ws_write_value(0x00404F4Fu, l);                    /* cheat mode's FPS and world.level texts */
    ws_write_value(0x00404F9Cu, g_wsWidth - 30 - r);
    for (j = 0; j < sizeof(kWsHudEdges) / sizeof(kWsHudEdges[0]); j++)
        ws_write_value(kWsHudEdges[j].addr, kWsHudEdges[j].num ? g_wsWidth + kWsHudEdges[j].add - r
                                                               : kWsHudEdges[j].add + l);
    FlushInstructionCache(GetCurrentProcess(), NULL, 0);
    log_msg("widescreen: narrowed view, bars %d / %d px", l, r);
}

static void __cdecl hkPlayerUpdate(void)
{
    ws_inset_update();
    oPlayerUpdate();
}

static int __cdecl hkFindGroundY(BYTE *plane, int x, int y, int rows)
{
    int r;
    if (!ws_wraps(plane) || (*G_PLAYERSTATEFLAGS & PS_WRAP_X)) return oFindGroundY(plane, x, y, rows);
    *G_PLAYERSTATEFLAGS |= PS_WRAP_X;
    r = oFindGroundY(plane, x, y, rows);
    *G_PLAYERSTATEFLAGS &= ~PS_WRAP_X;
    return r;
}

static int __cdecl hkWallCollision(int *a, int flag)
{
    BYTE *plane = (BYTE *)(G_PLANES + *(short *)((BYTE *)a + 0x5E) * PLANE_STRIDE);
    short maxScroll;
    int r;
    if (!ws_wraps(plane) || (*G_PLAYERSTATEFLAGS & PS_WRAP_X)) return oWallCollision(a, flag);
    maxScroll = *(short *)(plane + 0x30);
    *(short *)(plane + 0x30) = 0x4000;            /* map right end out of reach */
    *G_PLAYERSTATEFLAGS |= PS_WRAP_X;
    r = oWallCollision(a, flag);
    *G_PLAYERSTATEFLAGS &= ~PS_WRAP_X;
    *(short *)(plane + 0x30) = maxScroll;
    return r;
}

/* Write one site: width > 0 = the widescreen value, 0 = the original bytes. */
static void ws_write_site(const WsPatch *p, int width)
{
    unsigned char buf[64];
    BYTE *site = (BYTE *)p->addr, *imm = site + p->immOff;
    int v, n;
    DWORD old;
    if (!p->addr) return;
    n = hex_to_bytes(p->orig, buf, sizeof(buf));
    VirtualProtect(site, n, PAGE_EXECUTE_READWRITE, &old);
    if (!width) {
        memcpy(site, buf, n);
    } else {
        if (p->repl) {
            hex_to_bytes(p->repl, buf, sizeof(buf));
            memcpy(site, buf, n);
        }
        if (p->immSize == 0) {
            /* pure instruction rewrite, nothing width-dependent to fill in */
        } else if (p->den == 0) {
            float f = p->num > 0 ? (float)width / p->num : (float)-p->num / width;
            memcpy(imm, &f, 4);
        } else {
            if (p->den > 0) v = width * p->num / p->den + p->add;
            else            v = (width * p->num + (-p->den) - 1) / -p->den * -p->den + p->add;
            if (p->immSize == 1)      *(signed char *)imm = (signed char)v;
            else if (p->immSize == 2) *(short *)imm = (short)v;
            else                      *(int *)imm = v;
        }
    }
    VirtualProtect(site, n, old, &old);
}

static int apply_widescreen(int width)
{
    size_t i, count = sizeof(kWsPatches) / sizeof(kWsPatches[0]);
    unsigned char buf[64], rbuf[64];
    int bad = 0;

    /* verify everything first: all or nothing */
    for (i = 0; i < count; i++) {
        const WsPatch *p = &kWsPatches[i];
        int n = hex_to_bytes(p->orig, buf, sizeof(buf));
        if (!p->addr) continue;
        if (p->repl && hex_to_bytes(p->repl, rbuf, sizeof(rbuf)) != n) {
            log_msg("widescreen: site %08lX (%s) replacement length differs", (unsigned long)p->addr, p->what);
            bad++;
            continue;
        }
        if (n <= 0 || p->immOff + p->immSize > n || memcmp((void *)p->addr, buf, n)) {
            log_msg("widescreen: site %08lX (%s) does not match the expected bytes", (unsigned long)p->addr, p->what);
            bad++;
        }
    }
    for (i = 0; i < sizeof(kWsCalls) / sizeof(kWsCalls[0]); i++) {
        int n = hex_to_bytes(kWsCalls[i].orig, buf, sizeof(buf));
        if (n < 5 || memcmp((void *)kWsCalls[i].addr, buf, n)) {
            log_msg("widescreen: call site %08lX does not match the expected bytes", (unsigned long)kWsCalls[i].addr);
            bad++;
        }
    }
    if (bad) { log_msg("widescreen: %d mismatching site(s), nothing patched", bad); return 0; }

    g_wsDelta = width - 320;
    for (i = 0; i < sizeof(kWsCalls) / sizeof(kWsCalls[0]); i++) {
        BYTE *site = (BYTE *)kWsCalls[i].addr;
        int n = hex_to_bytes(kWsCalls[i].orig, buf, sizeof(buf));
        DWORD old;
        VirtualProtect(site, n, PAGE_EXECUTE_READWRITE, &old);
        site[0] = 0xE8;                                   /* call stub */
        *(DWORD *)(site + 1) = (DWORD)kWsCalls[i].stub - (DWORD)(site + 5);
        memset(site + 5, 0x90, n - 5);
        VirtualProtect(site, n, old, &old);
    }

    for (i = 0; i < count; i++) {
        WsPatch p = kWsPatches[i];
        size_t j;
        for (j = 0; j < sizeof(kWsHudEdges) / sizeof(kWsHudEdges[0]); j++)
            if (p.addr == kWsHudEdges[j].addr) {
                p.num = kWsHudEdges[j].num;
                p.den = 1;
                p.add = kWsHudEdges[j].add;
            }
        ws_write_site(&p, width);
    }
    FlushInstructionCache(GetCurrentProcess(), NULL, 0);
    log_msg("widescreen: %u sites patched for a %dx240 screen, power and lives counters at the screen edges",
            (unsigned)count, width);
    return 1;
}

/* Demo mode on: the logic sites (kWsDemoSites) get their original bytes back; off: the wide ones. */
static void ws_set_demo_mode(int on)
{
    size_t i, j;
    if (!g_wsWidth || on == g_demo) return;
    for (i = 0; i < sizeof(kWsPatches) / sizeof(kWsPatches[0]); i++)
        for (j = 0; j < sizeof(kWsDemoSites) / sizeof(kWsDemoSites[0]); j++)
            if (kWsPatches[i].addr == kWsDemoSites[j])
                ws_write_site(&kWsPatches[i], on ? 0 : g_wsWidth);
    FlushInstructionCache(GetCurrentProcess(), NULL, 0);
    g_wsDelta = on ? 0 : g_wsWidth - 320;          /* airship stubs: the camera is the original one */
    g_demo = on;
    log_msg("widescreen: demo playback %s (original logic camera)", on ? "starts" : "ends");
}

void ws_init(int width)
{
    if (width > 320 && apply_widescreen(width)) {
        g_wsWidth = width;
        mh_hook(FN_MAP_WORLDMAP_UPDATE, hkMapUpdate, (void **)&oMapUpdate, "Map_WorldMap_Update");
        mh_hook(FN_MAPT_TRAPDOOR_UPDATE, hkMapTrapdoorUpdate, (void **)&oMapTrapdoorUpdate,
                "MapT_MapTrapdoor_Update");
        mh_hook(FN_MAPF_PROP_UPDATE, hkMapfUpdate, (void **)&oMapfUpdate, "Mapf_MapParallaxProp_Update");
        mh_hook(FN_PARALLAX_APPLY_CAMERA, hkParallaxApplyCamera, (void **)&oParallaxApplyCamera,
                "ParallaxPlane_ApplyCamera");
        mh_hook(FN_PARALLAX_SPRITE_DRAW, hkParallaxSpriteDraw, (void **)&oParallaxSpriteDraw,
                "ParallaxSprite_Draw");
        mh_hook(FN_ACTOR_WALL_COLLISION, hkWallCollision, (void **)&oWallCollision, "Actor_CheckWallCollision");
        mh_hook(FN_PLANE_FIND_GROUND_Y, hkFindGroundY, (void **)&oFindGroundY, "Plane_FindGroundY");
        mh_hook(FN_PLAYER_UPDATE, hkPlayerUpdate, (void **)&oPlayerUpdate, "Player_Update");
    }
    /* no saved window size: 2x (after the widescreen patch, which checks the game's 320 default;
       ws_map_window_size widens it at the first surface init) */
    *G_WINCLIENTW = 640;
    *G_WINCLIENTH = 480;
    if (g_wsWidth || cfg.dbgTrace) {
        /* demo playback: spawning on the original layout (the hooks pass straight through without
           widescreen; with DemoTrace they also log) */
        mh_hook(FN_LEVEL_LOADWORLD, hkLevelLoadWorld, (void **)&oLevelLoadWorld, "Level_LoadWorld");
        mh_hook(FN_LEVEL_START, hkLevelStart, (void **)&oLevelStart, "Level_Start");
        mh_hook(FN_ACTORS_DRAWANDCULL, hkActorsDrawAndCull, (void **)&oActorsDrawAndCull, "Actors_DrawAndCull");
        mh_hook(FN_OBJECTS_SPAWN, hkObjectsSpawn, (void **)&oObjectsSpawn, "Objects_SpawnNearCamera");
        mh_hook(FN_ACTORS_UPDATEALL, hkActorsUpdateAll, (void **)&oActorsUpdateAll, "Actors_UpdateAll");
        mh_hook(FN_TRAPDOOR_INIT, hkTrapdoorInit, (void **)&oTrapdoorInit, "Trapdoor_Init");
    }
    if (cfg.dbgTrace) {
        mh_hook(FN_OBJECT_TRYSPAWN, hkTrySpawn, (void **)&oTrySpawn, "Object_TrySpawn");
        mh_hook(FN_ACTOR_DESPAWN, hkActorDespawn, (void **)&oActorDespawn, "Actor_Despawn");
        mh_hook(FN_ACTOR_KILL, hkActorKill, (void **)&oActorKill, "Actor_Kill");
    }
}
