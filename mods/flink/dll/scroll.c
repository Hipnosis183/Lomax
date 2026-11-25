/*
 * scroll.c - Flink's Scroll Points (README "Scrolling screens"): the camera moving on its own at a
 * fixed pace (3-2, 9-2, 12-1, 16-2, 21-1, 28-1 across, 27-1 down), or keeping its x or y.
 *
 * Flink. The Scroll Point (MAIN.WLD's system type 107, 0x2E628) runs once and removes itself
 * (0x32D9E). It sets the camera's state, which stays until another Scroll Point or the map's end:
 *   - param 2 in 0x40..0xDC: the player's height on the screen (0x872E, default 0xA0); the camera
 *     jumps there (0x19DC4);
 *   - checkbox bit 0: the camera's x stays (0x8723), bit 1: its y (0x8722; then no lower than the
 *     map's height - 0xE8);
 *   - param 0 / param 1 x 0x28F: the camera's x / y speed (0x8730 / 0x8734, hundredths of a px a
 *     frame; 0 = it follows the player). The camera's own speed (0x8738 / 0x873C) goes toward it by
 *     0x200 a frame, and is 0 again at the map's edges (Camera_Update 0x19E12: a kept axis first,
 *     then the scrolling one, else the player's).
 *   - While the x scrolls, the player stays 26..294 px inside the 320-px screen (0x1CCCC). While the
 *     y scrolls down and the player is left above the screen's top, he dies (0x1BB40, 0x1A8D0);
 *     falling out of its bottom is the ordinary pit.
 *
 * Lomax. Its own scrolling screen is ForceScrolling (object 8, ForceScroll_Init 0x41C270 and
 * ForceScroll_Update 0x41C2E0, the airship's). It keeps the speed in its actor (+0x80 target,
 * +0x84 the speed, which Math_ApproachValue moves 0x1000 a tick toward it, +0x88 the camera's x in
 * 16.16), sets player state 0x20 (Player_Update leaves the camera's x alone), 0x8000000 (nor keeps
 * Lomax inside the screen) and 0x20000000 (the map wraps around); each tick it moves the camera's x
 * (once the speed is reached, by 1 px a tick, whatever the speed), keeps Lomax 16..304 px inside the
 * screen (Lomax+ widens the 304 with the view) with the camera's speed when he is pinned at an edge,
 * and kills him squeezed between the left edge and a wall (g_PlayerWallHit 2). It stops while Lomax
 * dies, the level ends or the music fades. It has no y, no end (the airship's map wraps) and no lock.
 *
 * Here. The converter's 'Forc Flink' is object 8 with Flink's params (x speed, y speed, height, the
 * checkboxes). force_spawn (the object's spawn) sets the state as Flink's and the actor removes itself
 * at its first update; Lomax's ForceScroll_Init isn't run (it would wrap the map). After Player_Update
 * (player_tick, grab.c) scroll_after moves the camera: the x by Flink's speed and ramp, up to the
 * plane's nMaxScrollX (+0x30), and then ForceScroll_Update with a stand-in actor whose speed is
 * reached and the camera 1 px short of where it goes, so that Lomax's own code adds that 1 px, keeps
 * Lomax on the screen and crushes him; the y by its speed up to nMaxScrollY (+0x32). State 0x20 /
 * 0x40 keep each axis that scrolls or stays, from Player_Update; 0x8000000 while the x scrolls. Flink's
 * height becomes g_CameraPlayerOffsetY (+ 8: Lomax's view is 240 px high, Flink's 224), which the
 * game's own camera follows, back to the game's value at every map load (scroll_reset, world_load).
 */
#include "flink.h"

#define SPEED_UNIT         0x28F         /* a param's 1/100 px, as Flink's and ForceScroll_Init's */
#define RAMP               0x200         /* the camera's speed goes toward the param's by this a tick */
#define HEIGHT_MIN         0x40          /* Flink's height param, else unchanged */
#define HEIGHT_MAX         0xDC
#define VIEW_EXTRA         8             /* (240 - 224) / 2 */
#define CAM_MIN_Y          16            /* Player_Update keeps the camera's y from 16 */

static struct {
    int on;              /* a Scroll Point ran in this map load */
    int keepx, keepy;    /* checkbox bits 0 and 1 */
    int tx, ty;          /* the params' speeds, 16.16 */
    int vx, vy;          /* the camera's speed */
    int fx, fy;          /* the camera, 16.16 */
    int synced;          /* fx / fy were taken from the camera */
    int snap;            /* jump to the new height */
    int wrap;            /* the map wraps around across (scroll_wrap: the Village Boss) */
} g;
static DWORD g_bits;                     /* the player state bits set here */
static int g_offset = -1;                /* g_CameraPlayerOffsetY as the game had it */
static BYTE g_fs[ACTOR_STRIDE];          /* ForceScroll_Update's actor: +0x80, +0x84, +0x88 */

static int scrolls_x(void) { return g.tx && !g.keepx; }

/* a Scroll Point moves or holds the camera's x (enemy.c: Flink's camera is then Lomax's) */
int scroll_owns_x(void) { return g_active && g.on && (g.keepx || g.tx); }
static int scrolls_y(void) { return g.ty && !g.keepy; }

/* a Scroll Point keeps the camera's y: Flink's screen then ends 8 px above the map's bottom (0x2E628) */
int scroll_keeps_y(void) { return g_active && g.on && g.keepy; }

/* Flink's camera speed 0x8738 not 0: scrolling and not stopped at the map's edge (scroll_after) */
int scroll_x_moving(void) { return g_active && g.on && scrolls_x() && g.vx; }

static void set_bits(DWORD want)
{
    *G_PLAYERSTATE = (*G_PLAYERSTATE & ~(g_bits & ~want)) | want;
    g_bits = want;
}

/* every map load (world_load) and every start and restart (trapdoor_spawn: a lost life doesn't load
   the map again). The Start comes before the Scroll Points in the plane's list, so those set at the
   start (0x4000) spawn after it. */
void scroll_reset(void)
{
    if (g.on) flog("scroll state cleared");
    if (g_bits) set_bits(0);
    if (g_offset >= 0) *G_CAMERAPLAYEROFFSETY = g_offset;
    g_offset = -1;
    memset(&g, 0, sizeof(g));
}

static void approach(int *v, int to)
{
    if (*v < to) *v = *v + RAMP < to ? *v + RAMP : to;
    else if (*v > to) *v = *v - RAMP > to ? *v - RAMP : to;
}

static void __cdecl point_gone(BYTE *a) { Actor_Kill(a); }
static void __cdecl draw_none(BYTE *a) { (void)a; }

/* object 8: Flink's Scroll Point (0x2E628) in Flink's maps, Lomax's ForceScrolling elsewhere */
void __cdecl force_spawn(BYTE *a, BYTE *def)
{
    int h;
    if (!g_active || !is_type(a, "Forc Flink")) { ForceScroll_Init(a, def); return; }
    h = def_param(a, def, 2, 0);
    if (h >= HEIGHT_MIN && h <= HEIGHT_MAX) {
        if (g_offset < 0) g_offset = *G_CAMERAPLAYEROFFSETY;
        *G_CAMERAPLAYEROFFSETY = h + VIEW_EXTRA;
        g.snap = 1;
    }
    g.keepx = def_param(a, def, 3, 0) & 1;
    g.keepy = def_param(a, def, 3, 0) >> 1 & 1;
    g.tx = def_param(a, def, 0, 0) * SPEED_UNIT;
    g.ty = def_param(a, def, 1, 0) * SPEED_UNIT;
    g.synced = 0;
    g.on = 1;
    if (def && (D_FLAGS(def) & DF_START) && (scrolls_x() || scrolls_y())) trapdoor_skip_drop();   /* (exits.c) */
    flog("scroll point at (%d,%d): speed %d/%d, height %d,%s%s", A_X(a), A_Y(a), def_param(a, def, 0, 0),
         def_param(a, def, 1, 0), h, g.keepx ? " x stays" : "", g.keepy ? " y stays" : "");
    A_FLAGS(a) = (A_FLAGS(a) & ~AF_CONTACT) | AF_HIDDEN;
    A_UPDATE(a) = (void *)point_gone;            /* (its placement stays spawned: once a map load) */
    A_DRAW(a) = (void *)draw_none;
}

static DWORD wanted_bits(void)
{
    DWORD b = 0;
    if (!g.on) return 0;
    if (g.keepx || scrolls_x()) b |= PS_CAMX_STAYS;
    if (scrolls_x()) b |= PS_NO_EDGES;
    if (g.keepy || scrolls_y()) b |= PS_CAMY_STAYS;
    if (g.wrap) b |= PS_WRAP;
    return b;
}

/* before Player_Update: it leaves alone what moves here */
void scroll_before(void)
{
    if (!g_active || !g.on) return;
    set_bits(wanted_bits());
}

/* Flink's map wrap (0x8726, set by the Village Boss 0x29AAE): Camera_Update no longer stops the
   camera at the map's edges, and the map repeats across. Lomax's own wrap is the airship's, player
   state 0x20000000: its tile layers always repeat (TileLayer_Draw takes cells modulo the width) and
   with the state the ground and walls do too (Plane_FindGroundY, Actor_CheckWallCollision). Flink's
   16-bit positions wrap at 65536 on their own; Lomax's 16.16 ones would overflow at 32768 px, so
   past REBASE_AT the camera, Lomax and every actor of the plane go back REBASE_BY px, a whole number
   of the map's and the half-speed background's widths (512 both in 16-2), which shows nothing (and
   Lomax+'s FPS Override doesn't interpolate a jump that big). */
#define REBASE_AT          16384
#define REBASE_BY          8192
void scroll_wrap(void)
{
    if (!g_active || g.wrap) return;
    g.wrap = 1;
    g.on = 1;
    set_bits(wanted_bits());
    flog("the map wraps around");
}

/* Flink's 0x8723 set and the speed 0 (the Village Boss defeated, 0x2A1A0): the camera's x stays */
void scroll_stop_x(void)
{
    if (!g_active) return;
    g.keepx = 1;
    g.tx = g.vx = 0;
    g.on = 1;
    set_bits(wanted_bits());
}

static void rebase(BYTE *pl)
{
    int k, plane = (int)((pl - G_PLANES) / PLANE_STRIDE), d = REBASE_BY;
    g.fx -= d << 16;
    P_CAMX(pl) -= d;
    *G_PLAYERX -= d;
    *G_PLAYERXFIXED -= d << 16;
    for (k = 0; k < MAX_ACTORS; k++) {
        BYTE *a = actor_at(k);
        if (!actor_active(k) || !A_TYPE(a) || A_PLANE(a) != plane || A_FLAGS(a) & AF_SCREEN) continue;
        A_X(a) -= d;
        A_FX(a) -= d << 16;
    }
    flog("wrapping map: everything back %d px (camera at %d)", d, P_CAMX(pl));
}

/* after Player_Update: the camera (0x19E12) and Lomax on the screen */
void scroll_after(void)
{
    BYTE *pl = *G_PCURPLANE;
    int moving, max, top;
    if (!g_active || !g.on || !pl || *G_BPAUSED || *G_PLAYERSTATE & PS_FROZEN) return;
    set_bits(wanted_bits());
    moving = !*G_PLAYERDEATHTIMER && !*G_BLEVELENDING && !*G_BFADEOUTMUSIC;   /* as ForceScroll_Update */

    if (g.snap) {                                /* 0x19DC4: to the new height at once */
        int y = *G_PLAYERY - *G_CAMERAPLAYEROFFSETY;
        if (y > P_MAXY(pl)) y = P_MAXY(pl);
        if (y < CAM_MIN_Y) y = CAM_MIN_Y;
        P_CAMY(pl) = y;
        *G_CAMERASCROLLVELY = 0;
        g.snap = 0;
        g.synced = 0;
    }
    if (!g.synced) {
        g.fx = P_CAMX(pl) << 16;
        g.fy = P_CAMY(pl) << 16;
        g.synced = 1;
    }

    if (scrolls_x()) {
        approach(&g.vx, g.tx);
        if (moving) {
            g.fx += g.vx;
            max = P_MAXX(pl) << 16;
            if (!g.wrap) {                       /* (wrapping, no edges: Camera_Update 0x19EAE) */
                if (g.fx < 0) { g.fx = 0; g.vx = 0; }
                if (g.fx > max) { g.fx = max; g.vx = 0; }
            } else if (g.fx >= REBASE_AT << 16) rebase(pl);
            P_CAMX(pl) = (g.fx >> 16) - 1;       /* ForceScroll_Update adds 1 */
        }
        *(int *)(g_fs + 0x80) = g.vx;            /* reached: no ramp of its own */
        *(int *)(g_fs + 0x84) = g.vx;
        *(int *)(g_fs + 0x88) = g.fx;
        ForceScroll_Update(g_fs);
    } else g.fx = P_CAMX(pl) << 16;

    if (scrolls_y()) {
        approach(&g.vy, g.ty);
        if (moving) {
            g.fy += g.vy;
            max = P_MAXY(pl) << 16;
            if (g.fy < CAM_MIN_Y << 16) { g.fy = CAM_MIN_Y << 16; g.vy = 0; }
            if (g.fy > max) { g.fy = max; g.vy = 0; }
            P_CAMY(pl) = g.fy >> 16;
        }
        top = P_CAMY(pl);
        if (g.vy > 0 && moving && *G_PLAYERY < top) {   /* left above the screen (0x1BB40) */
            flog("scrolled off the top at (%d,%d), camera y %d", *G_PLAYERX, *G_PLAYERY, top);
            player_kill();
        }
    } else g.fy = P_CAMY(pl) << 16;
}
