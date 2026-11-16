/*
 * game.h - what FLINK.DLL knows of the retail LOMAX.EXE (base 0x400000): its globals, functions,
 * object numbers and data layouts. Names follow the Ghidra project and ..\LOADER\docs\decompilation.md.
 *
 * Patch sites (the bytes FLINK.DLL overwrites) are not here: each is defined next to the code that
 * patches it, with the retail bytes it checks first.
 */
#ifndef GAME_H
#define GAME_H

#include <windows.h>
#include <string.h>

#define FOURCC(s)          ((DWORD)(s)[0] << 24 | (DWORD)(s)[1] << 16 | (DWORD)(s)[2] << 8 | (DWORD)(s)[3])

typedef void (__cdecl *ActorFn)(BYTE *actor);
typedef void (__cdecl *SpawnFn)(BYTE *actor, BYTE *placement);

/* ---- Actors ---------------------------------------------------------------------------------------
   g_Actors: 98 actors of 0x120 bytes. +0 type, +4 placement, +8 current record, +0x18 update, +0x1C
   draw, +0x3C/+0x40 x/y (the feet), +0x44/+0x48 the same in 16.16, +0x4C/+0x50 vx/vy (16.16, y down),
   +0x58 flags, +0x5E plane, +0x60 draw layer, +0x62 cull phase (3: never despawned by the camera),
   +0x64 the record's frame count, +0x68 / +0x6A the record's speed and position (8.8), +0x70 hidden
   pieces (bit n hides slot n), +0x7C bottom clip line; from +0x80 the object's own fields. */
#define G_ACTORS           ((BYTE *)0x0046ABC0u)
#define ACTOR_STRIDE       0x120
#define MAX_ACTORS         98

#define A_TYPE(a)          (*(BYTE **)(a))
#define A_DEF(a)           (*(BYTE **)((BYTE *)(a) + 0x04))
#define A_RECORD(a)        (*(BYTE **)((BYTE *)(a) + 0x08))
#define A_UPDATE(a)        (*(void **)((BYTE *)(a) + 0x18))
#define A_DRAW(a)          (*(void **)((BYTE *)(a) + 0x1C))
#define A_X(a)             (*(int *)((BYTE *)(a) + 0x3C))
#define A_Y(a)             (*(int *)((BYTE *)(a) + 0x40))
#define A_FX(a)            (*(int *)((BYTE *)(a) + 0x44))
#define A_FY(a)            (*(int *)((BYTE *)(a) + 0x48))
#define A_VX(a)            (*(int *)((BYTE *)(a) + 0x4C))
#define A_VY(a)            (*(int *)((BYTE *)(a) + 0x50))
#define A_FLAGS(a)         (*(DWORD *)((BYTE *)(a) + 0x58))
#define A_PLANE(a)         (*(short *)((BYTE *)(a) + 0x5E))
#define A_LAYER(a)         (*(short *)((BYTE *)(a) + 0x60))
#define A_CULL(a)          (*(short *)((BYTE *)(a) + 0x62))
#define A_NFRAMES(a)       (*(short *)((BYTE *)(a) + 0x64))
#define A_FRAME(a)         (*((BYTE *)(a) + 0x6B))          /* the record's whole frame */
#define A_HIDE(a)          (*(WORD *)((BYTE *)(a) + 0x70))
#define A_CLIPY(a)         (*(short *)((BYTE *)(a) + 0x7C))
#define A_CLIPTOP(a)       (*(short *)((BYTE *)(a) + 0x7E))  /* nClipTopY: drawn only below it (trapdoors; -20000 none) */

#define AF_HIDDEN          0x4u
#define AF_LEFT            0x8u          /* facing left (mirrored) */
#define AF_HURTS           0x20u         /* touching it hurts Lomax */
#define AF_SCREEN          0x40u         /* x/y in screen space */
#define AF_HITTABLE        0x10000u      /* Lomax's stomp and attacks reach it */
#define AF_CONTACT         0x30020u      /* hurts, hittable and 0x20000: cleared, nothing touches it */
#define CULL_NEVER         3
#define KILLED_LAYER       0x4D2         /* +0x60 after Actor_Kill / Actor_Despawn */

/* Actor type: +4 its name, +0x18 its placements' param count, +0x1C its bank flags */
#define T_NAME(t)          ((const char *)(t) + 4)
#define T_NPARAMS(t)       (*(short *)((BYTE *)(t) + 0x18))
#define T_BANKS(t)         (*(WORD *)((BYTE *)(t) + 0x1C))

/* Placement (a world file's object): +4 the next linked one, +0xC/+0xE x/y, +0x10 flags (bit 0
   "once", 0x2000 never spawned by proximity, 0x4000 the Start, 0x8000 linked), +0x14 state (bit 0
   used, 0x8000 spawned: Object_TrySpawn sets it, Actor_Kill keeps it), +0x1A the params (shorts) */
#define D_NEXT(d)          (*(BYTE **)((BYTE *)(d) + 0x04))
#define D_X(d)             (*(short *)((BYTE *)(d) + 0x0C))
#define D_Y(d)             (*(short *)((BYTE *)(d) + 0x0E))
#define D_FLAGS(d)         (*(WORD *)((BYTE *)(d) + 0x10))
#define D_STATE(d)         (*(WORD *)((BYTE *)(d) + 0x14))
#define D_PARAM(d, n)      (*(short *)((BYTE *)(d) + 0x1A + 2 * (n)))
#define DF_ONCE            0x1u
#define DF_START           0x4000u
#define DS_USED            0x1u
#define DS_SPAWNED         0x8000u

/* ---- Planes ---------------------------------------------------------------------------------------
   g_Planes: 0x9C bytes each. +4/+8 camera x/y, +0x10/+0x14 width/height in 16 px cells, +0x1C the
   collision cells, +0x38 nDepth (its sprite-queue bucket, from Planes_UpdateDepth; an actor's is this
   + {3,4,8,10,12,14,6,2}[layer], Actor_GetDrawDepth 0x416920), +0x3C nRotating, +0x40 nOrbitPos
   (short), +0x48 empty. */
#define G_PLANES           ((BYTE *)0x0049B880u)
#define PLANE_STRIDE       0x9C
#define PLANE(n)           (G_PLANES + (n) * PLANE_STRIDE)
#define P_CAMX(p)          (*(int *)((BYTE *)(p) + 0x04))
#define P_CAMY(p)          (*(int *)((BYTE *)(p) + 0x08))
#define P_W(p)             (*(int *)((BYTE *)(p) + 0x10))
#define P_H(p)             (*(int *)((BYTE *)(p) + 0x14))
#define P_COLL(p)          (*(BYTE **)((BYTE *)(p) + 0x1C))
#define P_MAXX(p)          (*(short *)((BYTE *)(p) + 0x30))   /* nMaxScrollX: the camera's x goes up to this */
#define P_MAXY(p)          (*(short *)((BYTE *)(p) + 0x32))   /* and its y */
#define P_DEPTH(p)         (*(int *)((BYTE *)(p) + 0x38))
#define P_ROTATING(p)    (*(int *)((BYTE *)(p) + 0x3C))
#define P_ORBITPOS(p)      (*(short *)((BYTE *)(p) + 0x40))
#define P_EMPTY(p)         (*(int *)((BYTE *)(p) + 0x48))
#define G_PCURPLANE        ((BYTE * volatile *)0x00498F1Cu)
#define NO_GROUND          20000         /* Plane_FindGroundY: no ground */
#define G_GROUNDSLOPECLASS ((volatile int *)0x0052C4C8u)  /* Plane_FindGroundY: 0 flat, 1 gentle, 2 45 degrees */
#define G_GROUNDMIRRORED   ((volatile int *)0x0052C5A4u)  /*   the slope descends to the left */
#define G_TICKCOUNTER      ((volatile int *)0x0052C564u)  /* the game's tick count: + 1 each tick */

/* ---- The player ----------------------------------------------------------------------------------- */
#define G_PLAYERX          ((volatile int *)0x0059F23Cu)
#define G_PLAYERY          ((volatile int *)0x0059F240u)
#define G_PLAYERXFIXED     ((volatile int *)0x0059F244u)   /* 16.16 */
#define G_PLAYERYFIXED     ((volatile int *)0x0059F248u)
#define G_PLAYERVX         ((volatile int *)0x0059F24Cu)
#define G_PLAYERVY         ((volatile int *)0x0059F250u)
#define G_PLAYERFLAGS      ((volatile DWORD *)0x0059F258u)  /* g_Player +0x58: AF_LEFT = facing left */
#define G_PLAYERPLANE      ((volatile short *)0x0059F25Eu)
#define G_PLAYERACTOR      ((BYTE *)0x0059F200u)            /* g_Player, an Actor */
#define G_PLAYERANIM       ((BYTE * volatile *)0x0059F208u) /* g_Player.pAnim: hitbox below */
#define HB_X1(anim)        (*(const short *)((anim) + 0x10))  /* the hitbox, from the feet */
#define HB_Y1(anim)        (*(const short *)((anim) + 0x12))
#define HB_X2(anim)        (*(const short *)((anim) + 0x14))
#define HB_Y2(anim)        (*(const short *)((anim) + 0x16))
#define G_PLAYERSTATE      ((volatile DWORD *)0x0049C4C4u)  /* 1 = in the air, 0x10 = on a rope, 0x200 = in the water,
                                                               0x4000080 = frozen (no actor updates) */
#define PS_AIR             0x1u
#define PS_ROPE            0x10u
#define PS_WATER           0x200u
#define PS_FROZEN          0x4000080u
#define PS_FLY             0x20000u      /* the cheat's fly / no-clip mode */
#define ROPE_STATE         0x1010u       /* Player_TryGrabHangPoint: hanging (Player_Update draws him hanging, leaves him in place) */
#define ROPE_CLEAR         0x400400u     /*   (and clears these) */
#define ROPE_CLIMB         0x6000u       /* climbing up / down the rope */
#define PS_CAMX_STAYS      0x20u         /* Player_Update doesn't move the camera's x (x - 160) */
#define PS_CAMY_STAYS      0x40u         /*   nor its y (smoothed to y - g_CameraPlayerOffsetY) */
#define PS_NO_EDGES        0x8000000u    /*   nor keeps Lomax 16 px inside the screen's edges */
#define PS_WRAP            0x20000000u   /* the map wraps around across (the airship); ForceScroll_Init sets it */
#define G_PLAYERHURTTIMER  ((volatile int *)0x0059E938u)   /* g_PlayerHurtTimer: Player_TakeHit sets 180 (helmet lost) or 1000 (killed) */
#define G_BHASHELMET       ((volatile int *)0x0052C570u)   /* g_bHasHelmet: Lomax wears the helmet */
#define G_BHELMETINFLIGHT  ((volatile int *)0x0059E944u)   /* g_bHelmetInFlight: his helmet is thrown */
#define G_BHASSPAREHELMET  ((volatile int *)0x0059F1E4u)   /* g_bHasSpareHelmet: the backup helmet */
#define G_PLAYERGROUNDY    ((volatile int *)0x0052C558u)
#define G_PLAYERSPEEDCAP   ((volatile int *)0x0049C4B4u)   /* g_PlayerSpeedCapOverride: Player_Update's walking limit instead of
                                                              g_PlayerMoveTable's (16.16, 0 none; Barrel_Update sets it, Level_Start clears it) */
#define G_PLAYERDEATHTIMER ((volatile int *)0x0059C708u)
#define G_PRIDINGPLATFORM  ((BYTE * volatile *)0x0059C744u)
#define G_JUMPHOLDFORCE    ((volatile int *)0x0059E94Cu)
#define G_JUMPTAKEOFFTIMER ((volatile short *)0x0059E764u)
#define G_BAIRSPIN         ((volatile int *)0x0059F164u)
#define G_WATERESCAPETIMER ((volatile int *)0x0059E95Cu)
#define G_LIVES            ((volatile int *)0x0052C56Cu)   /* up to 30 */
#define G_BPENDING1UP      ((volatile int *)0x0059E770u)   /* g_bPending1Up: a 1UP is flying to the HUD (Level_Start grants it if still set) */
#define G_CONTINUES        ((volatile int *)0x0059F160u)

/* ---- Input ---------------------------------------------------------------------------------------- */
#define G_INPUTHELD        ((volatile WORD *)0x00464434u)   /* the buttons held this tick */
#define G_INPUTPRESSED     ((volatile WORD *)0x00464438u)   /* the buttons pressed this tick */
#define G_HELMMASK         ((volatile DWORD *)0x0052C6C8u)  /* g_HelmButtonMask: throws the helmet (control scheme) */
#define G_BCHEATSENABLED   ((volatile int *)0x004645B8u)
#define G_JUMPMASK         ((volatile DWORD *)0x004777CCu)  /* the Jump button's g_InputHeld bits (control scheme) */
#define INPUT_UP           0x1000
#define G_BINPUTLOCKED     ((volatile int *)0x0052C52Cu)
#define G_CONTROLSCHEME    ((volatile int *)0x00464580u)   /* the control scheme Level_Start copies the masks of */
#define G_SCHEMEMASKS      ((const DWORD *)0x00464588u)     /* g_ControlSchemeMasks: {Jump, Helm, Ability, DropHelmet} per scheme */
#define SCHEME_ABILITY     2
#define G_BPAUSED          ((volatile int *)0x0052C5F8u)

/* Actors_UpdateAll (0x40EE50) runs the actors whose bit is set in three masks: slots 0-31, 33-64 and 66-97 */
#define G_ACTORMASK0       ((volatile DWORD *)0x0052C5A0u)
#define G_ACTORMASK1       ((volatile DWORD *)0x0059E768u)
#define G_ACTORMASK2       ((volatile DWORD *)0x0059F1F8u)
#define NO_CLIP            20000         /* +0x7C: nothing clipped (the player's, Level_Start) */
#define G_BD3DTRANSLUCENCY ((volatile int *)0x00469A00u)  /* translucent sprites really blended (else a checkerboard, or opaque with Lomax+'s Solid) */

/* ---- Levels and flow ------------------------------------------------------------------------------ */
#define G_CURSECTION       ((volatile int *)0x004645C4u)
#define G_CURSUBLEVEL      ((volatile int *)0x004645BCu)
#define G_LOADEDSECTION    ((volatile int *)0x004645C0u)
#define G_LASTREALSECTION  ((volatile int *)0x004645C8u)
#define G_BGOTOTITLE       ((volatile int *)0x0052C6C4u)
#define G_BBONUSOPEN       ((volatile int *)0x0059C718u)
#define G_WATERLEVELY      ((volatile int *)0x0059F1C4u)   /* g_WaterLevelY: the Water actor's y, set by Water_Update */
#define G_COINS            ((volatile int *)0x00498F2Cu)   /* int g_Coins: 100 give a 1UP */
#define G_LEMMINGSFREED    ((volatile int *)0x0052C5A8u)   /* int g_LemmingsFreed: the HUD's lemming count (Lomax: 50 open the bonus trapdoor; Flink: defeated enemies, a currency) */
#define G_HUDLEMMINGFLASH  ((volatile int *)0x0052C700u)   /* int g_HudLemmingFlashTimer: the HUD's count flashes while > 0 */
#define G_BGOTOMAP         ((volatile int *)0x0059E958u)
#define G_BDEMOMODE        ((volatile int *)0x0052C6D0u)
#define G_BLEVELENDING     ((volatile int *)0x0059F174u)
#define G_BLEVELEXITREACHED ((volatile int *)0x0049C4B0u)
#define G_MAPSCROLLTARGETX ((volatile int *)0x0049C4B8u)   /* g_MapScrollTargetX: the map screen's camera x target */
#define G_MAPZOOMBIAS      ((volatile int *)0x00464630u)   /* g_MapZoomBias: + 120 = the map's orbit offset per cent */
#define G_BFRAMEREADY      ((volatile int *)0x0059F1DCu)   /* g_bFrameReady */
#define G_FADESTEP        ((volatile int *)0x0052C5F4u)
#define G_RESTARTX         ((volatile int *)0x0046AB44u)    /* where Lomax restarts after a life */
#define G_RESTARTY         ((volatile int *)0x0046AB48u)
#define G_PRESTARTPLANE    ((BYTE * volatile *)0x0059E918u)
#define G_RESTARTORBITOFFSET ((volatile int *)0x0046AB7Cu)
#define G_CAMERAORBITOFFSET ((volatile short *)0x0052C5C4u)
/* the plane doors' walk into or out of a background plane (PlaneDoor_Update 0x432D30): while the
   scale is not 0, Actor_Draw draws Lomax at that scale (per mille) through the plane he leaves, his
   sprite lifted (g_PlaneTransitionLiftY 0x4777C4) toward that plane's vanishing point; Player_Update
   leaves his animation and input alone (0x411480), and he can't be hit */
#define G_PLANETRANSITIONSCALE ((volatile int *)0x0052C568u)
#define G_PPLANETRANSITIONFROM ((BYTE * volatile *)0x0059E774u)
#define G_PLAYERSPINTIMER  ((volatile int *)0x00498F70u)
#define G_CAMERAPLAYEROFFSETY ((volatile int *)0x00464420u)   /* 160: the camera's y follows Lomax's y minus this */
#define G_CAMERASCROLLVELY ((volatile int *)0x0059E78Cu)   /* 16.16, the camera's y smoothing speed */
#define G_PLAYERWALLHIT    ((volatile int *)0x0059E950u)   /* Actor_CheckWallCollision for Lomax: 2 = a wall on his right */
#define G_SCREENSHAKE      ((volatile int *)0x0059E93Cu)   /* g_ScreenShake: Player_Update adds +-(15 + rand & 15) * it / 64 px to the camera's y, -1 every 4 ticks (FallingTree_Update sets 10) */
#define G_SCREENSHAKEOFFSETY ((volatile int *)0x0059E798u) /* g_ScreenShakeOffsetY: this tick's offset */
#define G_CAMERAYBEFORESHAKE ((volatile int *)0x0052C588u) /* g_CameraYBeforeShake: the camera's y without it; Player_Update puts it back first */
#define SHAKE_NONE         (-12345678)   /*   (no shake) */
#define G_LANGUAGE         ((volatile int *)0x00464570u)    /* 0 English, 1 French, 2 German, 3 Italian, 4 Spanish */

/* ---- Music and sound ------------------------------------------------------------------------------ */
#define G_BEXITFADESMUSIC  ((volatile int *)0x0059F170u)
#define G_BFADEOUTMUSIC    ((volatile int *)0x0059E75Cu)
#define G_BSECTIONCHANGE   ((volatile int *)0x0052C59Cu)   /* g_bSectionChangePending: Game_UpdateFade runs Game_SetupNextLevel */
#define SND_POTHIT         0x11
#define SND_SPRING         0x35          /* the bounce pad's launch */
#define SND_DEBRIS         0x3D
#define SND_FIRE           60            /* ZOOM1: Lomax has no fire sound */
#define SND_HIT            0x59          /* BOUNCE2, a Lomax enemy being hit */
#define SND_THROW1         0x30
#define SND_THROW2         0x31
#define SND_POTBOUNCE      0x3F          /* POTBNCE1 */
#define SND_LAND           0x0C          /* FALL1, Lomax landing */
#define SND_STOMP          2             /* Lomax bouncing off an enemy's head (Player_BounceOffEnemy 0x412D90) */

/* ---- Data ----------------------------------------------------------------------------------------- */
#define G_SINTABLE         ((const short *)0x004639C0u)    /* 1024 a turn, 16384 = 1.0 */
#define G_ANIMBANKS        ((BYTE * const *)0x0059E960u)   /* bank -> its frames: frame n (from 1) at + n * 0x38 + 0x10 */
#define S_ITEMS            ((const char *)0x00466974u)     /* "Items", MAIN.WLD's pickups */
#define COIN_THROWS        ((const int *)0x004645E8u)      /* ClayPot_UpdateOpened's coins: {vx, vy} x 8 */

/* ---- Objects: g_ObjectSpawnFuncs[89] (0x4647B8), by object number --------------------------------- */
#define SPAWN_TABLE        0x004647B8u
#define OBJ_START          0             /* the Start trapdoor */
#define OBJ_EXIT           2
#define OBJ_FORCE          8             /* ForceScrolling, the airship's scrolling screen */
#define OBJ_CHECKPOINT     3
#define OBJ_MUSIC          37
#define OBJ_SPIK           51            /* a decoration */
#define OBJ_MEGA           64            /* a moving platform */
#define OBJ_SWIN           46            /* the swing ("Swin"), a pendulum platform */
#define OBJ_ROTA           21            /* the wheel of platforms ("Rota") */
#define OBJ_ROPE           67            /* the rope (Lomax's climbable rope) */
#define OBJ_BOAT           65            /* "Trir" */
#define OBJ_MUTA           74            /* ZombieRunner */
#define OBJ_POTS           85            /* ClayPot */
#define OBJ_GENE           86            /* the trigger */
#define OBJ_MAPTRAP        38            /* the map screen's trapdoor */
#define OBJ_HERO           39            /* the map screen's little Lomax ("Little Hero") */
#define OBJ_MAPCTL         41            /* the map screen ("Map Control") */
#define OBJ_TITLECTL       14            /* the title screen's control ("TitleControl") */

/* ---- Functions (typed: call them by name, or (DWORD)name for their address) ----------------------- */
#define Trapdoor_SpawnA          ((SpawnFn)0x0042A780u)
#define LevelExit_Spawn          ((SpawnFn)0x00427670u)
#define ForceScroll_Init         ((SpawnFn)0x0041C270u)   /* +0x80 target speed (param x 0x28F), +0x84 speed, +0x88 camera x 16.16; state |= 0x28000020 */
#define ForceScroll_Update       ((ActorFn)0x0041C2E0u)   /* moves the camera, keeps Lomax 16 px inside the screen, crushes him against a wall */
#define Checkpoint_Spawn         ((SpawnFn)0x00427760u)
#define Musi_MusicTrigger_Init   ((SpawnFn)0x00423A60u)
#define Deco_Spawn               ((SpawnFn)0x00427060u)
#define MovingPlatform_Spawn     ((SpawnFn)0x00429A90u)
#define DriftingBoat_Init        ((SpawnFn)0x00432450u)
#define Enemy_ZombieRunner_Spawn ((SpawnFn)0x0042E1B0u)
#define ClayPot_Spawn            ((SpawnFn)0x0042BEE0u)
#define Swin_Swing_Init          ((SpawnFn)0x00425780u)   /* +0x80 radius px, +0x84 speed, +0x88 strength, +0x8C phase
                                                            (16 a step, 1024 steps a turn), +0x94 ridden, +0x98 arms,
                                                            +0x9C/+0xA0 the pivot */
#define Rota_RotatingPlatforms_Init ((SpawnFn)0x0041F180u)   /* +0x80 radius px, +0x84 top speed, +0x88 speed, +0x8C phase
                                                            (16 a step, 1024 steps a turn), +0x90 turns only ridden (flag 2),
                                                            +0x98 arms, +0x9C/+0xA0 the pivot, +0xF4.. the platforms */
#define Rota_RotatingPlatforms_Update ((ActorFn)0x0041F4C0u)
#define Rota_RotatingPlatforms_DrawSpokes ((ActorFn)0x0041F5F0u)
#define Swin_Swing_Update        ((ActorFn)0x004259B0u)
#define Swin_Swing_Draw          ((ActorFn)0x00425BB0u)
#define Swin_Swing_GetSurfaceHeight ((int (__cdecl *)(BYTE *, int relx))0x00425980u)   /* tilts with arms >= 1, else 0 */
#define Rope_Spawn               ((SpawnFn)0x00429F60u)   /* +0x80 re-arm timer, +0x84 length px, +0x88 segments */
#define MapT_MapTrapdoor_Init    ((SpawnFn)0x00423BE0u)   /* Lomax at its x, y - 40, in it; its update moves it to g_MapScrollTargetX + 50 */
#define Litt_MapHeroMarker_Init  ((SpawnFn)0x00423AB0u)   /* param - g_LastRealSection == 1 or killed; sets g_MapScrollTargetX */
#define TitleScreen_Init         ((SpawnFn)0x0041D670u)   /* Game_ResetNewGame, state |= 0x8000060, update TitleScreen_Update,
                                                            draw TitleScreen_CheckStartOrExit, +0x80 fade-in timer 30 */
#define TitleScreen_Update       ((ActorFn)0x0041D6F0u)   /* the fade in, the camera's pan, then TitleScreen_CheckStartOrExit */
#define TitleScreen_CheckStartOrExit ((ActorFn)0x0041D7B0u)  /* a button starts the game (+0xA4 = 1); Lomax past the
                                                            camera's x + 320 starts the attract demos (+0xA4 = 0x4D2) */
#define Map_WorldMap_Init        ((SpawnFn)0x00424100u)   /* moves itself to a high slot, update Map_WorldMap_Update */
#define Map_WorldMap_Update      ((ActorFn)0x004241C0u)
#define Trigger_Spawn           ((SpawnFn)0x0042D0D0u)   /* +0x80 the link, +0x84 height, +0x88 re-armable */

#define Draw_RopeSegments        ((void (__cdecl *)(void))0x00425D40u)   /* a chain's links, every `step` px (pushed by its callers) */
#define Rope_WaitForGrab         ((ActorFn)0x00429FC0u)   /* Player_TryGrabHangPoint; then the think is Rope_UpdateClimbing */
#define Rope_UpdateClimbing      ((ActorFn)0x0042A000u)   /* the player hangs on it */
#define Rope_Draw                ((ActorFn)0x0042A280u)   /* 'ROPE' frame 0 every 32 px, frame 1 at the last */
#define MovingPlatform_Update    ((ActorFn)0x00429BB0u)
#define Water_Update             ((ActorFn)0x00431A20u)
#define Water_Draw               ((ActorFn)0x00431D80u)
#define Water_SpawnSplash        ((void (__cdecl *)(BYTE *src))0x004320E0u)   /* SPL2 and SPLA at src's x, g_WaterLevelY + 5; sound 0x2C */
#define TriggerGroup_UpdateDespawn ((ActorFn)0x0042D2B0u)
#define HintText_Draw            ((ActorFn)0x00433DF0u)
#define Item_UpdateCoinOrHelmet  ((ActorFn)0x0042C6A0u)   /* +0x84 < 0 a coin, else a helmet */
#define Item_UpdateOneUp         ((ActorFn)0x0042C970u)   /* the 1UP balloon */
#define Item_DrawWithSparkles    ((ActorFn)0x0042C660u)
#define Item_UpdateCoinLifeToHud ((ActorFn)0x0042CA60u)   /* the 1UP flying to the HUD's lives: +1 life (up to 30) on arrival if g_bPending1Up */

#define Game_SetupNextLevel      ((int (__cdecl *)(void))0x00417BD0u)   /* Level_LoadWorld's result */
#define World_LoadWLD            ((int (__cdecl *)(const char *file, unsigned planeMask, unsigned activeMask))0x004147E0u)
#define Level_InitPlanes         ((int (__cdecl *)(int keepState))0x0040EC40u)   /* keepState 0: the shown planes (plane 19, MAIN.WLD's continue screen, while g_Lives is 0) */
#define CD_PlayTrackWrapper      ((void (__cdecl *)(int n))0x0040E0B0u)  /* plays disc track n + 1 */
#define Sound_PlayEffect         ((void (__cdecl *)(void *source, int id))0x0040D5F0u)
#define Sound_PlayLoop           ((int (__cdecl *)(void *source, int id))0x0040D5F0u)   /* Sound_PlayEffect of a looping sound: its voice, < 0 none */
#define Sound_StopVoice          ((void (__cdecl *)(int voice))0x0040D840u)
/* g_SoundDefs[99] (0x461D08, 0x14 bytes each), what Sound_PlayEffect plays for an id: the driver's
   sample, the name (SFX\<name>.WAV; NULL: an alias), volume 0..255, pitch (0x100 normal), looping.
   Sound_InitAndLoadSfx (0x403B80) fills the samples; CLomaxApp_ExitInstance frees the named ones */
typedef struct { int voice; const char *name; short volume, pitch, loop, cooldown; int pad; } SoundDef;
#define G_SOUNDDEFS              ((SoundDef *)0x00461D08u)
#define SOUND_DEFS               99
#define G_BSOUNDAVAILABLE        ((volatile int *)0x00461C78u)   /* g_bSoundAvailable: the driver is open */
#define SndDrv_LoadWav           ((int (__cdecl *)(const void *wav, const void *rear, unsigned flags))0x004382D0u)
                                 /* a RIFF image in memory: PCM mono 22050 Hz 16-bit, data at +0x2C, its size
                                    at +0x28; the samples are copied; -> the sample (>= 0) or an error */
#define CD_Pause                 ((void (__cdecl *)(void))0x0040E110u)   /* PowerWheel_Open's, on a level of world 5 */
#define CD_Resume                ((void (__cdecl *)(void))0x0040E150u)   /* "play cdaudio from <paused position> to n+2 notify" */
#define CDAudio_SetVolume        ((void (__cdecl *)(int volume255))0x00438460u)
#define G_CDAUDIOAVAILABLE       ((volatile int *)0x00461C7Cu)   /* g_bCDAudioAvailable */
#define G_CFGUSECD               ((volatile int *)0x00461CF4u)   /* g_cfgUseCD */
#define G_REQUESTEDCDTRACK       ((volatile int *)0x00461CFCu)   /* g_RequestedCDTrack */
#define G_CURCDTRACK             ((volatile int *)0x0052C518u)   /* g_CurCDTrack: CD_PlayTrack's (same track: no-op), CD_RestartTrack replays it */
#define G_HWND                   ((HWND volatile *)0x0046A218u)  /* g_hWnd: MM_MCINOTIFY goes there (Lomax_WndProc: CD_RestartTrack) */
#define G_LOGOHOLDSCDVOLUME      ((volatile int *)0x00498F6Cu)   /* g_bLogoHoldsCDVolume */
#define G_CDVOLUME               ((volatile int *)0x00464574u)   /* the CD volume setting, 0..100 */
#define CD_PAUSEPOS              ((const char *)0x0046A848u)     /* CD_Pause's position, "tt:mm:ss:ff" */
#define IAT_MCISENDSTRINGA       0x005A8AA4u                      /* LOMAX.EXE's import of winmm's mciSendStringA */

#define ObjType_FindByName       ((BYTE *(__cdecl *)(const char *name))0x004162D0u)   /* MAIN.WLD's types first, then the level's; NULL: none */
#define G_OBJTYPES               ((BYTE * const *)0x00498F78u)   /* g_ObjTypes: the level's types (MAIN.WLD's from index 0x100) */
#define G_NUMOBJTYPES            ((volatile int *)0x00498F20u)   /* g_NumObjTypes: the level's */
#define Actor_Alloc              ((BYTE *(__cdecl *)(BYTE *parent, const char *type))0x00412800u)  /* type, place, plane from parent */
#define Actor_MoveToHighSlot     ((BYTE *(__cdecl *)(BYTE *))0x00412B50u)   /* a copy in a high slot (the original killed), or itself */
#define Actor_Kill               ((void (__cdecl *)(BYTE *))0x00412C50u)   /* its placement stays spawned */
#define Actor_Despawn            ((void (__cdecl *)(BYTE *))0x00412B80u)
#define Object_TrySpawn          ((BYTE *(__cdecl *)(BYTE *placement))0x004126A0u)   /* sets placement +0x15 bit 7 */
#define Actor_SetAnim            ((int (__cdecl *)(BYTE *, DWORD fourcc))0x00416140u)
#define Actor_SetAnimIfChanged   ((int (__cdecl *)(BYTE *, DWORD fourcc))0x00416110u)
#define Actor_AdvanceAnim        ((int (__cdecl *)(BYTE *))0x004161B0u)
#define Actor_Draw               ((void (__cdecl *)(BYTE *, int scale, int angle, int alpha))0x0040C270u)  /* scale 1000 */
#define Actor_GetDrawDepth       ((int (__cdecl *)(BYTE *))0x00416920u)   /* its bucket, higher drawn later */
#define Actor_CheckHitByPlayer   ((int (__cdecl *)(BYTE *))0x00413E40u)   /* spin, tumble or helmet */
#define ThrownHelmet_Update      0x0042F660u   /* void ThrownHelmet_Update(Actor *): the thrown helmet actor's update */
#define Actor_ShatterIntoPieces  ((void (__cdecl *)(BYTE *, int force))0x00430690u)  /* breaks up its BOOM record */
/* its pieces: +0x80 count, +0x84 one link node per 8 pieces (x, y, vx, vy 16.16 from the node's +8),
   +0x94 8 bytes a piece (sprite frame, sprite flags): 17 fit in the actor */
#define Shatter_DrawPieces       ((void (__cdecl *)(BYTE *))0x004308F0u)  /* Actor_Despawn once none is on screen */
#define Actor_AllocLink          ((BYTE *(__cdecl *)(BYTE *))0x00412D10u) /* a link node, freed with the actor */
#define Plane_FindGroundY        ((unsigned (__cdecl *)(BYTE *plane, int x, int y, int maxTiles))0x00413B90u)  /* NO_GROUND = none */
#define Platform_MoveAndCarryPlayer ((int (__cdecl *)(BYTE *, int (__cdecl *surface)(BYTE *, int relx)))0x00413510u)  /* 1 = stood on */
#define Sprite_QueueWorld        ((void (__cdecl *)(BYTE *plane, const BYTE *frame, int x, int y, unsigned flags, int clipBottomY))0x0040AA70u)
                                 /* flags: bucket << 24 | 8 mirror x */
#define Sparkles_Init            ((void (__cdecl *)(BYTE *, void *state, int count))0x0042B910u)
#define Item_SpawnCoin           ((BYTE *(__cdecl *)(BYTE *src))0x0042C240u)  /* a coin ('Items') 16 px above src */
#define Player_TakeHit           ((void (__cdecl *)(int enemyX, int, int))0x00412E30u)  /* knock-back away from enemyX */
#define Actor_CheckPlayerPush    ((unsigned (__cdecl *)(BYTE *block, int mode, int dy))0x004131C0u)
                                 /* the actor's hitbox is a block Lomax walks into (mode 0): g_pPushBlock, so that this tick's
                                    Player_Update holds him at its edge in his push pose (a block with vx 0 moves with him; vx 1:
                                    it doesn't). The diggable wall and the falling tree call it from their updates. */
#define Player_BounceOffEnemy    ((void (__cdecl *)(void))0x00412D90u)   /* vy -4.0, sound 2, a poof */
#define Player_Update            ((void (__cdecl *)(void))0x0040FB70u)
#define Effect_SpawnPoof         ((int (__cdecl *)(int x, int y))0x004286D0u)   /* the stomp's poof (Hero PLO2) at x, y */
#define Fx_SpawnGroundDebris     ((void (__cdecl *)(void))0x004309F0u)   /* one grass (or, world 5, coin) bit off Lomax's feet */
#define Effect_SpawnDustPuff     ((void (__cdecl *)(BYTE *, short x, short y))0x0042FCA0u)   /* a grass puff (Gras DREI) at a
                                                                                    step, 8 ticks apart, on material 0 */
#define Deco_Update              ((ActorFn)0x00427090u)
#define Text_DrawString          ((int (__cdecl *)(BYTE *, int x, int y, const char *))0x0041F030u)
#define Ui_DrawWindowFrame       ((void (__cdecl *)(BYTE *, int width, int height))0x0041EC10u)  /* 16 px cells */

/* ---- Small helpers on the game's actors ----------------------------------------------------------- */

/* the actor's slot in g_Actors, -1 if it isn't one */
static inline int slot_of(const BYTE *a)
{
    int i = (int)(a - G_ACTORS) / ACTOR_STRIDE;
    return a >= G_ACTORS && i < MAX_ACTORS && a == G_ACTORS + i * ACTOR_STRIDE ? i : -1;
}

static inline BYTE *actor_at(int slot) { return slot >= 0 && slot < MAX_ACTORS ? G_ACTORS + slot * ACTOR_STRIDE : NULL; }

/* the actor's type name starts with name */
static inline int is_type(const BYTE *a, const char *name)
{
    const BYTE *t = A_TYPE(a);
    return t && !strncmp(T_NAME(t), name, strlen(name));
}

/* its type's param n from its placement, or dflt if the type has fewer params */
static inline int def_param(const BYTE *a, const BYTE *def, int n, int dflt)
{
    return def && T_NPARAMS(A_TYPE(a)) > n ? D_PARAM(def, n) : dflt;
}

static inline void set_anim(BYTE *a, const char *tag) { Actor_SetAnimIfChanged(a, FOURCC(tag)); }

/* the record's frame, held: Actor_AdvanceAnim at speed 0 only syncs the drawn entries to it
   (Actor_SyncAnimEntry 0x416210: +0x6C/+0x6E, which Actor_Draw reads) */
static inline void hold_frame(BYTE *a, int frame)
{
    *(short *)(a + 0x68) = 0;
    *(short *)(a + 0x6A) = (short)(frame << 8);
}

/* x/y from the 16.16 position */
static inline void set_pos(BYTE *a)
{
    A_X(a) = A_FX(a) >> 16;
    A_Y(a) = A_FY(a) >> 16;
}

static inline void place_y(BYTE *a, int y)
{
    A_Y(a) = y;
    A_FY(a) = y << 16;
}

static inline void draw_actor(BYTE *a) { Actor_Draw(a, 1000, 0, 0); }

/* a record's entries in the actor type's list (as Actor_SetAnim walks it: +0 next, +4 FourCC; the
   entries at +0x20, 8 bytes each: bank, frame from 1, dx, dy, flags (0x80 ends a frame)) */
static inline const BYTE *record_of(const BYTE *a, DWORD fourcc)
{
    const BYTE *n = *(BYTE * const *)A_TYPE(a);
    for (; n; n = *(BYTE * const *)n) if (*(const DWORD *)(n + 4) == fourcc) return n + 0x20;
    return NULL;
}

/* the slot runs (Actors_UpdateAll's masks) */
static inline int actor_active(int slot)
{
    if (slot >= 0 && slot < 32) return *G_ACTORMASK0 >> slot & 1;
    if (slot >= 33 && slot < 65) return *G_ACTORMASK1 >> (slot - 33) & 1;
    if (slot >= 66 && slot < MAX_ACTORS) return *G_ACTORMASK2 >> (slot - 66) & 1;
    return 0;
}

/* its hitbox in the world, mirrored when it faces left (as Actor_CheckHitByPlayer 0x413E40 reads
   it); 0 without a record */
static inline int actor_box(const BYTE *a, int *x1, int *y1, int *x2, int *y2)
{
    const BYTE *r = A_RECORD(a);
    if (!r) return 0;
    if (A_FLAGS(a) & AF_LEFT) { *x1 = A_X(a) - HB_X2(r); *x2 = A_X(a) - HB_X1(r); }
    else { *x1 = A_X(a) + HB_X1(r); *x2 = A_X(a) + HB_X2(r); }
    *y1 = A_Y(a) + HB_Y1(r);
    *y2 = A_Y(a) + HB_Y2(r);
    return 1;
}

/* the ground under x, y on the actor's plane (within 4 cells), NO_GROUND if none */
static inline int ground_y(const BYTE *a, int x, int y) { return (int)Plane_FindGroundY(PLANE(A_PLANE(a)), x, y, 4); }

/* fell below its plane's bottom */
static inline int below_map(const BYTE *a) { return A_Y(a) > P_H(PLANE(A_PLANE(a))) * 16 + 64; }

#endif
