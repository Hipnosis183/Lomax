/*
 * levels.c - Flink's levels in Lomax's level table, and the loading of their maps.
 *
 * LEVELS.INI: [Levels] Count=n, then k=FILE FILE... for k = 1..n: Flink's main level k (a level
 * point of its world map) and the maps on its path, played as Lomax's level k and sub-levels k-1..
 * Its other sections are read by hints.c ([Hints]), exits.c ([Arrivals]) and music.c ([Music]).
 *
 * Level table. g_LevelDefs (0x466450, 0x18 bytes a row) has 24 rows: the 21 levels, then the
 * bonus level, the map and the title (sections 21-23). Flink has 31 main levels, so the table moves
 * into the DLL with 35 rows: Flink's levels in rows 0..30, the bonus level, map and title in rows
 * 31-33, and Flink's intro and ending in row 34 (scenes.c: a copy of the title's row with one sub-level
 * of 4 planes, whose file is the scene's). The code reaches the table only through the 28 field addresses in TABLE_REFS and indexes
 * it by section; sections are numbered only in Game_SetupNextLevel (title 0x17, bonus 0x15, map
 * 0x16, the range check < 0x18), the password check (section < 0x16) and the F1 level select
 * (wraps at 0x16). Those are patched too. A Flink row: every plane of its files (all of a level's
 * files have the same planes; the converter writes 3), one sub-level per file, row 0's jingles and
 * ambience, world index 0 (a real level: the game counts it as progress).
 *
 * The attract demos replay recorded input on Lomax's own levels (sections from the table at
 * 0x466410), so while a demo is being set up rows 0..20 hold Lomax's original rows and the files
 * are not replaced (setup_next_level, which wraps the three calls of Game_SetupNextLevel).
 *
 * Sub-levels. Level_Start reloads the world for the next sub-level (g_bLevelExitReached), so the
 * World_LoadWLD call in Level_LoadWorld (0x40EC2C) goes through world_load, which reads the Flink
 * file of the current section and sub-level. World_LoadWLD opens "WORLDS\<name>" from the game's
 * folder, so the name is ..\MODS\FLINK\WORLDS\Lnn.WLD (levels_load; the DLL's folder must be inside
 * the game's). A sub-level ends at an Exit (exits.c). After the last level the game goes back to the
 * title.
 *
 * Secret levels (README "Secret levels"). Flink's world map opens a hidden path when a level ends at a
 * Warp with checkbox 1 (0x2AEAA sets 0x827B; the map points then turn their hidden neighbours on,
 * 0x2D420). LEVELS.INI's [Secret] lists them: s=FILE, level s is reached only by finishing its level
 * at that map, and after it comes the level after that one (3-5 -> 7-1 -> 4-1). The main flow skips
 * them (6 -> 8). The level-complete banner (Insert_Show 4 or 6) has counted g_LastRealSection up by
 * then, and the map leaves for it (Map_WorldMap_Update), so setup_next_level, on its way to the map
 * after a completed level, sets it to the level that comes next (next_level).
 *
 * Secret levels play as Lomax's bonus level: the Finish that opens one goes
 * straight into it, without the map (setup_next_level clears g_bGoToMap and sets g_CurSection, as
 * Game_SetupNextLevel does itself for the bonus trapdoor), and a death in it leaves it without losing
 * a life, as a fall off the bonus level's bottom does (0x4101D3: g_bGoToMap, g_bSectionChangePending,
 * a fade-out with the music; secret_after), and the map then leaves for the level after it.
 * Lomax's deaths: Player_Update starts g_PlayerDeathTimer at 300 and takes the life at 250
 * (0x4102AF), or, drowned, takes it at once (0x410160); secret_after leaves on either and puts the
 * life back every tick until the next section loads.
 *
 * The title's sub-level 1 can be Flink's title screen (title.c, LEVELS.INI [Title]): world_load opens
 * its file, with 3 planes, and sync_lomax_rows gives the title's row aSubLevelPlaneMask[1] = 7.
 *
 * Sound: Sound_PlayEffect plays only sounds from planes drawn at buckets 0x4D..0xB0. A Flink map's
 * play plane is its second, so while one is loaded Planes_UpdateDepth's bucket table (0x464B28) is
 * {0xA0, 0x90, ...} instead of {0x90, 0x40, ...} (see world_load). Flink's title screen (title.c)
 * has the same planes, so it gets Flink's buckets too, with Lomax's other settings: with Lomax's,
 * Lomax's own sounds (his duck, jump, landing) would be silent there.
 *
 * The continue screen (README "Continue screen") isn't a world load: after the last life Level_Start
 * (0x40F670) keeps the level loaded and calls Level_InitPlanes(0) (0x40F72F), which with g_Lives 0
 * shows only plane 19, MAIN.WLD's (loaded once at start-up), whose ContinueControl (object 42,
 * Cont_ContinueScreen_Init 0x424500) runs it. YES (Trapdoor_Update: g_bContinueTaken, lives 3) comes
 * back through the same call, which then shows the level's planes again, without a reload, as after a
 * lost life. So that call goes through init_planes, which takes the continue screen out of Flink's
 * mode (g_active 0, Lomax's settings, the scrolling screen's state cleared) and gives it back after
 * YES; NO and the time-out go to the title through world_load. Before this, FLINK.DLL went on running
 * the level's per-tick code on the continue screen: a scrolling map's player state
 * 0x20 kept Player_Update from centring the camera on the narrow plane, so on the first continue of a
 * session (the plane's camera still 0) it showed at the left with Lomax+'s bars cutting it at 320 px.
 */
#include "flink.h"

#define LEVELDEFS          0x00466450u   /* g_LevelDefs[24], 0x18 bytes a row */
#define ROW                0x18
#define LOMAX_LEVELS       21
#define ROWS               35            /* 31 levels, bonus, map, title, Flink's intro and ending */
#define MAX_PLANES         16            /* aSubLevelPlaneMask is a ushort */
#define CALL_WORLD_LOADWLD 0x0040EC2Cu   /* Level_LoadWorld: call World_LoadWLD */
#define DEPTH_TABLE        0x00464B28u   /* Planes_UpdateDepth: the shown planes' draw buckets, in order */
#define CALL_INIT_PLANES   0x0040F72Fu   /* Level_Start: call Level_InitPlanes (after a reload, for the
                                            continue screen and after a continue) */

/* the code's references to g_LevelDefs fields (all of them: a scan of .text for 0x466450..0x466690) */
static const struct { DWORD at, value; } TABLE_REFS[] = {
    { 0x40EB29, 0x466450 }, { 0x40EC09, 0x466454 },
    { 0x403663, 0x466458 }, { 0x404D1F, 0x466458 }, { 0x417DA9, 0x466458 },
    { 0x403689, 0x466459 }, { 0x417DCF, 0x466459 },
    { 0x40EBE6, 0x46645A }, { 0x40EC20, 0x46645A }, { 0x4101DE, 0x46645A }, { 0x413BC4, 0x46645A },
    { 0x41A482, 0x46645A }, { 0x41B3EA, 0x46645A }, { 0x41B40C, 0x46645A }, { 0x41B42F, 0x46645A },
    { 0x42796F, 0x46645A }, { 0x427B59, 0x46645A }, { 0x427BEA, 0x46645A }, { 0x42B224, 0x46645A },
    { 0x430A1A, 0x46645A }, { 0x430C5E, 0x46645A }, { 0x430D4D, 0x46645A },
    { 0x414EAE, 0x46645B }, { 0x417E10, 0x46645C }, { 0x417E4C, 0x46645C }, { 0x40EB42, 0x46645E },
};
/* section numbers in the code: the immediate's address, its size, the retail value, the new one
   (LEVEL_COUNT: the number of Flink levels) */
#define LEVEL_COUNT 0xFFFFFFFFu
static const struct { DWORD at; int size; DWORD value, now; } SECTION_IMMS[] = {
    { 0x417C3B, 4, 0x17, ROW_BONUS + 2 },   /* Game_SetupNextLevel: title */
    { 0x417C75, 4, 0x15, ROW_BONUS },       /*   bonus level */
    { 0x417CA1, 4, 0x16, ROW_BONUS + 1 },   /*   map */
    { 0x417CE6, 1, 0x18, ROWS },            /*   cmp eax, 0x18 (sections past it become 0) */
    { 0x4179D9, 1, 0x16, LEVEL_COUNT },     /* Password_DecodeAndApply: cmp di, 0x16 (section < 22) */
    { 0x417DFD, 1, 0x16, LEVEL_COUNT },     /* Debug_SelectNextWorld: cmp eax, 0x16 (wraps to 0) */
};
static const DWORD SETUP_CALLS[] = { 0x404AD6, 0x4083D9, 0x4046BC };   /* call Game_SetupNextLevel */

/* Planes_UpdateDepth's buckets (0x464B28) for Lomax's levels and for Flink's maps, whose play plane
   is the second (see world_load) */
static const short DEPTHS_LOMAX[4] = { 0x90, 0x40, 0x20, 0x10 }, DEPTHS_FLINK[4] = { 0xA0, 0x90, 0x20, 0x10 };
int g_active;
int g_nlevels, g_nsubs[MAX_LEVELS];
char g_files[MAX_LEVELS][MAX_SUBS][MAX_PATH];
static BYTE g_orig[24][ROW];                    /* the retail table */
static BYTE g_rows[ROWS][ROW];                  /* the table the game uses */
static BYTE g_flinkRows[LOMAX_LEVELS][ROW];     /* rows 0..20 in Flink's mode */
static int g_flink = 1;                          /* rows 0..20 are Flink's (not during demos) */
/* [Secret]: level k (0-based) is secret, reached by finishing level g_secretFrom[k] - 1 at its
   sub-level g_secretSub[k] (0: not secret) */
static int g_secretFrom[MAX_LEVELS], g_secretSub[MAX_LEVELS];
static int g_secretLeaving, g_secretLives;      /* leaving a secret level after a death; the lives to keep */
static int g_cont;                               /* the continue screen is up over a Flink map */

const char *map_name(int sec, int sub)
{
    const char *b = strrchr(g_files[sec][sub], '\\');
    return b ? b + 1 : g_files[sec][sub];
}

/* No ambience anywhere, whatever the Ambient sound setting: every row's cAmbientIndex (+9) is -1, so
   Game_SetupNextLevel's Sound_LoadAmbient (0x40DE90) sets g_CurAmbient to -1 and Sound_StartAmbient
   (0x40D9A0; also called by the sound menu, CLomaxApp_InitInstance and Lomax+'s menu) plays nothing.
   Sound_FreeAllAmbient, before it, stops the loop of a level loaded before. */
#define ROW_AMBIENT 9
static void no_ambient(void)
{
    int k;
    for (k = 0; k < ROWS; k++) g_rows[k][ROW_AMBIENT] = 0xFF;
}

static void use_flink_rows(int on)
{
    if (on != g_flink) flog(on ? "rows 0-20: Flink's" : "rows 0-20: Lomax's (demo)");
    g_flink = on;
    memcpy(g_rows, on ? (void *)g_flinkRows : (void *)g_orig, sizeof(g_flinkRows));
    no_ambient();
}

int level_secret(int sec) { return sec >= 0 && sec < g_nlevels && g_secretFrom[sec]; }

/* The play order's detours (README "Levels", Play order), Lomax's
   level numbers: finishing level `from` leads to level `to` (DETOUR_END: past the last level, the
   title). So the game goes 1..16, 25..28, 18..22, 29..31, 23, 24 and ends after 24. A detour with a
   shortcut bit applies only once that shortcut was taken (README "Spells", Shortcuts): SHRINK at 14-1
   goes on to 25, and then 28 leads back to 14 and 16 past 25..28 to 18; GHOST at 20-1 goes on to 29,
   and then 31 leads back to 20 and 22 past 29..31 to 23. Those come first. */
#define DETOUR_END 0
static const struct { int from, to, shortcut; } DETOURS[] = {
    { 28, 14, SHORTCUT_SHRINK }, { 16, 18, SHORTCUT_SHRINK }, { 31, 20, SHORTCUT_GHOST }, { 22, 23, SHORTCUT_GHOST },
    { 16, 25, 0 }, { 28, 18, 0 }, { 22, 29, 0 }, { 31, 23, 0 }, { 24, DETOUR_END, 0 },
};
int g_shortcuts;                                 /* SHORTCUT_* taken in this game (password.c keeps them) */
static int g_mapFrom = -1;                       /* the level the world map was entered from, -1: none */
static int g_mapFromNone;                        /* a password: the next map has none */

/* the level whose point the world map pans from: the one just left, when it isn't the one the map
   leaves for (a lost life, the time-out); -1: none (the map then pans from the level before) */
int level_map_from(void) { return g_mapFrom != *G_LASTREALSECTION ? g_mapFrom : -1; }
void level_map_from_clear(void) { g_mapFromNone = 1; }

/* the level after finishing level sec at sub-level sub, with these shortcuts taken: a secret level
   opened by that map, the level after the one that opened a secret level, a detour, or the next one
   that isn't secret */
static int next_level_with(int sec, int sub, int shortcuts)
{
    int k;
    unsigned i;
    for (k = 0; k < g_nlevels; k++)
        if (g_secretFrom[k] == sec + 1 && g_secretSub[k] == sub) return k;
    if (level_secret(sec)) sec = g_secretFrom[sec] - 1;
    for (i = 0; i < COUNT(DETOURS); i++)
        if (DETOURS[i].from == sec + 1 && DETOURS[i].from <= g_nlevels &&
            (!DETOURS[i].shortcut || (shortcuts & DETOURS[i].shortcut))) {
            if (DETOURS[i].to == DETOUR_END) return g_nlevels;
            if (DETOURS[i].to <= g_nlevels && !level_secret(DETOURS[i].to - 1)) return DETOURS[i].to - 1;
        }
    for (k = sec + 1; k < g_nlevels && level_secret(k); k++) ;
    return k;
}

static int next_level(int sec, int sub) { return next_level_with(sec, sub, g_shortcuts); }

/* a shortcut (spells.c): taken, and the screen fades out to the world map, which leaves for level `to`
   (Lomax's number; g_LastRealSection, as the level-complete banner sets it), the lives kept */
void level_shortcut(int shortcut, int to)
{
    if (to < 1 || to > g_nlevels) return;
    g_shortcuts |= shortcut;
    password_sync();
    *G_LASTREALSECTION = to - 1;
    *G_BGOTOMAP = 1;
    *G_BSECTIONCHANGE = 1;
    if (!*G_FADESTEP) *G_FADESTEP = -0x40;
    *G_BFADEOUTMUSIC = 1;
    flog("level %d-%d: shortcut %d to level %d", *G_LOADEDSECTION + 1, *G_CURSUBLEVEL + 1, shortcut, to);
}

/* a level's place in the play order (0 first), for the world map's files: a secret level takes the
   place of the level after it; g_nlevels or more, past the last */
int level_wld_index(int sec) { return sec >= 0 && sec < ROWS ? g_rows[sec][0x0A] : 0; }

int level_rank(int sec)
{
    int k, r;
    if (sec < 0) return 0;
    if (sec >= g_nlevels) return g_nlevels;
    if (level_secret(sec)) sec = next_level_with(sec, -1, 0);
    for (k = 0, r = 0; k < g_nlevels && r <= g_nlevels; k = next_level_with(k, -1, 0), r++)
        if (k == sec) return r;
    return g_nlevels;
}

/* the play order's place for the world map's files (worldmap_file): level_rank, but a level whose
   shortcut was taken (SHRINK's 14, GHOST's 20) counts as the level after it, so that the map shows
   the change its area makes (the tree's base, the path over the clouds) when the game comes back to it */
int level_map_rank(int sec)
{
    if ((g_shortcuts & SHORTCUT_SHRINK && sec == SHORTCUT_SHRINK_LEVEL - 1) ||
        (g_shortcuts & SHORTCUT_GHOST && sec == SHORTCUT_GHOST_LEVEL - 1))
        return level_rank(next_level_with(sec, -1, 0));
    return level_rank(sec);
}

/* player_tick, around Player_Update: a death in a secret level leaves it, as the bonus level's
   bottom does, and keeps the life */
void secret_before(void)
{
    if (!g_secretLeaving) g_secretLives = *G_LIVES;
}

void secret_after(void)
{
    if (!g_active || !level_secret(*G_LOADEDSECTION)) return;
    if (!g_secretLeaving && (*G_PLAYERDEATHTIMER > 0 || *G_LIVES < g_secretLives) && !*G_BSECTIONCHANGE) {
        g_secretLeaving = 1;
        *G_BGOTOMAP = 1;
        *G_BSECTIONCHANGE = 1;
        if (!*G_FADESTEP) *G_FADESTEP = -0x40;
        *G_BFADEOUTMUSIC = 1;
        flog("level %d: died, leaving the secret level", *G_LOADEDSECTION + 1);
    }
    if (g_secretLeaving) *G_LIVES = g_secretLives;
}

static void secrets_load(const char *ini)
{
    static char buf[1024];
    char file[MAX_PATH], *p;
    int k, j, sec, n = (int)GetPrivateProfileSectionA("Secret", buf, sizeof(buf), ini), found;
    for (p = buf; n && *p; p += strlen(p) + 1) {
        found = 0;
        if (sscanf(p, "%d=%259s", &sec, file) == 2 && sec >= 1 && sec <= g_nlevels)
            for (k = 0; k < g_nlevels && !found; k++)
                for (j = 0; j < g_nsubs[k] && !found; j++)
                    if (k != sec - 1 && !_stricmp(map_name(k, j), file)) {
                        g_secretFrom[sec - 1] = k + 1; g_secretSub[sec - 1] = j; found = 1;
                    }
        if (found) flog("level %d: secret, after %d-%d", sec, g_secretFrom[sec - 1], g_secretSub[sec - 1] + 1);
        else flog("[Secret] %s: not understood", p);
    }
}

/* The bonus level, map and title rows follow the retail rows 21-23, which other mods may change:
   Lomax+ sets the title's clear-screen flag (+8 bit 4) for its language screen right after its
   hook of Game_SetupNextLevel loads it, and clears it before the next load. Copied during every
   load and after every Game_SetupNextLevel (this wrapper calls the function from outside any hook
   on it). */
static void sync_lomax_rows(void)
{
    memcpy(g_rows[ROW_BONUS], (BYTE *)LEVELDEFS + LOMAX_LEVELS * ROW, 3 * ROW);
    if (g_flink) worldmap_row(g_rows[SECTION_MAP]);   /* Flink's world map (worldmap.c) */
    title_row(g_rows[SECTION_TITLE]);                  /* Flink's title (title.c) */
    no_ambient();
}

/* wraps Game_SetupNextLevel: picks the rows, and sends the game past the last level to the title.
   The map leaves for section g_LastRealSection, which the level-complete banner counts up; past the
   last level that is g_nlevels, up to 31 = the bonus row (the bonus level itself is reached only
   through g_bBonusTrapdoorOpen, set in Game_SetupNextLevel). */
static int __cdecl setup_next_level(void)
{
    int reset = *G_LIVES < 0 || (!*G_CONTINUES && !*G_LIVES);   /* Game_ResetNewGame: the title */
    int other = reset || *G_BGOTOTITLE || *G_BBONUSOPEN || *G_BGOTOMAP;
    int demo = !other && *G_BDEMOMODE, r;
    use_flink_rows(!demo);
    /* to the map after a completed Flink level (the banner counted g_LastRealSection up; a lost
       life or the time-out goes to the map without counting), or out of a secret level after a death */
    if (!reset && *G_BGOTOMAP && !*G_BGOTOTITLE && !*G_BBONUSOPEN && *G_LOADEDSECTION >= 0 &&
        *G_LOADEDSECTION < g_nlevels && (*G_LASTREALSECTION == *G_LOADEDSECTION + 1 ||
        (g_secretLeaving && level_secret(*G_LOADEDSECTION)))) {
        int next = next_level(*G_LOADEDSECTION, *G_CURSUBLEVEL);
        if (next != *G_LASTREALSECTION)
            flog("level %d done at %d-%d: level %d next", *G_LOADEDSECTION + 1, *G_LOADEDSECTION + 1, *G_CURSUBLEVEL + 1, next + 1);
        *G_LASTREALSECTION = next;
        /* the game is finished: Flink's ending instead of the map (as its MapControl, 0x2D774, starts
           the ending when the map loads with the last level done), then the intro and the title */
        if (next >= g_nlevels) scene_ending();
        if (level_secret(next)) {                     /* straight in, as the bonus level */
            *G_BGOTOMAP = 0;
            *G_CURSECTION = next;
            *G_CURSUBLEVEL = 0;
            flog("level %d: straight in, no map", next + 1);
        }
    }
    g_secretLeaving = 0;
    if (!other && !demo && *G_CURSECTION >= g_nlevels && *G_CURSECTION <= ROW_BONUS) {
        flog("level %d: past the last level, back to the title", *G_CURSECTION + 1);
        *G_BGOTOTITLE = 1;
        *G_LASTREALSECTION = 0;
    }
    if (reset || *G_BGOTOTITLE) spellfx_new_game();  /* to the title: no SHIELD (spellfx.c) */
    if ((reset || *G_BGOTOTITLE) && g_shortcuts) {   /* to the title: a new game has none */
        g_shortcuts = 0;
        password_sync();
        flog("shortcuts cleared");
    }
    /* the world map pans from the point of the level just left (worldmap.c, level_map_from) */
    g_mapFrom = *G_BGOTOMAP && !*G_BGOTOTITLE && !reset && !g_mapFromNone && *G_LOADEDSECTION >= 0 &&
                *G_LOADEDSECTION < g_nlevels ? *G_LOADEDSECTION : -1;
    g_mapFromNone = 0;
    r = Game_SetupNextLevel();
    sync_lomax_rows();
    return r;
}

/* the game's settings for Flink's maps or for Lomax's */
static void flink_settings(int flink)
{
    /* Sound_PlayEffect (0x40D5F0) plays only sounds whose plane's draw bucket is 0x4D..0xB0 (scaled
       by bucket / 0x90). A Flink map's play plane is the second, bucket 0x40: Lomax was silent. Its
       planes get 0xA0 / 0x90 instead, keeping plane 0 (Flink's priority tiles) in front of the play
       plane's actors (bucket + 2..14). */
    patch(DEPTH_TABLE, flink ? DEPTHS_FLINK : DEPTHS_LOMAX, sizeof(DEPTHS_LOMAX));
    chains_set(flink);
    move_set(flink);
    powers_set(flink);
    sfx_set(flink);
}

/* World_LoadWLD in Level_LoadWorld: Flink's file for the section and sub-level, and every file's
   per-map state cleared */
static int __cdecl world_load(const char *file, unsigned planeMask, unsigned activeMask)
{
    const char *scene = scene_file();               /* (first: the title's load may become the intro's) */
    int sec = *G_LOADEDSECTION, sub = *G_CURSUBLEVEL;
    sync_lomax_rows();
    g_active = g_flink && sec >= 0 && sec < g_nlevels && sub >= 0 && sub < g_nsubs[sec];
    if (g_active) {
        file = g_files[sec][sub];
        flog("level %d-%d: %s (planes %X)", sec + 1, sub + 1, file, planeMask);
    }
    g_map = 0;
    if (g_flink && sec == SECTION_MAP && worldmap_file()) {   /* Flink's world map (worldmap.c) */
        g_map = 1;
        file = worldmap_file();
        planeMask = activeMask = 1;
        flog("map for level %d: %s", *G_LASTREALSECTION + 1, file);
    }
    g_title = 0;
    if (scene) {                                    /* Flink's intro and ending (scenes.c) */
        file = scene;
        planeMask = activeMask = SCENE_PLANES;
    } else if (title_file(sec, sub)) {              /* Flink's title (title.c) */
        g_title = 1;
        file = title_file(sec, sub);
        planeMask = activeMask = 7;
        flog("title: %s", file);
    }
    worldmap_reset();
    title_reset();
    scenes_reset();
    water_reset();
    exits_reset();
    if (!*G_BLEVELEXITREACHED) gone_reset();        /* entered from the map: all back (a continue: init_planes) */
    platforms_reset();
    jumppads_reset();
    seesaws_reset();
    enemies_reset();
    deco_reset();
    chests_reset();
    monkeys_reset();
    grab_reset();
    throwplats_reset();
    leaves_reset();
    floatings_reset();
    groundtype_reset();
    snakes_reset();
    balls_reset();
    stahls_reset();
    spikes_reset();
    drops_reset();
    apples_reset();
    bigapples_reset();
    butterflies_reset();
    lava_reset();
    prot_reset();
    great_reset();
    burst_reset();
    fountain_reset();
    rollingstones_reset();
    ropeslides_reset();
    swings_reset();
    wheels_reset();
    scroll_reset();
    steers_reset();
    hands_reset();
    walls_reset();
    rocks_reset();
    razorbacks_reset();
    storms_reset();
    blitz_reset();
    circ_reset();
    wagons_reset();
    explodes_reset();
    wolky_reset();
    bridges_reset();
    wurms_reset();
    puges_reset();
    villageboss_reset();
    metaboss_reset();
    bos1_reset();
    boss_fight_reset();
    donkeys_reset();
    cyclindudes_reset();
    plants_reset();
    spells_reset();
    stomp_reset();
    sfx_reset();
    g_cont = 0;
    flink_settings(g_active);
    if (g_title || scene) patch(DEPTH_TABLE, DEPTHS_FLINK, sizeof(DEPTHS_FLINK));   /* their play plane is the second too */
    if (g_active) *G_BBONUSOPEN = 0;               /* no bonus levels with Flink's (enemy.c, enemy_defeated) */
    return World_LoadWLD(file, planeMask, activeMask);
}

/* Level_InitPlanes in Level_Start (see the top): runs before Actors_ResetAndSpawnInitial and the
   control scheme's load (powers.c), so the spawns and the masks already see the mode */
static int __cdecl init_planes(int keep)
{
    if (!*G_LIVES && g_active) {                     /* the continue screen: Lomax's */
        g_cont = 1;
        g_active = 0;
        scroll_reset();
        flink_settings(0);
        flog("continue screen: Lomax's, level %d-%d kept", *G_LOADEDSECTION + 1, *G_CURSUBLEVEL + 1);
    } else if (*G_LIVES && g_cont) {                 /* YES: the level again, as after a lost life */
        g_cont = 0;
        g_active = 1;
        flink_settings(1);
        gone_reset();                                /* dead enemies, opened chests: all back
                                                        (Level_InitPlanes(0) clears the placements'
                                                        own states, so g_gone is all that kept them) */
        *G_COINS = 0;                                /* and the counters from 0: */
        *G_LEMMINGSFREED = 0;                        /* coins, defeated enemies (enemy_defeated), */
        g_spirits = 0;                               /* spirits (spirits.c) */
        flog("continue taken: level %d-%d, everything back, counters 0", *G_LOADEDSECTION + 1, *G_CURSUBLEVEL + 1);
    }
    razorbacks_reset();                                   /* (the stampede's shake: razorback.c) */
    return Level_InitPlanes(keep);
}

/* the number of planes (HEAD chunks) of a Lomax world file, 0 if it isn't one */
static int count_planes(const char *path)
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    BYTE hdr[16];
    DWORD got, size;
    int n = 0, first = 1;
    if (f == INVALID_HANDLE_VALUE) return 0;
    while (ReadFile(f, hdr, 8, &got, NULL) && got == 8) {
        size = (DWORD)hdr[4] << 24 | hdr[5] << 16 | hdr[6] << 8 | hdr[7];
        if (first) {                            /* WRLD "PS-X", 1234 */
            if (memcmp(hdr, "WRLD", 4) || size < 8 || !ReadFile(f, hdr + 8, 8, &got, NULL) || got != 8 ||
                memcmp(hdr + 8, "PS-X", 4) || *(DWORD *)(hdr + 12) != 1234) { n = 0; break; }
            size -= 8;
            first = 0;
        }
        if (!memcmp(hdr, "HEAD", 4)) n++;
        if (!memcmp(hdr, "END ", 4)) break;
        if (SetFilePointer(f, (LONG)size, NULL, FILE_CURRENT) == INVALID_SET_FILE_POINTER) break;
    }
    CloseHandle(f);
    return n;
}

/* the retail bytes at the level table's patch sites */
int levels_retail(void)
{
    size_t i;
    for (i = 0; i < COUNT(TABLE_REFS); i++)
        if (*(DWORD *)TABLE_REFS[i].at != TABLE_REFS[i].value) { flog("table ref %lX", TABLE_REFS[i].at); return 0; }
    for (i = 0; i < COUNT(SECTION_IMMS); i++)
        if ((SECTION_IMMS[i].size == 4 ? *(DWORD *)SECTION_IMMS[i].at : *(BYTE *)SECTION_IMMS[i].at) != SECTION_IMMS[i].value)
            { flog("section number %lX", SECTION_IMMS[i].at); return 0; }
    for (i = 0; i < COUNT(SETUP_CALLS); i++)
        if (!is_call(SETUP_CALLS[i], (DWORD)Game_SetupNextLevel)) { flog("call %lX", SETUP_CALLS[i]); return 0; }
    return is_call(CALL_WORLD_LOADWLD, (DWORD)World_LoadWLD) && is_call(CALL_INIT_PLANES, (DWORD)Level_InitPlanes) &&
           is_bytes(DEPTH_TABLE, DEPTHS_LOMAX, sizeof(DEPTHS_LOMAX));
}

/* reads LEVELS.INI and builds Flink's rows; 0 if a file is missing or isn't a usable world */
int levels_load(void)
{
    const char *folder = LEVELS_FOLDER;
    char cwd[MAX_PATH], rel[MAX_PATH], key[8], list[1024], path[MAX_PATH], *tok;
    size_t n;
    int k, planes;
    memcpy(g_orig, (void *)LEVELDEFS, sizeof(g_orig));
    /* the game opens "WORLDS\" + the name it's given from its folder (the current one), so the
       name is ..\<the DLL's folder, from the game's>\<folder>\FILE */
    n = GetCurrentDirectoryA(sizeof(cwd), cwd);
    if (!n || n >= sizeof(cwd) || _strnicmp(g_dir, cwd, n) || g_dir[n] != '\\') {
        flog("%s is not inside the game's folder %s", g_dir, cwd);
        return 0;
    }
    snprintf(rel, sizeof(rel), "..\\%s\\%s", g_dir + n + 1, folder);
    snprintf(path, sizeof(path), "%s\\%s\\LEVELS.INI", g_dir, folder);
    g_nlevels = GetPrivateProfileIntA("Levels", "Count", 0, path);
    if (g_nlevels < 1 || g_nlevels > MAX_LEVELS) { flog("%s: Count %d (1-%d)", path, g_nlevels, MAX_LEVELS); return 0; }
    for (k = 0; k < g_nlevels; k++) {
        BYTE *row = g_rows[k];
        unsigned mask = 0;
        snprintf(key, sizeof(key), "%d", k + 1);
        GetPrivateProfileStringA("Levels", key, "", list, sizeof(list), path);
        g_nsubs[k] = 0;
        for (tok = strtok(list, " ,\t"); tok; tok = strtok(NULL, " ,\t")) {
            char wld[MAX_PATH];
            if (g_nsubs[k] == MAX_SUBS) { flog("level %d: more than %d maps", k + 1, MAX_SUBS); return 0; }
            snprintf(g_files[k][g_nsubs[k]], MAX_PATH, "%s\\%s", rel, tok);
            snprintf(wld, sizeof(wld), "%s\\%s\\%s", g_dir, folder, tok);
            planes = count_planes(wld);
            if (planes < 1 || planes > MAX_PLANES || (mask && mask != (1u << planes) - 1)) {
                flog("%s is missing, not a Lomax world or has other planes than its level (%d)", wld, planes);
                return 0;
            }
            mask = (1u << planes) - 1;
            g_nsubs[k]++;
        }
        if (!g_nsubs[k]) { flog("level %d has no maps", k + 1); return 0; }
        memcpy(row, g_orig[0], ROW);                 /* row 0's jingles and ambience */
        *(DWORD *)(row + 0) = mask;                  /* dwPlaneMask */
        *(DWORD *)(row + 4) = mask;                  /* dwActivePlaneMask (each file has one palette) */
        row[10] = 0;                                 /* bWldIndex: MEDIEVAL, a real level */
        row[11] = 0;                                 /* bRotatingPlanes */
        *(WORD *)(row + 12) = (WORD)g_nsubs[k];      /* wNumSubLevels */
        memset(row + 14, 0, 10);
        for (planes = 0; planes < g_nsubs[k]; planes++) *(WORD *)(row + 14 + planes * 2) = (WORD)mask;
        flog("level %d: %d maps (%s ...)", k + 1, g_nsubs[k], g_files[k][0]);
    }
    for (; k < ROW_BONUS; k++) memcpy(g_rows[k], g_rows[0], ROW);   /* not reached: the title */
    memcpy(g_rows[ROW_BONUS], g_orig[21], 3 * ROW);                  /* bonus level, map, title */
    memcpy(g_rows[SECTION_SCENE], g_orig[23], ROW);                  /* the scenes: the title's, */
    scene_row(g_rows[SECTION_SCENE]);                                /*   one sub-level of 4 planes */
    memcpy(g_flinkRows, g_rows, sizeof(g_flinkRows));
    hints_load(path);
    arrivals_load(path);
    music_load(path);
    secrets_load(path);
    spells_load(path);
    snprintf(cwd, sizeof(cwd), "%s\\%s", g_dir, folder);
    worldmap_load(path, rel, cwd);
    title_load(path, rel, cwd);
    scenes_load(path, rel, cwd);
    sync_lomax_rows();                               /* (the title's row for the first load) */
    return 1;
}

void levels_install(void)
{
    size_t i;
    BYTE imm[4];
    DWORD v;
    for (i = 0; i < COUNT(TABLE_REFS); i++) patch_dword(TABLE_REFS[i].at, (DWORD)g_rows + (TABLE_REFS[i].value - LEVELDEFS));
    for (i = 0; i < COUNT(SECTION_IMMS); i++) {
        v = SECTION_IMMS[i].now == LEVEL_COUNT ? (DWORD)g_nlevels : SECTION_IMMS[i].now;
        memcpy(imm, &v, 4);
        patch(SECTION_IMMS[i].at, imm, SECTION_IMMS[i].size);
    }
    for (i = 0; i < COUNT(SETUP_CALLS); i++) patch_call(SETUP_CALLS[i], (const void *)setup_next_level);
    patch_call(CALL_WORLD_LOADWLD, (const void *)world_load);
    patch_call(CALL_INIT_PLANES, (const void *)init_planes);
}
