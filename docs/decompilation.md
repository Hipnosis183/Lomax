# LOMAX.EXE — reverse-engineering notes

Lomax (Psygnosis, 1997). Win95 game built with MSVC and statically linked MFC 4.2, using DirectDraw, Direct3D (optional), DirectInput, DirectSound, and MCI CD audio.
Image base is 0x400000. Game code runs from 0x401000 to 0x4388c0; MFC and the CRT run from 0x4388ca to 0x458dff.
All names below are applied in the Ghidra project. Important functions have plate comments with more detail.

## Ghidra project state

- **Functions:** all 1,027 game-range functions are named; none is left as `FUN_`.
  - 202 of them are compiler-generated C++ exception stubs: `_EHHandler` / `_UnwindN`, see "MFC classes and compiler-generated code".
  - About 350 game functions are reached only through vtables or callback tables, so Ghidra's auto-analysis doesn't find them; the project defines them explicitly.
- **Code gaps:** none in game code except padding, the 12 switch tables (typed as data) and 7 unreachable bytes after `CD_PlayTrack` (a duplicate `return 0` epilogue).
- **Globals:** every global that game code touches has a name. The appendix lists 262 of them by subsystem.
- **Library range:** the 1,319 functions and 328 globals of the linked-in C runtime, CTL3D and MFC are all named too; see [library.md](library.md). The whole program (2,346 functions) has no `FUN_`/`DAT_` name and no duplicate function name.
- **Structs:** `Actor`, `Plane`, `SoundDef`, `AmbientDef`, `LevelDef`, `SfxEntry`, `SpriteFrame` and `SpriteQueueEntry`, plus the DirectX 3 types `D3DDEVICEDESC`, `D3DPRIMCAPS`, `D3DTLVERTEX`, `D3DEXECUTEBUFFERDESC`, `D3DEXECUTEDATA`, `D3DMATERIAL` and `DDBLTFX`.
- **Shared actor typing:** the `Actor` struct is applied to `g_Actors[98]` and `g_Player`.

## Boot and main loop

| Address | Name | What it does |
|---|---|---|
| 0x402f10 | CLomaxApp_InitInstance | Reads the registry `HKCU\Software\Psygnosis\Lomax` (System / Controls / Settings) and parses `/FULLSCREEN /SYSTEMRAM /LOGGING`. It then creates the window, calls DirectX_Init and DDraw_InitSurfaces, checks the CD (CD_VerifyLomaxDisc), plays logo.avi and loads the SFX. Finally it runs the message loop. |
| 0x404400 | Lomax_WndProc | Handles menu commands (0x8008–0x8018), hotkeys F5–F8 (window/fullscreen and 8/16-bit modes), Alt+Enter, and the debug keys. |
| 0x404aa0 | Game_Frame | Runs on every idle loop iteration. It is a 60 Hz fixed step (`ms*10/167`, really 59.88 Hz, whole milliseconds), with at most 5 ticks per frame, presented without vsync (see `[Fixes] VSync`). |
| 0x403c30 | CLomaxApp_ExitInstance | Saves the registry and tears everything down. |

Each tick runs these functions in order:

1. Sound_UpdateLoopVolumes
2. Input_Update
3. Game_HandlePauseKey
4. Objects_SpawnNearCamera
5. Actors_UpdateAll
6. **Player_Update (0x40fb70)**
7. Game_UpdateFade
8. `++g_TickCounter`

Rendering then runs Planes_UpdateDepth, Actors_DrawAndCull and Planes_DrawTilemaps. These fill 256 priority buckets (`g_pSpriteQueue`), which are Blt'ed or drawn as D3D quads. After that come the fade overlays (Render_DissolveFade / Render_ScreenFade) and DDraw_Present (0x4059c0).

The internal resolution is hard-coded at **320x240** (constants 0x140/0xF0 throughout). Only 8 and 16 bpp are supported.

## Input

- **Keyboard state:** `g_DIKeyState[0x110]` holds the DIK scancodes. Bytes 0x100–0x10C are joystick pseudo-keys. Key names are in `g_KeyNameTable`.
- **Key bindings:** `g_KeyBinds[13]` holds them in this order: Left, Right, Up, Down, Jump, Shoot, Ability, Select, Run, Helm, LookDown, LookUp, Pause.
- **Button masks:** `g_InputHeld` and `g_InputPressed` are what gameplay reads. Bits: Left 0x8000, Right 0x2000, Up 0x1000, Down 0x4000, Jump 0x40, Shoot 0x20, Ability 0x80, Select 0x100, Run 0x08, Helm 0x10, LookDown 0x01, LookUp 0x04, Pause 0x800.
- **Control schemes:** the three PS1-style schemes are data. A 3 × 4 table at 0x464588 gives the {Jump, Helm, Ability, DropHelmet} button masks, and `g_ControlScheme` (0x464580) picks the row. Level_Start copies that row into the live masks; demos always use scheme 0.
- **Demo playback:** Input_Update replays an RLE stream of (buttons, count) pairs; 0x7fff ends it. Attract demos cycle rows 0–6 of the demo table at 0x466410 (`g_DemoIndex`).
- **The ending is a demo:** Evil Ed phase 2 sets `g_DemoIndex` = 7 and turns demo mode on, so the ending plays through the demo system (row 7, CD track 0x15).
- **Cheats:** hold **Tab + Backspace + Left Ctrl** to set `g_bCheatsEnabled`. With cheats on:
  - F1 / F2 form a level select. F1 (`Debug_SelectNextWorld`, 0x417DF0) steps `g_CurSection` through 0–21. F2 (`Debug_SelectNextSubLevel`, 0x417E30) steps `g_CurSubLevel` through the world's sub-levels. Nothing changes on screen until F3.
  - F3 loads the selected level: `Game_SetupNextLevel` then `Level_Start`. With F1/F2 untouched, that reloads the current level.
  - **I** toggles the FPS / level overlay.
  - Ability + a direction toggles fly / no-clip mode (player state 0x20000).
  - Holding Up while Evil Ed spawns skips to his phase 2.

## Actors

- **Actor pool:** `g_Actors[98]` at 0x46abc0, with 0x120 bytes per slot. Active slots are tracked by the bitmasks `g_ActorMask0/1/2` (33 slots each).
- **Player:** `g_Player` at 0x59f200 is the same `Actor` struct, but static. Its state bits are in `g_PlayerState` (0x49c4c4).
- **Allocation:** Actor_Alloc (0x412800) and Actor_AllocHigh.
- **Removal:** Actor_Despawn (0x412b80) lets the object respawn; Actor_Kill (0x412c50) removes it permanently.

Key `Actor` fields:

| Off | Field | Notes |
|---|---|---|
| +0x00 | pObjType | Object type record (from ObjType_FindByName). Its +0x16 holds the type id. |
| +0x04 | pPlacement | Level placement record. Its +0x14 is persistent state (boss hits, dig progress; setting it to 0 stops respawn). +0x1A..+0x28 are per-type params. |
| +0x08 | pAnim | Current animation. Hitbox is at +0x10..+0x16 and attack box at +0x18..+0x1E. |
| +0x18 / +0x1C | pfnUpdate / pfnDraw | Per-tick callbacks. |
| +0x20..+0x2C | pfnHotspot0-3 | Sprite hotspot callbacks (gun muzzle, smoke...). |
| +0x3C / +0x40 | nX / nY | Integer pixel position, copied each tick from the fixed-point position. |
| +0x44 / +0x48 | nXFixed / nYFixed | 16.16 position. |
| +0x4C / +0x50 | nVx / nVy | 16.16 velocity. |
| +0x58 | dwFlags | 0x8 facing left, 0x20 hurts player, 0x10000 hittable, 0x20000 was hit, 0x80000 interacts with water, 0x100000 remove, 0x800000 depth target, 0x4000000 always update (even when paused). |
| +0x5E | nPlane | Depth plane. |
| +0x60 | nDrawLayer | Draw layer. |
| +0x62 | nCullPhase | Which of the 3 cull ticks checks this actor. `Object_TrySpawn` copies the placement's spawn phase (0–3); `Actors_DrawAndCull` despawns off-screen actors only when this equals its rotating 0–2 counter, so **3 = never culled** (bosses, HUD, text, effects set 3). |
| +0x6A | nAnimPos | Animation position; frame = value >> 8. |
| +0x70 | wHiddenSlotMask | Bit n hides sprite parts with slot n. |
| +0x74 | nDrawOnBackPlane | Non-zero: keep drawing while a rotating plane faces away (HUD, timer, text, water, plane doors/ladders). |
| +0x78 | dwBlinkTimer | Invincibility blink. Non-zero counts up each draw; bits 0x1000/0x2000 make the sprite skip alternate frames. The player gets 0x2000 while `g_PlayerHurtTimer` < 0. |
| +0x7C | nGroundY | 20000 means no ground. Also the bottom clip line for drawing (sprites sink into the ground/water). |
| +0x7E | nClipTopY | Top clip line for drawing. |
| +0x80.. | nVar80... | Per-object scratch fields. |

### Object types

Objects are spawned from placement records in a level.

1. Object_TrySpawn (0x4126a0) reads the type id from `pObjType+0x16`.
2. It calls `g_ObjectSpawnFuncs[id](Actor*, Placement*)`, a table of 89 entries at 0x4647b8.
3. The id comes from the first 4 characters of the type name, matched against `g_ObjectTypeTags` (0x464650). The tags are stored byte-reversed, e.g. "ratS" = `Star`.

The id list is in the plate comments on Object_TrySpawn and on the table.

| Ids | Objects |
|---|---|
| 0, 1 | Start and exit trapdoors |
| 2 | Level exit |
| 3 | Checkpoint |
| 5 | Evil Ed boss |
| 7 | Airship boss |
| 13, 14 | Title screen |
| 17–36, 43–47 | Hazards and enemies: Cowboy 25, Dracula 26, Pirate 29, Werewolf 30, Thief 44, … |
| 37 | Music trigger |
| 38–42 | Map and continue screens |
| 48–64 | Environment objects: trees, boats, rope, updrafts, fish… |
| 67–88 | Rope, zombies, clay pot, trigger, parallax |

## Player (0x40fb70 Player_Update)

Player_Update is a single large state machine. Related functions are Player_TakeHit (0x412e30), Player_BounceOffEnemy (0x412d90) and Player_ThrowHelmet (0x42f5f0).

Physics values are 16.16 fixed point, per tick:

| Constant | Value |
|---|---|
| Walk speed | 3.0 px/tick |
| Run speed | 4.0 px/tick |
| Acceleration | 0x2000 |
| Gravity | +0x6000; maximum fall speed 6 px/tick |
| Helicopter glide | +0x2000; maximum 1 px/tick |
| Jump | vy = -4.0, plus a hold force `g_JumpHoldForce` of 0x9800 that decays by 0x800 per tick |
| Bounce pad | vy = -0x60000 |

Helmet and powers:

- **Hits:** taking a hit while wearing the helmet (`g_bHasHelmet`) only knocks the helmet off. Without it, the player dies and `g_PlayerDeathTimer` runs for 300 ticks.
- **Choosing a power:** the power wheel (PowerWheel_*) sets `g_CurrentPower`, a value from 0 to 5. Each power has its own counter, decremented when Ability is pressed:
  - 0 is the grapple (`Power_FireGrappleHelmet`).
  - 1 is the helicopter.
  - 2 is the bomb helmet (state 0x800, `Power_ThrowBombHelmet`).
  - 3 is the digger (`g_DigPowerCharges`).
  - 4 is the platform builder (`Power_SpawnPlatform`).
  - 5 is the fire helmet (state 0x40000; the thrown helmet burns).
- **Ammo refill:** set by `g_PowerAmmoRefill` = {10, 5, 10, 5, 5, 5}. The HUD and pickups find the counters through `g_PowerAmmoPtrsHud` / `g_PowerAmmoPtrsPickup`.
- **Counters:**
  - `g_Lives` (0x52c56c) has a cap of 30.
  - `g_Continues`: lives and continues both start at 3.
  - `g_Coins` (0x498f2c): 100 coins give a 1UP.
  - `g_LemmingsFreed`: 50 freed lemmings open the bonus trapdoor.

Movement details:

- **Moving platforms:** Actors_UpdateAll saves `g_PlayerTickStartY` and `g_PlayerNextFeetY` each tick. Each moving platform under the player updates `g_PlatformTopY` and claims `g_pRidingPlatform`, and a solid block pushed from the side claims `g_pPushBlock`. Last tick's values are kept in `g_PrevPlatformTopY` / `g_pPrevPushBlock`.
- **Water:** being in water is a timed escape. `g_WaterEscapeTimer` starts at 120 ticks, warnings sound at 100/60/20, and the player drowns at 0.
- **Spin:** pressing Jump again in mid-air is the SPIN move (`g_bAirSpin`).
- **Dizzy idle:** sliding down a steep slope arms a 180-tick timer. Standing still before it runs out plays the 'DIZZ' animation 3 times.
- **Camera:** LookUp/LookDown move the camera target by `g_CameraLookOffsetY` (∓40 px).

## Levels and files

- **Level table:** `g_LevelDefs[24]` at 0x466450, 0x18 bytes per row. Rows 21, 22 and 23 are the bonus level, map and title. `g_WldFileNames` lists MAIN, LOGO, TITLE, MAP, BONUS, ED and the other WLD files.
- **Level state:** `g_CurSection`, `g_CurSubLevel` and `g_LoadedSection`. Game_SetupNextLevel picks where to go next from `g_bGoToTitle`, `g_bGoToMap`, `g_bDemoMode` and `g_bBonusTrapdoorOpen`, with `g_bSectionChangePending` set when the section changes.
- **Level flow:**
  - A level starts at its `Start` trapdoor (object 0, placement flag 0x4000; `Trapdoor_Init` 0x42A820). Lomax comes out at the trapdoor's y − 40 and drops; Lomax's levels hang it 72–98 px above the floor.
  - A sub-level ends at an `Exit` (object 2, `LevelExit_Update` 0x4276A0). It fires when Lomax is on the ground on its plane, within 48 px of its height, and past the map's right end − 50 px; it sets `g_bLevelExitReached` and fades out 30 ticks later.
  - `Level_Start` then advances `g_CurSubLevel` and reloads the world (`Level_LoadWorld`), and the next sub-level's Start trapdoor stays hidden while Lomax walks in from x = −100.
  - The last sub-level ends at a `Finish` trapdoor (object 1): Up within 32 px, standing up to 120 px below it. The "level complete" banner (`Insert_Show(4)`) counts up `g_LastRealSection` and goes to the map, which leaves for that section.
- **Level table references:** the code reaches `g_LevelDefs` only through 28 field addresses (0x466450–0x46645E) and indexes it by section. Sections are numbered only in `Game_SetupNextLevel` (title 0x17, bonus 0x15, map 0x16, sections from 0x18 reset to 0), the password check (section < 0x16, 5 bits kept) and the F1 level select (wraps at 0x16). So the table can be moved to a larger copy to add rows by rewriting those 28 field references.
- **Level timer quirk:** `g_LevelTimerSavedTicks` is read but never written, so the "restore saved timer" path always gets 0.
- **Game flow:** Game_ResetNewGame (0x40edc0), Game_SetupNextLevel (0x417bd0), Level_LoadWorld (0x40eb80), Level_InitPlanes and Level_Start (0x40f670).
- **WLD loader:** World_LoadWLD (0x4147e0) reads chunks in this order:
  - `WRLD` ("PS-X", 1234)
  - `NMY` (object types and animations)
  - `HEAD` (plane plus 10 palettes)
  - `TTAB` (16x16 tiles made from 8x8 cells)
  - `MAP` (tile map and collision bytes)
  - `GNMY` (object placements)
  - `ANIM` (sprites)
  - `CELL` (tile pixels)
  - `END`

  The full format is in the World_LoadWLD plate comment.
- **WLD details** (confirmed by writing WLD files that the game loads):
  - **Chunk sizes:** every size must be a multiple of 4. `File_ReadChunk` (0x416A70) advances its pointer by `size & ~3` but its byte count by `size`.
  - **TTAB:** stored big-endian. The `MAP` handler byte-swaps both the tile words and the plane's whole TTAB (`Plane_ByteSwapMapAndCheckEmpty`, 0x415430).
  - **HEAD layout:** `u32` width and `u32` height in 8 px units (BE); 10 palettes × 256 colours (LE); 0x50 bytes of editor data (`0xFF`); a 256-bit ANIM bitmap at +0x1458.
    - Colours are 15-bit with blue in bits 0–4 and red in bits 10–14. Colour 0 becomes the transparent key: white, with real white moved to 0x7FFE.
  - **Palettes:** only HEADs whose plane bit is in `activePlaneMask` load palettes, into the shared DIB blocks at 0x471B28. Every plane's tiles are built from the last ones loaded.
  - **Tile palette:** `Plane_BuildTileSurfaces` draws a 16×16 tile with one palette, the last non-zero cell's. All shipped tiles use palette 0.
  - **Cells and tiles:** cell 0 and tile 0 are empty.
  - **Collision byte:** one byte per map cell, with these bits:
    - `(b & 0xC0) == 0xC0` is a one-way floor: bits 4–5 are the shape, bit 3 mirrors it, bits 0–2 are the material.
    - `0x80` is a solid block: `0x08` blocks from below, `0x10` blocks moving left, `0x20` blocks moving right (`Actor_CheckWallCollision` 0x412F50).
  - **GNMY:** the first word of an entry is the entry's size in bytes.
- **Level rows:** `g_LevelDefs` rows say which planes of the world file a level uses.
  - `dwPlaneMask` selects planes by HEAD number. The loaded planes become `g_Planes[0..n-1]`, in file order.
  - `aSubLevelPlaneMask[k]` selects among those planes. Its bit *i* is the *i*-th plane of `dwPlaneMask`.
  - `g_WldFileNames` (0x466690) is MEDIEVAL, VILLAGE, HAUNTED, WEST, ED, BONUS, MAP, TITLE, LOGO, MAIN.
- **Plane order:** `Planes_UpdateDepth` gives the shown planes buckets 0x90, 0x40, 0x20, 0x10 (table 0x464B28), in order. The sprite queue draws bucket 0 first, so the first plane is in front.
  - The bucket also gates sound: `Sound_PlayEffect` (0x40D5F0) plays a positional sound only when its source's plane bucket is 0x4D..0xB0, at volume × min(bucket, 0x90) / 0x90. A play plane that isn't first (bucket 0x40) is silent.
  - Actors go into bucket plane depth + `{3,4,8,10,12,14,6,2}[nDrawLayer]` (0x464B48; the player uses layer 6).
  - The player starts on the plane holding the `Start` object (Level_InitPlanes, `g_StartPlane` 0x52C540).
- **Background scroll:** a plane that isn't the player's moves only through a `ScrollMap` object (type 87, `ParallaxPlane_Spawn` 0x42B670) placed on it.
  - Its 7 parameters are: X auto-scroll, X factor %, Y auto-scroll, Y factor %, X offset, Y offset, and a section/sub-level id. The id is used only when the placement's checkbox bit is set.
  - The `Parallax` objects are screen-space backdrop sprites, not planes.
- **Object spawning:** `Objects_SpawnNearCamera` (0x412450) spawns a placement only when it is inside the camera window plus a margin (x 80/160/340/340 and y 80/110/140/140 px, by the record's spawn phase) and outside the screen plus 40 px. So objects appear from just outside the screen, and never directly on it. A teleport skips everything already in view.
- **Type names:** the game copies an NMY type name into a 0x20-byte block at +4, then writes the type's fields from +0x14 (+0x16 behaviour, +0x18 parameter count, +0x1A checkbox count). Characters past 15 are overwritten, and past 27 they spill into the next allocation.
- **Moving platform** ("Mega", object 64, `MovingPlatform_Spawn` 0x429A90): the moving platform object.
  - Fields: base x/y at +0x98/+0x9C (the sine path is around them; the elevator mode returns up to +0x9C), +0xA8 elevator mode.
  - Landing (`Platform_MoveAndCarryPlayer` 0x413510): Lomax's x strictly inside the 'MAIN' hitbox x1..x2, his feet at the tick's start at or above the platform's previous top and the new top at or above his next feet. So he can't walk onto a platform whose top is above the floor he walks on.
  - Params: X amplitude, Y amplitude, X phase speed, Y phase speed, and the spacing of the 'PART' chain drawn down to the screen bottom.
  - Flags: 1 = not always moving; 2 = moves after being stood on, or returns up when combined with 4; 4 = sinks while stood on, at Y speed × 0x28F per tick; 8 = bobs.
  - Position: base + amplitude·sin(phase)/2, with the phase advancing by the speed each tick (1024 per turn).
  - The standing surface is the 'MAIN' record's hitbox x1..x2 at y + y1 (`Platform_MoveAndCarryPlayer` 0x413510; `g_pRidingPlatform` 0x59C744).
- **Rotating platforms** ("Rota", object 21, `Rota_RotatingPlatforms_Init` 0x41F180): a wheel of platforms.
  - Params: radius in px, platform count, speed, and start phase. The phase advances by speed × 16 each tick, and angle = phase / 16, out of 1024.
  - Flag 2: it turns only while ridden, ramping by 1 phase unit per tick.
  - The platforms are child actors with the 'TREE' record. The spokes are drawn from 'RING' and 'BLAD' (two frames) by `Draw_RopeSegments`.
- **Swing** ("Swin", object 46, `Swin_Swing_Init` 0x425780): a pendulum hanging from (x, y − length).
  - Params: length, speed, swing strength, arms, pre-run.
  - Each tick (`Swin_Swing_Update` 0x4259B0) the angular speed changes by the strength. The strength's sign flips once |speed| passes the speed param.
  - The phase adds the speed: 16 units per step, 1024 steps per turn. Pre-run runs that many ticks at spawn.
  - Records: 'TREE' is the platform (the actor itself); 'RING' and 'BLAD' (two frames) are the rope.
  - With arms ≥ 1 the platform tilts with the angle (`Swin_Swing_GetSurfaceHeight`) and two ropes are drawn.
  - The first landing pushes the swing by the player's speed.
- **Ropes and spokes** (`Draw_RopeSegments` 0x425D40, used by the swing "Swin" 46 and the wheel "Rota" 21): a 'RING' sprite every 8 px along the rope, plus 'BLAD' leaves (two frames) at random 20–51 px steps.
- **Water** ("Wate", object 81, `Water_Init` 0x4317F0, `Water_Update` 0x431A20, `Water_Draw` 0x431D80):
  - Position: the surface (`g_WaterLevelY` 0x59F1C4) is the placement's y − 20.
  - Drawing: `Water_Draw` tiles 6 strips of 64 px across the screen, from 12 cached frames each of 'WAV1' (front) and 'WAV2' (back), one frame per 4 ticks, plus 'WAV3' (the surface strip) and an optional 'BACK'. With fewer than 12 frames, the missing ones are read as garbage.
  - Effects: splashes ('SPLA', 'SPL2'), the sink splash ('SPL3'), droplets ('SPL4') and bubbles ('BUBL') are actors of the type named exactly "Water" (`Actor_AllocHigh(…, "Water")`). An animation missing from that type crashes `Actor_Draw`.
  - Drowning: once the player's feet are 32 px under the surface, he sinks and the level ends (`g_bLevelEnding`).
    - The exception is while `g_WaterEscapeTimer` (0x59E95C) runs. Then he is held 32 px under the surface, and jump throws him out (vy −4.0).
    - `Level_Start` sets the timer to 120 ticks. It counts down while he is in the water, and crossing back into the water zeroes it, so only the first dip is survivable.
    - The jump-out test also needs `g_bInputLocked` (0x52C52C) clear, but every tick in the water sets it. So he only escapes by holding jump as he lands.
    - Player state 0x200 means "in the water".
- **Drifting boat** ("Trir", object 65, `DriftingBoat_Init` 0x432450): 'BOOT'.
  - It bobs (vy ±0x2000, switching every 32 ticks) and rocks (0..128, drawn as a rotation of up to about 11°).
  - When first stood on it drifts at 1 px per tick for |param| ticks, in the direction of the param's sign, then slows down.
  - It has no wall check.
- **Bounce pad** ("Jump", object 68, `BouncePad_Init` 0x433ED0): plays 'PAD ' while stood on. At frame min(frames − 2, 4) it launches the player: state |= 0x403, vy −6.0, hold force 0xA000.
- **Planes:** `g_Planes[20]` at 0x49b880, 0x9C bytes each. These are scroll and depth planes, each with its tile map, collision map, placement list and zoom (750 per-mille means pre-shrunk 12x12 tiles).
- **Console leftovers:** the language menu is complete in the PC build. It is TITLE.WLD sub-level 0 (plane 4, `LanguageMenu` → `Obj_LangSelect_*` 0x417E60/0x417F00, font `Cset` from MAIN.WLD, demo file DEMO24N1.DAT), which the port skips by entering the title at sub-level 1. MAIN.WLD also has a `Loading` object type (anims `HERO`, `UHR` = hourglass, `TEXT` = "DISC ERROR!") that no PC code references; it is the PS1 loading screen (see loader.md, Loading Screen on the Extra page). The PS1 boot logos are a separate world, `LOGOXXXX.WLD` (five 640×512 planes: three logos plus a PAL/NTSC overlay), listed as `LOGO` in the PC's `g_WldFileNames` but not shipped.
- **Text:** GAME.TXT holds localised text (`@`, `#`, `&` markers), loaded by Text_LoadGameTxt (0x416ba0).
- **Passwords:** Password_DecodeAndApply (0x417950) and Map_DrawPasswordGems (0x4243c0). The password is XORed with 0xB7A4, then holds the section (5 bits), lives (5 bits), continues (2 bits) and a 4-bit checksum, shown as 8 two-bit gems.

## Sound

- **Effects table:** `g_SoundDefs[99]` at 0x461d08, 0x14 bytes per entry. Files are `SFX\NAME.WAV`. Ids 71–98 alias earlier samples at a different pitch and volume.
  - Common ids: 2 stomp, 5 coins, 20 fanfare, 22 jump, 32 death, 34 helmet lost, 57 YIPPEE, 66 helicopter loop.
- **Play calls:** Sound_PlayEffect(actor, id) at 0x40d5f0 is positional; actor -1 means no position and 0 means the player.
- **Jingles and ambience:** `g_JingleDefs` (GAMEOVER, LEVELEND, WORLDEND) load from `JINGLES\*.WAV`. `g_AmbientDefs[6]` load stereo `AMBIENT\*_L/_R.WAV` loops.
- **Music:** music is **CD audio** through MCI (CD_PlayTrack etc.). CD_VerifyLomaxDisc checks the track lengths against a table at 0x4625a0.
  - `CD_PlayTrack(n)` (0x40DFF0) plays disc track n+1 to n+2 with `notify`. `MM_MCINOTIFY` with `MCI_NOTIFY_SUCCESSFUL` makes `Lomax_WndProc` (0x404949) call `CD_RestartTrack` (0x40E0C0), which loops the track.
  - `CD_Pause`/`CD_Resume` (0x40E110/0x40E150) save the `tmsf` position and replay from it. `CD_Stop` is at 0x40E0E0.
  - Volume: `CDAudio_SetVolume(0..255)` (0x438460) sets `auxSetVolume` on the CD line (`g_cdAuxDeviceId` 0x466B68) to 65535·log2(1 + v/255). `Game_UpdateFade` (0x40F530) fades it with the screen. `g_cfgCDVolume` (0x464574) is 0..100.
  - The disc's `MUSIC\*.DAT` files are never opened by the game. They are ISO 9660 entries that point at audio tracks 2–25 with lengths counted at 2048 bytes per sector. Copies made through the file system lose 304 of every 2352 bytes. `LOADER.EXE` extracts the tracks from the raw image instead (see LOADER.md, "CD music").
- **Driver:** the SndDrv_* functions at 0x437f80–0x4387d0 are a custom driver. It is either a software mixer (16 voice slots, of which the first `g_mixerMaxVoices` (8) in the active list are mixed into a 32 KB stream every 34 ms on a winmm timer; the loader raises the limit to 16) or hardware DirectSound buffers.
  - Driver parameters: volume 0–255, pitch 0x100 = normal speed.
  - **Replacement WAVs must be PCM, mono, 22050 Hz, 16-bit, with a plain 44-byte header.**
  - **Backend choice:** a sound card that reports an emulated DirectSound driver (DSCAPS 0x20) gets the DirectSound-buffer backend (`DSHw_*`) instead of the mixer. Windows Vista and later never set that flag although every device is emulated, so the game picks its mixer there; the loader picks the DirectSound backend instead (see loader.md, "DirectSound mixing").
- **Mix modes** (low byte of the SndDrv_Init flags, `g_sndMixMode`):
  - 1 is mono.
  - 2 is stereo with 31 pan positions (centre 15).
  - 3 is binaural "Haas" stereo: the far ear gets the sample up to 25 samples (about 1.1 ms) later and quieter.
  - 4 gives each sample a second recording, and panning crossfades between the two.
  - 5 is stereo plus a per-voice echo tap that starts 128 frames late with its own pan and level.
- **Mixer inner loops:** they keep their working registers in globals (`g_pMixSrc`, `g_pMixDst`, `g_MixPitchStep`, …). The routine is picked from `g_MixerInnerLoopTable` by add-or-set, pitched, surround and mono bits. Voice flag 0x10 ("surround") writes the right channel in anti-phase, which a Dolby Pro Logic decoder sends to the rear speaker.
- **Dead code:** the mixer's post-process buffers, the effect pointer at 0x5A541C and both "voice end" callbacks are only ever freed or read, never set.
- **Bug, hardware backend:** `DSHw_Init` fills a 31-entry pan table at 0x5A02B0 (indices 0–30). Entry 30 (0x5A0328) is also where DSHw stores the last DirectSound HRESULT, so every call overwrites it with 0 (DS_OK = centre). Hard-right pan therefore plays centred: in DirectSound-buffer mode the right ambient channel (pan 30) comes out in the middle. The software mixer is not affected. The loader fixes it by sending those 30 stores elsewhere (see loader.md, "Hard-right pan").
- **Bug, hardware backend:** `DSHw_TimerService` treats a buffer as finished unless `GetStatus` is exactly 1 or 4. A playing loop is 5 (playing + looping), so each loop is rewound once and its buffer marked free while it keeps playing. The loader tests bit 0 instead (see loader.md, "Looping sounds in DirectSound mode").

## Rendering

- **Sprite queueing:** Sprite_QueueScreen, QueueWorld, QueueWorldScaled, QueueRotated and Queue3DQuad (0x40a7b0–0x40d570). Actor_Draw is at 0x40c270 and TileLayer_Draw at 0x40ce80.
- **Sprite flags:** bits 22–29 are the bucket, 0x8 / 0x10 mirror the sprite, and bits 16–23 are alpha.
- **Sprite banks:** `g_AnimBanks` (world) and `g_SpriteBanksLevel` hold 256 banks each. A `SpriteFrame` is 0x38 bytes.
- **Angles:** 0–1023 per turn. The table at 0x4639C0 is sine (`g_SinTable`, `short[1280]`) in 1.14 fixed point (16384 = 1.0). `g_pCosTable` (0x464400) points 256 entries into it, so it reads cosine. Rotated sprites and meshes multiply by them and shift right by 14.
- **DirectX globals:** g_pDD, g_pDDSPrimary, g_pDDSBack, g_pDDSStretch, g_pDDPalette, g_pD3DDevice, g_pDIKeyboard and g_pDirectSound.
- **Palettes:** each of a world's 10 palettes becomes a 0x528-byte DIB block at 0x471B28 (BITMAPINFO, 256 colours and an 8-bit remap table). Every tile and sprite surface is built from these blocks through StretchDIBits.
- **Mirroring:** CLomaxApp_InitInstance sets up four shared `DDBLTFX` blocks: no-tearing, mirror LR, mirror UD and mirror LR+UD. Each sprite-queue entry points at one of them. The D3D path checks for `&g_BltFxMirrorLR` and swaps u, so left-right flips survive as quads. Up-down flips have no D3D equivalent.
- **Surface memory fallback:** if the card can't do colour-keyed blits (`g_bDDHWColorKeyBlt`) or has no 320x240 mode (`g_bMode320x240Avail`), every surface goes to system memory. That also forces the software D3D driver. The Display Info dialog shows these flags.

Direct3D (DirectX 3, execute buffers):

- **Driver choice:** D3D_EnumDriversCallback keeps up to 5 `D3DDEVICEDESC`s (`g_D3DDeviceDescs`). The select functions keep those whose render bit depth matches the display, then prefer hardware (or software), then RGB over mono. `g_D3DRenderMode` is 1 = software ramp, 2 = software RGB, 3 = hardware RGB.
- **Texture format:** up to 10 formats are enumerated. Ramp mode wants a palettised format; otherwise the game wants 16-bit RGB, or a format with an alpha mask when it wants alpha. Without an alpha format it re-enumerates with colour keying, which must then be supported. `g_bD3DTranslucency` is set only when the chosen format really has alpha. Texture_ApplyAlphaMask sets alpha on every pixel that isn't the colour key (white, or 0xFF when palettised).
- **Execute buffers:** a 0x9C-byte one-shot buffer sets render states and the world/view/projection matrices. The per-frame buffer is 0x5624 bytes: room for 128 quads (4 `D3DTLVERTEX` each) at 0x0000–0x3FFF, then STATELIGHT + PROCESSVERTICES, then per-bucket texture/state ops, 2 triangles per quad and EXIT. Game_Frame executes it once per sprite bucket. Software drivers get a dummy extra quad.

## MFC classes and compiler-generated code

The game's MFC side is small. Each class was identified from its vtable: slot 0 is `GetRuntimeClass`, which returns the `CRuntimeClass` with the class name.

| Class | Vtable | Used for |
|---|---|---|
| CLomaxApp (CWinApp) | 0x459A30 | The app; `theApp` at 0x469D70. |
| CLomaxCmdLineInfo | 0x459AD8 | `/FULLSCREEN /SYSTEMRAM /LOGGING`. |
| CSystemTab, CControlsTab (CPropertyPage) | 0x459148, 0x459650 | Properties sheet pages. |
| CAboutDlg, CDisplayInfoDlg, CInsertCDDlg, CWrongCDDlg, CQuitConfirmDlg, CPasswordDlg, CPasswordInfoDlg, CBadPasswordDlg (CDialog) | 0x459020 … 0x459EC0 | Menu dialogs. `Menu_OnConfigureKeys` actually opens CDisplayInfoDlg. |
| CObject, CMenu, CPen, CGdiObject | 0x45B1A4, 0x45BFA4, 0x45C3DC, 0x45B814 | Inline destructors compiled into game code. |

Conventions in the Ghidra project:

- **Shared virtual stubs:** `CObject_Serialize`/`AssertValid`/`Dump` (0x401070–0x401090) and `CWnd_DoDataExchange` (0x401440) are empty bodies shared by ~40 MFC vtables. Each dialog has its own empty `<Class>_DoDataExchange`.
- **Implicit destructors:** the compiler emits one copy per source file (`CQuitConfirmDlg_dtor` in the WndProc file, `CAboutDlg_dtor` in the menu file, …). Nothing in them is game logic.
- **C++ exception stubs:** any function with destructible locals has an `<Owner>_EHHandler`, a 10-byte `mov eax, FuncInfo; jmp __CxxFrameHandler`. It also has one `<Owner>_UnwindN` funclet per unwind-map state, each calling one member or local destructor. Each handler's FuncInfo record (magic 0x19930520) lists its funclets. There are 52 handlers and 151 funclets, all in the MFC classes above plus `Lomax_WndProc`, the `Menu_On*` handlers, `World_LoadWLD` and `Password_DecodeAndApply`.
- **Switch tables:** the 12 jump tables (and their byte index tables) inside game functions are typed as data and labelled `<Function>_switchTable` / `_switchIndex`.
- **Static initialisers (CRT `__xc_a` list):** `theApp_DynamicInit` (constructs theApp, registers `theApp_AtexitDtor`) and `StaticInit_ClearUnused46a8c0`, which zeroes an 8-byte static (`g_UnusedStatic46a8c0`) that nothing reads.
- **Library code:** 0x4388CA–0x458DFF is the statically linked MSVC C runtime, the CTL3D 3-D controls library and MFC 4.2. The game calls into it through `global_operator_delete`, `atexit`, `sscanf`, `_CIpow`/`_CIlog`/`_CIexp`, `CMenu_Attach`, `CWinApp_SetRegistryKey`, `CWinApp_Enable3dControlsStatic`, `CCmdTarget_EndWaitCursor` and others. [library.md](library.md) has the layout, the naming conventions and a table for every library function and global.
- **Key-binding boxes:** each of the 13 edit boxes on the Controls tab has a one-instruction handler, `CControlsTab_UpdateKeyText_<Key>`, that jumps to the shared `CControlsTab_UpdateKeyText` (0x402170).

## Compatibility patches

The fixes (CD check, 32-bit desktops, borderless fullscreen, scaling, widescreen) are applied in memory by the loader, without touching the EXE: `LOADER.EXE` + `LOADER.DLL`, with sources in `LOADER\`. See [loader.md](loader.md).

Facts the patches rely on:
- **`g_bSpeed50Hz` (0x46441C):** the PS1 PAL switch, never written on PC. Read at 43 sites: second-based timers (50 vs 60 ticks), particle speeds and gravity (×1.2–1.4), and PAL 256-line layout offsets (HUD, copyright, title logo, map props, parallax sprites). Player and enemy code doesn't read it, so PAL is slower in real time. The 60 Hz step is `mov ebx, 0xA7` at 0x404BE8 in `Game_Frame`. See 50Hz Mode (Extra page) in loader.md.
- **`DDraw_InitSurfaces` signature:** it takes `(HWND hWnd, int bReload)`, cdecl. A non-zero `bReload` reloads the current level instead of calling `Game_SetupNextLevel`.
- **Supported desktop depths:** the original refuses windowed mode unless the desktop is 8 or 16 bpp. The check is in DDraw_InitSurfaces at 0x40799c.
- **Fullscreen on modern Windows:** fullscreen needs a 320x240 mode at 8 or 16 bpp, which DirectDraw on Windows 10/11 doesn't list.
- **Sprites and tiles:** they are drawn into surfaces through GDI DCs. Pixel values come from GDI SetPixel/GetPixel on the render surface, so any RGB format works if all offscreen surfaces share it.

## Appendix: global variables

262 globals used by game code, grouped by subsystem. Rows marked *(field)* are covered by a struct applied at a base address.

<details><summary>Graphics, DirectX and app setup (73)</summary>

| Address | Name | Type | Meaning |
|---|---|---|---|
| 0x45A028 | g_GUID_SysKeyboard | GUID | GUID_SysKeyboard {6F1D2B61-D5A0-11CF-BFC7-444553540000}, used for IDirectInput::CreateDevice in DirectX_Init |
| 0x45A02C | (field) g_GUID_SysKeyboard.Data2/Data3 | GUID field | part of the GUID |
| 0x45A030 | (field) g_GUID_SysKeyboard.Data4[0..3] | GUID field | part of the GUID |
| 0x45A034 | (field) g_GUID_SysKeyboard.Data4[4..7] | GUID field | part of the GUID |
| 0x461C88 | g_bD3DQuadsQueued | int (bool) | set by Sprite_Queue3DQuad when a textured quad is queued this frame. Game_Frame clears the D3D viewport Z-buffer only if it is set, and resets it every frame |
| 0x461C94 | g_MeshLookYFactor | int (=47) | Sprite_Queue3DQuad: screen-Y shift = -(47*look)/1000, where look = DAT_0052c5c4 (view look-up/down offset) |
| 0x461C98 | g_MeshLookZFactor | int (=53) | Sprite_Queue3DQuad: Z shift = (53*look)/100 |
| 0x461C9C | g_MeshZScalePct | int (=29) | percentage applied to the quad Z values before the D3D emit (depth squash) |
| 0x461CA4 | g_MeshBucketDepthScale | int (=288) | depth-to-sort-bucket scale: bucket = bias - sumZ*288/4000 + depthBias + 0x90 |
| 0x461CA8 | g_MeshBucketBias | int (=8) | constant bias in the bucket formula above |
| 0x461CBC | g_MeshSortBucket | int (init -1) | sprite-queue priority bucket for the current mesh poly list. -1 means compute it from the first poly. *_Draw / Mesh_DrawPolyList reset it to -1. Only 0x4E..0xC7 is accepted (EOL comment) |
| 0x461CDC | g_SavedCDVolume | uint (byte) | CD-audio aux volume read at startup (CDAudio_GetVolume), restored by CDAudio_SetVolume on exit |
| 0x462590 | g_WinClientRectScreen | RECT | window client rect converted to screen coords (DDraw_Present). Destination rect of the windowed Blt/stretch-Blt |
| 0x462594 | (field) g_WinClientRectScreen.top | RECT field |  |
| 0x462598 | (field) g_WinClientRectScreen.right | RECT field |  |
| 0x46259C | (field) g_WinClientRectScreen.bottom | RECT field |  |
| 0x464404 | g_pLevelHeapTop | pointer | bump-allocator top of the level heap (Heap_AllocLevel). Reset to 0x52c708 in DDraw_InitSurfaces |
| 0x46440C | g_pObjectHeapTop | pointer | bump-allocator top of the object heap (Heap_AllocObjects). Reset to 0x49c4c8 |
| 0x4645D0 | g_hHelmetSpinVoice | int (voice handle, -1 = none) | looping HELMSPN2 (sfx 0x13) voice of the thrown helmet. Stopped on catch/knock-off/exit, muted by Sound_MuteLoops |
| 0x469990 | g_BltFxNoTearing | DDBLTFX | dwSize=100, dwDDFX=DDBLTFX_NOTEARING(8). Used for the windowed back->primary / stretch Blts |
| 0x469994 | (field) g_BltFxNoTearing.dwDDFX | DDBLTFX field | =8 |
| 0x4699F4 | g_bD3DAlphaFlatBlend | int (bool) | driver has D3DPSHADECAPS_ALPHAFLATBLEND (0x1000). If set (or no alpha), SHADEMODE=FLAT, else GOURAUD |
| 0x4699F8 | g_D3DTexFormatCount | int | number of texture formats enumerated (max 10) by D3D_EnumTextureFormatsCallback |
| 0x469A04 | g_pD3DExecInstrEnd | byte* | end of the instruction stream (after D3DOP_EXIT) in the locked execute buffer. InstructionLength = end - start |
| 0x469A18 | (field) g_D3DDeviceDescs[i].dcmColorModel | D3DDEVICEDESC field | D3DCOLOR_MONO(1)/RGB(2). Drives g_D3DRenderMode selection |
| 0x469AAC | (field) g_D3DDeviceDescs[i].dwDeviceRenderBitDepth | D3DDEVICEDESC field | DDBD_* mask matched against the display depth in D3D_Select*Driver |
| 0x469E30 | g_D3DQuadVerts | D3DTLVERTEX[4] | staging quad. rhw=1, colour = alpha<<24 \ | 0xFFFFFF. Copied into the exec buffer at slot*0x80 |
| 0x469E34 | (field) g_D3DQuadVerts[0].sy | D3DTLVERTEX field |  |
| 0x469EB0 | g_D3DTexFormatIndex | int (-1 = none) | index of the chosen texture format in the DDSURFACEDESC table at 0x46a220 |
| 0x469EB4 | g_pD3DExecInstrCursor | byte* | current write pointer in the instruction area (STATERENDER / TRIANGLE ops) |
| 0x469EB8 | g_FPSFrameCount | int | frames presented this second; copied to g_FPS every 1000 ms |
| 0x469EC4 | g_bD3DAlphaBlendCaps | int (bool) | driver has alpha flat/gouraud blend + texture ALPHA + SRCALPHA/INVSRCALPHA blend caps |
| 0x469ECC | g_bD3DTexColorKeyCaps | int (bool) | current driver has D3DPTEXTURECAPS_TRANSPARENCY. Device init fails if neither this nor alpha-blend is available |
| 0x469ED4 | g_D3DTexColorKey | uint | texture colour-key pixel: 0xFF for 8-bit, else R\ | G\ | B masks of the format. Also the "transparent" value skipped by Texture_ApplyAlphaMask |
| 0x469ED8 | g_hD3DMatProjection | D3DMATRIXHANDLE | projection matrix handle (STATETRANSFORM 3), data at 0x461ba0 |
| 0x469EE0 | g_LastFrameTime | DWORD | timeGetTime() of the previous Game_Frame (fixed-timestep accumulator) |
| 0x469F38 | g_bD3DHWTexColorKey | int (bool) | HW driver supports texture transparency (shown in CDisplayInfoDlg) |
| 0x469F90 | g_D3DExecBufDesc | D3DEXECUTEBUFFERDESC | dwSize=0x14, dwFlags=D3DDEB_BUFSIZE. Used for CreateExecuteBuffer / Lock |
| 0x469F94 | (field) g_D3DExecBufDesc.dwFlags | field | =1 |
| 0x469F9C | (field) g_D3DExecBufDesc.dwBufferSize | field | 0x9C (state block) or 0x5624 (frame buffer) |
| 0x469FA0 | (field) g_D3DExecBufDesc.lpData | field | locked pointer |
| 0x469FB8 | g_BltFxColorFill | DDBLTFX | colour-fill blit FX (dwFillColor=0) used to clear the primary/back/fade surfaces with DDBLT_COLORFILL |
| 0x46A01C | g_pD3DExecBufData | byte* | base of the locked frame execute buffer (vertex area, 128 quads * 0x80) |
| 0x46A028 | g_BltFxMirrorLRUD | DDBLTFX | dwDDFX=MIRRORLEFTRIGHT\ | MIRRORUPDOWN(6). Sprite flip-both blit |
| 0x46A02C | (field) g_BltFxMirrorLRUD.dwDDFX | field | =6 |
| 0x46A08C | g_bD3DHardware | int (bool) | the selected D3D driver at startup is a HW driver (CDisplayInfoDlg) |
| 0x46A090 | g_hD3DMaterial | D3DMATERIALHANDLE | handle from IDirect3DMaterial::GetHandle, used in the STATELIGHT MATERIAL op |
| 0x46A098 | g_pD3DExecInstrStart | byte* | start of the instruction area (lpData+0x4000) |
| 0x46A09C | g_bD3DHWAlphaBlend | int (bool) | HW driver supports alpha blending (CDisplayInfoDlg) |
| 0x46A1B0 | g_bDDHWColorKeyBlt | int (bool) | DDraw HW can do colour-keyed blits (DDCAPS_BLT+COLORKEY, CKEYCAPS_SRCBLT). 0 forces system-memory surfaces and disables the overlay |
| 0x46A200 | (field) g_FadeTextureFrame.pTexSurface | SpriteFrame field | fade texture surface (0 = not built yet -> Render_DissolveFade fallback) |
| 0x46A204 | (field) g_FadeTextureFrame.pTexture | SpriteFrame field | IDirect3DTexture of the fade texture |
| 0x46A208 | (field) g_FadeTextureFrame.nTexture | SpriteFrame field | D3DTEXTUREHANDLE (-1 = none) |
| 0x46A20C | (field) g_FadeTextureFrame.nUnk2C | SpriteFrame field | zeroed at init |
| 0x46A210 | (field) g_FadeTextureFrame.flUScale | SpriteFrame field | max u |
| 0x46A214 | (field) g_FadeTextureFrame.flVScale | SpriteFrame field | max v |
| 0x46A21C | g_hD3DMatView | D3DMATRIXHANDLE | view matrix handle (STATETRANSFORM 2), data at 0x461be0 |
| 0x46A7A0 | g_D3DExecData | D3DEXECUTEDATA | dwSize=0x30. Vertex count/instruction offset+length set per bucket before Execute |
| 0x46A7D0 | g_bD3DTexPow2 | int (bool) | driver requires power-of-2 textures (D3DPTEXTURECAPS_POW2) |
| 0x46A828 | g_pD3DExecProcessVerts | D3DPROCESSVERTICES* | the PROCESSVERTICES op in the exec buffer. dwCount is patched to quads*4 per emit |
| 0x46A868 | g_D3DMaterialDesc | D3DMATERIAL | white diffuse/ambient material, dwRampSize=16. hTexture is updated per draw |
| 0x46A86C | (field) g_D3DMaterialDesc.diffuse.r | field | 1.0 |
| 0x46A8B8 | g_D3DLastTexHandle | int | texture handle last emitted (TEXTUREHANDLE state cache) |
| 0x46A8FC | g_D3DExecBufSize | int | frame execute-buffer size = 0x5624 |
| 0x46A904 | g_FrameTimeAccum | int | remainder of the fixed-timestep accumulator (elapsed_ms*10 mod 167) |
| 0x46A908 | g_BltFxMirrorLR | DDBLTFX | dwDDFX=MIRRORLEFTRIGHT(2). A sprite whose pBltFx==&this is also u-flipped in D3D_EmitTexturedQuad |
| 0x46A90C | (field) g_BltFxMirrorLR.dwDDFX | field | =2 |
| 0x46A970 | g_BltFxMirrorUD | DDBLTFX | dwDDFX=MIRRORUPDOWN(4) |
| 0x46A974 | (field) g_BltFxMirrorUD.dwDDFX | field | =4 |
| 0x46A9D8 | g_pD3DExecInstrBody | byte* | first instruction after the fixed STATELIGHT+PROCESSVERTICES header. The cursor is reset here for each bucket |
| 0x46AA44 | g_bMode320x240Avail | int (bool) | a 320x240 8/16-bpp mode was enumerated (DDraw_EnumModes320Callback). If 0, surfaces go to system memory (EOL comment) |
| 0x46AA48 | g_bD3DTexBlendCopy | int (bool) | ramp/mono SW driver + 8-bit texture + 8-bit display -> TEXTUREMAPBLEND=COPY instead of MODULATE |
| 0x46AAA8 | g_bD3DUseAlphaTextures | int (bool) | prefer alpha texture formats. Initialised from alpha-blend caps and cleared if no alpha format exists. Enables Texture_ApplyAlphaMask |

</details>

<details><summary>Sound driver and CD audio (64)</summary>

| Address | Name | Type | Meaning |
|---|---|---|---|
| 0x4625A0 | g_CDExpectedTrackTimes | short[69] | 23 x {min,sec,frame} expected lengths of CD tracks 2..24; CD_VerifyLomaxDisc compares them with MCI "status cdaudio length track N" (TMSF) to recognise the Lomax disc |
| 0x464568 | g_hAmbientVoiceL | uint | voice handle of the left ambient loop (pan 0), -1 = none |
| 0x46456C | g_hAmbientVoiceR | uint | voice handle of the right ambient loop (pan 0x1e), -1 = none |
| 0x466A08 | g_bMixerVoiceEndNotify | byte (unchanged) | when set, Mixer_TimerService calls g_pfnMixerVoiceEndCallback(slot) for voices that finished (flag 0x8000). Only ever cleared, so this is dead library code |
| 0x466A0C | g_bMixPostFxAllocated | byte (unchanged) | post-process buffers allocated; Mixer_Shutdown then calls Mixer_FreePostProcessBuffers |
| 0x466A10 | g_bMixPostFxEnabled | byte (unchanged) | run Mixer_CallPostProcess on each mixed chunk (also when no voice plays) |
| 0x466A14 | g_MixerPendingCmdCount | uint | number of queued voice-slot commands (start/stop/free) that Mixer_TimerService still has to apply; reused there as the count of finished voices |
| 0x466A18 | g_bDSHwInitialized | byte (unchanged) | DSHw backend initialised (DSHw_Init) |
| 0x466A1C | g_bDSHwVoiceEndNotify | byte (unchanged) | when set, DSHw_TimerService calls g_pfnDSHwVoiceEndCallback(voice\ | 0x400) when a tracked hardware voice stops. Never set |
| 0x466B6C | g_bSndTimerInCallback | undefined4 (unchanged) | re-entrancy guard of SndDrv_TimerCallback (1 while servicing) |
| 0x46AB60 | g_AmbientVolOffsetTarget | int | random target volume offset for the ambient loops (0..0x70 in steps of 16) |
| 0x46AB68 | g_AmbientPitchTarget | int | random target pitch for the ambient loops (0x100 +/- nVolVariance/2) |
| 0x52C554 | g_AmbientVariationTimer | int | frames left until a new random ambient volume/pitch target is picked ((rand&15)*4+24)*5 |
| 0x52C560 | g_AmbientPitch | uint | current ambient pitch, moves 1 per frame toward the target; sent with SndDrv_SetVoicePitch |
| 0x59F1F0 | g_AmbientVolOffset | int | current ambient volume offset, moves 4 per frame toward the target; added to AmbientDef.nVolume |
| 0x59F3EC | g_pfnMixerVoiceEndCallback | undefined4 (unchanged) | callback(slot) for finished software voices (see g_bMixerVoiceEndNotify). Never assigned |
| 0x59F3F0 | g_MixStreamWritePos | uint | byte offset of the next chunk to Lock/fill in the 32 KB g_pDSMixStreamBuffer; wraps at g_MixStreamBufferBytes |
| 0x59F404 | g_MixerSampleAltData | (unchanged) | g_MixerSamples[i]+4: second sample data, only in mix mode 4 (paired sample loaded through Mixer_LoadSample's 2nd arg, flag 0x20) |
| 0x59F408 | g_MixerSampleSize | (unchanged) | g_MixerSamples[i]+8: converted sample length in bytes |
| 0x59F40E | g_MixerSampleFlags | (unchanged) | g_MixerSamples[i]+0xe: 0x200 in use, 0x80 owns malloc'd data, 0x20 has alt data. (+0xc = rate 0x2b11/0x5622) |
| 0x59FE04 | g_pMixerCurVoice | int * | iterator over the active voice list; its address is passed to g_pfnMixerMixVoices |
| 0x59FE0C | g_MixerVoiceSlotCmd | (unchanged) | g_MixerVoiceSlots[i]+4 (ushort): pending command for the slot: 1 start/link, 4 stop+free, 5 free, 8 unlink |
| 0x59FEA8 | g_pfnDSHwVoiceEndCallback | undefined4 (unchanged) | hardware counterpart of g_pfnMixerVoiceEndCallback. Never assigned |
| 0x5A02EC | g_DSHwPanTableCenter | (unchanged) + plate comment | centre (index 15) of the 31-entry DS SetPan table at 0x5a02b0 built by DSHw_Init. Index 30 overlaps the HRESULT global 0x5a0328 (see findings) |
| 0x5A0350 | g_DSHwSampleInstVoice | (unchanged) | g_DSHwSamples[i]+0x20: int[8], the hardware voice index that plays each duplicate buffer instance |
| 0x5A0384 | g_DSHwSampleInstCount | (unchanged) | g_DSHwSamples[i]+0x54: number of DS buffers created for the sample (1 + DuplicateSoundBuffer copies, max 8) |
| 0x5A0388 | g_DSHwSampleBusyMask | (unchanged) | g_DSHwSamples[i]+0x58: bitmask of instances currently playing |
| 0x5A313C | g_DSHwVoiceSample | (unchanged) | g_DSHwVoices[v]+4: sample index of the hardware voice (voice = {flags 1 used/2 tracked/4 playing, sample, instance}, 32 voices x 12 bytes) |
| 0x5A3140 | g_DSHwVoiceInstance | (unchanged) | g_DSHwVoices[v]+8: buffer instance index (0..7) inside that sample |
| 0x5A32B8 | g_pMixMode4Table | byte * | mode-4 level table (malloc 0x800 = 16 pan x 32 vol x 4 bytes: L/R levels for the primary sample and L/R for the alt sample) |
| 0x5A32C0 | g_MixTablesMode | ushort | mix mode the tables were built for (Mixer_SetupMixMode); Mixer_FreeTables uses it to decide what to free |
| 0x5A32C8 | g_MixMode5Taps | (unchanged) + plate comment | Mixer_MixVoicesMode5 tap state, 2 x {int pos; ushort volIdxL, volIdxR, loopSel, pad}: tap 0 direct, tap 1 echo |
| 0x5A32D4 | g_MixMode5EchoTapPos | (unchanged) | g_MixMode5Taps[1].pos: echo tap's sample position (voice+0x18) |
| 0x5A32E0 | g_MixPostFxType | byte | post-process effect type: <3 means one buffer (g_pMixPostFxBuffer), 3 means five buffers (g_pMixPostFx3Buf0..4) |
| 0x5A5384 | g_DSCapsFlags | uint | g_DSCaps.dwFlags (DSCAPS+4): 2 PRIMARYSTEREO picks stereo, 8 PRIMARY16BIT picks 16-bit, 0x20 EMULDRIVER forces the DS-buffer backend |
| 0x5A53A4 | g_DSCapsFreeHwStaticBuffers | uint | g_DSCaps.dwFreeHwMixingStaticBuffers (DSCAPS+0x24), checked by SndDrv_HasFreeHwBuffers |
| 0x5A53C8 | g_DSCapsFreeHwMemBytes | uint | g_DSCaps.dwFreeHwMemBytes (DSCAPS+0x48), compared with the requested size in SndDrv_HasFreeHwBuffers |
| 0x5A53E4 | g_SndSampleRate | uint | g_SndWaveFormat.nSamplesPerSec: 22050 (0x5622) or 11025 (0x2b11) |
| 0x5A53E8 | g_SndAvgBytesPerSec | uint | g_SndWaveFormat.nAvgBytesPerSec |
| 0x5A53EC | g_SndBlockAlign | ushort | g_SndWaveFormat.nBlockAlign (wBitsPerSample follows at 0x5a53ee) |
| 0x5A5404 | g_pMixPostFx3Buf0 | void * | post-FX type-3 work buffer (freed only) |
| 0x5A5408 | g_pMixPostFx3Buf1 | void * | post-FX type-3 work buffer |
| 0x5A540C | g_pMixPostFx3Buf2 | void * | post-FX type-3 work buffer |
| 0x5A5410 | g_pMixPostFx3Buf3 | void * | post-FX type-3 work buffer |
| 0x5A5414 | g_pMixPostFx3Buf4 | void * | post-FX type-3 work buffer |
| 0x5A5418 | g_pMixPostFxBuffer | void * | single work buffer of post-FX types 0..2 |
| 0x5A5420 | g_pMixDst | int * | inner-loop "register": write pointer into the 32-bit accumulator buffer g_pMixBuffer |
| 0x5A5424 | g_MixPitchFrac | ushort | inner-loop 8.8 fractional position accumulator for pitched voices (saved to voice+0x28 / +0x2a) |
| 0x5A5426 | g_MixVolRight | byte | right-channel volume index (0..31) after panning; becomes the high byte of the volume-table index |
| 0x5A5530 | g_pMixPanTable | byte * | pan attenuation table, malloc 0x200 = 16 pan distances x 32 volumes giving the attenuated volume index (Mixer_BuildPanTable) |
| 0x5A5534 | g_MixVolLeft | byte | left-channel volume index (0..31) after panning |
| 0x5A5536 | g_MixPitchStep | ushort | inner-loop pitch step (voice+0x26, 0x100 = 1.0) |
| 0x5A5538 | g_pMixSrc | byte * | inner-loop read pointer into the 8-bit source sample |
| 0x5A553C | g_pMixMode3Table11k | byte * | mode-3 table for 11 kHz: 16 x {delay in samples 0..15, attenuation} (from 0x466a30/0x466a40) |
| 0x5A5544 | g_MixSampleCount | uint | inner-loop count of output frames left to mix |
| 0x5A5548 | g_pMixMode3Table22k | byte * | mode-3 table for 22 kHz: 16 x {delay 0..25, attenuation} (from 0x466a20/0x466a40) |
| 0x5A554C | g_MixVolLookupIdx | ushort | inner-loop volume-table index = volume<<8 \ | sample byte (lookup into the table at 0x5a5540) |
| 0x5A5550 | g_MixChunkSamples | uint | samples per chunk: 0x800 stereo, 0x400 mono (1024 frames) |
| 0x5A5560 | g_MixStreamBufferBytes | uint | size of the DS stream buffer, 0x8000 (32 KB) |
| 0x5A5564 | g_MixStreamBufferSamples | uint | stream buffer length in samples (0x4000 at 16-bit, 0x8000 at 8-bit) |
| 0x5A5568 | g_MixBufferDwords | uint | g_pMixBuffer length in dwords (0x800), used by Mixer_ClearMixBuffer |
| 0x5A556C | g_MixChunkBytes | uint | bytes Locked/written per timer tick = g_MixChunkSamples x bytes per sample |
| 0x5A5574 | g_MixChunkFrames | uint | frames mixed per tick (0x400 = about 46 ms at 22 kHz) |
| 0x5A5578 | g_MixBufferBytes | uint | malloc size of g_pMixBuffer (0x2000) |

</details>

<details><summary>Gameplay, levels and files (0x461CD4–0x52C588) (63)</summary>

| Address | Name | Type | Meaning |
|---|---|---|---|
| 0x461CD4 | g_JoyRetryDelay | undefined4 | Countdown before the next joyGetPosEx call. Set to 120 after a joystick error so a missing stick isn't polled on every call. |
| 0x461CFC | g_RequestedCDTrack | undefined4 | Last track requested with CD_PlayTrack, kept even when CD audio is off. Menu_OnProperties replays it when CD audio is switched back on. |
| 0x464420 | g_CameraPlayerOffsetY | undefined4 (=160) | Target camera Y is player Y minus this. Used by Player_Update, Level_Start and Trapdoor_Init. |
| 0x464428 | g_NumAnimChunksLoaded | undefined4 | Number of ANIM chunks read by World_LoadWLD. The MAIN.WLD count is copied to 0x46442c. Sprites_ReleaseAllBanks resets it. |
| 0x464560 | g_bLogoCreditsSwapNames | undefined4 | Logo_Init toggles it on each run. It decides whether the logo credits read "E KLOIBHOFER AND H NIEBORG" or the reverse order. |
| 0x464580 | g_ControlScheme | undefined4 | Selected control scheme (0..2), used as the row index into g_ControlSchemeMasks. |
| 0x464588 | g_ControlSchemeMasks | uint (row base of [3][4]) | Scheme table. Each row is {Jump, Helm, Ability, DropHelmet}. Has a comment. |
| 0x46458C | g_ControlSchemeMasks_Helm | uint | Column 1 of the table above (helmet-spin button mask). |
| 0x464590 | g_ControlSchemeMasks_Ability | uint | Column 2 (power/ability button mask). |
| 0x464594 | g_ControlSchemeMasks_DropHelmet | uint | Column 3 (button that drops the spare helmet). |
| 0x4645D4 | g_pFileBuffer | pointer (=0x4ac4c8) | Streaming file buffer used by File_Open, File_ReadChunk and File_CompactBuffer. GAME.TXT and demos are also read through it. |
| 0x4645D8 | g_FileBufferSize | undefined4 (=0x80000) | Capacity of g_pFileBuffer (512 KB). |
| 0x464628 | g_pDemoInput | ushort * | Read pointer into the demo RLE stream of (buttons, count) pairs. NULL means no demo. Level_Start points it at the 8 KB buffer 0x59c758. |
| 0x46462C | g_bDemoPlayback | undefined4 | Demo playback is active. Input_Update replaces g_InputHeld with recorded input, and Insert_Show suppresses banners. |
| 0x464630 | g_MapZoomBias | undefined4 (=0) | Added to 120 to give the per-cent factor for the world-map orbit/zoom offset in Map_WorldMap_Update. |
| 0x466730 | g_AirshipCoffinSpawnX0 | short (=500) | Scroll position (mod level width + 350) at which the airship boss phase 1 spawns a floating coffin. |
| 0x466732 | g_AirshipCoffinSpawnX1 | short (=730) | Second coffin spawn position. It is the tail of the spawn tables that start at 0x4666f8. |
| 0x4668A8 | g_szDemoFileFmt | string | `DEMOS\DEMO%02dN%d.DAT` (section+1, sub-level+1). |
| 0x46AB00 | g_hD3DWorldMatrix | undefined4 | IDirect3DDevice matrix handle bound to D3DTRANSFORMSTATE_WORLD. The view is 0x46a21c and the projection is 0x469ed8. |
| 0x46AB30 | g_WndTimerTickCount | undefined4 | Incremented by Lomax_WndProc on each matching WM_TIMER. Every 4th tick calls Game_Frame directly. |
| 0x46AB34 | g_D3DCurZEnable | undefined4 | Cached D3DRENDERSTATE_ZENABLE value in D3D_EmitTexturedQuad. -1 forces a re-emit. |
| 0x46AB40 | g_PlayerGroundMaterial | undefined4 | Material (low 3 bits of the collision byte) under the player. 0 = grass gives grass debris and dust puffs; 2 on wld 5 gives gold debris. |
| 0x46AB4C | g_PostSlideDizzyTimer | undefined4 | Set to 180 when sliding down a steep slope. When it reaches 0, g_DizzyLoopsLeft is cleared. |
| 0x46AB50 | g_DemoIndex | short | Attract-demo cycle index (0..6) into the table at 0x466410 (section, sub-level, power, seconds). Boss_EvilEd_UpdatePhase2 sets it to 7 to play the ending sequence. |
| 0x46AB54 | g_DropHelmetButtonMask | uint | Button mask for Helmet_DropSpare. Taken from the scheme table. |
| 0x46AB5C | g_PushEdgeOffsetX | undefined4 | X offset from the pushed or blocking actor (0x59f1fc) to the player. Player_Update uses it to keep the player against the object. |
| 0x46AB70 | g_ZoomTileSlotNext | undefined4 | Next free slot in the 12x12 zoom-tile surface pages (low 7 bits = slot on the page). |
| 0x46AB7C | g_RestartOrbitOffset | undefined4 | Rotating-plane orbit offset saved at the last checkpoint and restored on respawn (copy of 0x52c5c4). |
| 0x46ABB0 | g_ActorDrawMask0 | uint | Draw mask for slots 0..32: active actors without flag 0x4. Built by Actors_UpdateAll and used by Actors_DrawAndCull. |
| 0x46ABB4 | g_bTeeterAnimDone | undefined4 | The ledge-teeter ('EDGE') animation has played once. Cleared when the player moves. |
| 0x46ABBC | g_IdleTicks | undefined4 | Ticks spent standing idle. Compared with g_IdleFidgetDelay. |
| 0x471B20 | g_PlaneTransitionDepthBias | undefined4 | Depth-angle bias used while drawing the player during a plane transition: 50 for doors and ladders, 80 for Hang doors. |
| 0x471B28 | g_PaletteDIBs | undefined4 (array base) | 10 x 0x528-byte DIB palettes (BITMAPINFOHEADER + 256 RGBQUAD + 256-byte remap), one per WLD palette. Has a comment. |
| 0x471B2C | g_PaletteDIBs_biWidth | undefined4 | biWidth of entry 0. Patched before each StretchDIBits. |
| 0x471B30 | g_PaletteDIBs_biHeight | undefined4 | biHeight of entry 0. Negative means top-down. |
| 0x471B50 | g_PaletteDIBs_bmiColors | undefined1 | bmiColors of entry 0. World_LoadWLD walks the entries from here. |
| 0x476DA8 | g_ActorDrawMask2 | uint | Draw mask for slots 66..97. The mask for slots 33..65 (0x59f324) was not in this batch. |
| 0x4770B0 | g_DizzyLoopsLeft | undefined4 | Number of 'DIZZ' idle animations left to play (3) after a slope slide. |
| 0x4777C4 | g_PlaneTransitionLiftY | undefined4 | Y lift (0..24) of the player sprite while walking into or out of a background plane. |
| 0x4777C8 | g_pGameText | char * | GAME.TXT string pool, copied to the top of the level heap. Offsets and lengths are in 0x4770b8. |
| 0x4777D0 | g_bBlockerActorsActive | undefined4 | Set by breakable blocks and diggable walls, and cleared every 64 ticks. Enables actor-based blocking in Actor_CheckLedgeAhead and Actor_CheckBlockerAhead. |
| 0x4777D4 | g_PowerAmmo4_Platform | short | Ammo for power 4 (build platform: state 0x80000, 'BUI1', Power_SpawnPlatform). |
| 0x498F20 | g_NumObjTypes | undefined4 | Number of object types loaded from NMY chunks (index into g_ObjTypes). The MAIN.WLD count is copied to 0x52c5ec. |
| 0x498F24 | g_EnemyRandAccum | undefined4 | Shared 8-bit pseudo-random accumulator (seeded 123 by Level_Start) used for buzzard, Dracula, fish and bubble wander targets. |
| 0x498F28 | g_FileBufBytesAvail | undefined4 | Unread bytes left in g_pFileBuffer. |
| 0x498F30 | g_SpawnPhase | undefined4 | 4-tick spawn phase (0..3) in Objects_SpawnNearCamera. A placement spawns only when its phase matches. |
| 0x498F68 | g_pWldCellData | byte * | CELL chunk data (8x8 cells, 64 bytes each), the source for Plane_BuildTileSurfaces. |
| 0x498F6C | g_bLogoHoldsCDVolume | undefined4 | Set while the logo intro controls the CD volume. CD_PlayTrack and Game_UpdateFade leave the volume alone while it is set. |
| 0x498F70 | g_PlayerSpinTimer | undefined4 | Player 'SPIN' attack timer (15 ticks after a jump is re-pressed in the air). Many objects check it. |
| 0x49B878 | g_CullPhase | undefined4 | Rotating cull group (0..2) in Actors_DrawAndCull. Actors with nCullPhase equal to it can be despawned off-screen. |
| 0x49C4B4 | g_PlayerSpeedCapOverride | undefined4 | Overrides the player's maximum horizontal speed. Set by Barrel_Update while riding a rolling barrel. 0 = none. |
| 0x49C4B8 | g_MapScrollTargetX | undefined4 | World-map scroll target X (hero marker x - 160). |
| 0x49C4BC | g_FileSizeSectorAligned | undefined4 | Open file length rounded up to 2048-byte sectors. Drives the loading-progress value 0..14. |
| 0x52C4C8 | g_GroundSlopeClass | undefined4 | Plane_FindGroundY side output: 0 flat, 1 gentle (22.5 degrees), 2 steep (45 degrees). The direction flag is 0x52c5a4. |
| 0x52C4D0 | g_ParallaxDrawList | undefined4 (array base) | Actor* list of parallax props queued by ParallaxPlane_Update and drawn first in Actors_DrawAndCull. There is room for 24 entries before 0x52c530. |
| 0x52C544 | g_PowerAmmo5_FireHelmet | short | Ammo for power 5, the fire helmet. Sets state 0x40100, and `ThrownHelmet_Init` then uses the 'DREI' fire animation with actor flag 0x2000000. |
| 0x52C54C | g_RandSeed | ushort | Seed of the small 8-bit LCG in Rand_Next/Rand_Seed (seed+12345). |
| 0x52C55C | g_NumParallaxDrawList | undefined4 | Count for g_ParallaxDrawList. Reset each tick. |
| 0x52C568 | g_PlaneTransitionScale | undefined4 | Player scale in per-mille (750..1000) during a door, ladder or launcher plane switch. 0 = no transition, and the player can't be hit while it is non-zero. |
| 0x52C578 | g_pMeshScreenOrigin | short * | Points at the {x,y} screen origin of the pseudo-3D mesh being drawn. Read by Sprite_Queue3DQuad. |
| 0x52C580 | g_TileSlotNext | undefined4 | Next free slot in the 16x16 tile surface pages (128 per page). |
| 0x52C584 | g_IdleFidgetDelay | undefined4 | Idle ticks before the fidget animation (random 30..93). 10000 disables it until the next reset. |
| 0x52C588 | g_CameraYBeforeShake | undefined4 | Camera scrollY saved before the screen-shake offset is added. -12345678 = no shake. |

</details>

<details><summary>Gameplay: player, camera, platforms, bosses (0x52C590–0x5A5540) (62)</summary>

| Address | Name | Type | Meaning |
|---|---|---|---|
| 0x52C590 | g_CameraLookOffsetY | int | Look-up/look-down camera offset: +40 for LookDown, -40 for LookUp (also -40 when state 0x2000000 is set). Player_Update subtracts it from the target scroll Y. |
| 0x52C594 | g_JingleMusicFadeTimer | int | CD music ducking for jingles. Sound_PlayJingle sets it to 0x20 (1 for GAMEOVER). Game_UpdateFade counts it down while setting the CD volume to cfgVol·n·8/100, then sets -1 (CD stays muted). Level_Start resets it to 0. |
| 0x52C598 | g_DemoRepeatCount | short | RLE repeat counter for demo playback in Input_Update. Set to 1000 at the end of the demo. |
| 0x52C59C | g_bSectionChangePending | undefined4 | Set when leaving for another section (title, map, bonus exit, credits, boss end). Game_UpdateFade then skips Level_Start after the fade-out and Game_SetupNextLevel runs instead. Cleared by Game_SetupNextLevel, or by the YES trapdoor on the continue screen. |
| 0x52C5A4 | g_bGroundSlopeMirrored | undefined4 | Side output of Plane_FindGroundY: the slope under the probe has mirror bit 3 set. |
| 0x52C5AC | g_ActorLinkUsedMask | undefined4 | 32-bit allocation mask of the actor link-node pool (0x499778, 0x108 bytes per node). Used by Actor_AllocLink and Actor_FreeLinks. |
| 0x52C5B0 | g_bPlayerSlopeMirrored | undefined4 | Player's copy of g_bGroundSlopeMirrored, taken after each ground probe. It flips the walk/run table direction on slopes and the steep-slope slide direction. |
| 0x52C5B4 | g_pMeshRotation | undefined4 | Pointer to the current mesh's rotation angles (short[3], actor+0x88/+0x9C). The *_Draw functions set it before Mesh_DrawPolyList, and Sprite_Queue3DQuad reads it. |
| 0x52C5B8 | g_pLevelHeapLimit | undefined4 | Saved level-heap end pointer (copy of 0x464408). DDraw_InitSurfaces sets it after GAME.TXT is loaded. Level_LoadWorld restores it and clears the heap from g_pLevelHeapBase up to this pointer. |
| 0x52C5BE | g_CameraScrollYInt | short | Integer (high) word of the 16.16 smoothed camera Y at 0x52c5bc. It is written back to plane.scrollY each tick. |
| 0x52C5C4 | g_CameraOrbitOffset | short | Camera depth/orbit offset added to plane.nOrbitPos on rotating planes. The zoom is 1000000/(orbit+offset). It also shifts 3D quads. Checkpoints store 1000 - orbitPos. The world map zooms it from -100 to 0. Plane doors animate it. |
| 0x52C5EC | g_MainObjTypeCount | undefined4 | Number of object types loaded from MAIN.WLD (g_ObjTypes[0x100..]). |
| 0x52C600 | g_LemmingBurstFrames | undefined4 | Start of the pre-cached {sprite, flags} pairs for Lemming anims EINS/ZWEI/DREI (3×8 frames, 0xC0 bytes). Filled by Level_Start and read by LemmingBurst_DrawParticles. |
| 0x52C604 | g_LemmingBurstFrameFlags | undefined4 | The flags member of those pairs (stride 8). |
| 0x52C6C4 | g_bGoToTitle | undefined4 | The next section is the title (section 0x17, sub-level 1, lives reset to 3). Set by Game_ResetNewGame, credits and the end of the game. |
| 0x52C6CC | g_PlayerTickStartY | short | Player Y at the start of the tick (Actors_UpdateAll), used for platform and water crossing tests. |
| 0x52C6D0 | g_bDemoMode | undefined4 | Attract-mode demo is running: the section comes from the demo table and DEMOS\DEMOxxNy.DAT input is played back. When set, Level_Start also starts the credit text sequence. |
| 0x52C704 | g_PlatformTopY | short | Top Y of the highest moving platform under the player in this tick (20000 = none). Written by Platform_MoveAndCarryPlayer and Platform_MoveSolidBlock. |
| 0x52C708 | g_LevelHeap | undefined1 | Start of the level bump-heap storage (about 0x70000 bytes, reached through 0x464404/0x464408). GAME.TXT text and sprite surfaces are allocated here. |
| 0x59C710 | g_PlayerNextFeetY | short | Predicted player feet Y for this tick (y + vy + groundType + 1). Platform and water tests use it. |
| 0x59C714 | g_MeshDepthBias | undefined4 | Extra sprite-bucket bias for Mesh_DrawPolyList/Sprite_Queue3DQuad (for example 5 for StaticMesh). Reset to 0 after drawing. |
| 0x59C744 | g_pRidingPlatform | Actor * | Moving-platform actor the player is standing on (0 = none). |
| 0x59C748 | g_bContinueTaken | undefined4 | Set by the continue screen's YES trapdoor (uses a continue, lives = 3). Level_Start then restarts without advancing or reloading the level. |
| 0x59C74C | g_bPauseDisabled | undefined4 | Disables the Pause key on title, map, credits, continue screen, language menu and Evil Ed's ending. |
| 0x59C750 | g_PrevPlatformTopY | short | g_PlatformTopY from the previous tick. |
| 0x59C758 | g_DemoInputBuffer | undefined4 | 8 KB buffer for the demo input recording: (ushort buttons, ushort repeat) pairs, terminated by 0x7fff. |
| 0x59E75C | g_bFadeOutMusic | undefined4 | Fade the CD music together with the next screen fade-out (game over, title start, credits, bonus exit). It also stops ForceScroll. |
| 0x59E760 | g_GroundMaterial | undefined4 | Side output of Plane_FindGroundY: collision byte & 7 (surface material for footsteps/dust), -1 on empty planes. |
| 0x59E764 | g_JumpTakeoffTimer | undefined4 | Ticks after a jump during which ground snapping and hold force are skipped: 10 for a standing jump, 5 for a moving jump, 10 when jumping out of water. |
| 0x59E770 | g_bPending1Up | undefined4 | A 1UP is flying to the HUD. Item_UpdateCoinLifeToHud grants it on arrival. If it is still pending, Level_Start grants it. |
| 0x59E774 | g_pPlaneTransitionFrom | Plane * | Plane the player is leaving during a plane-door, ladder or launcher transition. Actor_Draw uses it to scale and position the player. |
| 0x59E788 | g_DustPuffCooldown | undefined4 | Cooldown between dust puffs (8 ticks). |
| 0x59E78C | g_CameraScrollVelY | int | 16.16 vertical camera smoothing velocity. It accelerates by ±0x4000/0xC000 toward the target. |
| 0x59E794 | g_GrappleOriginY | int | Y anchor of the grapple helmet (from the hotspot callback). |
| 0x59E798 | g_ScreenShakeOffsetY | undefined4 | Current random vertical screen-shake offset added to scrollY. |
| 0x59E79C | g_GrappleOriginX | undefined4 | X anchor of the grapple helmet. |
| 0x59E7D8 | g_ConfettiFrames | undefined4 | Pre-cached {sprite, flags} pairs for Confetti CON1..CON8 (8×5 frames, 0x140 bytes). Read by Burst_DrawParticles. |
| 0x59E7DC | g_ConfettiFrameFlags | undefined4 | The flags member of those pairs. |
| 0x59E918 | g_pRestartPlane | Plane * | Plane saved with the checkpoint/restart position (g_RestartX/Y). |
| 0x59E940 | g_pPrevPushBlock | Actor * | g_pPushBlock from the previous tick. Used to damp vx only on first contact. |
| 0x59E950 | g_PlayerWallHit | undefined4 | Result of Actor_CheckWallCollision for the player: 0 none, 1 wall, 2 wall that kills when ForceScroll pins the player. |
| 0x59E954 | g_LevelTimerSavedTicks | undefined4 | Saved level countdown restored by LevelTimer_Spawn(def = NULL). No write was found. |
| 0x59E958 | g_bGoToMap | undefined4 | The next section is the world map (0x16). Set on new game, after a bonus level and at the end of a boss. Cleared by Map_WorldMap_Init. |
| 0x59E95C | g_WaterEscapeTimer | undefined4 | Ticks left to jump out of water (120, reset on landing). When it reaches 0 the player drowns. Warning sounds play at 100/60/20. |
| 0x59F164 | g_bAirSpin | undefined4 | Mid-air spin: set by pressing Jump again while airborne (hold force < 0x9000), which plays the SPIN anim. On landing it starts the spin-recovery timer at 0x498f70. Cleared by hits, jumps and doors. |
| 0x59F168 | g_CurSpriteOffsetX | undefined4 | Side output of Actor_GetCurSprite: anim-entry X offset. |
| 0x59F16C | g_CurSpriteOffsetY | undefined4 | Side output of Actor_GetCurSprite: anim-entry Y offset. |
| 0x59F170 | g_bExitFadesMusic | undefined4 | Set by a MusicTrigger whose type has the "once" flag. LevelExit then sets g_bFadeOutMusic. |
| 0x59F178 | g_NextBonusSubLevel | undefined4 | Bonus-level sub-level rotation 0..4. Advanced each time the bonus trapdoor is used. |
| 0x59F1CC | g_LandAnimTimer | undefined2 | LND1 landing-recovery ticks (10 after a standing jump). Cancelled by Left/Right. |
| 0x59F1D0 | g_FileBytesRead | undefined4 | Bytes consumed from the open file. Used for the 0..14 loading progress. |
| 0x59F1D4 | g_pLevelHeapBase | undefined4 | Level-heap reset point (after persistent allocations). Restored by Level_LoadWorld. |
| 0x59F1D8 | g_pFileReadPos | undefined4 | Read cursor in the file streaming buffer (0x4645d4). |
| 0x59F1E0 | g_PowerAmmo1_Helicopter | short | Helicopter power uses (power 1). Decremented when the HELI anim starts. |
| 0x59F1E8 | g_CurSpriteFlags | undefined4 | Side output of Actor_GetCurSprite: anim-entry draw flags (+6). The DDraw/D3D init code also reuses it as a scratch HRESULT (see plate comment). |
| 0x59F1F4 | g_CreditsTextIndex | undefined4 | Index of the next credits text block (0..18, or 0..22 for the end-game credits). |
| 0x59F1FC | g_pPushBlock | Actor * | Solid block the player is pushing or touching from the side this tick. Player_Update moves it along with the player. |
| 0x59F320 | g_PowerAmmo2_BombHelmet | short | Ammo for power 2, the bomb helmet. Sets state 0x900 (also usable on ropes). State 0x800 makes `Player_ThrowHelmet` call `Power_ThrowBombHelmet`. |
| 0x59F324 | g_ActorDrawMask1 | undefined4 | Draw/alive bitmask for actor slots 33..65 built by Actors_UpdateAll (excludes flag 0x4). Read by Actors_DrawAndCull. |
| 0x59F348 | g_LogoCreditsTextBuf | undefined4 | Scratch string buffer for the "A GAME BY / E KLOIBHOFER AND H NIEBORG" reveal on the logo. |
| 0x59F3A8 | g_TypewriterTextBuf | undefined4 | Scratch string buffer for TextBlock_DrawTypewriter. |
| 0x5A5540 | g_pMixerVolumeTable | undefined4 | malloc'd 32×256 int volume lookup table for the software mixer (Mixer_*Add/Set). There is an 8-bit and a 16-bit/log variant. |

</details>
