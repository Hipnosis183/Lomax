/*
 * scenes.c - Flink's intro and ending (README "Intro and ending"), without Flink.
 *
 * Flink. Game_ColdStart (0x1448A) shows MAIN.WLD map 1 (0x199E0: the Psygnosis logo, whose Presents
 * runs it), then Game_RunIntro (0x1511E) loads INTRO.WLD's maps one scene at a time from the table at
 * 0x151DC: {8, 0} (space), {3, 2} (the fireball through the clouds), {5, 4} (the impact by the log),
 * {7, 6} (the dark sky), {9, 10} (the pond and the village); then the title. The world map's
 * MapControl (0x2D774), with the last level's bit set, starts Game_RunExtro (0x151F0), EXTRO.WLD's first
 * world from 0x152AE: {1, 2} (the demon's corpse thrown into the sky), {3, 0} (the sea, the wizard,
 * CONGRATULATIONS). Each scene is run by one of its objects (below), which counts its length, fades
 * out (0x338D4) and loads the next (0x14518); during the intro START (0x870B bit 7) clears the intro's
 * flag (0x9E08) and fades the music (CdAudio_StartFade 0x356BC), so the title comes next. Flink's hero
 * is moved off the screen (y -50) and is the camera's target: the camera follows him (0x1A040: toward
 * his x - 160, y - 150, speed distance / 16, changed by at most 0.25 px a frame, 0.75 when turning).
 * Timings are NTSC's ($813D set), one Flink frame a Lomax tick.
 *
 * Here: the converter (FLINK.EXE, scenes.c) writes SCENE0.WLD (the Psygnosis logo) .. SCENE5.WLD (the
 * intro) and SCENE6.WLD .. SCENE7.WLD (the ending), LEVELS.INI [Scenes]. They play as the level table's
 * row SECTION_SCENE (levels.c), one sub-level whose file is the scene's (scene_file, from world_load).
 * A scene goes on to the next as an Exit does (exits.c): g_bLevelExitReached, g_CurSubLevel -1 (Level_Start
 * adds 1) and the fade-out; Game_SetupNextLevel doesn't run, so its CD_Stop doesn't cut the music,
 * which goes on across the scenes as Flink's. The intro starts where the title would load (sub-level 1
 * of the title's row; Lomax+'s language menu, sub-level 0, comes first): at start-up and after the
 * ending. Its end, or any button, goes to the title (g_bGoToTitle, the music fading). The ending starts
 * where the world map would show after the last level (scene_ending, from setup_next_level).
 *
 * Each file holds one 'Spik Scene' object (scene_spawn): its update runs the scene (scene_update) and its
 * draw draws every sprite of the scene's list (g_spr) on the sprite's plane and layer, at its scale.
 * Each sprite is drawn through a stand-in actor of its own (a copy of the controller's struct, not in
 * the game's slots, as villageboss.c's), and a new one when it starts afresh somewhere else (a new
 * sprite, an asteroid or star coming back, a spark's trail put back): Lomax+'s FPS Override moves what
 * each Actor_Draw queued between ticks by the motion of that call's point, pairing the calls of two
 * ticks by actor and by the n-th call of that actor, so through one actor a sprite hidden, freed or
 * added would pair every later call with another sprite's, and the in-between frames would throw them
 * between those positions. The sprites' records are Flink's (tags of their own: the converter's
 * SCENE_SPRITES). Lomax isn't drawn (scene_hides_player, player_draw) and Player_Update doesn't run
 * (grab.c's player_tick returns at once).
 *
 * The planes (the converter's): Flink's priority tiles, the play plane (Flink's screen wide, 320 px,
 * empty: the sprites' and the objects'), the map's other tiles, the background. A view wider than 320
 * (Lomax+'s widescreen) shows the play plane centred (scroll = nMaxScrollX / 2, as Lomax+'s camera
 * puts any map narrower than the view), and Lomax+'s narrowed view covers what is beside it with
 * black bars, as on 5-1: Flink's 320 px and nothing more. The cameras are set here: the map's planes
 * follow Flink's camera with Flink's screen in the middle of the view (x - (view - 320) / 2, y - (view
 * - 224) / 2); a map lower than 240 has the converter's padding (Pad) above it. A sprite on the map is
 * drawn on the play plane at its place on the map's planes (view_x, view_y).
 *
 * Without Flink: his Flink (the hero on the log, 0x1F00A) and Head (his head at the pond, 0x2077C) run
 * their scenes; here their timing and camera are kept and nothing of him is drawn (nor his splash,
 * SPLA). Held (the ending's hero, 0x1E44E) is drawn nowhere; the CONGRATULATIONS letters he lets go
 * are. The dark sky's background (INTRO map 6) is Flink in the pond, painted in its tiles: the converter
 * paints him out pixel by pixel (SCENE_PAINT, scene_inpaint). Each scene's objects, in its function below.
 */
#include "flink.h"

#define SCENES             8
#define SCENE_ENDING       6             /* SCENE6..7 */
#define TITLE_SUB          1             /* the title's sub-level (title.c) */
#define SCREEN_W            320
#define SCREEN_H            224
#define VIEW_H             240
#define MAX_SPR            192
#define BAR_H              32            /* BLAK's height (the converter's: 512 x 32) */
#define CINE_H             24            /* Cinemascope's bars (0x1F590: its sprites at lines 0 and 200) */
#define FLASH              112           /* the lightning's flash: white at this alpha, */
#define FLASH_STEP         16            /*   this much less a tick */
#define SKIP_FADE          (-0x40)
#define TRACK_INTRO        116           /* Flink's music index 19 (0x1F7D0) -> disc track 17: TRACK117 */
#define TRACK_ENDING       117           /* index 20 (0x1EB6E) -> disc track 18: TRACK118 */
#define G_BPAUSEDISABLED   ((volatile int *)0x0059C74Cu)   /* g_bPauseDisabled: no pause menu */
#define G_CURPLANEMASK     ((volatile DWORD *)0x0059E77Cu) /* Level_LoadWorld's Level_ComputePlaneMask */

int g_scene = -1;                        /* the scene loaded, -1: none */
static int g_have;                       /* [Scenes] read and the files are there */
static int g_nintro;                     /* the intro's scenes (SCENE_ENDING when all are there) */
static char g_file[SCENES][MAX_PATH];
static int g_pad[SCENES];
static int g_eyesX, g_eyesY;             /* the villain's eyes' top-left (EYES) */
static int g_next;                       /* the scene the next load of the row opens */
static int g_introPending = 1;           /* the intro plays where the title loads next */

/* ---- the sprites ---------------------------------------------------------------------------------- */

enum { PL_FG = -1, PL_PLAY = 0, PL_MAP = 1, PL_BG = 2 };   /* from the play plane: priority tiles, play
                                                             plane, the map's tiles, background */
typedef struct Spr Spr;
typedef void (*SprFn)(Spr *);
struct Spr {
    SprFn tick;                          /* NULL: free */
    DWORD tag;
    int nframes, speed;                  /* the record's (Actor_SetAnim) */
    int x, y, vx, vy;                    /* 16.16, Flink's: the map's, or the screen's with `screen` */
    int screen, plane, list;             /* list: Flink's draw list (FLINK_LAYER) */
    int anim, once, hold;                /* 8.8 frame; plays once (and holds its last); not animated */
    int scale, alpha, hide, mirror;      /* per mille; 0 opaque */
    int t, a, b, c, d, e, f;             /* its own */
    int st;                              /* its stand-in (g_st), -1: none yet */
    int tl;                              /* a bitmap Flink scales: x, y its box's top-left (spr_draw) */
};
static Spr g_spr[MAX_SPR];

static BYTE *g_ctl;                      /* the 'Spik Scene' actor */
static BYTE *g_ctlType;
static int g_tick, g_ending, g_leaving, g_started;
static unsigned g_rand = 0x1F2E3D4Cu;
static int g_quake;
static int g_flash;                      /* the white flash's alpha */

/* the cameras: Flink's (16.16, its screen's top-left on the map) and the background's scroll */
static int g_camX, g_camY, g_camVX, g_camVY, g_heroX, g_heroY, g_maxY;
static int g_bgX, g_bgY;
static int g_vw, g_vh;                   /* Lomax's view */
static int g_lcx, g_lcy, g_mcx, g_mcy, g_bcx, g_bcy;   /* this tick's cameras: play plane, map, background */

/* the stand-ins: a pool, a new sprite taking one that wasn't drawn in the last two ticks */
#define MAX_ST             (MAX_SPR + 70 + 32)
static BYTE g_st[MAX_ST][ACTOR_STRIDE];
static int g_stOwned[MAX_ST], g_stDrawn[MAX_ST], g_stNext, g_draws = 10;

static int st_new(void)
{
    int k, i = -1, any = -1;
    BYTE *a;
    if (!g_ctl) return -1;
    for (k = 0; k < MAX_ST; k++) {
        int j = (g_stNext + k) % MAX_ST;
        if (g_stOwned[j]) continue;
        if (any < 0) any = j;
        if (g_stDrawn[j] < g_draws - 1) { i = j; break; }
    }
    if (i < 0 && (i = any) < 0) return -1;
    g_stNext = i + 1;
    g_stOwned[i] = 1;
    a = g_st[i];
    memcpy(a, g_ctl, ACTOR_STRIDE);
    A_FLAGS(a) &= ~AF_HIDDEN;
    A_HIDE(a) = 0;
    *(int *)(a + 0x78) = 0;                      /* (Actor_Draw's blink counter) */
    return i;
}

static void st_free(int *i) { if (*i >= 0 && *i < MAX_ST) g_stOwned[*i] = 0; *i = -1; }

static int rnd(void) { g_rand = g_rand * 1103515245u + 12345u; return (int)(g_rand >> 16) & 0x7FFF; }

static Spr *spr_new(SprFn tick, const char *tag, int x, int y, int plane, int list)
{
    int i;
    for (i = 0; i < MAX_SPR; i++)
        if (!g_spr[i].tick) {
            Spr *s = &g_spr[i];
            memset(s, 0, sizeof(*s));
            s->tick = tick;
            s->tag = FOURCC(tag);
            s->x = x; s->y = y;
            s->plane = plane; s->list = list;
            s->scale = 1000;
            s->st = -1;
            if (g_ctl) {                              /* the record's frames and speed */
                Actor_SetAnim(g_ctl, s->tag);
                s->nframes = A_NFRAMES(g_ctl) > 0 ? A_NFRAMES(g_ctl) : 1;
                s->speed = *(short *)(g_ctl + 0x68);
            }
            return s;
        }
    return NULL;
}

static void spr_tag(Spr *s, const char *tag)
{
    s->tag = FOURCC(tag);
    s->anim = 0;
    if (g_ctl) {
        Actor_SetAnim(g_ctl, s->tag);
        s->nframes = A_NFRAMES(g_ctl) > 0 ? A_NFRAMES(g_ctl) : 1;
        s->speed = *(short *)(g_ctl + 0x68);
    }
}

/* Flink's Actor_Animate (0x337EA): the 8.8 frame + the speed; at the end it loops, or with `once`
   holds its last frame; 1 when a once-played record has ended */
static int spr_animate(Spr *s)
{
    if (s->hold) return 0;
    s->anim += s->speed;
    if (s->anim >> 8 >= s->nframes) {
        if (s->once) { s->anim = (s->nframes - 1) << 8; return 1; }
        s->anim -= s->nframes << 8;
    }
    return 0;
}

static void spr_free(Spr *s) { s->tick = NULL; st_free(&s->st); }

/* the sprite starts afresh elsewhere: a new stand-in, so no in-between frame slides it there */
static void spr_jump(Spr *s) { st_free(&s->st); }

/* where a sprite is in Lomax's view: on Flink's screen (in the view's middle), or on the map by its
   camera; and the camera of the plane it is drawn on */
static int view_x(const Spr *s) { return s->screen ? (g_vw - SCREEN_W) / 2 + (s->x >> 16) : (s->x >> 16) - g_mcx; }
static int view_y(const Spr *s) { return s->screen ? (g_vh - SCREEN_H) / 2 + (s->y >> 16) : (s->y >> 16) + g_pad[g_scene] - g_mcy; }
static int cam_x(int pl) { return pl == PL_PLAY ? g_lcx : pl == PL_BG ? g_bcx : g_mcx; }
static int cam_y(int pl) { return pl == PL_PLAY ? g_lcy : pl == PL_BG ? g_bcy : g_mcy; }

/* ---- the scenes' files, the row, the sequence --------------------------------------------------- */

void scenes_load(const char *ini, const char *rel, const char *dir)
{
    char list[1024], path[MAX_PATH], *tok;
    int n = 0, k;
    g_have = 0;
    GetPrivateProfileStringA("Scenes", "Intro", "", list, sizeof(list), ini);
    for (tok = strtok(list, " ,\t"); tok && n < SCENE_ENDING; tok = strtok(NULL, " ,\t"), n++)
        snprintf(g_file[n], MAX_PATH, "%s", tok);
    if (n != SCENE_ENDING) { flog("[Scenes] Intro: %d files (%d): no intro or ending", n, SCENE_ENDING); return; }
    GetPrivateProfileStringA("Scenes", "Ending", "", list, sizeof(list), ini);
    for (tok = strtok(list, " ,\t"); tok && n < SCENES; tok = strtok(NULL, " ,\t"), n++)
        snprintf(g_file[n], MAX_PATH, "%s", tok);
    if (n != SCENES) { flog("[Scenes] Ending: %d files (%d): no intro or ending", n - SCENE_ENDING, SCENES - SCENE_ENDING); return; }
    GetPrivateProfileStringA("Scenes", "Pad", "", list, sizeof(list), ini);
    for (k = 0, tok = strtok(list, " ,\t"); k < SCENES; k++, tok = tok ? strtok(NULL, " ,\t") : NULL)
        g_pad[k] = tok ? atoi(tok) : 0;
    GetPrivateProfileStringA("Scenes", "Eyes", "", list, sizeof(list), ini);
    if (sscanf(list, "%d,%d", &g_eyesX, &g_eyesY) != 2) g_eyesX = g_eyesY = 0;
    for (k = 0; k < SCENES; k++) {
        snprintf(path, sizeof(path), "%s\\%s", dir, g_file[k]);
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) { flog("scene %s is missing: no intro or ending", path); return; }
        snprintf(path, sizeof(path), "%s\\%s", rel, g_file[k]);
        snprintf(g_file[k], MAX_PATH, "%s", path);
    }
    g_have = 1;
    g_nintro = SCENE_ENDING;
    flog("intro: %d scenes, ending: %d (%s ...)", SCENE_ENDING, SCENES - SCENE_ENDING, g_file[0]);
}

/* the scenes' row (a copy of the title's): one sub-level, the files' 3 planes */
void scene_row(BYTE *row)
{
    *(DWORD *)(row + 0) = SCENE_PLANES;
    *(DWORD *)(row + 4) = SCENE_PLANES;
    *(WORD *)(row + 12) = 1;
    memset(row + 14, 0, 10);
    *(WORD *)(row + 14) = SCENE_PLANES;
}

/* (world_load) the scene file for the section being loaded, or NULL. The title's sub-level 1 with the
   intro pending becomes the intro's first scene (the section is the scenes' then). */
const char *scene_file(void)
{
    g_scene = -1;
    if (!g_have) return NULL;
    if (*G_LOADEDSECTION == SECTION_TITLE && *G_CURSUBLEVEL == TITLE_SUB && g_introPending) {
        *G_CURSECTION = *G_LOADEDSECTION = SECTION_SCENE;
        *G_CURSUBLEVEL = 0;
        *G_CURPLANEMASK = SCENE_PLANES;
        g_next = 0;
        flog("the title's load: the intro first");
    } else if (*G_LOADEDSECTION != SECTION_SCENE) return NULL;
    if (g_next < 0 || g_next >= SCENES) g_next = 0;
    g_introPending = 0;
    g_scene = g_next;
    flog("scene %d: %s", g_scene, g_file[g_scene]);
    return g_file[g_scene];
}

int scene_on(void) { return g_scene >= 0; }

/* (setup_next_level) the game is finished, on its way to the map: the ending instead */
int scene_ending(void)
{
    if (!g_have) return 0;
    *G_BGOTOMAP = 0;
    *G_CURSECTION = SECTION_SCENE;
    *G_CURSUBLEVEL = 0;
    g_next = SCENE_ENDING;
    flog("the game is finished: the ending");
    return 1;
}

static void next_scene(int n)
{
    if (g_leaving) return;
    g_leaving = 1;
    g_next = n;
    *G_BLEVELEXITREACHED = 1;
    *G_CURSUBLEVEL = -1;                         /* (Level_Start adds 1) */
    flog("scene %d done: scene %d next", g_scene, n);
}

/* to the title: the intro's end or a button (and, after the ending, the intro again first) */
static void to_title(const char *why)
{
    if (g_leaving) return;
    g_leaving = 1;
    if (g_ending) g_introPending = 1;
    *G_BGOTOTITLE = 1;
    *G_BSECTIONCHANGE = 1;
    *G_BFADEOUTMUSIC = 1;
    flog("scene %d: %s, to the title%s", g_scene, why, g_ending ? " (the intro first)" : "");
}

/* ---- the cameras ------------------------------------------------------------------------------- */

/* Flink's camera (0x1A040) on one axis: toward target, speed distance / 16, changed by at most 0.25 px a
   frame (0.75 when turning) */
static void follow(int *pos, int *vel, int target, int max)
{
    int d = target - (*pos >> 16), want, v = *vel;
    if (!d) { *vel = 0; }
    else {
        want = (d << 16) >> 4;
        if (v < want) { if (want < 0) v += 0x8000; v += 0x4000; if (v > want) v = want; }
        else if (v > want) { if (want >= 0) v -= 0x8000; v -= 0x4000; if (v < want) v = want; }
        *vel = v;
        *pos += v;
    }
    if (*pos < 0) { *pos = 0; *vel = 0; }
    if (*pos > max << 16) { *pos = max << 16; *vel = 0; }
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int wrap(int v, int n) { return n > 0 ? ((v % n) + n) % n : v; }

static const signed char QUAKE[18] = { 0, 0, -1, 1, -2, 2, -3, 3, -2, 2, -3, 3, -4, 4, -3, 3, -4, 4 };  /* 0x19F84 */

/* the plane `off` from the play plane, NULL: none */
static BYTE *plane_at(int off)
{
    int ip;
    if (!*G_PCURPLANE) return NULL;
    ip = (int)(*G_PCURPLANE - G_PLANES) / PLANE_STRIDE + off;
    return ip >= 0 && ip < 16 && P_W(PLANE(ip)) > 0 ? PLANE(ip) : NULL;
}

/* every plane's camera: the play plane centred, the map's two from Flink's, the background's scroll */
static void set_cameras(void)
{
    BYTE *pl = *G_PCURPLANE, *mp = plane_at(PL_MAP), *fg = plane_at(PL_FG), *bg = plane_at(PL_BG);
    int H, cy, q = 0;
    if (!pl || !mp) return;
    g_vw = P_W(pl) * 16 - P_MAXX(pl);                /* (nMaxScrollX is negative on a plane narrower */
    if (g_vw < SCREEN_W) g_vw = SCREEN_W;            /*   than the view: Lomax+'s widescreen) */
    g_vh = P_H(pl) * 16 - P_MAXY(pl);
    if (g_vh < VIEW_H) g_vh = VIEW_H;
    H = P_H(mp) * 16;
    if (g_quake > 0) { q = QUAKE[g_quake >> 1 < 17 ? g_quake >> 1 : 17]; g_quake--; }
    g_mcx = (g_camX >> 16) - (g_vw - SCREEN_W) / 2;
    cy = (g_camY >> 16) + g_pad[g_scene] - (g_vh - SCREEN_H) / 2;
    g_mcy = (H < g_vh ? (H - g_vh) / 2 : clampi(cy, 0, H - g_vh)) + q;
    g_lcx = (P_W(pl) * 16 - g_vw) / 2;               /* (Lomax+'s narrowed view: nMaxScrollX / 2) */
    g_lcy = g_mcy;
    P_CAMX(pl) = g_lcx; P_CAMY(pl) = g_lcy;
    P_CAMX(mp) = g_mcx; P_CAMY(mp) = g_mcy;
    if (fg) { P_CAMX(fg) = g_mcx; P_CAMY(fg) = g_mcy; }
    if (bg) {
        g_bcx = P_CAMX(bg) = wrap((g_bgX >> 16) - (g_vw - SCREEN_W) / 2, P_W(bg) * 16);
        g_bcy = P_CAMY(bg) = wrap((g_bgY >> 16) - (g_vh - SCREEN_H) / 2, P_H(bg) * 16);
    }
}

/* the camera following Flink's hero (x - 160, y - 150), inside the map (its height maxH: Flink's) */
static void camera_follow(void)
{
    BYTE *pl = plane_at(PL_MAP);
    int maxX = pl ? P_W(pl) * 16 - SCREEN_W : 0;
    if (maxX < 0) maxX = 0;
    follow(&g_camX, &g_camVX, (g_heroX >> 16) - 160, maxX);
    follow(&g_camY, &g_camVY, (g_heroY >> 16) - 150, g_maxY > 0 ? g_maxY : 0);
}

static void camera_jump(void)
{
    BYTE *pl = plane_at(PL_MAP);
    int maxX = pl ? P_W(pl) * 16 - SCREEN_W : 0;
    g_camX = clampi((g_heroX >> 16) - 160, 0, maxX > 0 ? maxX : 0) << 16;
    g_camY = clampi((g_heroY >> 16) - 150, 0, g_maxY > 0 ? g_maxY : 0) << 16;
    g_camVX = g_camVY = 0;
}

/* ---- scene 0: the Psygnosis logo (MAIN.WLD map 1) ---------------------------------------------- */

/* Presents (0x25916): 100 frames; at 90 sound 0x1A, at 65 sound 0x35; at 0 the fade, then the intro.
   Its draw (0x25A3A) shows it while the timer - 60 is 31..0 (later: 0), the k-th of 32 bitmaps scaled to
   100 - 3k per cent (0x1DAD4), each centred in the full one's box (0x1DEEA), the box's top-left
   falling from y - 60 + 0.875 at 4.0 a frame, 0x2000 less each frame it is drawn. */
static void presents_tick(Spr *s)
{
    int k;
    if (s->t == 90) flink_sound(g_ctl, 0x1A);
    if (s->t == 65) flink_sound(g_ctl, 0x35);
    if (s->t > 0 && --s->t == 0) next_scene(1);
    k = s->t - 60;
    s->hide = k > 31;
    if (s->hide) return;
    if (k < 0) k = 0;
    s->scale = (100 - 3 * k) * 10;
    s->y += s->vy;
    if (s->vy) s->vy -= 0x2000;
}

static void scene0_start(void)
{
    Spr *s = spr_new(presents_tick, "PRES", 84 << 16, ((144 - 60) << 16) + 0xE000, PL_PLAY, 3);
    if (s) { s->t = 100; s->vy = 0x40000; s->hold = 1; s->hide = 1; s->tl = 1; }
}

/* ---- scene 1: space (INTRO map 8: the villain's portrait, map 1) ------------------------------- */

/* Stars (0x1F5BC): 2220 frames. 70 stars (0xBB5B2, 0x1FA36) fly at the viewer: x, y random
   +-(1000 .. 17256) (0x1FA56), depth 300, the depth 2.0 less a frame (0x9E0C, after 31 frames at 64.0
   at the start); on the screen at (x / depth + 160, y / depth + 88) of a 320 x 176 bitmap 24 lines
   down (0x1F756), a pixel of the shade of depth / 64 (0x1FB24: line 3 colours 10-13, none from 256 on);
   back at depth 300 once outside or past it. The camera (0x1F94C) eases (0x1F9B6: toward the target at
   a 32nd of the distance, its speed changed by at most 0.25 a frame, 0.75 when turning) through
   0x1F96A's points: (732, 0) for 120 frames, (420, 300) for 360, (0, 0). The palette level of the
   eyes' colours (0x1F894, main command 0x94) is 0 (black) but frames 422..450, 7 - |n - 14| / 2. Once
   the island comes ($86A5) the camera and the level stop. */
#define STARS 70
static struct { int x, y, z, st; } g_star[STARS];   /* st: its stand-in */
static int g_starSpeed, g_isle, g_wp, g_wpT, g_level;
static const short WAYPOINTS[4][3] = { { 120, 732, 0 }, { 360, 420, 300 }, { 120, 0, 0 }, { 6000, 0, 0 } };   /* 0x1F96A */

static int star_rand(void)                     /* 0x1FA56 */
{
    int v = (int)(signed char)(rnd() & 0xFF) << 7;
    return v < 0 ? v - 1000 : v + 1000;
}

static void stars_move(void)                   /* 0x1FA36 */
{
    int i, guard;
    for (i = 0; i < STARS; i++) {
        g_star[i].z -= g_starSpeed;
        for (guard = 0; guard < 8; guard++) {
            int z = g_star[i].z >> 16, sx, sy;
            if (z > 0) {
                sx = g_star[i].x / z + 160; sy = g_star[i].y / z + 88;
                if (sx >= 0 && sx < 320 && sy >= 0 && sy < 176) break;
            }
            g_star[i].x = star_rand(); g_star[i].y = star_rand(); g_star[i].z = 300 << 16;
            st_free(&g_star[i].st);
        }
    }
}

static int ease(int pos, int *vel, int target)  /* 0x1F9B6 */
{
    int want = ((target - (pos >> 16)) << 16) >> 5, v = *vel;
    if (v < want) { if (want < 0) v += 0x8000; v += 0x4000; if (v > want) v = want; }
    else if (v > want) { if (want >= 0) v -= 0x8000; v -= 0x4000; if (v < want) v = want; }
    *vel = v;
    return pos + v;
}

/* Asteroid (0x1FEB8): ten, from frame 600, 30 apart, BIG and SMAL in turn; as the stars, at (x / depth
   + 160, y / depth + 112), the bitmap of size depth / 19 (100 - 6k per cent) centred in its box, whose top-left is 16 px up
   and left (0x20098), on list (0x2008C) 3, 2, 1 for depth < 60, 120, 180, else 0; back at depth 300 once
   off the screen. */
static void asteroid_tick(Spr *s)
{
    int z = 300, sx = 0, sy = 0, k, guard;
    /* Flink's lists by depth { 3, 2, 1, 0, 0, 0 }, the nearest kept at 1: behind the island, the demon
       and the fart (list 0), which the asteroids, queued first, would cover there */
    static const int LIST[6] = { 3, 2, 1, 1, 1, 1 };
    if (s->t > 0) { s->t--; s->hide = 1; return; }
    s->c -= g_starSpeed;
    for (guard = 0; guard < 8; guard++) {
        z = s->c >> 16;
        if (z > 0) {
            sx = s->a / z + 160; sy = s->b / z + 112;
            if (sx > -48 && sx < 336 && sy > -48 && sy < 240) break;
        }
        s->a = star_rand(); s->b = star_rand(); s->c = 300 << 16;
        spr_jump(s);
    }
    s->hide = 0;
    s->x = (sx - 16) << 16; s->y = (sy - 16) << 16;
    k = z / 19; if (k > 15) k = 15;
    s->scale = (100 - 6 * k) * 10;
    s->list = LIST[z / 60 < 6 ? z / 60 : 5];
}

/* Isle (0x200DA): at frame 1080 it stops the camera ($86A5) and grows for 256 frames, from bitmap 31 to 0
   (a frame 0.125: 80 - 2.5k per cent), centred in the full bitmap's box at the screen's (60 + k, 60); the stars slow by
   1/256 a frame meanwhile (2.0 -> 1.0). */
static void isle_tick(Spr *s)
{
    int k;
    if (s->t > 0) { if (--s->t == 0) g_isle = 1; s->hide = 1; return; }
    s->hide = 0;
    if (s->a >= 0) { s->a -= 0x2000; if (s->a >= 0) g_starSpeed -= 0x100; }
    k = s->a < 0 ? 0 : s->a >> 16;
    s->x = (60 + k) << 16; s->y = 60 << 16;
    s->scale = (800 - 25 * k);
}

/* Asshole (0x202A6), the demon: from frame 1440, x 340 -> 240 a px a frame, y 140 bobbing 0.25 a frame
   (+ when the frame counter's bit 5 is clear, -). Its bitmaps (EINS, then DREI's frames, at 94 per cent)
   each centred in its full box (0x1DE66), the box at (x - 50, y - 80) + 0x2059E's offsets, in turn (0x203EA, NTSC): 0 for 215 frames,
   1 for 216 (ZWEI's selector-1 pieces, the hand, drawn too after 72), 2 .. 6 for 4 each, 7 for 1; then
   (0x205F8) the fart flies off: vx -0.5, vy -0.25, both + 0x40 a frame, shrinking from 100 per cent by
   0.25 a frame to 10, the next of Fart's 8 HEAD frames every 3, centred in the full box at (x - 20, y - 50). The
   hand (ZWEI's pieces, 0x328C4) is drawn before the bitmap, in front of it. */
static const short DEM_SEQ[][2] = { { 0, 215 }, { 1, 72 }, { 1, 144 }, { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 },
                                    { 6, 4 }, { 7, 1 }, { -7, 32767 } };
static const short DEM_OFF[8][2] = { { 0, 0 }, { -10, 0 }, { -10, -6 }, { -10, -11 }, { -11, -14 }, { 5, 2 }, { 15, 13 }, { 22, 24 } };

/* ZWEI's pieces are drawn through Flink's sprite camera (0x328C4: x - $8750, y - $8752), the bitmaps at
   fixed sprite coordinates; on Flink's screen the pieces stand 5 px higher on the body than (x, y) gives
   (measured on Flink's frame against the island and the body, which match it) */
#define HAND_UP 5
static void hand_tick(Spr *s) { (void)s; }

static void demon_tick(Spr *s)
{
    Spr *hand = NULL;
    int i;
    if (s->t > 0) { s->t--; s->hide = 1; return; }
    s->hide = 0;
    if (s->d >= 0) {                                  /* the demon */
        if (--s->e < 0) { s->d++; s->e = DEM_SEQ[s->d][1]; }
        if (DEM_SEQ[s->d][0] < 0) {                   /* the fart flies off */
            s->d = -1;
            s->vx = -0x8000; s->vy = -0x4000; s->f = 100 << 16; s->e = 0;
            s->a = (s->a >> 16) << 16; s->b = (s->b >> 16) << 16;
            spr_tag(s, "FART"); s->hold = 1; s->list = 0;
            spr_jump(s);
        } else {
            int k = DEM_SEQ[s->d][0];
            if ((s->a >> 16) != 240) s->a -= 0x10000;
            s->b += (g_tick & 0x20) ? -0x4000 : 0x4000;
            if (s->tag != FOURCC(k ? "DEM3" : "DEM1")) spr_tag(s, k ? "DEM3" : "DEM1");
            if (s->c != k + 1) { if (s->c) spr_jump(s); s->c = k + 1; }   /* a new pose: its own box, */
            s->hold = 1;                                                    /*   switched, not slid to */
            s->anim = (k == 0 ? 0 : (k - 1 > 6 ? 6 : k - 1)) << 8;
            s->x = s->a + ((DEM_OFF[k][0] - 50) << 16); s->y = s->b + ((DEM_OFF[k][1] - 80) << 16);
            s->scale = 940;
            for (i = 0; i < MAX_SPR; i++)          /* ZWEI's hand, 72 frames into index 1 */
                if (g_spr[i].tick == hand_tick) { hand = &g_spr[i]; break; }
            if (hand) {
                hand->hide = !(k == 1 && s->d == 2);
                hand->x = s->a; hand->y = s->b - (HAND_UP << 16);
                spr_animate(hand);
            }
        }
        return;
    }
    for (i = 0; i < MAX_SPR; i++) if (g_spr[i].tick == hand_tick) g_spr[i].hide = 1;
    s->vx += 0x40; s->a += s->vx;                    /* 0x205F8 */
    s->vy += 0x40; s->b += s->vy;
    s->f -= 0x4000; if (s->f < 10 << 16) s->f = 10 << 16;
    if (++s->e >= 3) { s->e = 0; s->anim = (((s->anim >> 8) + 1) & 7) << 8; }
    s->x = s->a - (20 << 16); s->y = s->b - (50 << 16);
    s->scale = (s->f >> 16) * 10;
}

static void star_tick(Spr *s) { (void)s; }      /* (drawn by stars_draw) */
static void eyes_tick(Spr *s) { s->hide = !g_level; if (g_level) s->anim = (g_level - 1) << 8; }   /* EYES: a frame a level */

static void scene1_start(void)
{
    Spr *s;
    int i;
    g_starSpeed = 64 << 16;
    for (i = 0; i < STARS; i++) { g_star[i].x = star_rand(); g_star[i].y = star_rand(); g_star[i].z = 300 << 16; g_star[i].st = -1; }
    for (i = 0; i < 31; i++) stars_move();
    g_starSpeed = 2 << 16;
    g_isle = 0; g_wp = -1; g_wpT = 0; g_level = 0;
    g_camX = 732 << 16; g_camY = 0; g_camVX = g_camVY = 0;
    spr_new(star_tick, "STAR", 0, 0, PL_BG, 3);
    if ((s = spr_new(eyes_tick, "EYES", g_eyesX << 16, g_eyesY << 16, PL_PLAY, 3))) { s->hold = 1; s->hide = 1; }
    for (i = 0; i < 10; i++)
        if ((s = spr_new(asteroid_tick, (9 - i) & 1 ? "ASML" : "ABIG", 0, 0, PL_PLAY, 3))) {
            s->screen = 1; s->hold = 1; s->hide = 1; s->tl = 1; s->t = 600 + 30 * i; s->c = 300 << 16;
            s->a = star_rand(); s->b = star_rand();
        }
    /* in Flink's list 0 the demon (and the fart) is in front of the island, and its ZWEI pieces
       (0x328C4) in front of its bitmap (0x2052C); the first queued is drawn in front */
    if ((s = spr_new(hand_tick, "DEM2", 0, 0, PL_PLAY, 0))) { s->screen = 1; s->hide = 1; }
    if ((s = spr_new(demon_tick, "DEM1", 0, 0, PL_PLAY, 0))) {
        s->screen = 1; s->hold = 1; s->hide = 1; s->tl = 1; s->t = 1440; s->d = 0; s->e = DEM_SEQ[0][1];
        s->a = 340 << 16; s->b = 140 << 16;
    }
    if ((s = spr_new(isle_tick, "ISLE", 0, 0, PL_PLAY, 0))) { s->screen = 1; s->hold = 1; s->hide = 1; s->tl = 1; s->t = 1080; s->a = 32 << 16; }
    music_play(TRACK_INTRO);
}

static void scene1_tick(void)
{
    stars_move();
    if (!g_isle) {
        int d1;
        if (--g_wpT <= 0 && g_wp < 3) { g_wp++; g_wpT = WAYPOINTS[g_wp][0]; }
        g_camX = ease(g_camX, &g_camVX, WAYPOINTS[g_wp][1]);
        g_camY = ease(g_camY, &g_camVY, WAYPOINTS[g_wp][2]);
        d1 = 450 - g_tick;                             /* ($62: 450 down) */
        g_level = d1 >= 0 && d1 < 28 ? 7 - (abs(d1 - 14) >> 1) : 0;
    }
    if (g_tick >= 2220) next_scene(2);
}

/* the stars: a 1 px sprite each, its shade, on the background plane, each through its stand-in */
static void stars_draw(int plane, int layer)
{
    int i;
    for (i = 0; i < STARS; i++) {
        int z = g_star[i].z >> 16, sx, sy;
        BYTE *a;
        if (z <= 0 || z >= 256) continue;
        if (g_star[i].st < 0 && (g_star[i].st = st_new()) < 0) continue;
        a = g_st[g_star[i].st];
        g_stDrawn[g_star[i].st] = g_draws;
        sx = g_star[i].x / z + 160; sy = g_star[i].y / z + 88 + 24;
        A_PLANE(a) = (short)plane;
        A_LAYER(a) = (short)layer;
        Actor_SetAnimIfChanged(a, FOURCC("STAR"));
        hold_frame(a, z >> 6);
        Actor_AdvanceAnim(a);
        A_X(a) = g_bcx + (g_vw - SCREEN_W) / 2 + sx; A_Y(a) = g_bcy + (g_vh - SCREEN_H) / 2 + sy;
        A_FX(a) = A_X(a) << 16; A_FY(a) = A_Y(a) << 16;
        draw_actor(a);
    }
}

/* ---- scene 2: the fireball through the clouds (INTRO map 3 over map 2) -------------------------- */

/* Meteor (0x1F196): 540 frames; the hero (the camera's target) at x 160 + a px a frame, y 150. The
   fireball FIRE at the screen's (430, 1100), 0.5 px left and 2 up a frame; every 3-6 frames (0x1F29E) a
   PUFF (once) at its x + -8..7, y + 30 + 0..31, falling (0xE0..0x11F)/256 a frame, vx (-64..63)/256. */
static void puff_tick(Spr *s)
{
    s->x += s->vx; s->y += s->vy;
    if (spr_animate(s)) spr_free(s);
}

static void meteor_tick(Spr *s)
{
    Spr *p;
    s->y -= 0x20000; s->x -= 0x8000;
    spr_animate(s);
    if (--s->t < 0) {
        s->t = (rnd() & 3) + 3;
        if ((p = spr_new(puff_tick, "METP", s->x + (((rnd() & 15) - 8) << 16), s->y + ((30 + (rnd() & 31)) << 16), PL_PLAY, 1))) {
            p->screen = 1; p->once = 1;
            p->vy = ((rnd() & 63) + 0xE0) << 8;
            p->vx = ((rnd() & 127) - 64) << 8;
        }
    }
}

static void scene2_start(void)
{
    Spr *s = spr_new(meteor_tick, "METF", 430 << 16, 1100 << 16, PL_PLAY, 2);
    if (s) s->screen = 1;
    g_heroX = 160 << 16; g_heroY = 150 << 16; g_maxY = 0;
    camera_jump();
}

static void scene2_tick(void)
{
    g_heroX += 0x10000;
    camera_follow();
    if (g_tick >= 540) next_scene(3);
}

/* ---- scene 3: the impact by the log (INTRO map 5 over map 4) ------------------------------------- */

/* Flink (0x1F00A) runs the scene: 780 frames; the background's x from 64 down by 1/3 px a frame (0x1F170).
   Impact (0x1EE8A) at (250, 104): at frame 450 a fire HEAD comes down to it over 40 frames from 20 px
   right and 100 up, then BOOM (once), then (0x1EF58) every 5-12 frames a PUF1 or PUF2 at its x - 2,
   y + 1, rising (0xE0..0x11F)/256 a frame, 1/256 less each, (0x20..0x3F)/256 right, gone at y 10. */
static void ipuff_tick(Spr *s)
{
    s->x += s->vx;
    s->vy -= 0x100;
    s->y -= s->vy;
    spr_animate(s);
    if ((s->y >> 16) <= 10) spr_free(s);
}

static void impact_tick(Spr *s)
{
    Spr *p;
    switch (s->d) {
    case 0:
        if (--s->t < 0) { s->d = 1; s->t = 40; s->hide = 0; spr_tag(s, "IHED"); }
        else return;
        /* fall through */
    case 1:
        s->x = s->a + s->t * 0x8000; s->y = s->b - s->t * 0x28000;
        spr_animate(s);
        if (--s->t < 0) { s->d = 2; spr_tag(s, "IBOM"); s->once = 1; s->x = s->a; s->y = s->b; }
        break;
    case 2:
        if (spr_animate(s)) { s->d = 3; s->hide = 1; s->t = 0; }
        break;
    default:
        if (--s->t < 0) {
            s->t = (rnd() & 7) + 5;
            if ((p = spr_new(ipuff_tick, rnd() & 1 ? "IPF2" : "IPF1", s->a - 0x20000, s->b + 0x10000, PL_PLAY, 3))) {
                p->vy = ((rnd() & 63) + 0xE0) << 8;
                p->vx = ((rnd() & 31) + 0x20) << 8;
            }
        }
    }
}

static void scene3_start(void)
{
    Spr *s = spr_new(impact_tick, "IHED", 250 << 16, 104 << 16, PL_PLAY, 2);
    if (s) { s->a = s->x; s->b = s->y; s->t = 450; s->hide = 1; }
    g_heroX = 100 << 16; g_heroY = 150 << 16; g_maxY = 0;
    g_bgX = 64 << 16;
    camera_jump();
}

static void scene3_tick(void)
{
    g_bgX -= 0x5555; if (g_bgX < 0) g_bgX = 0;
    if (g_tick >= 780) next_scene(4);
}

/* ---- scene 4: the dark sky (INTRO map 7 over map 6) ------------------------------------------------ */

/* Cloud (0x1F37A) runs it: 420 frames; shadow/highlight mode on (VDP register 12 = 0x89), so every tile
   but the high-priority ones (the background's bars) is at half brightness; the hero goes down 0.75 px a
   frame while he is less than 200 px under the camera's top (0x1F4DE). Its own sprite (EINS, ZWEI) is a
   shadow box (line 3 colour 15, see the converter's scene_record) over tiles that are all shadowed
   already: it shows nothing and is left out. Cinemascope (0x1F548): the bars, 24 lines at the top and
   bottom of the screen. */
static void shade_tick(Spr *s) { (void)s; }

static void scene4_start(void)
{
    Spr *s;
    g_heroX = 100 << 16; g_heroY = 150 << 16;
    g_maxY = 544 - SCREEN_H;
    camera_jump();
    if ((s = spr_new(shade_tick, "BLAK", 0, 0, PL_PLAY, 0))) s->hold = 1;
}

static void scene4_tick(void)
{
    if ((g_heroY >> 16) - (g_camY >> 16) <= 200) g_heroY += 0xC000;
    camera_follow();
    if (g_tick >= 420) next_scene(5);
}

/* ---- scene 5: the pond and the village (INTRO map 9 over map 10) ------------------------------------ */

/* Head (0x2077C) runs it: 1200 frames, then the title (the music fading); the hero from y 160 down 1/8 px
   a frame, at most 200 under the camera's top; the map's height counts 8 px less (0x207BA). Lightning
   (0x1ED10) x4, each param + 160 frames, then (0x1EDA4) 40 frames: ZAP1 once, the flash at its end, ZAP2
   looping; then its spark (HEAD) at x - 2, y - 2 flies up (0.25 px a frame, + 0x200 a frame) with three
   TAIL sparks behind (0x1EE3E: each one's phase + 0x30 a frame; at 4 back to its low byte and to the spark's
   x - 4, y - 3; from 0x400, 0x2AB, 0x156, unplaced). */
static const short BOLTS[4][3] = { { 62, 96, 330 }, { 88, 81, 270 }, { 150, 127, 300 }, { 209, 106, 360 } };

static void trail_tick(Spr *s) { (void)s; }

static void bolt_tick(Spr *s)
{
    int i;
    switch (s->d) {
    case 0:
        if (--s->t >= 0) return;
        s->d = 1; s->t = 40; s->hide = 0; s->once = 1; spr_tag(s, "ZAP1");
        /* fall through */
    case 1:
        if (s->once && spr_animate(s)) { g_flash = FLASH; s->once = 0; spr_tag(s, "ZAP2"); }
        else if (!s->once) spr_animate(s);
        if (--s->t < 0) {
            Spr *p;
            s->d = 2; spr_tag(s, "LHED");
            s->x -= 0x20000; s->y -= 0x20000; s->vy = 0x4000;
            for (i = 0; i < 3; i++)
                if ((p = spr_new(trail_tick, "LTAL", 0, 0, PL_PLAY, 2))) {
                    static const int PH[3] = { 0x400, 0x2AB, 0x156 };
                    p->hold = 1; p->hide = 1; p->a = PH[i]; p->b = (int)(s - g_spr);
                }
        }
        break;
    default:
        for (i = 0; i < MAX_SPR; i++) {
            Spr *p = &g_spr[i];
            if (p->tick != trail_tick || p->b != (int)(s - g_spr)) continue;
            p->a += 0x30;
            if ((p->a >> 8 & 0xFF) == 4) { p->a &= 0xFF; p->x = s->x - 0x40000; p->y = s->y - 0x30000; p->hide = 0; spr_jump(p); }
            p->anim = ((p->a >> 8) > 2 ? 2 : (p->a >> 8)) << 8;
        }
        s->vy += 0x200;
        s->y -= s->vy;
        if ((s->y >> 16) < (g_camY >> 16) - 64) {
            for (i = 0; i < MAX_SPR; i++) if (g_spr[i].tick == trail_tick && g_spr[i].b == (int)(s - g_spr)) spr_free(&g_spr[i]);
            spr_free(s);
        }
    }
}

static void scene5_start(void)
{
    Spr *s;
    int i;
    for (i = 0; i < 4; i++)
        if ((s = spr_new(bolt_tick, "ZAP1", BOLTS[i][0] << 16, BOLTS[i][1] << 16, PL_PLAY, 2))) {
            s->t = BOLTS[i][2] + 160; s->hide = 1;
        }
    g_heroX = 100 << 16; g_heroY = 160 << 16;
    g_maxY = 256 - 8 - SCREEN_H;
    camera_jump();
}

static void scene5_tick(void)
{
    g_heroY += 0x2000;
    if ((g_heroY >> 16) - (g_camY >> 16) >= 200) g_heroY = ((g_camY >> 16) + 200) << 16;
    camera_follow();
    if (g_tick >= 1200) to_title("the intro's end");
}

/* ---- scene 6: the demon's corpse thrown into the sky (EXTRO map 1 over map 2) -------------------- */

/* Arsch (0x1EAA0) runs it: 960 frames, music index 20; the corpse (DEAD) at x 160 from y 3350 up 5.0 a
   frame; for 480 frames the screen shakes (0x874E = 0x11 every 10-17); then it slows by 0x200 a frame
   and the camera's target, from the map's height - 70, rises 5.0 a frame, 0x400 less each (0x1EC2C).
   Every 2-5 frames a TAIL (once) at its x + -8..7, y + 0..15, falling (0x20..0x3F)/256 a frame, vx
   (-128..127)/256. The background's y (0x1ECA2): the target's - 150 (610 below 360 or above 760), * 1.3.
   Felsen (0x1E9B4): every 8-15 frames a rock (ROCK frame 0-2) from x 128..191, the map's height + 50,
   up (0x500..0x6FF)/256 a frame, 0x800 less each, vx (-512..511)/256; in front of the priority tiles
   once falling (+3 bit 7); after 100 frames gone off the screen. */
static void tail_tick(Spr *s)
{
    s->x += s->vx; s->y += s->vy;
    if (spr_animate(s)) spr_free(s);
}

static void corpse_tick(Spr *s)
{
    Spr *p;
    if (--s->t < 0) {
        s->t = (rnd() & 3) + 2;
        if ((p = spr_new(tail_tick, "ARST", s->x + (((rnd() & 15) - 8) << 16), s->y + ((rnd() & 15) << 16), PL_PLAY, 3))) {
            p->once = 1;
            p->vy = ((rnd() & 31) + 32) << 8;
            p->vx = ((rnd() & 255) - 128) << 8;
        }
    }
    if (--s->a >= 0) {
        if (--s->b < 0) { g_quake = 0x11; s->b = (rnd() & 7) + 10; }
    } else {
        s->vy -= 0x200;
        s->c -= 0x400;
        if (s->c >= 0) g_heroY -= s->c;
    }
    s->y -= s->vy;
}

static void rock_tick(Spr *s)
{
    int sy;
    sy = (s->y >> 16) - (g_camY >> 16);
    if (--s->t < 0 && (sy < -48 || sy > SCREEN_H + 48)) { spr_free(s); return; }
    s->vy -= 0x800;
    if (s->vy < 0) s->plane = PL_FG;
    s->y -= s->vy;
    s->x += s->vx;
}

static void felsen_tick(Spr *s)
{
    Spr *p;
    if (--s->t >= 0) return;
    s->t = (rnd() & 7) + 8;
    if ((p = spr_new(rock_tick, "ROCK", (0xA0 + (rnd() & 63) - 32) << 16, (1024 + 50) << 16, PL_PLAY, 3))) {
        p->hold = 1; p->anim = (rnd() % 3) << 8;
        p->vy = ((rnd() & 0x1FF) + 0x500) << 8;
        p->vx = ((rnd() & 0x3FF) - 0x200) << 8;
        p->t = 100;
    }
}

static void scene6_start(void)
{
    Spr *s;
    if ((s = spr_new(corpse_tick, "ARSC", 160 << 16, 3350 << 16, PL_PLAY, 1))) {
        s->hold = 1; s->vy = 0x50000; s->a = 480; s->c = 0x50000;
    }
    if ((s = spr_new(felsen_tick, "ROCK", 0, 0, PL_PLAY, 3))) s->hide = 1;
    g_heroX = 100 << 16; g_heroY = (1024 - 70) << 16;
    g_maxY = 1024 - SCREEN_H;
    camera_jump();
    music_play(TRACK_ENDING);
}

static void scene6_tick(void)
{
    int d = g_heroY >> 16;
    camera_follow();
    if (d < 360) d = 610; else { d -= 150; if (d >= 610) d = 610; }
    g_bgY = (d * 130 / 100) << 16;
    if (g_tick >= 960) next_scene(7);
}

/* ---- scene 7: the sea, the wizard, CONGRATULATIONS (EXTRO map 3) ----------------------------------- */

/* Smoke (0x1E838) runs it: 1260 frames, then the music fades; every 3-6 frames a PUF (once) at its x +
   -8..7, y + -4..3, rising (0x100..0x1FF)/256 a frame, 0x800 less each, vx (-128..127)/256.
   Wiz (0x1E606) at (97, 45): 240 frames moving 0.5 right, 0.25 up (0xC0 less a frame), WIZ, a TAIL (once,
   0.125 px left a frame) every 8; then 31 sparks (0x1E6EC: EINS, ZWEI or DREI from a random frame, up
   (0x80..0xFF)/256 a frame, 0x200 less each, vx (-128..127)/256, each twice with speeds of its own, for
   120-183 frames) and it goes. Held (0x1E44E): from x -80, y 290 he flies in at 4.0 right (0x800 less a
   frame) and 4.0 up (0x1000 less) after 420 frames; 180 later the letters (CONG's 15 frames) leave his x,
   at y 250 + 0x1E586's offsets, each after 0x1E5A4's wait, 5.0 up (0x1000 less a frame) and its vx while
   rising. */
static const short CONG_DY[15] = { 25, 19, 14, 10, 7, 4, 2, 2, 2, 4, 7, 10, 14, 19, 25 };
static const short CONG_VX[15] = { -280, -240, -200, -160, -120, -80, -40, 0, 40, 80, 120, 160, 200, 240, 280 };

static void smoke_puff_tick(Spr *s)
{
    if (spr_animate(s)) { spr_free(s); return; }
    s->x += s->vx;
    s->vy -= 0x800;
    s->y -= s->vy;
}

static void smoke_tick(Spr *s)
{
    Spr *p;
    if (--s->t >= 0) return;
    s->t = (rnd() & 3) + 3;
    if ((p = spr_new(smoke_puff_tick, "SMOK", s->x + (((rnd() & 15) - 8) << 16), s->y + (((rnd() & 7) - 4) << 16), PL_PLAY, 3))) {
        p->once = 1;
        p->vy = ((rnd() & 255) + 256) << 8;
        p->vx = ((rnd() & 255) - 128) << 8;
    }
}

static void wiztail_tick(Spr *s)
{
    if (spr_animate(s)) { spr_free(s); return; }
    s->x -= 0x2000;
}

static void spark_tick(Spr *s)
{
    if (--s->t < 0) { spr_free(s); return; }
    spr_animate(s);
    s->vy -= 0x200; s->y -= s->vy; s->x += s->vx;
    s->d -= 0x200; s->b -= s->d; s->a += s->c;          /* its twin */
}

static void wiz_tick(Spr *s)
{
    Spr *p;
    int i;
    if (--s->t < 0) {
        static const char *const TAGS[3] = { "WIZ1", "WIZ2", "WIZ3" };
        for (i = 0; i < 31; i++)
            if ((p = spr_new(spark_tick, TAGS[rnd() % 3], s->x, s->y, PL_PLAY, 3))) {
                p->anim = (rnd() % 3) << 8;
                p->vy = ((rnd() & 127) + 128) << 8; p->vx = ((rnd() & 255) - 128) << 8;
                p->t = (rnd() & 63) + 120;
                p->d = ((rnd() & 127) + 128) << 8; p->c = ((rnd() & 255) - 128) << 8;
                p->a = s->x; p->b = s->y; p->e = 1;
            }
        spr_free(s);
        return;
    }
    if (!(s->t & 7) && (p = spr_new(wiztail_tick, "WIZT", s->x, s->y, PL_PLAY, 3))) p->once = 1;
    spr_animate(s);
    s->vy -= 0xC0;
    s->y -= s->vy;
    s->x += s->vx;
}

static void letter_tick(Spr *s)
{
    if (--s->t >= 0) return;
    s->hide = 0;
    s->vy -= 0x1000;
    if (s->vy < 0) return;
    s->y -= s->vy;
    s->x += s->vx;
}

static void held_tick(Spr *s)
{
    int k;
    if (s->t > 0) { s->t--; return; }
    if (s->a > 0 && --s->a == 0)
        for (k = 0; k < 15; k++) {
            Spr *p = spr_new(letter_tick, "CONG", (s->x >> 16) << 16, (250 + CONG_DY[k]) << 16, PL_PLAY, 3);
            if (!p) break;
            p->hold = 1; p->anim = k << 8; p->hide = 1;
            p->t = 5 * k; p->vx = CONG_VX[k] << 8; p->vy = 0x50000;
        }
    s->vx -= 0x800;
    if (s->vx >= 0) s->x += s->vx;
}

static void scene7_start(void)
{
    Spr *s;
    if ((s = spr_new(smoke_tick, "SMOK", 63 << 16, 186 << 16, PL_PLAY, 3))) s->hide = 1;
    if ((s = spr_new(wiz_tick, "WIZA", 97 << 16, 45 << 16, PL_PLAY, 2))) { s->t = 240; s->vx = 0x8000; s->vy = 0x4000; }
    if ((s = spr_new(held_tick, "CONG", -80 * 0x10000, 290 << 16, PL_PLAY, 3))) { s->hide = 1; s->t = 420; s->a = 180; s->vx = 0x40000; }
    g_heroX = 100 << 16; g_heroY = -50 * 0x10000; g_maxY = 0;
    camera_jump();
}

static void scene7_tick(void)
{
    if (g_tick >= 1260) to_title("the ending's end");
}

/* ---- the controller ------------------------------------------------------------------------------- */

typedef struct { void (*start)(void), (*tick)(void); } Scene;
static void scene0_tick(void) { }
static const Scene SCENE[SCENES] = {
    { scene0_start, scene0_tick }, { scene1_start, scene1_tick }, { scene2_start, scene2_tick },
    { scene3_start, scene3_tick }, { scene4_start, scene4_tick }, { scene5_start, scene5_tick },
    { scene6_start, scene6_tick }, { scene7_start, scene7_tick },
};

static int ctl_alive(void) { return g_ctl && A_TYPE(g_ctl) == g_ctlType && g_scene >= 0; }

static void __cdecl scene_update(BYTE *a)
{
    int i;
    if (a != g_ctl || !ctl_alive()) return;
    *G_BPAUSEDISABLED = 1;
    if (!g_started) {
        g_started = 1;
        *G_BLEVELEXITREACHED = 0;                    /* (the reload from the scene before) */
        sfx_load();
        if (!*G_FADESTEP) *G_FADESTEP = 0x40;        /* the fade-in */
    }
    g_tick++;
    if (!g_ending && !g_leaving && *G_INPUTPRESSED) to_title("a button");
    SCENE[g_scene].tick();
    for (i = 0; i < MAX_SPR; i++) if (g_spr[i].tick) g_spr[i].tick(&g_spr[i]);
    if (g_leaving && !*G_FADESTEP) *G_FADESTEP = SKIP_FADE;   /* the fade-out, once the fade-in is over */
    if (g_flash > 0) g_flash -= FLASH_STEP;
    set_cameras();
}

/* a sprite through its stand-in, on its plane (ip: the play plane's index) */
static void spr_draw(Spr *s, int ip)
{
    int pl = ip + s->plane < 0 ? PL_PLAY : s->plane;
    BYTE *a;
    if (s->st < 0 && (s->st = st_new()) < 0) return;
    a = g_st[s->st];
    g_stDrawn[s->st] = g_draws;
    A_PLANE(a) = (short)(ip + pl);
    A_LAYER(a) = (short)FLINK_LAYER(s->list);
    A_FLAGS(a) = (A_FLAGS(a) & ~AF_LEFT) | (s->mirror ? AF_LEFT : 0);
    Actor_SetAnimIfChanged(a, s->tag);
    hold_frame(a, s->anim >> 8);
    Actor_AdvanceAnim(a);
    A_X(a) = cam_x(pl) + view_x(s); A_Y(a) = cam_y(pl) + view_y(s);
    if (s->tl && A_RECORD(a)) {                      /* the point: the box's middle (the frame's one */
        const BYTE *e = A_RECORD(a) + 0x20 + *(const short *)(a + 0x6E) * 8;   /* entry: minus its half) */
        A_X(a) -= *(const short *)(e + 2); A_Y(a) -= *(const short *)(e + 4);
    }
    A_FX(a) = A_X(a) << 16; A_FY(a) = A_Y(a) << 16;
    Actor_Draw(a, s->scale, 0, s->alpha > 0 && s->alpha < 255 ? s->alpha : 0);
    if (s->tick == spark_tick && s->e) {             /* the spark's twin */
        A_X(a) = cam_x(pl) + (s->a >> 16) - g_mcx; A_Y(a) = cam_y(pl) + (s->b >> 16) + g_pad[g_scene] - g_mcy;
        draw_actor(a);
    }
}

/* each sprite through its stand-in; the controller draws the bars, the shade and the flash */
static void __cdecl scene_draw(BYTE *a)
{
    int x = A_X(a), y = A_Y(a), i, ip;
    short plane = A_PLANE(a), layer = A_LAYER(a);
    DWORD flags = A_FLAGS(a);
    if (a != g_ctl || !ctl_alive()) return;
    g_draws++;
    ip = *G_PCURPLANE ? (int)(*G_PCURPLANE - G_PLANES) / PLANE_STRIDE : plane;
    for (i = 0; i < MAX_SPR; i++) {
        Spr *s = &g_spr[i];
        int p;
        if (!s->tick || s->hide) continue;
        if (s->tick == star_tick) { stars_draw(ip + PL_BG, FLINK_LAYER(s->list)); continue; }
        if (s->tick != shade_tick) { spr_draw(s, ip); continue; }
        A_PLANE(a) = (short)ip;                       /* the dark sky: half brightness under the bars */
        A_LAYER(a) = (short)FLINK_LAYER(s->list);
        Actor_SetAnimIfChanged(a, s->tag);
        hold_frame(a, 0);
        Actor_AdvanceAnim(a);
        for (p = 0; p < g_vh; p += 32) {
            A_X(a) = g_lcx; A_Y(a) = g_lcy + p;
            Actor_Draw(a, 1000, 0, 0x80);
        }
    }
    /* the bars: Lomax's lines outside Flink's 224, and Cinemascope's (0x1F590: scenes 4 and 5, Flink's
       lines 0-23 and 200-223) */
    {
        int top = (g_vh - SCREEN_H) / 2 + (g_scene == 4 || g_scene == 5 ? CINE_H : 0);
        if (top > 0) {
            A_PLANE(a) = (short)(ip > 0 ? ip - 1 : ip);
            A_LAYER(a) = (short)FLINK_LAYER(0);
            Actor_SetAnimIfChanged(a, FOURCC("BLAK"));
            hold_frame(a, 0);
            Actor_AdvanceAnim(a);
            A_X(a) = g_mcx; A_Y(a) = g_mcy + top - BAR_H;
            draw_actor(a);
            A_Y(a) = g_mcy + g_vh - top;
            draw_actor(a);
        }
    }
    if (g_flash > 0) {                               /* the lightning's flash */
        A_PLANE(a) = (short)(ip > 0 ? ip - 1 : ip);
        A_LAYER(a) = (short)FLINK_LAYER(0);
        Actor_SetAnimIfChanged(a, FOURCC("WHIT"));
        hold_frame(a, 0);
        Actor_AdvanceAnim(a);
        A_X(a) = g_mcx; A_Y(a) = g_mcy;
        Actor_Draw(a, 1000, 0, g_flash);
    }
    A_PLANE(a) = plane; A_LAYER(a) = layer; A_FLAGS(a) = flags;
    A_X(a) = x; A_Y(a) = y;
}

/* 'Spik Scene' (from spik_spawn): the scene's controller */
void scene_spawn(BYTE *a)
{
    if (g_scene < 0) return;
    memset(g_spr, 0, sizeof(g_spr));
    memset(g_stOwned, 0, sizeof(g_stOwned));
    g_ctl = a; g_ctlType = A_TYPE(a);
    g_tick = 0; g_leaving = 0; g_started = 0; g_quake = 0; g_flash = 0;
    g_ending = g_scene >= SCENE_ENDING;
    g_bgX = g_bgY = 0;
    g_heroX = 100 << 16; g_heroY = 150 << 16; g_maxY = 0;
    g_camX = g_camY = g_camVX = g_camVY = 0;
    A_FLAGS(a) &= ~(0x200000u | AF_CONTACT);
    A_CULL(a) = CULL_NEVER;
    A_UPDATE(a) = (void *)scene_update;
    A_DRAW(a) = (void *)scene_draw;
    SCENE[g_scene].start();
    set_cameras();
    flog("scene %d starts", g_scene);
}

void scenes_reset(void)
{
    g_ctl = NULL;
    memset(g_spr, 0, sizeof(g_spr));
    memset(g_stOwned, 0, sizeof(g_stOwned));
}

/* Lomax isn't drawn and doesn't run in a scene (player_draw, player_tick) */
int scene_hides_player(void) { return g_scene >= 0; }
