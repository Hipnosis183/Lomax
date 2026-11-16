/*
 * worldmap.c - Flink's world map in place of Lomax's map screen (README "World map").
 *
 * The converter writes Flink's world map (MAIN.WLD map 3, 1280 x 768) as MODS\FLINK\WORLDS\MAP.WLD,
 * and one more file for each of Flink's map changes (MAP1.WLD ...), each with map 4's storm clouds as
 * an ANIM of blocks. LEVELS.INI's [Map] gives each level's point on it (k=x,y, Flink's 'Map ' actor
 * of that level), Start, the START sign, and Clouds=bank,frames; [MapFiles] from=FILE says from which
 * level (1-based) each file is shown. Without them, or during the attract demos, the map screen is
 * Lomax's own.
 *
 * Lomax's map screen (section 0x16, row SECTION_MAP) is one plane 960 x 240 that scrolls across only:
 * - Map Control (object 41, Map_WorldMap_Init 0x424100) moves itself to a high slot with the update
 *   Map_WorldMap_Update (0x4241C0): it fades in after 80 ticks, scrolls g_pCurPlane's x from
 *   nMaxScrollX toward g_MapScrollTargetX (velocity (x - target) / 16, changed by at most 0.25 a tick,
 *   0.75 when turning, up to 3 px a tick), counts g_CameraOrbitOffset up from -100 with the distance
 *   left ((target - x + 13) * (120 + g_MapZoomBias) / 100, held at 0 from the first time it reaches
 *   it), arms the section change (g_bFrameReady: g_CurSection = g_LastRealSection) and, on a button
 *   once Lomax is in the air going up slower than 3 px a tick, fades out. Its draw is the password
 *   (Map_DrawPasswordGems 0x4243C0).
 * - Little Hero (object 39, Litt_MapHeroMarker_Init 0x423AB0): param = section + 1; the others kill
 *   themselves, the current one sets g_MapScrollTargetX = max(0, x - 160) and, 30 ticks after the
 *   orbit offset reaches 0, shows the little Lomax hopping on its spot.
 * - MapTrapdoor (object 38): Lomax waits in it until the orbit offset is 0, drops out onto the map's
 *   floor (Plane_FindGroundY 0x413B90 gives the map section, world index 6, a fixed floor at y
 *   0xBE - 0x44 = 122) and a button makes him jump back in; 100 px up the screen fades.
 * Lomax+ hooks Map_WorldMap_Update and the trapdoor's update for its widescreen view.
 *
 * On Flink's map:
 * - The camera is FLINK.DLL's (map_update replaces the Map Control's update, keeping all it does but
 *   the scroll and the button): it pans in both directions with the same rule, per axis, from the
 *   point of the level just played (level_map_from: the detours and shortcuts leave levels out of
 *   number order, so the level before can be far off; else, after a lost life or a password, the
 *   previous level's, the START sign before level 1) to the current one, each centred in the view
 *   (the plane's width - nMaxScrollX, as Lomax+ widens it) and held inside the map so the map always
 *   fills the screen; the orbit offset counts the larger distance left. The pan starts with the fade
 *   in: Flink's points are close together, and a pan of 60 px was over before the screen showed.
 * - The storm clouds wave as Flink's (MapControl's draw hook 0x2DC5E, VDP scroll per line across and
 *   per 16-px column down, a phase p + 8 of 1024 a frame). Lomax scrolls a plane only as a whole; the
 *   converter computes the screen at 64 phases (cloud_frames), each as 15 bands of 16 lines, in
 *   banks of 8 phases ([Map] Clouds: first bank, phases, bands, phases a bank; one chunk has to fit
 *   File_ReadChunk's 512 KB buffer), and
 *   map_draw, the Map Control's draw, queues this tick's phase's bands (Sprite_QueueScreen; a frame's
 *   offset is its place on the screen) at the map plane's bucket + 1, under every actor (their
 *   plane's + 2 at least). Moving 439 blocks of the plane every tick would cost too much for
 *   Lomax+'s FPS Override at 120.
 * - Lomax's tree top from Lomax's map ([Map] Platform: the first Mapforeground prop's pieces) is
 *   drawn mirrored under him (bucket + 2, Sprite_QueueScreen flag 8), so that he stands on it as on
 *   Lomax's map, mirrored ([Map] Feet: 23 px right of its origin there, 64 above), screen-fixed as Ed
 *   and his tree are on Lomax's map.
 * - There is no trapdoor: Lomax stands on his tree top at the screen's bottom (place_lomax: the tree
 *   top's drawn pixels centred under the password box and resting on the screen's bottom line;
 *   LOMAX_DX / LOMAX_FEET without one), facing left, from the start, input locked (world_before holds
 *   him there every tick and shifts him, his clip line and the camera so that the map's fixed floor
 *   is under his feet during Player_Update). A button fades the screen out (the trapdoor's jump is gone:
 *   Lomax doesn't jump on the map), and the level starts.
 */
#include "flink.h"

#define MAP_PLANE          0
#define MAX_MAPFILES       8
#define HERO_DY            16            /* the view centres this much above the hero's feet */
#define LOMAX_VIEW_W       320
#define VIEW_H             240
#define MAX_STEP           0x30000       /* Map_WorldMap_Update: 3 px a tick */
#define ORBIT_NEAR         13            /*   the orbit offset is 0 within 13 px */
#define MAP_FLOOR          122           /* Plane_FindGroundY's floor on the map section */
#define LOMAX_DX           34            /* Lomax's x from the view's right edge (without a tree top) */
#define LOMAX_FEET         232           /*   his feet's y on the screen */
#define BOX_HALF           56            /* Map_DrawPasswordGems: 8 gems 16 px apart from the actor's x
                                            in a 7-cell frame; its centre is x + 7 * 16 / 2 */
#define BOX_CX_RIGHT       76            /* the box's centre from the right edge: 320 - (0xBC + 56) */
#define BUTTONS            0x1F8         /* Map_WorldMap_Update's buttons (and 0x40) */
#define WAVE_STEP          8             /* the wave's phase: + 0x10 bytes (8 of 1024 entries) a frame */
#define CLIP_NONE          (-20000)

#define Map_DrawPasswordGems     ((ActorFn)0x004243C0u)
#define Sprite_QueueScreen       ((void (__cdecl *)(const BYTE *frame, unsigned flags, int x, int y))0x0040A7B0u)

int g_map;                               /* Flink's world map is loaded */
#define MAX_PIECES         8

static POINT g_point[MAX_LEVELS], g_start, g_feet;
static int g_npoints, g_haveStart, g_cloudBank, g_cloudPhases, g_cloudBands, g_cloudPerBank;
static struct { int bank, frame, dx, dy; } g_piece[MAX_PIECES];   /* Lomax's tree top */
static int g_npieces;
static struct { int from; char file[MAX_PATH]; } g_files_map[MAX_MAPFILES];
static int g_nfiles;

static int g_ready;                      /* map_setup ran for this load */
static int g_vw, g_vh, g_tx, g_ty;       /* the view's size, the camera's target */
static int g_cx, g_cy, g_velx, g_vely;   /* the camera (16.16) and its speed */
static int g_latched;                    /* the orbit offset reached 0 */
static int g_placed, g_leaving, g_wave;  /* Lomax put at the bottom right, leaving; the wave's phase */
static int g_lomaxX, g_lomaxFeet;        /* his x and his feet's y on the screen */
static int g_treeX, g_treeY;             /* the tree top's origin on the screen */
static int g_boxCX;                      /* the password box's centre on the screen */
static RECT g_pbox;                      /* [Map] PlatformBox */

/* frame n (from 1) of a bank of the map's sprites */
static const BYTE *bank_frame(int bank, int n)
{
    const BYTE *b = G_ANIMBANKS[bank];
    return b ? b + n * 0x38 + 0x10 : NULL;
}

/* Lomax's place: the tree top mirrored (as Lomax faces left), its drawn pixels
   ([Map] PlatformBox, unmirrored: the branch's frame has an empty last line) centred across under the
   password box (g_boxCX) and resting on the view's bottom, kept on the screen; Lomax on it at its
   centre (on Lomax's map he stands 23 px right of the origin, [Map] Feet's x,
   which left him 21 px off the box's centre), at the height he stands at there ([Map] Feet's y; his
   hitbox is -9..9 about his x, his standing sprite within a pixel or two of it). Without a tree
   top: LOMAX_DX from the right edge, feet at LOMAX_FEET. */
static void place_lomax(void)
{
    int lo, hi;
    g_lomaxX = g_vw - LOMAX_DX; g_lomaxFeet = LOMAX_FEET;
    if (!g_npieces || g_pbox.right <= g_pbox.left) return;
    g_treeX = g_boxCX + (g_pbox.left + g_pbox.right) / 2;     /* mirrored, it spans x - right .. x - left */
    lo = g_pbox.right; hi = g_vw + g_pbox.left;
    if (g_treeX > hi) g_treeX = hi;
    if (g_treeX < lo) g_treeX = lo;
    g_treeY = g_vh - g_pbox.bottom;
    g_lomaxX = g_treeX - (g_pbox.left + g_pbox.right) / 2;   /* the tree's centre (his x is his middle) */
    g_lomaxFeet = g_treeY + g_feet.y;
}

void worldmap_load(const char *ini, const char *rel, const char *dir)
{
    static char buf[4096];
    char key[16], val[64], path[MAX_PATH], *p;
    int k, x, y, n;
    g_npoints = g_nfiles = 0;
    for (k = 1; k <= g_nlevels; k++) {
        snprintf(key, sizeof(key), "%d", k);
        GetPrivateProfileStringA("Map", key, "", val, sizeof(val), ini);
        if (sscanf(val, "%d,%d", &x, &y) != 2) break;
        g_point[k - 1].x = x; g_point[k - 1].y = y;
        g_npoints = k;
    }
    GetPrivateProfileStringA("Map", "Start", "", val, sizeof(val), ini);
    g_haveStart = sscanf(val, "%d,%d", &x, &y) == 2;
    if (g_haveStart) { g_start.x = x; g_start.y = y; }
    GetPrivateProfileStringA("Map", "Clouds", "", val, sizeof(val), ini);
    if (sscanf(val, "%d,%d,%d,%d", &g_cloudBank, &g_cloudPhases, &g_cloudBands, &g_cloudPerBank) != 4 ||
        g_cloudBank < 1 || g_cloudPhases < 1 || g_cloudBands < 1 || g_cloudPerBank < 1 ||
        g_cloudBank + (g_cloudPhases - 1) / g_cloudPerBank > 255)
        g_cloudBank = g_cloudPhases = g_cloudBands = g_cloudPerBank = 0;
    GetPrivateProfileStringA("Map", "Platform", "", buf, sizeof(buf), ini);
    for (g_npieces = 0, p = buf; g_npieces < MAX_PIECES; g_npieces++) {
        int b, f, dx, dy, used;
        if (sscanf(p, "%d,%d,%d,%d%n", &b, &f, &dx, &dy, &used) != 4 || b < 1 || b > 255 || f < 1) break;
        g_piece[g_npieces].bank = b; g_piece[g_npieces].frame = f;
        g_piece[g_npieces].dx = dx; g_piece[g_npieces].dy = dy;
        p += used;
        if (*p == ',') p++;
    }
    GetPrivateProfileStringA("Map", "Feet", "0,0", val, sizeof(val), ini);
    if (sscanf(val, "%d,%d", &x, &y) == 2) { g_feet.x = x; g_feet.y = y; }
    GetPrivateProfileStringA("Map", "PlatformBox", "", val, sizeof(val), ini);
    if (sscanf(val, "%ld,%ld,%ld,%ld", &g_pbox.left, &g_pbox.top, &g_pbox.right, &g_pbox.bottom) != 4) g_npieces = 0;
    n = (int)GetPrivateProfileSectionA("MapFiles", buf, sizeof(buf), ini);
    for (p = buf; n && *p && g_nfiles < MAX_MAPFILES; p += strlen(p) + 1) {
        char file[MAX_PATH];
        if (sscanf(p, "%d=%259s", &k, file) != 2) { flog("[MapFiles] %s: not understood", p); continue; }
        snprintf(path, sizeof(path), "%s\\%s", dir, file);
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) { flog("world map %s is missing", path); continue; }
        g_files_map[g_nfiles].from = k;
        snprintf(g_files_map[g_nfiles].file, MAX_PATH, "%s\\%s", rel, file);
        g_nfiles++;
    }
    if (g_nfiles && g_npoints < g_nlevels) { flog("[Map]: %d points for %d levels: Lomax's map", g_npoints, g_nlevels); g_nfiles = 0; }
    if (g_nfiles) flog("world map: %d files, %d points, clouds from bank %d (%d phases of %d bands, %d a bank), tree top %d pieces",
                       g_nfiles, g_npoints, g_cloudBank, g_cloudPhases, g_cloudBands, g_cloudPerBank, g_npieces);
}

/* the file for the level the map leaves for (g_LastRealSection), NULL: Lomax's map. "From level k
   on" is by the play order (level_rank, levels.c: the detours put 25-28 before 18; level_map_rank: a
   level whose shortcut was taken shows its area's change) */
const char *worldmap_file(void)
{
    int i, best = -1, rank = level_map_rank(*G_LASTREALSECTION);
    for (i = 0; i < g_nfiles; i++)
        if (level_rank(g_files_map[i].from - 1) <= rank &&
            (best < 0 || level_rank(g_files_map[i].from - 1) > level_rank(g_files_map[best].from - 1))) best = i;
    if (best < 0 && g_nfiles) best = 0;
    return best < 0 ? NULL : g_files_map[best].file;
}

/* the map's row of the level table: not rotating (Lomax's map zooms in on a rotating plane: its x
   and y aren't Flink's map's); one plane, as Lomax's */
void worldmap_row(BYTE *row)
{
    if (!g_nfiles) return;
    *(DWORD *)(row + 0) = 1;            /* dwPlaneMask */
    *(DWORD *)(row + 4) = 1;            /* dwActivePlaneMask */
    row[11] = 0;                        /* bRotatingPlanes */
    *(WORD *)(row + 14) = 1;            /* aSubLevelPlaneMask[0] */
}

void worldmap_reset(void) { g_ready = 0; }

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* the camera that centres point p in the view, inside the map */
static void centre(POINT p, int *x, int *y)
{
    BYTE *pl = PLANE(MAP_PLANE);
    *x = clamp(p.x - g_vw / 2, 0, P_W(pl) * 16 - g_vw);
    *y = clamp(p.y - HERO_DY - g_vh / 2, 0, P_H(pl) * 16 - g_vh);
}

static POINT point_of(int level)
{
    POINT none = { 0, 0 };
    if (level >= 0 && level < g_npoints) return g_point[level];
    return g_haveStart ? g_start : none;
}

/* the view and the pan, once the planes are loaded */
static void map_setup(void)
{
    BYTE *pl = PLANE(MAP_PLANE);
    int level = *G_LASTREALSECTION, fx, fy;
    if (g_ready) return;
    g_ready = 1;
    g_vw = P_W(pl) * 16 - P_MAXX(pl);
    if (P_MAXX(pl) <= 0 || g_vw < LOMAX_VIEW_W) g_vw = LOMAX_VIEW_W;
    g_vh = P_H(pl) * 16 - P_MAXY(pl);
    if (P_MAXY(pl) <= 0 || g_vh < VIEW_H || g_vh > 2 * VIEW_H) g_vh = VIEW_H;
    if (g_vw > P_W(pl) * 16) g_vw = P_W(pl) * 16;
    if (g_vh > P_H(pl) * 16) g_vh = P_H(pl) * 16;
    centre(point_of(level), &g_tx, &g_ty);
    centre(point_of(level_map_from() >= 0 ? level_map_from() : level - 1), &fx, &fy);
    g_cx = fx << 16; g_cy = fy << 16; g_velx = g_vely = 0; g_latched = 0;
    g_placed = g_leaving = 0; g_wave = 0;
    g_boxCX = g_vw - BOX_CX_RIGHT;                 /* (until the Map Control's spawn says) */
    place_lomax();
    P_CAMX(pl) = fx; P_CAMY(pl) = fy;
    *G_MAPSCROLLTARGETX = g_tx;
    flog("world map: level %d, view %dx%d, camera (%d,%d) -> (%d,%d)", level + 1, g_vw, g_vh, fx, fy, g_tx, g_ty);
}

/* Map_WorldMap_Update's scroll (0x424250..0x4242EF) on one axis: pos 16.16, target in px */
static void pan(int *pos, int *vel, int target)
{
    int d = (((*pos >> 16) - target) << 16) / 16, v = *vel;
    if (v < d) { v += v < 0 ? 0xC000 : 0x4000; if (v > d) v = d; }
    else if (v > d) { v -= v > 0 ? 0xC000 : 0x4000; if (v < d) v = d; }
    *vel = clamp(v, -MAX_STEP, MAX_STEP);
    *pos -= *vel;
}

/* the Map Control's update on Flink's map: Map_WorldMap_Update with the camera panning in x and y,
   and the button fading out (Lomax doesn't jump) */
static void __cdecl map_update(BYTE *a)
{
    BYTE *pl = PLANE(MAP_PLANE);
    int *timer = (int *)(a + 0xA4), t = *timer, dx, dy;
    WORD in = *G_INPUTPRESSED;
    map_setup();
    g_wave += WAVE_STEP;
    if (t > 0) *timer = --t;                       /* the fade in */
    if (t == 0) { if (!*G_FADESTEP) *G_FADESTEP = 0x40; *timer = -1; }
    *G_BLEVELENDING = 1;
    if (*G_BFRAMEREADY) { *G_CURSUBLEVEL = 0; *G_BSECTIONCHANGE = 1; *G_CURSECTION = *G_LASTREALSECTION; }
    if (*timer < 0) {                              /* (from the fade in on, so that the pan is seen) */
        pan(&g_cx, &g_velx, g_tx);
        pan(&g_cy, &g_vely, g_ty);
    }
    P_CAMX(pl) = g_cx >> 16;
    P_CAMY(pl) = g_cy >> 16;
    *G_MAPSCROLLTARGETX = g_tx;
    *G_PLAYERSTATE |= PS_CAMY_STAYS;
    if (!g_latched) {
        dx = abs(g_tx - P_CAMX(pl)); dy = abs(g_ty - P_CAMY(pl));
        t = (ORBIT_NEAR - (dx > dy ? dx : dy)) * (*G_MAPZOOMBIAS + 120) / 100;
        if (t >= 0) { t = 0; g_latched = 1; }
        *G_CAMERAORBITOFFSET = (short)t;
    }
    if (*G_BFRAMEREADY && !*G_FADESTEP && !g_leaving && g_placed && ((in & BUTTONS) || (in & 0x40))) {
        g_leaving = 1;                             /* (Lomax stays on his tree top: no jump) */
        *G_FADESTEP = -0x40;
        *G_BFADEOUTMUSIC = 1;
    }
}

/* the storm clouds at this tick's phase of the wave and Lomax's tree top (see the top); then the
   password, the Map Control's own draw */
static void __cdecl map_draw(BYTE *a)
{
    const BYTE *f;
    unsigned depth = (unsigned)P_DEPTH(PLANE(MAP_PLANE));
    int n, k;
    if (g_map && g_ready) {
        if (g_cloudBank) {
            k = (g_wave & 1023) * g_cloudPhases / 1024;
            for (n = 0; n < g_cloudBands; n++)
                if ((f = bank_frame(g_cloudBank + k / g_cloudPerBank, k % g_cloudPerBank * g_cloudBands + n + 1)) &&
                    *(const short *)(f + 0xC) > 1)
                    Sprite_QueueScreen(f, (depth + 1) << 24, 0, 0);
        }
        for (n = 0; n < g_npieces; n++)          /* mirrored about the tree top's origin (flag 8) */
            if ((f = bank_frame(g_piece[n].bank, g_piece[n].frame))) {
                int ox = *(const short *)(f + 0x10), w = *(const short *)(f + 0xC);
                Sprite_QueueScreen(f, (depth + 2) << 24 | 8, g_treeX - (g_piece[n].dx + ox + w) - ox,
                                   g_treeY + g_piece[n].dy);
            }
    }
    Map_DrawPasswordGems(a);
}

/* object 41: on Flink's map, the copy Map_WorldMap_Init moved to a high slot gets map_update and map_draw */
void __cdecl mapctl_spawn(BYTE *a, BYTE *def)
{
    int i;
    Map_WorldMap_Init(a, def);
    if (!g_map) return;
    map_setup();
    for (i = 0; i < MAX_ACTORS; i++)
        if (A_UPDATE(actor_at(i)) == (void *)Map_WorldMap_Update) {
            A_UPDATE(actor_at(i)) = (void *)map_update;
            if (A_DRAW(actor_at(i)) == (void *)Map_DrawPasswordGems) A_DRAW(actor_at(i)) = (void *)map_draw;
            g_boxCX = A_X(actor_at(i)) + BOX_HALF;   /* (its screen x, which Lomax+ moves in widescreen) */
            place_lomax();
        }
    *G_PLAYERSTATE |= PS_CAMY_STAYS;
}

/* object 39: the target the hero set is the map's centred one */
void __cdecl hero_spawn(BYTE *a, BYTE *def)
{
    Litt_MapHeroMarker_Init(a, def);
    if (!g_map) return;
    map_setup();
    *G_MAPSCROLLTARGETX = g_tx;
}

/* Player_Update on the map: Lomax at the view's bottom right, facing left, input locked; he, his clip
   line and the camera are moved up so that the map's fixed floor (y 122) is under his feet for the
   call, and back after it. */
static int g_shiftOn, g_shifted, g_savedCamY;

static void shift_player(int dy)
{
    *G_PLAYERY += dy;
    *G_PLAYERYFIXED += dy << 16;
    if (abs(A_CLIPTOP(G_PLAYERACTOR)) < 10000) A_CLIPTOP(G_PLAYERACTOR) = (short)(A_CLIPTOP(G_PLAYERACTOR) + dy);
}

void worldmap_before(void)
{
    BYTE *pl = PLANE(MAP_PLANE);
    int x, feet;
    g_shiftOn = 0;
    if (!g_map || !g_ready) return;
    g_shiftOn = 1;
    x = P_CAMX(pl) + g_lomaxX;
    feet = P_CAMY(pl) + g_lomaxFeet;
    if (!g_placed) {                             /* (no trapdoor: he is there from the start) */
        g_placed = 1;
        *G_PLAYERY = feet;
        *G_PLAYERYFIXED = feet << 16;
        *G_PLAYERVY = 0;
        *G_PLAYERSTATE &= ~(DWORD)(PS_AIR | PS_ROPE);
        A_CLIPTOP(G_PLAYERACTOR) = CLIP_NONE;
        A_PLANE(G_PLAYERACTOR) = MAP_PLANE;
    }
    *G_PLAYERX = x;
    *G_PLAYERXFIXED = x << 16;
    *G_PLAYERVX = 0;
    *G_PLAYERFLAGS |= AF_LEFT;
    *G_BINPUTLOCKED = 1;
    g_shifted = feet - MAP_FLOOR;
    shift_player(-g_shifted);
    g_savedCamY = P_CAMY(pl);
    P_CAMY(pl) -= g_shifted;
}

void worldmap_after(void)
{
    if (!g_shiftOn) return;
    shift_player(g_shifted);
    P_CAMY(PLANE(MAP_PLANE)) = g_savedCamY;
    *G_PLAYERFLAGS |= AF_LEFT;
    g_shiftOn = 0;
}
