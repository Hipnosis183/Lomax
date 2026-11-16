/*
 * title.c - Flink's title screen in place of Lomax's (README "Title screen").
 *
 * The converter writes Flink's title screen (FLINK.PRG's built-in world 6, map 2 over map 1) as
 * MODS\FLINK\WORLDS\TITLE.WLD with Lomax's title objects (TitleControl, PRESS BUTTON TO START, the
 * copyright), a Music trigger with Lomax's title track and Flink's logo ('Spik Logo'). LEVELS.INI's
 * [Title]: File, Logo = its x on Flink's 320-px screen, first and last y; Hang = Lomax's feet from the
 * logo while he hangs from it; HeroY = his height on Flink's 224-line screen that the camera keeps.
 * Without them the title is Lomax's own.
 *
 * Lomax's title is section 0x17 (row ROW_BONUS + 2 here), sub-level 1 (sub-level 0 is the language
 * menu that Lomax+ can show first, from Lomax's TITLE.WLD plane 4): world_load (levels.c) opens
 * Flink's file for sub-level 1 with its 3 planes, and the row's aSubLevelPlaneMask[1] says 7 so that
 * Level_ComputePlaneMask (0x40EB10) counts them (title_row). The Start in the file (0x2000: never
 * spawned) only makes the play plane the start plane, as Lomax's title's does.
 *
 * Flink's title (the converter's notes at title()): its Logo falls from y -50 to the map's height -
 * 0x87, 1 px a frame, with Flink hanging from it at (x - 0x49, y + 0x3C); the camera follows him down
 * from the map's top (Flink's 0x1A040: toward his y - 0x96, speed distance / 16, changed by at most
 * 0.25 px a frame, 0.75 when turning, a dead zone of 1 px while he hangs), and when the logo stops he
 * drops onto the ground. Here:
 * - the logo ('Spik Logo', Lomax's decoration, object 51: logo_spawn makes it harmless and never
 *   culled) is centred in the view as Flink's is in its 320-px screen; it starts falling when the
 *   TitleControl fades the screen in (its 30-tick timer);
 * - Lomax hangs from it in Lomax's rope pose: player state 0x1010 as Player_TryGrabHangPoint (0x413D60)
 *   sets it (Player_Update then draws him hanging and leaves him in place; nothing else of the rope is
 *   needed), held at the logo's Hang point around Player_Update (title_before / title_after, from
 *   player_tick), input locked; when the logo stops he lets go (rope state cleared, in the air) and
 *   lands. While g_bPaused (the window out of focus, the menu open) the logo, Lomax and the camera
 *   stand still, as Lomax's title does. Then he can walk, kept 16 px inside the view (TitleScreen_Init's
 *   player state 0x8000000 cleared), but not jump (Jump is hidden from
 *   Player_Update by title_before and given back by title_after); the buttons TitleScreen_CheckStartOrExit reads (Jump among them)
 *   start the game, as on Lomax's title;
 * - the camera is Flink's (title_camera, after Player_Update; TitleScreen_Init sets player state 0x60,
 *   so Player_Update leaves it alone), its x at the map's left;
 * - a button: TitleScreen_CheckStartOrExit fades out to the map, as on Lomax's title, and the logo flies
 *   up 6 px a tick as Flink's does (0x2CA0C), Lomax with it if he still hangs.
 * - The music: the file's Music trigger plays the title track as the title loads (music.c). On a fresh
 *   start that is too early: CLomaxApp_InitInstance (0x402F10) loads the title (0x40347F) before it
 *   sets g_bCDAudioAvailable (0x4034EF), so CD_PlayTrack only records the request, and then calls
 *   CD_Stop (0x403639) before the intro AVI. Lomax's own logo played its track 240 ticks in. So while
 *   the requested track isn't the one playing and CD audio is up, title_before plays it again (else
 *   TRACK102 plays only after New Game, not on a fresh start).
 * - No attract demos: TitleControl's update is title_update, Lomax's
 *   TitleScreen_Update without its camera pan, and TitleScreen_CheckStartOrExit's walk-off start of
 *   the demos (Lomax past the camera's x + 320: +0xA4 = 0x4D2, g_bDemoMode) becomes a normal start
 *   (+0xA4 = 1). Its test is Lomax's x > the plane's nMaxScrollX (+0x30) + 320, which he can't
 *   reach inside the view with the camera at the map's left. Lomax's own title demo (DEMOS\DEMO24N2.DAT)
 *   already never played with FLINK.DLL: Level_Start loads DEMO<section + 1>N<sub + 1>.DAT and the title
 *   is section 33 here.
 */
#include "flink.h"

#define TITLE_SUB          1             /* the title's sub-level (0 is Lomax+'s language menu) */
#define TITLE_PLANES       7             /* the file's planes: priority tiles, play map, background */
#define FLINK_SCREEN_W     320
#define FLINK_SCREEN_H     224
#define LOMAX_VIEW_W       320
#define VIEW_H             240
#define FALL_DELAY         30            /* TitleScreen_Init's fade-in timer (+0x80) */
#define LEAVE_STEP         6             /* the logo flies up 6 px a tick (0x2CA0C) */
#define START_DEMO         0x4D2         /* TitleScreen_CheckStartOrExit's +0xA4 for the attract demos */

int g_title;                             /* Flink's title is loaded */
static char g_file[MAX_PATH];
static int g_have;                       /* [Title] read and the file is there */
static int g_logoX, g_logoY0, g_logoY1, g_hangX, g_hangY, g_heroY;

enum { T_WAIT, T_FALL, T_DOWN, T_LEAVE };
static BYTE *g_logo;                     /* the logo actor */
static BYTE *g_logoType;
static int g_phase, g_tick, g_y;         /* the logo's phase, ticks, y (the map's) */
static int g_vw, g_vh, g_camX, g_camY, g_camV;   /* the view, the camera (y 16.16) and its speed */
static int g_ready, g_hanging;

void title_load(const char *ini, const char *rel, const char *dir)
{
    char val[MAX_PATH], path[MAX_PATH];
    g_have = 0;
    GetPrivateProfileStringA("Title", "File", "", val, sizeof(val), ini);
    if (!val[0]) return;
    snprintf(path, sizeof(path), "%s\\%s", dir, val);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) { flog("title %s is missing: Lomax's title", path); return; }
    snprintf(g_file, sizeof(g_file), "%s\\%s", rel, val);
    GetPrivateProfileStringA("Title", "Logo", "", val, sizeof(val), ini);
    if (sscanf(val, "%d,%d,%d", &g_logoX, &g_logoY0, &g_logoY1) != 3 || g_logoY1 < g_logoY0) { flog("[Title] Logo: not understood"); return; }
    GetPrivateProfileStringA("Title", "Hang", "", val, sizeof(val), ini);
    if (sscanf(val, "%d,%d", &g_hangX, &g_hangY) != 2) { flog("[Title] Hang: not understood"); return; }
    g_heroY = GetPrivateProfileIntA("Title", "HeroY", FLINK_SCREEN_H / 2, ini);
    g_have = 1;
    flog("title: %s, logo at x %d from y %d to %d, Lomax at %+d,%+d, %d down the screen", g_file, g_logoX, g_logoY0, g_logoY1, g_hangX, g_hangY, g_heroY);
}

/* the file for the section and sub-level being loaded, NULL: Lomax's title */
const char *title_file(int sec, int sub)
{
    return g_have && sec == SECTION_TITLE && sub == TITLE_SUB ? g_file : NULL;
}

/* the title's row: sub-level 1 shows the file's 3 planes (Lomax's: 0x0F of its 5) */
void title_row(BYTE *row)
{
    if (g_have) *(WORD *)(row + 14 + 2 * TITLE_SUB) = TITLE_PLANES;
}

void title_reset(void)
{
    g_ready = g_hanging = 0;
    g_logo = NULL;
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* the view, once the planes are loaded */
static void title_setup(void)
{
    BYTE *pl = *G_PCURPLANE;
    if (g_ready || !pl) return;
    g_ready = 1;
    g_vw = P_W(pl) * 16 - P_MAXX(pl);
    if (P_MAXX(pl) <= 0 || g_vw < LOMAX_VIEW_W) g_vw = LOMAX_VIEW_W;
    g_vh = P_H(pl) * 16 - P_MAXY(pl);
    if (P_MAXY(pl) <= 0 || g_vh < VIEW_H) g_vh = VIEW_H;
    if (g_vw > P_W(pl) * 16) g_vw = P_W(pl) * 16;
    if (g_vh > P_H(pl) * 16) g_vh = P_H(pl) * 16;
    g_camX = 0;
    g_phase = T_WAIT; g_tick = 0; g_y = g_logoY0;
    g_camY = clamp(g_y + g_hangY - (g_heroY + (g_vh - FLINK_SCREEN_H) / 2), 0, P_H(pl) * 16 - g_vh) << 16;
    g_camV = 0;
    P_CAMX(pl) = g_camX; P_CAMY(pl) = g_camY >> 16;
    flog("title: view %dx%d, camera (%d,%d)", g_vw, g_vh, g_camX, g_camY >> 16);
}

/* the logo's x on the map: Flink's screen x, the 320-px screen centred in the view */
static int logo_x(void) { return g_camX + g_logoX + (g_vw - FLINK_SCREEN_W) / 2; }

static void __cdecl logo_update(BYTE *a)
{
    if (a != g_logo) return;
    A_X(a) = logo_x(); A_Y(a) = g_y;
    A_FX(a) = A_X(a) << 16; A_FY(a) = A_Y(a) << 16;
}

/* 'Spik Logo' (from spik_spawn): the logo, harmless, never culled, at its first y */
void logo_spawn(BYTE *a)
{
    if (!g_title) return;
    A_FLAGS(a) &= ~(0x200000u | AF_HURTS);
    A_CULL(a) = CULL_NEVER;
    A_UPDATE(a) = (void *)logo_update;
    g_logo = a; g_logoType = A_TYPE(a);
    title_setup();
    logo_update(a);
}

static int logo_alive(void) { return g_logo && A_TYPE(g_logo) == g_logoType && A_UPDATE(g_logo) == (void *)logo_update; }

/* Flink's camera (0x1A040) on the map's y: toward Lomax's y - his height on the screen */
static void title_camera(void)
{
    BYTE *pl = *G_PCURPLANE;
    int target = *G_PLAYERY - (g_heroY + (g_vh - FLINK_SCREEN_H) / 2), d = target - (g_camY >> 16);
    int want, v = g_camV, max = P_H(pl) * 16 - g_vh;
    if (abs(d) <= (g_hanging ? 1 : 0)) { g_camV = 0; g_camY = target << 16; }
    else {
        want = (d << 16) >> 4;
        if (v < want) { if (want < 0) v += 0x8000; v += 0x4000; if (v > want) v = want; }
        else if (v > want) { if (want >= 0) v -= 0x8000; v -= 0x4000; if (v < want) v = want; }
        g_camV = v;
        g_camY += v;
    }
    if (g_camY < 0) { g_camY = 0; g_camV = 0; }
    if (g_camY > max << 16) { g_camY = max << 16; g_camV = 0; }
    P_CAMX(pl) = g_camX;
    P_CAMY(pl) = g_camY >> 16;
}

static void place_lomax(void)
{
    int x = logo_x() + g_hangX, y = g_y + g_hangY;
    *G_PLAYERX = x; *G_PLAYERXFIXED = x << 16;
    *G_PLAYERY = y; *G_PLAYERYFIXED = y << 16;
    *G_PLAYERVX = 0; *G_PLAYERVY = 0;
    *G_PLAYERFLAGS &= ~AF_LEFT;
}

/* player_tick, around Player_Update. Lomax doesn't jump on the title: Jump is
   taken out of the buttons for Player_Update and given back after it, for TitleScreen_CheckStartOrExit,
   for which Jump starts the game. */
static WORD g_jumpHeld, g_jumpPressed;

void title_before(void)
{
    WORD jump = (WORD)*G_JUMPMASK;
    g_jumpHeld = g_jumpPressed = 0;
    if (!g_title || !g_ready || !logo_alive()) return;
    g_jumpHeld = *G_INPUTHELD & jump; g_jumpPressed = *G_INPUTPRESSED & jump;
    *G_INPUTHELD &= (WORD)~jump; *G_INPUTPRESSED &= (WORD)~jump;
    /* the title's music once CD audio is up (see the top: a fresh start loads the title before it) */
    if (*G_REQUESTEDCDTRACK > 0 && *G_CURCDTRACK != *G_REQUESTEDCDTRACK && *G_CDAUDIOAVAILABLE && *G_CFGUSECD) {
        flog("title: music %d again (CD audio is up now)", *G_REQUESTEDCDTRACK);
        CD_PlayTrackWrapper(*G_REQUESTEDCDTRACK);
    }
    if (g_hanging || g_phase == T_WAIT) *G_BINPUTLOCKED = 1;
    else *G_PLAYERSTATE &= ~PS_NO_EDGES;          /* landed: he walks, kept inside the view */
    if (*G_BPAUSED) return;                       /* paused (window out of focus, the menu): all stands still */
    g_tick++;
    if (*G_FADESTEP < 0 && g_phase != T_LEAVE) { g_phase = T_LEAVE; flog("title: leaving, the logo flies up"); }
    switch (g_phase) {
    case T_WAIT:
        if (g_tick == 1) { g_hanging = 1; flog("title: Lomax hangs from the logo"); }
        if (g_tick >= FALL_DELAY) g_phase = T_FALL;
        break;
    case T_FALL:
        if (++g_y >= g_logoY1) {
            g_y = g_logoY1; g_phase = T_DOWN;
            if (g_hanging) {                     /* he lets go */
                g_hanging = 0;
                *G_PLAYERSTATE = (*G_PLAYERSTATE & ~(ROPE_STATE | ROPE_CLIMB)) | PS_AIR;
                *G_PLAYERVY = 0;
                flog("title: the logo stopped at y %d, Lomax drops at (%d,%d)", g_y, *G_PLAYERX, *G_PLAYERY);
            }
        }
        break;
    case T_LEAVE:
        if (g_y > (g_camY >> 16) - 200) g_y -= LEAVE_STEP;
        break;
    }
    logo_update(g_logo);
    if (g_hanging) {
        *G_PLAYERSTATE = (*G_PLAYERSTATE & ~(ROPE_CLEAR | ROPE_CLIMB)) | ROPE_STATE | PS_AIR;
        *G_BAIRSPIN = 0;
        *G_JUMPHOLDFORCE = -1;
        place_lomax();
    }
}

void title_after(void)
{
    *G_INPUTHELD |= g_jumpHeld; *G_INPUTPRESSED |= g_jumpPressed;
    g_jumpHeld = g_jumpPressed = 0;
    if (!g_title || !g_ready || !logo_alive() || *G_BPAUSED) return;
    if (g_hanging) {
        *G_PLAYERSTATE &= ~ROPE_CLIMB;
        place_lomax();
    }
    title_camera();
}

/* TitleControl: TitleScreen_Update without the camera's pan, and no attract demos */
static void __cdecl title_check(BYTE *a)
{
    TitleScreen_CheckStartOrExit(a);
    if (*(int *)(a + 0xA4) == START_DEMO) {      /* (never: he stays inside the view) a start instead */
        *(int *)(a + 0xA4) = 1;
        *G_BDEMOMODE = 0;
        flog("title: no attract demo");
    }
}

static void __cdecl title_update(BYTE *a)
{
    int *t = (int *)(a + 0x80);
    title_setup();
    if (*t > 0) --*t;
    if (*t == 0) { *t = -1; if (!*G_FADESTEP) *G_FADESTEP = 0x40; }
    if (*t > 0) return;
    title_check(a);
}

/* object 14: on Flink's title, TitleControl's update and draw */
void __cdecl titlectl_spawn(BYTE *a, BYTE *def)
{
    TitleScreen_Init(a, def);
    if (!g_title) return;
    title_setup();
    if (A_UPDATE(a) == (void *)TitleScreen_Update) A_UPDATE(a) = (void *)title_update;
    if (A_DRAW(a) == (void *)TitleScreen_CheckStartOrExit) A_DRAW(a) = (void *)title_check;
}
