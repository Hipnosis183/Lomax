# Lomax+

Lomax+ is a loader for Lomax. `LOADER.EXE` starts the untouched retail `LOMAX.EXE` and installs fixes in memory (`LOADER.DLL`, MinHook). Options are in the game's Properties: the System page (display) and the Extra page (the rest), saved in the registry. No third-party wrappers are used.

| # | Problem | Fix |
|---|---|---|
| 1 | The game refuses to start without the Lomax CD ("insert CD" / "wrong CD" dialogs). | The disc is never needed or read. The CD music plays from files extracted once from a disc image; without them there is no music. |
| 2 | On any modern desktop it shows *"Lomax will only run on 8 and 16 bit deep desktops."* and quits. | On 32-bit desktops the game renders into 16-bit offscreen surfaces and each frame is presented by the DLL, which converts to the desktop format. |
| 3 | Exclusive fullscreen needs 320x240 8/16-bit modes that modern Windows doesn't offer. | Borderless fullscreen, with crisp scaling. |
| 4 | 4:3 only. | Optional widescreen (432x240 by default). |

## Files

| File | What it is |
|---|---|
| `LOADER.EXE` | Loader: starts `LOMAX.EXE` with `LOADER.DLL` injected. |
| `LOADER.DLL` | The in-memory fixes. |
| `LOADER.LOG` | Written with Loader Debug Log on (Properties > Extra). |
| `MUSIC\TRACKnn.WAV` | CD music tracks, extracted by `LOADER.EXE` from a `.cue`/`.bin` disc image (see [CD music](#cd-music)). |
| `LOADER\` | Sources: `loader.c`, the DLL's modules in `dll\` (see [Source layout](#source-layout)), `build.bat`, `readme.md`, the 32-bit MinHook in `minhook\` (`libminhook.a`, `minhook.h`) and the docs in `docs\`. |
| `LOMAX.EXE` | Original, untouched. SHA-256 `5D5DE600…8CA13A79`. Installed by `LOADER.EXE` from a disc image when missing (see [Installing from the disc image](#installing-from-the-disc-image)). |
| `LOADER\docs\loader.md` | This document. |
| `LOADER\docs\decompilation.md` | Reverse-engineering notes (addresses, structures, file formats). |
| `LOADER\docs\library.md` | What each linked-in C runtime, CTL3D and MFC function and global in LOMAX.EXE is. |

## How it starts the game

1. Without `LOMAX.EXE` in its folder, `LOADER.EXE` installs the game from a disc image (see [Installing from the disc image](#installing-from-the-disc-image)). With no `MUSIC\TRACKnn.*` files yet, it then extracts the CD music from the image (see [CD music](#cd-music)).
2. `LOADER.EXE` starts `LOMAX.EXE` from its own folder (the game folder), suspended and with no arguments. The names `LOMAX.EXE` and `LOADER.DLL` are fixed.
3. The working directory is the game folder, wherever the loader was started from.
4. It lets the game's main thread run up to the EXE's entry point (a `jmp $` placed there, then the original bytes put back with the thread suspended again). Windows initialises the process and every imported DLL on the first thread that runs; if that were the loader's remote thread, on Windows XP the common controls would register their window classes where the game's thread can't use them, and Properties wouldn't open (no tab control). The game's own code still hasn't run.
5. It loads `LOADER.DLL` into the game through a remote `LoadLibraryA` thread.
6. It calls the DLL's exported `LomaxFix_Init` through a second remote thread, and waits for it. The DLL reads the options from the registry (`HKCU\Software\Psygnosis\Lomax\System` and `\Extra`); missing values mean defaults.
7. It resumes the game.

If initialisation fails, the game is closed and an error is shown.

### Safety check

The DLL checks the first bytes of each hooked function, and every widescreen site, before doing anything. If they don't match the retail EXE, it installs nothing (or no widescreen) and logs the address.

## Background

### The CD check

**Original behaviour:**
1. MCI `open cdaudio`. If that fails, the game shows `CInsertCDDlg` (dialog 0xAF) and loops.
2. `CD_VerifyLomaxDisc` (0x40E900) compares the lengths of tracks 2–24 with a table at 0x4625A0. If they don't match, it shows `CWrongCDDlg` (dialog 0xAE) and loops.

### Why it failed on 32-bit desktops

In windowed mode, `DDraw_InitSurfaces` reads the desktop depth with `GetDeviceCaps` and aborts unless it is 8 or 16 bpp. Its offscreen surfaces are created without a pixel format, so they take the desktop's format, and the frame is shown with `IDirectDraw::Blt` straight to the primary surface.

Fullscreen is no way out either. It needs a 320x240 mode at 8 or 16 bpp, and on this PC (Windows 11) DirectDraw lists only 32-bpp modes. Testing confirmed this: `EnumDisplayModes` from a 32-bit process returned 26 modes, all 32 bpp, none at 320x240.

## Hooks

| Fix | Hook | What it does |
|---|---|---|
| Keyboard + controller bindings (always on) | `Input_Update` 0x416660, `Input_ReadKeyboardAndJoystick` 0x4068C0, `CControlsTab_OnInitDialog` / `OnOK` / `OnTimer` / `UpdateKeyText` 0x402290 / 0x402390 / 0x402220 / 0x402170 (MinHook) | A keyboard and a controller slot per action; see [Controls](#controls). |
| Shared tiles (always on) | `Plane_BuildTileSurfaces` 0x415634, 0x415640, 0x415761–0x415791 (code patch) | The game draws each plane's 16x16 tiles (four 8x8 cells, each with flip bits and a palette) into tile pages, and reuses the drawing of an identical tile from an earlier plane. "Identical" only compared the cells and their flips, not their palettes, so a background tile made of the same cells as a level tile got the level's colours. In level 1.2 the tree canopy's top row showed black blocks this way (1.1's level plane has no such tile, so its canopy was right). The comparison now includes the palette bits (mask 0x6F00 instead of 0x6000). |
| Hard-right pan (always on) | 30 stores in `DSHw_*` 0x435352–0x435B7F (code patch) | The DirectSound-buffer backend is used instead of the software mixer when DirectSound reports an emulated driver. It pans with a 31-entry table that `DSHw_Init` fills at 0x5A02B0. The last entry (0x5A0328) is also where every `DSHw_*` function stores the last DirectSound result, so after the first call it held 0 = centre: sounds panned hard right, including the right channel of every ambient loop, played in the middle. Nothing reads that result, so the 30 stores now go to a variable in the DLL. |
| DirectSound mixing (always on, Windows Vista and later) | `SndDrv_Init` 0x437F80 (MinHook), jz 0x438057 during the call (code patch) | The sound driver has two backends: the game's own software mixer (one streaming DirectSound buffer, 8 mixed voices, hard clipping) and DirectSound mixing (`DSHw_*`: one static DirectSound buffer per sample, mixed by DirectSound, 32 voices). The game takes the second only when DirectSound reports an emulated driver (`DSCAPS_EMULDRIVER`). Since Vista every DirectSound device is emulated by the Windows audio engine, but without that flag, so the game always takes its own mixer. On Windows 6.0 and later (`GetVersion`, so XP compatibility mode keeps XP's choice) the loader counts the device as emulated for the call. It then creates one buffer the way `DSHw_LoadSample` does; if that or `SndDrv_Init` fails, the driver is shut down and started again with the game's own test, which gives the software mixer. The log shows the choice (`sound: DirectSound mixing` or `sound: software mixer`). On XP the game's test is unchanged. |
| Looping sounds in DirectSound mode (always on) | `DSHw_TimerService` 0x435A68 (code patch) | The DirectSound backend's timer frees a voice's buffer unless `GetStatus` is exactly 1 (playing) or 4 (looping). A playing loop reports 5, so every loop (ambience, helicopter) is rewound once and its buffer marked free while it keeps playing; another start of the same sample can then take the buffer and end the loop. The test becomes "bit 0 set" (`test cl,1; jnz`). `DSHw_StopVoice` frees a stopped loop's buffer itself. |
| 16 mixed voices (always on) | `Mixer_Init` 0x4342A6, `Mixer_SetMaxVoices` 0x4343CC (code patch) | The game's software mixer has 16 voice slots but mixes only the first 8 voices of its list (`g_mixerMaxVoices`, 0x5A5570). New voices go to the front, so with more than 8 sounds playing the oldest ones, usually the ambient loops and the helicopter, go silent mid-sound and carry on later. With the patch the limit starts at 16, and `Mixer_SetMaxVoices`, which refused values above 8, always sets 16. Every per-voice state lives in the voice record, so nothing else depends on the limit. With DirectSound mixing (Vista and later) the mixer is only the fallback. |
| Sound mixer crash (always on) | `Mixer_TimerService` 0x434D21 (code patch) | The software mixer runs on a multimedia-timer thread. It sets a counter (0x466A14) to the number of voices that just finished, then walks the voice list removing that many. The main thread adds 1 to the same counter, without a lock, whenever it queues a sound. A sound started in between made the walk run past the list's end and read address 0: an access violation at 0x434D26 that closed the game at random. The walk now stops at the list's end, and the main thread's queued commands are still applied on the next timer callback. |
| Sound menu (always on) | Game window subclassed (`WM_COMMAND`, `WM_INITMENUPOPUP`); main menu from `LoadMenuA` | A **Sound** menu between Display and Language toggles Effects, Ambient and Music. It uses the same flags as the Properties checkboxes (`g_cfgUseEffects` / `g_cfgUseAmbient` / `g_cfgUseCD`, 0x461CEC / 0x461CF0 / 0x461CF4) and the same follow-up as `Menu_OnProperties` (0x40E2C0) after OK: CD music starts or stops, the sound driver starts or pauses, looping effects stop, and the ambient sound starts or stops. Items are checked from the flags, and greyed when sound (`g_bSoundAvailable`, 0x461C78) or music (`g_bCDAudioAvailable`) isn't available. The game saves the flags on exit, as before. |
| Display menu (always on) | Game window subclassed (`WM_COMMAND`, `WM_INITMENUPOPUP`); main menu from `LoadMenuA` | A **Display** menu between File and Sound: Screen Size (1x–4x, the same sizes as Properties, radio-checked from the window's size) and Fullscreen (checked while in borderless fullscreen; greyed without the 16-bit emulation). Both act at once. |
| System page (always on) | Dialog 102 in memory; `CSystemTab_OnInitDialog` / `OnOK` 0x4016A0 / 0x4018C0 (MinHook) | The Display box with the display settings (see [Display settings](#display-settings-properties--system)). In the Sound box, "CD Music" is now "Music", and each checkbox is centred under its slider. |
| Extra page (always on) | `PropertySheetA` (game import table) | The **Extra** tab after Controls, with the remaining options (see [Extra settings](#extra-settings-properties--extra)). |
| Sheet width (always on) | `PropertySheetA` (game import table), sheet subclass | The sheet makes its tab area wider than the pages, which left a gap right of the group boxes. Once the sheet exists, the tab control, the sheet (keeping its centre) and its buttons are narrowed so the right gap equals the left one; each page gets the new width when it is created. |
| No "Use 4 channels only" (always on) | Dialog 102 in memory; `CLomaxApp_InitInstance` 0x40300A (code patch) | The System page's "Use 4 channels only" checkbox (control 0x3FB) is hidden. `g_cfg4Channels` (0x461CF8) stays 0: the store of the saved value is NOPed, so ambient sound is always on (the mixer mixes 16 voices, see above), and the game writes 0 back on exit. |
| Lomax+ branding (always on) | `CreateWindowExA`, `LoadMenuA`, `CreateDialogIndirectParamA`, `PropertySheetA`, `MessageBoxA` (game import table) | The window title, the menu item "About Lomax+...", the About dialog (caption and "Lomax+ for Windows 95"), the Properties caption, the exit question and message box titles say "Lomax+". Message texts are unchanged. |
| CD audio (always on) | `mciSendStringA` (game import table) | Every `cdaudio` command is answered by the DLL, so no drive or image is ever used. The commands play the `MUSIC` files, or do nothing when there are none. The "insert CD" dialog never appears. |
| CD audio (always on) | `CD_VerifyLomaxDisc` 0x40E900 (MinHook) | Always returns 1 without checking a disc. `g_bCDAudioAvailable` is 1 with music files and 0 without, which makes the game's `CD_*` functions do nothing. |
| 16-bit emulation (automatic) | `GetDeviceCaps` (game import table) | `BITSPIXEL` reports 16 instead of the real 32. |
| 16-bit emulation (automatic) | `DDraw_InitSurfaces(HWND, int bReload)` 0x407930 (MinHook) | Before surfaces are created, hooks `IDirectDraw2::CreateSurface` (vtbl+0x18). After they are created, hooks `IDirectDrawSurface::Blt` (vtbl+0x14) on the primary. |
| 16-bit emulation (automatic) | `CreateSurface` (ddraw.dll, MinHook) | Offscreen-plain surfaces without a pixel format get 16-bit RGB565. |
| 16-bit emulation (automatic) | `Blt` (ddraw.dll, MinHook) | A `Blt` onto the primary surface in windowed mode is replaced by a GDI `StretchBlt` into the window, scaled with the sharp scaler and letterboxed to the picture's shape. |
| Borderless fullscreen (with 16-bit emulation) | `Display_SetVideoMode` 0x403FA0 | Modes 2 and 3 (F7/F8) switch to borderless fullscreen. Modes 0 and 1 (F5/F6) set a 1x / 2x window (the widescreen width included), leaving borderless fullscreen first. |
| Hardware translucency (always on) | `D3D_InitDevice` 0x406AF0 (MinHook) | On a hardware Direct3D driver, the initial `TEXTUREMAPBLEND` (immediate at 0x407239) becomes `MODULATEALPHA` (4) instead of `MODULATE` (2), when the driver supports it. Translucent sprites and fade quads carry their opacity in the vertex alpha, but their textures also have an alpha bit (set on every non-colour-key texel). By the spec, `MODULATE` then takes alpha from the texture only, so on current drivers (NVIDIA) water came out solid and fades were missing. `MODULATEALPHA` multiplies the two. The state block's `ANTIALIAS` = none entry (already the default) also becomes `TEXTUREADDRESS` = `CLAMP` (0x407285 2→3, 0x40728A `ecx`→`eax`). The game left addressing on `WRAP`; clamping is a safeguard for the first column of each quad, which samples on texel 0's border. Quad edges: the sprite queue builds translucent quads from x to x+w−1 and y to y+h−1, with texture extents (w−1)/T and (h−1)/T. That is, inclusive end pixels, which 1997 rasterizers filled. Current ones apply the top-left fill rule and leave out the last column and row, so the 64-px water tiles had a 1-pixel gap. The quads now end at x+w and y+h: `lea …-1` → `+0` at 0x40A8D5/0x40A8F6 (`Sprite_QueueScreen`), 0x40ACD6/0x40ACF7 (`Sprite_QueueWorldScaled`) and 0x40CCBA/0x40CCE4 (`Actor_Draw`). The texture spans w/T and h/T (`dec edi`/`dec esi` → `nop` at 0x408F82/0x408F9F, `Texture_CreateFromFrame`). Half-pixel offset: current drivers put pixel centres on integer coordinates, exactly on the texel borders of a 1:1 sprite quad. So the view-matrix translation `_41`/`_42` (0x461C10/0x461C14, 0 in the game) moves all geometry by −½ pixel, and every pixel samples a texel centre. The software drivers are left unchanged. |
| Borderless fullscreen (with 16-bit emulation) | `Lomax_WndProc` 0x404400 | Alt+Enter toggles borderless fullscreen; `WM_SYSCHAR` Enter is swallowed so there's no beep. Esc leaves fullscreen. On `WM_DESTROY` the windowed position and size are put back before the game saves them to the registry. |

## How borderless fullscreen works

The game stays on its windowed code path (`g_bWindowed = 1`), which already works through the 16-bit surfaces and the GDI present. The DLL then:
- removes the menu and the window frame (`WS_POPUP`);
- sizes the window to the monitor it's on (`MonitorFromWindow`);
- hides the cursor, keeping the game's own `ShowCursor` counter (0x461D00) consistent.

GDI scales the 320x240 frame to the whole screen, letterboxed to keep its shape. The display mode never changes, so Alt+Tab and multiple monitors behave like any normal window.

If the game re-creates its surfaces (lost surfaces, mode change), the window is resized to cover the monitor again. Leaving fullscreen restores the original style, menu and window rectangle.

## Display settings (Properties > System)

The display options are in the **Display** box of Properties > System and are saved next to the game's own settings in `HKCU\Software\Psygnosis\Lomax\System` (REG_DWORD). The DLL reads them before the game starts. The System page template (dialog 102) is changed in memory before MFC loads it: the page is 219 dialog units tall instead of 175, the Display box takes the new rows, the Sound box moves down and ends below its checkboxes, and "Use 4 channels only" is hidden. The new controls are created after `CSystemTab_OnInitDialog` (0x4016A0) and read after `CSystemTab_OnOK` (0x4018C0).

| Setting | Registry value | Default | Applied | Meaning |
|---|---|---|---|---|
| Renderer | `Renderer` (0 = Direct3D 11, 1 = GDI) | Direct3D 11 | At once | How the picture reaches the window: Direct3D 11 = a flip-model swap chain, scaled on the GPU and without tearing (see [Direct3D 11 presenter](#direct3d-11-presenter)); GDI = the CPU scaler and a GDI copy. Direct3D 11 falls back to GDI for the session when anything in it fails. Only used while the 16-bit emulation is active (greyed otherwise). |
| Screen Size | the game's `Settings\WinW` / `WinH` | 2x (640 x 480) | At once | The window's client size: 1x–4x the internal picture (320 x 240, or W x 240 with widescreen). It replaces the game's list ("Current Settings", the two windows, the 8/16-bit fullscreen modes). The page's index (+0x8C), which `Menu_OnProperties` would pass to `Display_SetVideoMode`, is cleared, and the loader sizes the window itself; it also leaves fullscreen. The game's default size (`g_WinClientW/H`, used when the registry has none) becomes 640x480. |
| Start in Fullscreen | `StartFullscreen` | Off | Next start | Borderless fullscreen after the first surface init. |
| Widescreen, Width | `Widescreen` (0 = Disabled (320), 1 = 16:9 (432), 2 = Max (512), 3 = Custom (320 - 512)), `WidescreenWidth` (the custom width) | Disabled | Next start | Widen the internal screen (see [Widescreen](#widescreen)). Width is editable only with Custom: 320–512, rounded to a multiple of 16. |
| VSync | `VSync` | On | Next start | Game ticks locked to the display refresh. `Game_Frame` times its ticks with `timeGetTime` (`ms*10/167`, 59.88 Hz) and presents each frame at once, without waiting for the display. Whole milliseconds put frames 16 or 17 ms apart (sometimes 15 or 18), and 59.88 Hz drifts against a 60/120 Hz display (a full cycle every ~8 s at 60 Hz); whenever the frames come close to a refresh, the jitter puts them on alternate sides of it and frames are shown twice and skipped for a moment, with the FPS counter (presents per second) still at 60. The option replaces that timing block (0x404BD1, up to the 5-tick clamp) with `vsync_ticks`. A thread waits on the display driver's vertical blank (`D3DKMTWaitForVerticalBlankEvent`, for the monitor the game window is on, followed when it moves) and timestamps every refresh. With a refresh rate within 2% of a multiple of the tick rate (60, 120, 180, 240, 59.94 Hz; 50 and 100 Hz with `Speed50Hz`), one tick runs every N refreshes, drawn and presented right after the refresh, so each frame has a whole refresh to reach the screen and stays there for N refreshes. The game runs at refresh/N: 60.0 instead of 59.88 ticks per second at 60 Hz. After a stall, up to 5 ticks catch up, as in the original. Other rates (75, 144 Hz), no vblank events, or none for 100 ms fall back to the original timer; the loading screen keeps its own. DWM timing isn't usable for this: `DWM_TIMING_INFO.cRefresh` only moves when DWM composes, so a tick count based on it runs too fast on some setups and too slow on others, and frames timed halfway between refreshes from `qpcVBlank` skip about one frame in five at 120 Hz. Measured at 120 Hz: 60.00 ticks per second, each frame started within 0.1 ms of the refresh. With Loader Debug Log on, the log shows ticks and refreshes every 5 s. |
| FPS Override | `FPSOverride`, `FPS` | Off (120) | Next start | Frames drawn per second above the tick rate (up to 1000); the game logic stays at 60 Hz. The frames between ticks run no game code: they redraw the last tick's sprite queue with everything moved part of the way, see [High frame rate](#high-frame-rate). 0 = one frame per tick, as the original. Off = one frame per tick, as the original. The value is kept between 1 and 1000. |

After OK, when Widescreen, VSync or FPS Override now differs from what the running session started with, a message box (posted to the game window, so it shows once the sheet has closed) says "Some settings need a restart to take effect." Changing one back, or Start in Fullscreen, shows nothing.

With widescreen, the HUD is always at the edges: with widescreen, the power icon and its ammo (x 27/30/35) keep their distance from the left edge and the lives counter (x 278/285/295, i.e. W-42/W-35/W-25) its distance from the right edge, instead of moving with the centred status bar; lemmings, coins and the centre bar stay centred. The 1UP that flies to the lives icon (`Item_UpdateCoinLifeToHud`, 0x42CAAC) targets the moved icon. Implemented as an override of seven `Hud_Draw` entries in `widescreen.c` (`kWsHudEdges`).

## Extra settings (Properties > Extra)

The other options are on the **Extra** page, the last tab of Properties, and are saved in `HKCU\Software\Psygnosis\Lomax\Extra` (REG_DWORD). The DLL reads them before the game starts; a `LOADER.INI` file is ignored. The page is added when the game's sheet ("Lomax Properties", with System and Controls) reaches `PropertySheetA`: MFC passes its pages as a `PROPSHEETPAGE` array, and a copy of it gets a third entry with a dialog template built by the DLL (`PSP_DLGINDIRECT`) and its own dialog procedure, which reads the controls on `PSN_APPLY`. It takes the System page's font and dialog units, so the pages match. Like the System page, changing a setting that only applies at the next start brings up "Some settings need a restart to take effect." after OK.

| Setting | Registry value | Default | Applied | Meaning |
|---|---|---|---|---|
| Loading Screen | `LoadingScreen` | Off | Next start | Restores the console loading screen: Lomax with the hourglass, the unused `Loading` object of MAIN.WLD (anims `HERO`, `UHR`, `TEXT`). On the PS1 (SLES_004.51 `FUN_8002f6e0`, `FUN_8002fdf0`) it shows on every section change while the CD reads: HERO at (160,140), the hourglass 20 px to its left with its frame 0–14 set by the read progress, "DISC ERROR!" below only after a read error; 64-frame palette fade in and out. On PC, a hook on `Game_SetupNextLevel` starts it when the loaded section changes; `Game_Frame` (0x404AA0) is then replaced for 428 ticks (64 fade in, a 5 s hold of 300, 64 fade out; HERO stands for its first 24 frames, 2.85 s at 36/256 frame per tick, before Lomax looks at his wrist): colour-fill, HERO and UHR drawn from two dummy actors with the game's `Actor_SetAnim`/`Actor_AdvanceAnim`, each frame's parts in reverse order as the sprite queue draws them (the face and arm overlays over the body) (the hourglass empties over fade-in and hold), the game's own fade (`Render_DissolveFade` or `Render_ScreenFade`), and `DDraw_Present` with `g_bFrameReady` (0x59F1DC) set. No game tick runs, and the CRT `rand()` seed (`_getptd()`+0x14) is restored after each fade, so demos keep their sync. No music plays during it: the level's Music object already started its track at level start, so the first frame sends `stop cdaudio` (through the game's import, so the loader's music player handles it too), and at the end `CD_RestartTrack` (0x40E0C0) plays that track from its beginning. |
| Language Select | `LanguageSelect` | Off | Next start | Restores the console language menu before the title. TITLE.WLD's title section (0x17) has two sub-levels: plane mask 0x10, a 20×16 screen with a `LanguageMenu` placement (object id 4, `Obj_LangSelect_Init`/`_Update` 0x417E60/0x417F00), and 0x0F, the title. The PC port always entered sub-level 1 (`Game_SetupNextLevel` 0x417C3F `mov [g_CurSubLevel], 1`). A hook on `Game_SetupNextLevel` makes the first title load use sub-level 0, then updates the Language menu's radio mark (0x800F + `g_Language` 0x464570). The screen's own code is unchanged: up/down choose ENGLISH/FRANCAIS/DEUTSCH/ITALIANO/ESPAGNOL, jump/start or 20 s confirm. |
| 50Hz Mode | `Speed50Hz` | Off | Next start | PAL timing. The PS1 PAL release runs its per-tick game code at 50 Hz, so the player, enemies and animations move about 17% slower. The PC port kept the switch, `g_bSpeed50Hz` (0x46441C), but never sets it. Set, it makes second-based timers count 50 ticks (level timer, credits, the language menu's timeout) and raises particle launch speeds and gravity (debris, bursts, birds, pot lids, confetti, ambient emitters). The option sets the flag and changes `Game_Frame`'s step from 167 to 200 (`mov ebx` immediate at 0x404BE9), so the game ticks and draws at 50 Hz; the loading screen follows it. The flag also moves sprites for the PAL 256-line screen (HUD 4 px up, copyright 20 px down, title logo 8 px up, world-map props 16 px down, flag-2 parallax sprites without their 16 px raise). Those ten reads (in `Hud_Draw`, `Copyright_Draw`, `Logo_DrawWithAuthorCredits`, `Mapf_MapParallaxProp_Init`, `ParallaxSprite_Spawn`) are pointed at a zero dword, so the 240-line PC layout stays. |
| Cheat Mode | `Cheats` | Off | At once | Sets `g_bCheatsEnabled` (F1/F2/F3, I, fly mode), with no Tab+Backspace+LCtrl needed. The I key also shows the FPS / level overlay. |
| RGB Emulation | `RGBEmulation` | On | Next start | Hooks `D3D_SelectHardwareDriver` / `D3D_SelectSoftwareDriver` (0x405D40 / 0x405ED0). When the game picks the software "Ramp Emulation" driver, it switches to "RGB Emulation" (`g_D3DRenderMode` 2). Ramp needs 8-bit palettized textures, which are filled through a GDI BitBlt that ignores the texture palette on current Windows, so scaled and rotated sprites (title logo, boat) came out purple. RGB uses 16-bit RGB565 textures instead. |
| Fast Blit | `FastBlit` | On | Next start | The loader copies each frame's sprites and tiles itself instead of making one DirectDraw `Blt` per sprite (see [Fast sprite blitting](#fast-sprite-blitting)). Several times faster in busy scenes on current Windows, where 16-bit DirectDraw is emulated on the CPU. Off = DirectDraw's `Blt`, still timed in the log for comparison. |
| Transparency | `Transparency` (0 = Automatic, 1 = Solid, 2 = Dither, 3 = Blend) | Automatic | Next start | Translucent sprites and fades: Automatic, Solid, Dither, Blend. Automatic: the game's own choice. It takes the hardware path, with real blending, only when DirectDraw reports hardware colour-keyed blits (NVIDIA does, Intel doesn't); otherwise it uses a software driver, whose blend caps are 0, with checkerboard sprites and the tile dissolve. Dither: always the checkerboard and dissolve. After `D3D_InitDevice`, `g_bD3DTranslucency` (0x469A00) is cleared, which sends sprites to `Sprite_GetDitheredSurface` and fades to `Render_DissolveFade`. Solid: the same, plus hooks so that translucent sprites are drawn opaque and fades aren't drawn. `Sprite_GetDitheredSurface` (0x40A510) returns the sprite's own surface (+0x18), which is the game's own fallback. `Render_DissolveFade` (0x4055C0) draws nothing, but still makes its 32×32 calls to the game's `rand()` (0x43AAD0) while a fade runs and the game isn't paused. Game logic uses the same generator (spring boxes, particles, bosses), so dropping those calls would desync the attract demos. Blend: forces the hardware path (surfaces in video memory, so `D3D_SelectHardwareDriver` picks the "Direct3D HAL"); the z-buffer request falls back from 32 to 16/24 bits. On the Intel drivers the first Direct3D draw then crashes inside the driver (`igc32.dll`), whatever the render states. |
| Loader Debug Log | `Log` | Off | At once | Writes `LOADER.LOG` in the game folder (hooks installed, CD state, mode changes). It is emptied when the game starts with the log on. |
| — | `DemoTrace`, `Mute` | 0 | Next start | Testing aids, not on the page (set them in the registry): log spawns, despawns and the player position every 30 ticks (to compare a demo between width 320 and a wide run); set the effect/ambient/CD volumes to 0. |

## Building

You need MSYS2 with the mingw32 toolchain. The 32-bit MinHook (`libminhook.a` + `minhook.h`) is included in `LOADER\minhook\`. Run:

```bash
LOADER\build.bat
```

The MinHook and MSYS2 locations can be overridden with the `MINHOOK` and `MSYS2` environment variables. The script produces `LOADER.EXE` and `LOADER.DLL` in `LOADER\build\`; copy both into the game folder. Both are 32-bit and depend only on Windows system DLLs (`kernel32`, `user32`, `gdi32`, `msvcrt`).

### Source layout

`LOADER.EXE` is `loader.c`. `LOADER.DLL` is every `.c` file in `dll\`, one module per feature; each one keeps its own hooks, state and LOMAX.EXE addresses and installs its hooks from an `*_init` function, which `LomaxFix_Init` (`main.c`) calls in order. `lomax.h` declares what they share: the addresses used by more than one module, the configuration (`cfg`, filled from the registry by `extra_load` and `disp_load`) and the helpers in `main.c` (log, import-table and MinHook hooks, code patching after a byte check, registry values, window subclassing).

| Module | What it does |
|---|---|
| `main.c` | The entry point, the configuration, the helpers. |
| `ui.c` | The user interface: Lomax+ branding, the Properties sheet (the Extra page added, the tab area fitted), Properties > System (the Display box and its settings, the Display menu), Properties > Extra (the page and its settings), the Sound menu, the restart note. |
| `controls.c` | Keyboard and controller bindings, the controller, the Controls page. |
| `audio.c` | Mixer crash, 16 mixed voices, DirectSound mixing on Vista+, looping sounds, hard-right pan, CD check, CD music from files. |
| `render.c` | 16-bit surfaces, Direct3D driver and transparency, shared tiles, `DDraw_InitSurfaces`. |
| `present.c` | The windowed present: sharp scaling through GDI or Direct3D 11. |
| `window.c` | Borderless fullscreen, the window size. |
| `game.c` | Language select, 50 Hz mode, loading screen; `Game_Frame` and its timing block. |
| `widescreen.c` | Widescreen (with its patch table, `kWsPatches`) and demo playback. |
| `vsync.c` | VSync. |
| `hfr.c` | FPS Override; narrow layers mirrored in widescreen. |
| `fastblit.c` | Fast Blit. |

## Tested (Windows 11, 32-bit desktop, no CD)

The loader started the original `LOMAX.EXE`, and all hooks reported `MH_OK`.
- **CD:** the CD check was skipped with no dialog.
- **Display:** the title screen rendered correctly, both at the default size and at 640x480 after F6.
- **Exit:** the game quit cleanly through its own Exit dialog.
- **Borderless fullscreen** (1920x1080 monitor):
  - F8 covered the whole monitor (frameless, no menu), with the 4:3 picture letterboxed. The game's own counter showed 60 FPS.
  - Esc returned to the 656x539 window at its old position.
  - Alt+Enter toggled fullscreen in both directions, several times.
  - Quitting while fullscreen saved the windowed geometry to the registry (`WinX/WinY` 151/104, `WinW/WinH` 640/480), not the monitor size.

- **Sharp scaling:** at 1440x1080 the pixels stayed crisp and evenly sized, and the game's counter still showed 60 FPS.

Not tested: a second monitor, and running with the real CD.

## Scaling

At 1080p the 4:3 picture is 1440x1080, which is 4.5× the original. Plain nearest-neighbour scaling can't make every pixel 4.5 screen pixels wide, so each original pixel is drawn 4 or 5 pixels wide. When the camera scrolls, those widths move across the image, which looks like shimmering.

The picture is always scaled sharp and keeps its shape (black bars on a window of another shape). Same result as nearest-neighbour up to the next whole multiple above the target (×5) followed by an area-averaging shrink, but computed directly by the DLL's own scaler. See "Fast sharp scaler" below. Falls back to a GDI scaler (enlarge into a cached bitmap, then `HALFTONE` shrink) if the surface can't be read as RGB565. Crisp, with only a 1-pixel soft blend on pixel edges. No shimmer. Takes about 1.6 ms (4:3, 1440x1080) or 2.5 ms (widescreen, 1920x1066) per frame.

## Fast sharp scaler

The GDI scaler takes longer than one frame in widescreen fullscreen (a 2160x1200 intermediate image shrunk with `HALFTONE`), which caps the game at 50 FPS. `fast_sharp()` is used instead:

1. **Weights (computed once per window size):** for every output column and row, it stores the source pixel it falls in, the neighbour it may overlap, and an 8-bit blend weight. The weight is the share of the output pixel's box that crosses a source-pixel edge after the ×k pre-scale, so it is 0 everywhere except at pixel edges.
2. **Horizontal scaling:** the game's back surface (RGB565) is locked read-only. Each source row is scaled horizontally once into a 32-bit row cache, using a 64K-entry 565→RGB lookup table.
3. **Vertical scaling:** output rows are copied from the cache with `memcpy`, or blended between two cached rows at row edges, into a 32-bit DIB section.
4. **Output:** one 1:1 `BitBlt` to the window.

With Loader Debug Log on, the average present time is logged every 600 frames, e.g. `present: 432x240 -> 1920x1066 fast sharp, avg 2.49 ms`.

## Installing from the disc image

Put `LOADER.EXE` and `LOADER.DLL` in an empty folder together with the PC disc image (`.cue` + `.bin`, in the folder or one folder down, found as for the music) and start `LOADER.EXE`. When `LOMAX.EXE` isn't there, it installs the game from the image, then extracts the music and starts the game. Neither the image nor the original `SETUP.EXE` has to be mounted or run.

- **Reading the image.** The `.cue`'s first data track (`MODE2/2352` on this disc) holds an ISO 9660 file system. Each raw 2352-byte sector carries 2048 bytes of user data: at offset 24 for Mode 2 Form 1 (the mode byte at offset 15 is 2), at 16 for Mode 1. `MODE1/2048` images are read as is. The primary volume descriptor at sector 16 gives the root directory, whose records give each file's first sector and length.
- **What is copied.** What `SETUP.EXE` installs: `LOMAX.EXE`, `GAME.TXT`, `LOMAX.HLP` / `.CNT` / `.GID`, and the `AMBIENT`, `DEMOS`, `JINGLES`, `SFX` and `WORLDS` folders (110 files, 11.6 MB), with the disc's dates and without the read-only flag. Left out: `DIRECTX` (DirectX 3 setup), `MUSIC` (pointers to the audio tracks; the music comes from the extraction instead), `SETUP.EXE`, `REMLOMAX.EXE`, the `DSETUP` DLLs, `AUTORUN.INF`, `LOMAX.ICO` and `BLANK.BIN` (padding).
- **Nothing else is needed.** The game opens its data relative to its folder, and its registry settings (`HKCU\Software\Psygnosis\Lomax`: `System`, `Controls`, `Settings`) are read with defaults and written by the game itself on exit.
- **Interrupted installs.** `LOMAX.EXE` is copied last, so an install that stops halfway runs again on the next start. Errors (no image, missing `.bin`, not the Lomax PC disc, a file that can't be written) are reported in a message box.

Checked against a `SETUP.EXE` install: all 110 files are identical.

## CD music

**How the game plays it.** Music is plain CD audio, played through MCI by track number:
- `CD_PlayTrack(n)` (0x40DFF0) sends `set cdaudio time format tmsf` and `play cdaudio from n+1 to n+2 notify`.
- At the end of the track, MCI posts `MM_MCINOTIFY` / `MCI_NOTIFY_SUCCESSFUL`. `Lomax_WndProc` then calls `CD_RestartTrack` (0x40E0C0), which plays the track again; this is how the music loops.
- `CD_Pause` (0x40E110) saves `status cdaudio position` (`tt:mm:ss:ff`) and stops. `CD_Resume` (0x40E150) plays from that position.
- Volume goes through `CDAudio_SetVolume(0..255)` (0x438460): `auxSetVolume` on the CD line, 65535·log2(1 + v/255). `Game_UpdateFade` fades it along with the screen.

**The disc's `MUSIC\*.DAT` files.** The game never opens them; `LOMAX.EXE` has no `MUSIC` path. Each one is an ISO 9660 entry pointing at the first sector of an audio track (`MEDI1.DAT` = track 2 … `WESTWLD4.DAT` = track 25). Its length counts 2048 bytes per sector, while audio sectors hold 2352. Copying one through the file system keeps bytes 24–2071 of each sector, so 13% of the audio is lost, in a gap every 1/75 s.

**Extraction (`LOADER.EXE`).** With no `MUSIC\TRACK??.*` files:
1. It looks for a `.cue` in the game folder, then one folder down (e.g. `cd\LOMAX95.cue`), and reads its `FILE`, `TRACK` and `INDEX 01` lines.
2. Each audio track is copied raw (2352 bytes per sector) into `MUSIC\TRACKnn.WAV` (44.1 kHz, 16-bit stereo), with nn = the disc track number. A track runs from its `INDEX 01` to the next track's `INDEX 01` in the same file, which is what `play from n to n+1` plays, or to the end of its file.
3. A small window shows the progress; failures are reported in a message box.

For this disc that is tracks 2–26, about 600 MB. The image can be removed afterwards.

**Playback (`LOADER.DLL`).** The disc is never used: every MCI string containing `cdaudio` is answered by the DLL instead of MCI, so no drive or mounted image is read. `CD_VerifyLomaxDisc` always succeeds. Without `MUSIC\TRACK??.*` files, `g_bCDAudioAvailable` is set to 0 and there is no music. When the files exist:
- `g_bCDAudioAvailable` stays 1.
- A thread plays `MUSIC\TRACKnn.*` through `waveOut`, in 4 × 0.1 s buffers.
  - WAV is read directly.
  - FLAC, MP3, M4A and WMA go through Media Foundation (`mfplat`/`mfreadwrite`, loaded only when such a file is used). So does OGG, when its codec (Web Media Extensions) is installed.
  - Files must decode to 44.1 kHz 16-bit stereo.
- At the end of a track, the same `MM_MCINOTIFY` is posted to the game window, so the game's own looping works unchanged.
- `status cdaudio position` returns the `waveOut` position as `tt:mm:ss:ff`, so pause and resume work unchanged. Resuming seeks in the file (Media Foundation formats are decoded from the start up to that point).
- `CDAudio_SetVolume` is hooked and becomes a software gain on the samples, with the same log2 curve. Volume fades and the Properties volume work as before.

## Widescreen

### What changes

With **16:9**, the internal screen becomes **432x240** (27 tiles, 1.8:1). **Max** is 512x240, and **Custom** accepts 320–512 in steps of 16. Height stays 240. Off by default.

The game hardcodes its width as instruction immediates, so a table in `widescreen.c` (`kWsPatches`) lists **199 sites**. Each entry has the original bytes and a formula in `W`. The DLL checks every site before writing anything; if one doesn't match, it patches nothing and logs the address.

| Area | Changes |
|---|---|
| Display | Surfaces 448 wide (multiple of 32 for the dissolve-fade tiles; only 432 is shown). D3D viewport and z-buffer. View matrix _11 = 320/W and dvScaleX = W/2, so 3D sprites stay 1:1. Present source rect. Screen sizes 1x–4x (and F5/F6) = W×240 … 4W×960. |
| Drawing | Right-edge clipping in every `Sprite_Queue*` function, `Actor_Draw` and `TileLayer_Draw`. Fade loops. Screen centre for D3D quads and the pseudo-3D meshes. `MeshProp_Draw` (imm8 → imm16 rewrite). `×320` multiplies rewritten as `imul ×W`. |
| Camera | Camera x = player.x − W/2. Scroll limit = (map tiles − W/16)×16. Map right edge for collisions. Player and forced-scroll screen clamps. Zoom centring. Trapdoor start/checkpoint cameras. |
| Narrow levels | 16 of 104 maps are narrower than 27 tiles. The camera clamp is rewritten to centre them (scroll = maxScroll/2 when the map is narrower than the screen). The present draws black bars over the wrapped tiles on both sides, but only for maps up to 25 tiles wide, so the 26-tile Evil Ed arena has none. On such maps the repeated floor at the sides is solid. The collision code has a horizontal wrap the game never uses: with bit 0x20000000 of the player state flags (0x49C4C4), `Actor_CheckWallCollision` (0x412F50) and `Plane_FindGroundY` (0x413B90) read the collision map at x modulo the map width. Hooks set the bit for the duration of those calls and lift the wall check's stop at the map's right end. Without this, Lomax, kept only 16 px inside the screen edges, walked off the floor at width 512, where 48 px of the map repeat on each side. Off during demos. Parallax background planes and screen-space parallax sprites get the same centring offset (hooks on `ParallaxPlane_ApplyCamera` and `ParallaxSprite_Draw`), so slower or fixed layers such as the continue screen's lit floor patches stay lined up. |
| Narrow backgrounds | Background tile layers narrower than the screen (not the current plane) continue mirrored past their end instead of repeating, as the backdrop sprites do. This applies only on an axis where the original 320x240 screen never reached the layer's end. Layers the original already showed repeated keep the repeat: their parallax scroll (camera range × factor + offset) left the map, or their offset moves. Examples are the forest and hills of 2.1 and the castles of 3.2. The log lists them (`background plane N repeats`). |
| Objects | Spawn, no-pop-in and despawn windows. Rockslides, ambient flyers, floater spawner, particles, confetti, spare helmet, falling meshes. Water strips. Sound panning centre. |
| UI | HUD shifted as a block to stay centred. Banners, level timer, hint box, PRESS BUTTON TO START, logo, credits text table, continue counter, title pan and exit, map password box. Copyright and language-select x (small code caves in `int3` padding). |
| World map | Three hooks (`Map_WorldMap_Update`, `MapT_MapTrapdoor_Update`, `Mapf_MapParallaxProp_Update`). The original places everything from the map camera max(0, marker − 160): the trapdoor at camera + 50 (Lomax drops from it and lands under it), the sun rays at 2·camera + 50 and Evil Ed with his light at 2·camera + 280 on the 2× parallax plane, so the trapdoor is always 50 px from the left edge and Ed 40 px from the right one. In widescreen the map scrolls to max(0, camera − (W−320)/2), which centres the marker, but no further right than the map's end (`nWidthTiles`·16), past which only the black the original showed is allowed. The framing is kept a little inside the edges, k = (W−320)/4 px (28 at W 432): the trapdoor gets scroll + k in place of the camera, so it and Lomax's landing spot are 50 + k px from the left edge; the rays move with it, and Ed and his light are put k px in from W − 40. |
| Backdrops | Screen-fixed parallax sprites about a screen wide (no repeat, factor 0, no auto-scroll: the horizon pictures of the WEST and HAUNTED worlds) only cover the 320 px they were drawn for. The `ParallaxSprite_Draw` hook draws an extra mirrored copy past each uncovered edge, so the picture continues seamlessly instead of showing the layer behind it. |
| Demos | Recorded demos (title screen and attract demos: whenever `Game_SetupNextLevel` loaded a `DEMOS` recording) replay input, so the simulation must match the original. While one is loaded the camera and drawing stay widescreen, but the game logic that reads the camera sees the one the original game would have: player x - 160 clamped to the original limit, or, when other code places the camera, the wide camera's position/movement + (W-320)/2. `Objects_SpawnNearCamera` and `Actors_UpdateAll` run with every active plane's scroll swapped to that camera (parallax planes via remembered wide→original pairs, which also covers the values planes keep across level loads), and the despawn test in `Actors_DrawAndCull` reads it through a stub. The logic sites inside those (spawn/despawn windows, screen-edge objects, forced scroll, airship) get their original bytes back; `Trapdoor_Init` keeps placing the wide camera. Checked by tracing all 8 attract demos at width 320 vs 432: Lomax's position matches tick for tick. Objects placed at the 4:3 screen edges can pop in inside the wider view during demos. Normal play is unaffected. |
| Airship boss | Spawn x moved off the new right edge. The spawn-trigger keys (`camX % w + 350`) are computed from camX + (W−320) by two small stubs in the DLL, so the attack choreography keeps its timing. |

### Window size

The startup window size comes from the registry (`WinW`/`WinH`), where the game saves the 4:3 size. With widescreen on, the DLL maps the four screen sizes (320x240 … 1280x960) to W×240 … 4W×960 at startup and back on exit, so the registry always holds the 4:3 values (and widescreen off or plain `LOMAX.EXE` still start at the right size).

### Expected side effects

- **Earlier activation:** objects activate and spawn about 56 px earlier on each side relative to Lomax, and despawn later. Things the designers hid just off-screen are now visible.
- **Narrow areas:** single-screen areas (bonus rooms, ending, continue/language screens, the Evil Ed arena) show black bars on the sides.
- **Rockslides:** they trigger relative to the wider right edge.
- **Airship boss:** debris spreads further, and its crusher is drawn with one more tile.

### Testing status

Played through levels of every world in widescreen (HUD, bosses, world map, continue screen, narrow areas). The 7 attract demos were traced at width 320 and 432 and match tick for tick.

## High frame rate

**FPS Override** draws more frames per second than the game's 60 Hz tick rate.

The game logic keeps its fixed 60 Hz tick (50 with `Speed50Hz`), so game speed, physics and animation rates don't change. Only drawing is faster.

- **Tick frames.** When a tick is due, `Game_Frame` runs as before: ticks, then builds the frame into its sprite queue (tiles from `TileLayer_Draw`, actors from `Actor_Draw`), blits, fades and presents it.
- **In-between frames.** When no tick is due but a frame is (every 1/FPS s), no game code runs. `Game_Frame`'s timing block (0x404BD1) is replaced by `hfr_begin_frame`, which in that case jumps straight to the blit (0x404CB0) and redraws the last tick's queue with every sprite and tile moved. Fades use `rand()` for the dissolve pattern; its seed is put back after these frames, so demos keep their sync.
- **Motion.** While a tick's frame is built, each queued entry is tagged with what drew it: an actor (and which of its draw calls this frame), a tile layer, a text string (`Text_DrawString`, anchored at its screen position, so text stays still while the camera moves and scrolling credits move smoothly; it only moves between two frames with the same characters, because the title's letter-by-letter reveal of the authors' names redraws the string one character later and 8 px to the right, which interpolated as the line sliding back and forth), a screen-fixed piece (`Sprite_QueueScreen` from anywhere else, such as the menu window frames: never moved), or, for anything else, the camera. The same tag in the previous tick's frame gives its motion per tick. Every frame is drawn at `alpha` of the way from the previous tick to the last one, where `alpha` is the time since the last tick divided by the tick length. That puts the picture one tick (16.7 ms) behind the game, the usual cost of interpolation.
- **Zoom.** Each source also records the scale it is drawn at around its anchor, so in-between frames show a change of scale part of the way too. This covers:
  - tile layers and actors on the rotating depth planes, whose zoom changes while Lomax walks between lanes through a door or ladder;
  - actors drawn scaled (objects coming towards the screen);
  - Lomax himself during that walk and the world-map drop.

  Recording the scale matters because a zoomed plane's anchor, scroll × zoom, changes by several px per tick while the picture barely moves. Treated as plain movement, it would shake the in-between frames left and right during lane changes.
  - **Scaled sources** (zoom other than 1000, or 750 with its pre-shrunk tiles) get both edges of every rectangle moved.
  - **Unscaled sources** keep their sizes and move as one piece by a single offset: a layer's taken at the screen centre, a sprite's at its anchor. That keeps tiles edge to edge and never needs a stretched blit.
  - **Why it's decided per source:** deciding per rectangle would move a zoomed tile that happens to be exactly 16 px wide as unscaled next to scaled neighbours, opening 1 px grid lines for a frame during the zoom.
- **Nested frames.** The pause key is handled inside `Game_Frame`'s ticks, and `Game_Pause` / `Game_Resume` draw a frame by calling `Game_Frame` again. The outer call's state is kept across the inner one, and a nested call never draws an in-between frame. Otherwise the outer frame would not be recorded, and the next in-between frame would lay the inner frame's saved rectangles and D3D flags over a different queue, using a plain sprite's surface as a texture handle (a crash in `D3DIM.DLL` when pausing quickly during a lane change).
  - **Extra check:** an in-between frame is only drawn when the queue still holds as many entries as were recorded.
- **Not interpolated:** jumps of more than 48 px per tick (teleports, a camera wrapping on looping planes, a reused actor slot), checked at the source's anchor or the nearest screen point. These move at 60 Hz as before.
- **Screen edges.** The game clips what it queues to the screen and skips tiles outside it. Clipped edge tiles are widened back to their full 16x16 (12x12 at 0.75 zoom) before they move, and the ring of tiles just outside the screen is queued as well (clipped away until a frame moves it in), so fast scrolling uncovers tiles instead of the previous frame's pixels. The pool holds 1700 entries (0x477BDC up to the next global at 0x498F1C); the ring stops short of it.
  - **Zoomed layers** (during lane changes) get the same treatment. Their tiles are the 16×16 tiles scaled, and tile k starts at `floor(k·16·zoom/1000) − scroll`, as in `TileLayer_Draw`. The edge tiles are widened back in proportion, and the ring follows the same positions. Ring tiles come from the video-memory sheet only when `g_bHWStretchBlt` (0x461C84) is set, as the game's own zoomed tiles do.
  - **Sprites** (enemies, coins, large parallax background sprites) that kept the edge the game cut would open a strip of the previous frame when moving inward, 3–4 px at typical scroll speeds. Each sprite frame has its own surface, with its rectangle starting at (0, 0). So a side cut at the screen edge is extended back to the surface's bound, scaled and mirrored as drawn, by up to 64 source pixels, before the entry moves and is clipped again.
  - **Guard:** this happens only when the opposite side of that axis is at its surface bound or cut too. A piece of a shared sheet (a tile drawn as a sprite) is never extended.
  - **Padding:** `Sprite_CreateSurface` (0x415E30) rounds a frame's surface width up to a multiple of 8 and fills the extra columns with palette index 0, which is transparent. A side up to 7 px short of the surface's right bound therefore counts as at the bound.
  - **Why the allowance matters:** without it, most sprites fail the guard, and every full-update frame is off in its first 1–3 columns, as many as the scroll has moved (checked on frames saved from the attract demo).
- **Direct3D quads** (scaled, rotated or translucent sprites with hardware Direct3D: the title logo, the intro boat, "Let's go", money bags). `D3D_EmitTexturedQuad` clears the entry's +0x44 flag once it has drawn it, so the flag is restored with the rectangles every frame. Without that, in-between frames blitted them as plain sprites and they flickered as if transparent.
- **3D scenery** (the 'SURF' meshes: arches and towers, plane doors and ladders, drawn by `Sprite_Queue3DQuad`). Their quads have 3D vertices that Direct3D projects using a z-buffer. `Game_Frame` clears that z-buffer before the blit only when `g_bHas3DQuads` (0x461C88) is set, and it resets that flag on every call, before its timing block.
  - **In-between frames:** the loader sets the flag again for them. Otherwise the depth buffer stays uncleared, and the quads are drawn against the previous frame's depths and flicker whenever the camera moves.
  - **Movement:** the quads move with their mesh's origin (the object's position minus its own plane's scroll, plus the lift and depth from the depth planes' rotation), in depth as well.
- **Pacing with VSync** (the default). The refresh thread of `VSync` (driver vertical blank events) drives the frames: one is drawn right after every K-th refresh, K = refresh / FPS rounded, at least 1 (every refresh when FPS is at or above the refresh rate). Ticks are counted on the same refreshes:
  - refresh rate within 2% of a multiple of 60 Hz (60, 120, 180, 240 Hz): one tick every N refreshes; at 120 Hz the frames alternate between a tick frame and one exactly halfway;
  - other rates (144, 165 Hz): ticks at the original 16.7 ms length, timed from the refreshes' timestamps, so every frame shows the game exactly one frame period further on (144 Hz: 2.4 frames per tick).
  Without vertical blank events, or with VSync off, the pacing below takes over. (DWM timing isn't used: `cRefresh` only moves when DWM composes, and frames timed halfway between refreshes from `qpcVBlank` skip about one frame in five.)
- **Pacing without VSync.** Every frame waits for its 1/FPS slot, including those with a tick; a tick that falls due between two slots runs at the next one, still counted from when it was due, so the game speed doesn't change. (Pacing only the in-between frames would let a third, very short frame slip in before most ticks: 140–144 frames per second at FPS 120.) A slot is used up only by a frame that is drawn.
  - **Why:** while no frame can be drawn (no tick due and nothing to redraw yet, right after a level loads), the game calls `Game_Frame` millions of times a second. Moving the slot on every call would push it hours ahead and freeze the game on the way from the title to the world map.
  - Not synchronised with the display, so the frames drift against the refresh as in the original game.
- **FPS Override off, VSync on**: one frame per tick, the ticks locked to the refresh.

### Limits

- More frames than the monitor's refresh rate can't be shown; with `VSync` the frames never exceed it. Without `VSync`, presentation isn't synced to the refresh.
- Sprites that only appear in a tick's frame (just scrolled in) can pop in at the screen edge.
- Sprites drawn without `Actor_Draw`, `Text_DrawString` or `Sprite_QueueScreen` (some particles) follow the camera, which is right for world-space effects. On-screen text ("PRESS BUTTON TO START", the credits) has its own anchor instead, so it doesn't flicker while the camera moves.
- The log prints, every 5 s, how many tick frames and in-between frames were drawn.
- **Variable refresh rate (G-SYNC, FreeSync):** in borderless fullscreen the display refreshes when a frame arrives, so frames timed on its refreshes waited on refreshes that waited on frames. At 180 Hz this settled at about 100 frames per second, and with ticks counted on those refreshes the game slowed to 55%. When the measured refresh rate falls more than 1.5% below the display mode's rate, the loader logs it and times frames and ticks by the clock until the window moves to another monitor (`vb_follows_frames`).

Tested in the game at 60 and 120 Hz. It was developed as a separate `LOADER_HFR.EXE` and became the main loader once stable.

## Direct3D 11 presenter

Renderer **Direct3D 11** (the default).

### Why it is needed

The game's windowed present (`primary->Blt`) is replaced by the loader. With the GDI renderer, the loader scales the 16-bit picture on the CPU into a DIB (the fast sharp scaler) and copies it into the window with `BitBlt`.

DWM composes the window from those pixels on its own schedule and doesn't know when a frame is complete. A copy still in progress when DWM composes shows as a tear. The copy takes about 3 ms at 1920×1080 and 5 ms or more at 2560×1440, most of a 120 Hz refresh. With `VSync` it also starts right after the refresh, about when DWM tends to read. So fullscreen at 1440p tore often.

### What the loader does

- **Upload:** the RGB565 picture (432×240) is converted to a BGRA texture through a lookup table.
- **Scaling:** a pixel shader scales it into the swap chain's back buffer, with the same layout (shape kept) and widescreen black bars as the GDI path:
  - sharp: nearest up to the next integer factor, then an area-average down, as the CPU scaler does. It is computed directly: the sample point snaps to the texel centre except within one output pixel of a texel edge, where the hardware's bilinear filter blends.
- **Present:**
  - The swap chain is flip-model (`FLIP_DISCARD`, or `FLIP_SEQUENTIAL` before Windows 10), with 2 buffers and a maximum frame latency of 1.
  - `Present(0, 0)` never waits, and the frame pacing stays with the game loop (`VSync`).
  - Without `DXGI_PRESENT_ALLOW_TEARING`, a flip-model swap chain never tears. DWM, or the display directly when Windows promotes the borderless fullscreen window to independent flip, only shows finished frames.
  - `MakeWindowAssociation` stops DXGI from handling Alt+Enter, which stays with the loader's borderless fullscreen.
- **Resizing:** the back buffers follow the client area (`ResizeBuffers`).
- **Shaders:** the HLSL is compiled at start-up with `d3dcompiler_47.dll`, which ships with Windows 10/11.
- **Fallback:** if anything fails (no Direct3D 11 device, no DXGI 1.2, the shader compiler missing, `Present` failing with a removed device), the log says what failed and the GDI path takes over for the rest of the session. The log also shows the present time every 600 frames.

## Fast sprite blitting

**Fast Blit** on (the default).

### Why it is needed

`Game_Frame` draws its sprite queue bucket by bucket, with one `IDirectDrawSurface::Blt` per tile or sprite. The flags are `DDBLT_KEYSRC`, plus `DDBLT_DDFX` for mirroring, and the blit stretches when the rectangles differ. Each bucket's Direct3D quads are executed after its blits.

On current Windows, 16-bit DirectDraw is emulated on the CPU, and every `Blt` has a large fixed cost (locks, rectangle and format checks, colour-key setup) next to copying a 16×16 tile. A busy screen means hundreds of blits per frame, all of them repeated on every in-between frame with FPS Override. That held 180 fps down to around 90 in scenes with many coins and enemies.

### What the loader does

The `Blt` call in that loop (0x404DFD) is replaced by `sb_blt`:

- **Locking:**
  - The target and each source surface are locked once per frame.
  - They are unlocked before a bucket's Direct3D quads are executed (0x404E13) and after the last bucket (0x404EB9), before the fades, the GDI text and the present.
- **Copying:** the pixels are copied by a plain loop with DirectDraw's rules:
  - the source colour key (or key range);
  - left-right and up-down mirroring;
  - nearest-pixel stretching;
  - nothing drawn for rectangles DirectDraw would refuse (empty, or outside a surface; the game sets no clipper).
- **Fallback:** anything else goes to the real `Blt`: other flags or effects, a surface that isn't 16-bit, different pixel formats, or a failed lock.

### Log

With Loader Debug Log on, the log shows the loop's time per frame every 5 s, with the number of blits and how many went to DirectDraw (normally 0).

- **The loop's time:** from the first blit to the end of the last bucket, including the buckets' Direct3D quads.
- **With Fast Blit off:** the same line times DirectDraw's own `Blt`, for comparison.

### Results and limits

- **Tested:** held 180 fps in the scenes where the DirectDraw path dropped to 90.
- **Unchanged:** the fades, the background clear, the 3D quads (Direct3D's software renderer) and the final scaling to the window. They run once per frame and weren't the bottleneck.
- **History:** developed as a separate `LOADER_SB.EXE`.

## Controls

The game binds one input per action. `g_KeyBinds[13]` (0x4643C0) lists them in this order: Left, Right, Up, Down, Jump, Shoot, Ability, Select, Run, Helm, LookDown, LookUp, Pause. Each entry is an index into `g_DIKeyState`:
- 1–0xFF: DirectInput keyboard scancodes;
- 0x100–0x103: joystick 0's X/Y axes (`Joy-Left/Right/Up/Down`);
- 0x104–0x10C: its buttons 1–9 (`Joy-Button1`–`9`).

The Controls page gives the focused field the first new press, but refused one that another action already had.

The loader changes this:
- **Two slots per action.** The keyboard slot stays in `g_KeyBinds`, and the game saves it as before. The controller slot is saved as `Pad<action>` (`PadLeft`, `PadJump`, …) next to the game's values in `HKCU\Software\Psygnosis\Lomax\Controls`.
- **Assigning.** In a field, a key sets the keyboard slot and a controller input sets the controller slot. An input that another action already has in the same slot is swapped with it. Changes apply on OK/Apply, as before.
- **Emptying and resetting.** Right-clicking a field empties both its slots. The **Reset** button, added under the two icons in the Directional Buttons box, restores the default keys and controller layout on the page.
- **Layout.** The page is rearranged when it opens (`ctl_layout`): Directional Buttons has the keyboard and joystick icons side by side with Reset below them, and Up, Down, Left and Right in one column of 100 px fields with their labels to the left. Action Buttons is laid out like a pad: Select, Spare Helmet and Run with their labels above; Ability and Shoot with their labels at the sides; Jump below them; Look Up, Look Down and Pause at the bottom with their labels below. The action fields are 70 px wide and every field is 20 px tall (at 96 DPI; the layout scales with the dialog font). The tab order follows the same order, with Reset after the directions. The Action Buttons box ends at the same height as the System page's Sound box.
- **Display.** A field shows both inputs, keyboard first, for example `W / A`. `CControlsTab_UpdateKeyText` (0x402170) writes this text too. The game calls it from every field's change handler and from the timer; in the original it writes the key's name alone, which would fight with the combined text and flicker.
- **In play.** `Input_Update` reads `g_DIKeyState[g_KeyBinds[i]]`. Right after it polls the devices, an action whose controller input is down reads `g_DIKeyState[0x10F]` instead. The game clears that byte on every read and never binds it; the loader sets it here. `g_KeyBinds` is restored afterwards.
- **Run's default key** is L-Shift instead of R-Control. This applies when no key is saved, and to Reset.
- **First run.** A joystick input bound in a keyboard slot moves to the controller slot, and the keyboard slot gets its default key back (if no other action uses it). Controller slots that were never saved get the default layout. Slots holding the loader's older default layout get the current one.

The controller is the first joystick, read by the loader through `joyGetPosEx` with everything it reports. Inputs that the controller doesn't have are ignored.

Bindings are stored as positions on an Xbox controller. The fields name them the way the controller in use labels them: Xbox names for a Microsoft controller (vendor 045E), Sony names for a Sony one, and raw numbers for any other. The names follow the last controller seen; before any is seen, they are Xbox names. They change on the open page when another kind of controller is used.

| Position | Xbox | Sony | Other controllers |
|---|---|---|---|
| Left stick | `LS-Left` … `LS-Down` | `LS-Left` … `LS-Down` | `LS-Left` … `LS-Down` (X/Y axes) |
| Face buttons | `A` `B` `X` `Y` | `Cross` `Circle` `Square` `Triangle` | `J1`–`J4` |
| Shoulders | `LB` `RB` | `L1` `R1` | `J5` `J6` |
| Menu buttons | `Back` `Start` (View, Menu) | `Create` `Options` | `J7` `J8` |
| Stick clicks | `L3` `R3` | `L3` `R3` | `J9` `J10` |
| Extra buttons | `J11`–`J16` | `PS` `Touchpad` `Mute` | `J11`–`J16` |
| D-pad | `D-Up` … `D-Left` | `D-Up` … `D-Left` | `D-Up` … `D-Left` (POV hat; diagonals count as both directions) |
| Triggers | `LT` `RT` (Z axis, up / down) | `L2` `R2` (their buttons) | `LT` `RT` (Z axis) |
| Right stick | `RS-Left` … `RS-Down` (U / R axes) | `RS-Left` … `RS-Down` (Z / R axes) | `RS-Left` … `RS-Down` (U / R axes) |

- **Sony controllers.** DualShock 4, DualSense and DualSense Edge (vendor 054C, products 05C4, 09CC, 0BA0, 0CE6, 0DF2) number their buttons and axes differently. They are recognised by their IDs and translated to the same positions, so they share the defaults and saved bindings with an Xbox controller: Cross is Xbox A, Options is Xbox Start, and so on. Their triggers are separate inputs.
- **Other controllers** keep the joystick interface's raw numbering.
- **Left stick and D-pad.** In play, a left stick direction and the same D-pad direction count as one input. An action bound to `LS-Left` also follows `D-Left`, and the reverse. The Controls page still records whichever one was pressed.
- **Axes** count as pressed past 30% of their range from the centre.
- **Xbox triggers.** Through this interface, an Xbox controller's two triggers share the Z axis, so pressing both together reads as neither.
- **Log.** With Loader Debug Log on, the log names the controller, gives its vendor:product ID and axes, and says whether the Sony translation is in use.

Default controller layout:

| Action | Xbox | Sony |
|---|---|---|
| Directions | Left stick | Left stick |
| Jump | A | Cross |
| Shoot | B | Circle |
| Ability | X | Square |
| Select | LB | L1 |
| Spare Helmet | Y | Triangle |
| Run | RB | R1 |
| Pause | Start | Options |

Look Down and Look Up are unbound.

## Known limitations

- **Real exclusive fullscreen isn't available:** the game only knows 320x240 at 8 or 16 bpp, and DirectDraw on Windows 10/11 doesn't offer those modes.
- **Music needs the extracted files.** The music is played only from `MUSIC\TRACKnn` files, never from the disc. Without them (no disc image to extract from), there is no music. Effects, jingles and ambient sounds work.
- **Transparency effects:** whether Direct3D effects (alpha-blended sprites) use the hardware or the dithered fallback depends on the driver. The game picks this itself; the DLL doesn't change it.
- **To undo:** run `LOMAX.EXE` directly. Nothing in the game files is modified.
