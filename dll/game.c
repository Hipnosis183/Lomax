/*
 * game.c - the console versions' extras: the language select screen, 50 Hz (PAL) timing and the
 * loading screen; Game_Frame and its timing block, which VSync and FPS Override replace.
 */
#include "lomax.h"

/* ---- Language select screen ----------------------------------------------------------------
   The console versions open with a language menu. It is still in the PC data and code: TITLE.WLD
   (section 0x17) has two sub-levels, plane mask 0x10 (plane 4: a 20x16 screen with a "Start" and a
   "LanguageMenu" placement) and 0x0F (the title). The "Lang" tag is object id 4, Obj_LangSelect_Init
   (0x417E60) / Obj_LangSelect_Update (0x417F00): up/down change g_Language (ENGLISH, FRANCAIS,
   DEUTSCH, ITALIANO, ESPAGNOL, the same order as the Language menu 0x800F..0x8013); jump/start or
   20 s confirm, fade out and set the title flag, so the next section is the title. The PC port only
   skipped it: Game_SetupNextLevel always enters the title at sub-level 1
   (0x417C3F: mov [g_CurSubLevel], 1). LanguageSelect makes the first title load use sub-level 0, and
   afterwards updates the Language menu's radio mark, which the screen doesn't touch.
   Game_Frame clears the back buffer only for level rows whose flag byte (+8) has bit 4 (the other
   bits are the jingle flags). The title's tiles cover the screen, so its row doesn't; the language
   plane has no tiles, and on PC the hand cursor left trails. The bit is set while it shows. */
#define G_TITLESUBLEVELIMM       ((volatile BYTE *)0x00417C45u)
#define G_LANGUAGE               ((volatile int *)0x00464570u)
#define G_TITLEROWFLAGS          ((volatile BYTE *)(0x00466450u + 0x17 * 0x18 + 8))   /* g_LevelDefs[0x17] */
#define LEVELFLAG_CLEAR          0x04
static IntFn_t oSetupNextLevel;
#define G_LOADEDSECTION          ((volatile int *)0x004645C0u)
static void loading_start(void);

static int __cdecl hkSetupNextLevel(void)
{
    static int s_state;                       /* 0 = boot, 1 = language screen shown, 2 = done */
    int r, before = *G_LOADEDSECTION;
    if (!cfg.langSelect) {
        r = oSetupNextLevel();
        if (cfg.loadScreen && r && *G_LOADEDSECTION != before) loading_start();
        return r;
    }
    if (s_state == 0) {
        patch_code_byte(G_TITLESUBLEVELIMM, 0);
        r = oSetupNextLevel();
        patch_code_byte(G_TITLESUBLEVELIMM, 1);
        *G_TITLEROWFLAGS |= LEVELFLAG_CLEAR;
        s_state = 1;
        log_msg("language select screen");
        if (cfg.loadScreen && r) loading_start();
        return r;
    }
    if (s_state == 1) *G_TITLEROWFLAGS &= ~LEVELFLAG_CLEAR;
    r = oSetupNextLevel();
    if (s_state == 1) {
        s_state = 2;
        if (*G_HMENU && *G_LANGUAGE >= 0 && *G_LANGUAGE <= 4)
            CheckMenuRadioItem(*G_HMENU, 0x800F, 0x8013, 0x800F + *G_LANGUAGE, MF_BYCOMMAND);
        log_msg("language select: language %d", *G_LANGUAGE);
    }
    if (cfg.loadScreen && r && *G_LOADEDSECTION != before) loading_start();
    return r;
}

/* ---- 50 Hz (PAL) mode -----------------------------------------------------------------------
   The PS1 PAL release runs at 50 Hz with the same per-tick game code, so the player, enemies and
   animations move about 17% slower than on NTSC. The PC port kept its switch, g_bSpeed50Hz
   (0x46441C), which nothing sets. When it is set, timers that count seconds use 50 ticks
   (LevelTimer_Spawn/Draw, the credits, the language menu's 20 s timeout), and particle launch
   speeds and gravity are raised (debris, bursts, birds flying off, pot lids, confetti, ambient
   emitters) to keep their real-time arcs. Speed50Hz sets it and changes Game_Frame's step from
   167 to 200 (ms*10 per tick), so the game ticks and draws at 50 Hz.
   The same switch also moves sprites for the PAL screen, 256 lines instead of 240: the HUD 4 px up,
   the copyright 20 px down, the title logo and its credits 8 px up, the world-map props 16 px down,
   and flag-2 parallax sprites lose their 16 px raise. Those reads are pointed at a zero dword, so the
   PC's 240-line layout stays. */
#define G_BSPEED50HZ             ((volatile int *)0x0046441Cu)
#define G_TICKDIVIMM             ((volatile DWORD *)0x00404BE9u)  /* Game_Frame: mov ebx, 167 */
static const DWORD kPalLayoutReads[] = {   /* the address operand of each layout-only read */
    0x0042A392u, 0x0042A4D6u, 0x0042A531u, 0x0042A668u,   /* Hud_Draw: digit rows y 0x25.. vs 0x29.. */
    0x0041C462u, 0x0041C485u,                             /* Copyright_Draw: y + 0x14 */
    0x0041D327u, 0x0041D555u,                             /* Logo_DrawWithAuthorCredits: y - 8 */
    0x00423F15u,                                          /* Mapf_MapParallaxProp_Init: y + 0x10 */
    0x0042D5D1u,                                          /* ParallaxSprite_Spawn: no -0x10 raise */
};
int g_tickHz = 60;
int g_tickDiv = 167;                /* Game_Frame's step: ms*10 per tick */
static const int g_zeroDword = 0;

void speed50_init(void)
{
    const DWORD zero = (DWORD)&g_zeroDword, step = 200;
    size_t i;
    if (!cfg.speed50) return;
    if (*G_TICKDIVIMM != 167) { log_msg("Speed50Hz: unexpected Game_Frame step %lu", *G_TICKDIVIMM); return; }
    for (i = 0; i < sizeof(kPalLayoutReads) / sizeof(kPalLayoutReads[0]); i++) {
        volatile DWORD *op = (volatile DWORD *)kPalLayoutReads[i];
        if (*op != (DWORD)G_BSPEED50HZ) {
            log_msg("Speed50Hz: unexpected code near %08lX", (unsigned long)kPalLayoutReads[i]);
            return;
        }
    }
    for (i = 0; i < sizeof(kPalLayoutReads) / sizeof(kPalLayoutReads[0]); i++)
        patch_code(kPalLayoutReads[i], &zero, 4);
    patch_code((DWORD)G_TICKDIVIMM, &step, 4);
    *G_BSPEED50HZ = 1;
    g_tickHz = 50;
    g_tickDiv = 200;
    log_msg("Speed50Hz: game ticks at 50 Hz (PAL timing), PC screen layout kept");
}

/* ---- Loading screen ------------------------------------------------------------------------
   PS1 (SLES_004.51): whenever a new section (world) loads, FUN_8002f6e0 shows the "Loading" object
   of MAIN.WLD from a VSync callback while the CD reads: HERO (Lomax) at (160,140), UHR (the
   hourglass) 20 px to its left with its frame 0..14 set by the read progress (FUN_8002205c), TEXT
   ("DISC ERROR!") 16 px below only after a read error. The palette fades in over 64 frames and out
   over 64 once the data is in (FUN_8002fac4/FUN_8002fa78). A retry of the same section shows none.
   The PC port loads at once and shows only the hourglass mouse cursor (World_LoadWLD), but the
   object type and its sprites are still in MAIN.WLD.
   LoadingScreen: after Game_SetupNextLevel loads a different section, Game_Frame is replaced for
   LOAD_TICKS ticks (60 Hz, or 50 with Speed50Hz) by a frame that colour-fills the back buffer, draws HERO and UHR through
   two dummy actors driven by the game's animation code (Actor_SetAnim/AdvanceAnim; the hourglass
   empties over fade-in + hold), adds the game's own fade (Render_DissolveFade or Render_ScreenFade,
   chosen as Game_Frame does) and calls DDraw_Present, with g_bFrameReady set for it (the game clears
   it while a level starts and sets it from its own ticks). No game tick runs meanwhile, and the CRT
   rand() seed that the dissolve fade advances is put back after each frame, so demos stay in sync.
   No music plays during the screen: the new level's Music object already started its CD track at
   level start, so the first frame sends "stop cdaudio" (through the game's import, so the music player
   answers it too) and the end plays that track from its beginning with CD_RestartTrack.
   The hold lasts 5 s, plus 64 ticks of fade on each side; HERO stands for its first 24 frames
   (36/256 frame per tick, 2.85 s) before Lomax looks at his wrist. */
#define FN_DDRAW_PRESENT         0x004059C0u
#define FN_RENDER_SCREEN_FADE    0x004057B0u
#define FN_OBJTYPE_FIND          0x004162D0u  /* ObjType * __cdecl (const char *)   */
#define FN_ACTOR_SET_ANIM        0x00416140u  /* int __cdecl (Actor *, fourcc)       */
#define FN_ACTOR_ADVANCE_ANIM    0x004161B0u  /* int __cdecl (Actor *)               */
#define FN_ACTOR_SYNC_ANIM       0x00416210u  /* void __cdecl (Actor *)              */
#define FN_CD_RESTART_TRACK      0x0040E0C0u  /* void (void): plays g_CurCDTrack from its start */
#define G_CURCDTRACK             ((volatile int *)0x0052C518u)   /* -1 = none */
#define G_BAPPINACTIVE           ((volatile int *)0x00461C90u)
#define G_PDDSBACK               ((void *volatile *)0x00469A0Cu)
#define G_PDDSSTRETCH            ((void *volatile *)0x00469EDCu)
#define G_PDDSFADETILESYS        ((void *volatile *)0x0046998Cu)
#define G_FILLBLTFX              ((BYTE *)0x00469FB8u)            /* DDBLTFX, dwFillColor at +0x50 */
#define G_ANIMBANKS              ((BYTE *volatile *)0x0059E960u)
#define LOAD_FADE                64
#define LOAD_HOLD                (5 * g_tickHz)                   /* 5 s between the fades */
#define LOAD_TICKS               (LOAD_FADE * 2 + LOAD_HOLD)
#define FOURCC_HERO              0x4845524Fu
#define FOURCC_UHR               0x55485220u
#define ACTOR_SIZE               0x120

typedef int (__cdecl *ActorFn_t)(BYTE *);
typedef void (__cdecl *FadeFn_t)(void *);
static IntFn_t oGameFrame;
static BYTE   g_loadHero[ACTOR_SIZE], g_loadUhr[ACTOR_SIZE];
static int    g_loadTick = -1, g_loadAccum;
static DWORD  g_loadTime;

static void loading_start(void)
{
    BYTE *type = (BYTE *)((ActorFn_t)FN_OBJTYPE_FIND)((BYTE *)"Loading");
    if (!type) { log_msg("loading screen: no \"Loading\" object type"); return; }
    memset(g_loadHero, 0, sizeof(g_loadHero));
    memset(g_loadUhr, 0, sizeof(g_loadUhr));
    *(BYTE **)g_loadHero = type;
    *(BYTE **)g_loadUhr = type;
    if (!((int (__cdecl *)(BYTE *, DWORD))FN_ACTOR_SET_ANIM)(g_loadHero, FOURCC_HERO) ||
        !((int (__cdecl *)(BYTE *, DWORD))FN_ACTOR_SET_ANIM)(g_loadUhr, FOURCC_UHR)) {
        log_msg("loading screen: HERO/UHR animations missing");
        return;
    }
    g_loadTick = 0;
    g_loadAccum = 0;
    g_loadTime = 0;                           /* timing starts with the first frame drawn (at boot
                                                 the logo video plays in between) */
    log_msg("loading screen: section %d", *G_LOADEDSECTION);
}

/* draws the sprites of the actor's current frame (the part records up to the one flagged 0x80) as
   Actor_Draw does: each part at x + dx, y + dy. Actor_Draw queues them in order, and the sprite
   queue pushes each to the front of its bucket list, so they are drawn in reverse: the first part
   ends on top (HERO: face/eye and arm overlays first, the 32x59 body last). */
static void loading_draw_actor(BYTE *a, void *dst, int x, int y, int w, int h)
{
    BYTE *anim = *(BYTE **)(a + 8);
    int first = *(short *)(a + 0x6E), last = first, n = *(unsigned short *)(anim + 8), e;
    while (last < n - 1 && !(*(const unsigned short *)(anim + 0x20 + last * 8 + 6) & 0x80)) last++;
    for (e = last; e >= first; e--) {
        const BYTE *ent = anim + 0x20 + e * 8;
        BYTE *bank = ent[0] ? G_ANIMBANKS[ent[0] | 0x100] : NULL;      /* MAIN.WLD banks */
        if (bank && ent[1]) {
            BYTE *sf = bank + ent[1] * 0x38 + 0x10;                     /* SpriteFrame */
            void *surf = *(void **)(sf + (*G_BUSESTRETCH ? 0x18 : 0x14));
            int sw = *(short *)(sf + 0x0C), sh = *(short *)(sf + 0x0E);
            RECT d, r = { 0, 0, sw, sh };
            d.left = x + *(short *)(ent + 2);
            d.top = y + *(short *)(ent + 4);
            d.right = d.left + sw;
            d.bottom = d.top + sh;
            if (surf && d.left >= 0 && d.top >= 0 && d.right <= w && d.bottom <= h)
                ((Blt_t)VTBL(dst, VT_DDS_BLT))(dst, &d, surf, &r, 0x01008000, NULL);  /* KEYSRC | WAIT */
        }
    }
}


static int __cdecl hkGameFrame(void)
{
    DWORD now;
    int ticks, frame, fadeStep, oldFade, oldReady, first;
    void *dst;
    BYTE *ptd;
    DWORD seed;
    int W = g_wsWidth ? g_wsWidth : 320;
    if (g_loadTick < 0) return hfr_game_frame();
    g_hfrValid = 0;
    if (*G_BAPPINACTIVE) return 1;
    now = timeGetTime();
    first = !g_loadTime;
    if (first) {
        void **mci = find_iat_slot("WINMM.dll", "mciSendStringA");
        g_loadTime = now;
        if (mci && *G_BCDAUDIOAVAILABLE && *G_CURCDTRACK >= 0)
            ((MciSendStringA_t)*mci)("stop cdaudio", NULL, 0, NULL);
        log_msg("loading screen shown");
    }
    g_loadAccum += (int)(now - g_loadTime) * 10;
    g_loadTime = now;
    ticks = g_loadAccum / g_tickDiv;                            /* 60 Hz (50 with Speed50Hz), as Game_Frame */
    g_loadAccum %= g_tickDiv;
    if (ticks < 1 && !first) return 1;
    if (ticks > 5) ticks = 5;
    while (ticks-- > 0) {
        ((ActorFn_t)FN_ACTOR_ADVANCE_ANIM)(g_loadHero);
        if (++g_loadTick >= LOAD_TICKS) {
            g_loadTick = -1;
            if (*G_BCDAUDIOAVAILABLE && *G_CURCDTRACK >= 0)
                ((void (__cdecl *)(void))FN_CD_RESTART_TRACK)();
            *G_FRAMETIME = timeGetTime();                      /* no catch-up ticks afterwards */
            *G_FRAMEACCUM = 0;
            return 1;
        }
    }
    dst = *G_BUSESTRETCH ? *G_PDDSSTRETCH : *G_PDDSBACK;
    if (!dst) return 1;
    frame = g_loadTick * 15 / (LOAD_FADE + LOAD_HOLD);
    *(short *)(g_loadUhr + 0x6A) = (short)(min(frame, 14) << 8);
    ((ActorFn_t)FN_ACTOR_SYNC_ANIM)(g_loadUhr);

    *(DWORD *)(G_FILLBLTFX + 0x50) = 0;
    ((Blt_t)VTBL(dst, VT_DDS_BLT))(dst, NULL, NULL, NULL, 0x01000400, G_FILLBLTFX);   /* COLORFILL | WAIT */
    loading_draw_actor(g_loadUhr, dst, W / 2 - 20, 140, W, 240);
    loading_draw_actor(g_loadHero, dst, W / 2, 140, W, 240);

    /* the game's fade: level 0..63 in, then 64..1 out (g_FadeStep >0 = in, <0 = out) */
    if (g_loadTick < LOAD_FADE) fadeStep = LOAD_FADE - g_loadTick;
    else if (g_loadTick >= LOAD_FADE + LOAD_HOLD) fadeStep = -(LOAD_TICKS - g_loadTick);
    else fadeStep = 0;
    if (fadeStep) {
        ptd = (BYTE *)((GetPtd_t)FN_GETPTD)();
        seed = *(DWORD *)(ptd + 0x14);
        oldFade = *G_FADESTEP;
        *G_FADESTEP = fadeStep;
        if (*G_BD3DTRANSLUCENCY && !*G_BUSESTRETCH && *G_PDDSFADETILESYS)
            ((FadeFn_t)FN_RENDER_SCREEN_FADE)(dst);
        else
            ((FadeFn_t)FN_RENDER_DISSOLVE_FADE)(dst);
        *G_FADESTEP = oldFade;
        *(DWORD *)(ptd + 0x14) = seed;
    }
    oldReady = *G_BFRAMEREADY;
    *G_BFRAMEREADY = 1;
    ((void (__cdecl *)(void))FN_DDRAW_PRESENT)();
    *G_BFRAMEREADY = oldReady;
    return 1;
}

int game_frame_orig(void)                      /* for FPS Override */
{
    return oGameFrame();
}

/* Game_Frame's timing block (timeGetTime ... the 5-tick clamp), which VSync and FPS Override replace:
   1 if it is the retail one (the step at +0x18 may already be Speed50Hz's 200) */
int game_timing_check(const char *who)
{
    static const char kOrig[] =
        "ff15a08a5a008bf08bf8a1e09e46008b0d04a946002bf0bba70000008d34b68d34718bc699f7fb8bc6891504a946"
        "0099f7fb8bf0893de09e460085f67f10b8010000005d5f5e5b81c494000000c383fe057e05be05000000";
    BYTE want[GF_TIMING_LEN];
    const BYTE *site = (const BYTE *)GF_TIMING;
    int i;
    if (hex_to_bytes(kOrig, want, sizeof(want)) != GF_TIMING_LEN) return 0;
    for (i = 0; i < GF_TIMING_LEN; i++)
        if (site[i] != want[i] && i != 0x18) {
            log_msg("%s: unexpected code at %08lX, off", who, (unsigned long)(GF_TIMING + i));
            return 0;
        }
    return 1;
}

void game_init(void)
{
    if (cfg.langSelect || cfg.loadScreen)
        mh_hook(FN_GAME_SETUP_NEXT_LEVEL, hkSetupNextLevel, (void **)&oSetupNextLevel, "Game_SetupNextLevel");
    if (cfg.loadScreen || cfg.hfrFps)
        mh_hook(FN_GAME_FRAME, hkGameFrame, (void **)&oGameFrame, "Game_Frame");
    if (cfg.cheats) *G_BCHEATSENABLED = 1;
}
