/*
 * exits.c - Exits, doors and arrivals (README "Arrivals" and "Doors").
 *
 * A sub-level ends at a Lomax Exit (object 2, LevelExit_Update 0x4276A0), which fires only past the
 * map's right end - 50 px; Flink's exits stand anywhere, so in Flink's levels the Exit's update is
 * exit_update, which also fires at the Exit itself; a door (Flink's 'look up' exit) fires with Up
 * instead. An Exit leads to any sub-level of the level (the next, an earlier one, a side room or the
 * same map) and the map starts with Lomax standing where it leads instead of walking in from the left.
 *
 * Arrivals. Flink's Exit point (0x2AF7A) loads its 'level' with its 'entry', and there the revive
 * point with that entry (0x2B124) puts Flink on itself: he appears there, standing. Lomax's next
 * sub-level starts at its Start trapdoor (Trapdoor_Init 0x42A820, g_bLevelExitReached set): the
 * restart point is the trapdoor's x and the floor under it, and Lomax walks in from x = -100 at that
 * height with the input locked; Player_Update doesn't follow the ground while g_bLevelExitReached is
 * set (0x410C31), so in a Flink map, whose floor at the left isn't that height, he walked in the air
 * or under the floor. Trapdoor_Update ends the walk-in once he passes the restart x (the trapdoor's
 * +0xA8 set). LEVELS.INI's [Arrivals] lists, for every Exit (map file, x, y), the map it leads to and
 * the point he arrives at (x, floor y; the converter's arrival_points), and " door" for a door.
 * exit_update looks it up when the Exit fires; trapdoor_spawn, after the Start's own Trapdoor_Init on
 * the exit path, stands Lomax there instead (on the trapdoor's floor without an arrival): restart
 * point, player, his floor, the camera as Trapdoor_Init puts them, and the walk-in cleared
 * (g_bLevelExitReached, g_bLevelEnding, the input lock, the trapdoor's +0xA8). The screen fades in as
 * before; the trapdoor stays hidden. Flink shows nothing on a change of map, so the "Let's go" banner
 * and voice that Trapdoor_Update gives 10 ticks after an exit (+0x90 countdown, then sound 26 LETSGO
 * and Insert_Show(1)) are skipped: +0x90 = -1, as Trapdoor_Update leaves it once done. It still comes
 * at the level's start and after a lost life.
 *
 * Secret levels (levels.c) start as Flink's do, with Lomax standing on the floor under the Start
 * trapdoor and no "Let's go": Trapdoor_Init's first-start path (g_RestartX < 0)
 * has him shot out of the trapdoor (+0x80 counting 0xF0 down, player state 0x53, the clip line at
 * the trapdoor) and then "Let's go" as he lands (+0x80 -100 -> -200, sound 26 LETSGO and
 * Insert_Show(1)); secret_start puts the trapdoor in the state Trapdoor_Update leaves it in after
 * all that (+0x80 -200, +0x90 -1, closed ZWEI) instead.
 */
#include "flink.h"

#define MAX_ARRIVALS       64
#define DOOR_RANGE         16            /* Flink's door: his x within 16 px of its x, */
#define DOOR_ABOVE         80            /*   his feet 0..80 px above its y (0x2B008, 0x2B042) */
#define DOOR_TICKS         10            /*   10 frames before the fade (0x2B010) */
#define FADE_TICKS         64            /* the fade-out: g_FadeStep -64 -> 0 */
#define WALK_TO            850           /* the walk in: Lomax shrinks from 1000 to 750 per mille (as */
                                         /*   PlaneDoor_Update's) by the time the screen is black */
#define WALK_SPEED         0x18000       /* his walk to the door's centre first: 1.5 px a tick */
#define WALK_LIFT          5             /* and he rises this many px as he shrinks, for depth, */
                                         /*   drawn only (player_draw) */

/* [Arrivals]: the Exit's map and x, y, the map it leads to and the arrival's x, y, a door? */
static struct { int sec, sub, ex, ey, tsec, tsub, ax, ay, door; } g_arr[MAX_ARRIVALS];
static int g_narr;
static int g_arrival = -1;               /* the arrival of the Exit just taken, for trapdoor_spawn */
static int g_upReleased;                 /* Up was released since the map loaded (doors) */
static int g_walk;                       /* the door's walk in: ticks + 1 since he turned, 0 = none */
static int g_walkX = -1;                 /*   the door's centre while he walks to it, -1 once there */

/* the door's walk in ends with the map it leads to: Level_Start clears g_PlaneTransitionScale; the
   Start trapdoor's spawn (trapdoor_spawn) the rest */
static void walk_end(void)
{
    if (!g_walk) return;
    g_walk = 0;
    g_walkX = -1;
    *G_PLANETRANSITIONSCALE = 0;
    *G_PLAYERSTATE &= ~PS_ROPE;
    set_anim(G_PLAYERACTOR, "STAY");
}

static BYTE *g_drop;                     /* the Start trapdoor of a first start (trapdoor_skip_drop) */

void exits_reset(void) { g_upReleased = 0; g_drop = NULL; }

/* Lomax walks into a door with the animation of his plane doors (PlaneDoor_Update 0x432D30, Up on
   the front plane: 'IN  ', player state 0x10, vx 0, then the scale in g_PlaneTransitionScale) but
   not their path: PlaneDoor_Update lifts his sprite and Actor_Draw draws him toward a vanishing
   point of the plane behind, an arc (none). Here he only shrinks, his feet on the
   ground. g_PlaneTransitionScale stays non-zero, so that Player_Update leaves his animation alone
   (0x411480) and he can't be hit, but player_draw (groundtype.c) draws him through the plain path with
   door_walk_scale: Actor_Draw scales the sprite's parts around his x, y, his feet.
   First he walks to the door's centre (the Exit's x, Flink's exit point: the middle of the doorway),
   'WALK' at WALK_SPEED, facing it, moved here (Player_Update doesn't move him while the scale is not
   0); there he turns ('IN  ') and the fade's DOOR_TICKS start. */
static void walk_start(int doorX)
{
    g_walk = 1;
    g_walkX = doorX;
    *G_PLAYERSTATE |= PS_ROPE;
    *G_PLAYERVX = 0;
    *G_BAIRSPIN = 0;
    *G_PLAYERSPINTIMER = 0;
    *G_PPLANETRANSITIONFROM = *G_PCURPLANE;     /* (not read on player_draw's path) */
    *G_PLANETRANSITIONSCALE = 1000;
}

/* one tick of the walk: 1 while he walks to the door's centre, then 0 (he has turned) */
static int walk_tick(void)
{
    int t, n = DOOR_TICKS + FADE_TICKS;
    if (g_walkX >= 0) {
        int x = *G_PLAYERXFIXED, to = g_walkX << 16;
        if (x != to) {
            if (abs(to - x) <= WALK_SPEED) x = to;
            else x += to > x ? WALK_SPEED : -WALK_SPEED;
            if (to < *G_PLAYERXFIXED) *G_PLAYERFLAGS |= AF_LEFT;
            else *G_PLAYERFLAGS &= ~AF_LEFT;
            *G_PLAYERXFIXED = x;
            *G_PLAYERX = x >> 16;
            set_anim(G_PLAYERACTOR, "WALK");
            return 1;
        }
        g_walkX = -1;
        set_anim(G_PLAYERACTOR, "IN  ");
    }
    t = g_walk - 1;
    if (t > n) t = n;
    *G_PLANETRANSITIONSCALE = 1000 - (1000 - WALK_TO) * t / n;
    g_walk++;
    return 0;
}

/* Lomax's scale while he walks into a door (player_draw), 0 otherwise */
int door_walk_scale(void) { return g_walk ? *G_PLANETRANSITIONSCALE : 0; }

/* how many px higher he is drawn while he walks into a door: 0 at the door's centre, WALK_LIFT once
   the screen is black, following the shrink, so he seems to step back into the doorway */
int door_walk_lift(void)
{
    int s = door_walk_scale();
    return s ? WALK_LIFT * (1000 - s) / (1000 - WALK_TO) : 0;
}

static int find_map(const char *name, int *sec, int *sub)
{
    int k, j;
    for (k = 0; k < g_nlevels; k++)
        for (j = 0; j < g_nsubs[k]; j++)
            if (!_stricmp(map_name(k, j), name)) { *sec = k; *sub = j; return 1; }
    return 0;
}

void arrivals_load(const char *ini)
{
    static char buf[8192];
    char from[MAX_PATH], to[MAX_PATH], kind[16], *p;
    DWORD n = GetPrivateProfileSectionA("Arrivals", buf, sizeof(buf), ini);
    for (p = buf; n && *p && g_narr < MAX_ARRIVALS; p += strlen(p) + 1) {
        int i = g_narr, got;
        kind[0] = 0;
        got = sscanf(p, "%259s %d %d=%259s %d %d %15s", from, &g_arr[i].ex, &g_arr[i].ey, to, &g_arr[i].ax, &g_arr[i].ay, kind);
        g_arr[i].door = !_stricmp(kind, "door");
        if (got >= 6 && find_map(from, &g_arr[i].sec, &g_arr[i].sub) && find_map(to, &g_arr[i].tsec, &g_arr[i].tsub) &&
            g_arr[i].tsec == g_arr[i].sec) g_narr++;
        else flog("[Arrivals] %s: not understood", p);
    }
    flog("%d arrivals", g_narr);
}

static int find_arrival(int sec, int sub, int ex, int ey)
{
    int i;
    for (i = 0; i < g_narr; i++)
        if (g_arr[i].sec == sec && g_arr[i].sub == sub && g_arr[i].ex == ex && g_arr[i].ey == ey) return i;
    return -1;
}

#define G_PLAYERCLIPTOPY   ((volatile short *)0x0059F27Eu)   /* g_Player.nClipTopY (-20000: none) */

/* a secret level's start: Lomax on the floor under the trapdoor, no drop and no "Let's go" */
static void secret_start(BYTE *a)
{
    int x = *G_RESTARTX, y = *G_RESTARTY;
    *G_PLAYERX = x; *G_PLAYERXFIXED = x << 16;
    *G_PLAYERY = y; *G_PLAYERYFIXED = y << 16;
    *G_PLAYERVX = 0; *G_PLAYERVY = 0;
    *G_PLAYERGROUNDY = y;
    *G_PLAYERSTATE &= ~0x51u;                   /* in the trapdoor (0x10), camera y held (0x40), in the air */
    *G_PLAYERCLIPTOPY = -20000;
    set_anim(a, "ZWEI");                        /* closed */
    *(int *)(a + 0x80) = -200;                  /* landed: Trapdoor_Update's start sequence done */
    *(int *)(a + 0x90) = -1;                    /* no "Let's go" (Trapdoor_Init set 60 ticks to it) */
    flog("secret level: standing at (%d,%d)", x, y);
}

/* A map whose screen scrolls from the start (a Scroll Point with the Start's flag 0x4000: 3-2, 12-1,
   16-2, 21-1) starts with Lomax standing, as a secret level: during the drop
   out of the trapdoor player state 0x40 holds the camera's y at the trapdoor while the x scrolls on,
   so the screen was off until he landed. The Scroll Points set at the start spawn right after the
   Start (scroll.c), so force_spawn calls this with g_drop still the trapdoor of this start. */

void trapdoor_skip_drop(void)
{
    BYTE *a = g_drop, *pl = *G_PCURPLANE;
    int cx, cy;
    g_drop = NULL;
    if (!a || !pl || *(int *)(a + 0x80) <= -200 || *G_RESTARTY == NO_GROUND) return;
    secret_start(a);
    cx = *G_PLAYERX - 0xA0;                     /* the camera on him, as trapdoor_spawn's */
    cy = *G_PLAYERY - *G_CAMERAPLAYEROFFSETY;
    if (cx > P_MAXX(pl)) cx = P_MAXX(pl);
    if (cx < 0) cx = 0;
    if (cy > P_MAXY(pl)) cy = P_MAXY(pl);
    if (cy < 16) cy = 16;
    P_CAMX(pl) = cx;
    P_CAMY(pl) = cy;
    flog("scrolling start: no trapdoor drop");
}

/* the Start trapdoor (object 0): Lomax stands at the arrival after an Exit */
void __cdecl trapdoor_spawn(BYTE *a, BYTE *def)
{
    int exiting = *G_BLEVELEXITREACHED, i = g_arrival, x, y, g, first = *G_RESTARTX < 0;
    g_arrival = -1;
    g_drop = NULL;
    Trapdoor_SpawnA(a, def);
    walk_end();
    groundtype_reset();                                /* every start and restart: off the mud */
    scroll_reset();                              /*   the camera follows Lomax (a Scroll Point sets it again) */
    if (g_active && !exiting && first && def && (D_FLAGS(def) & DF_START) && level_secret(*G_LOADEDSECTION) &&
        *G_RESTARTY != NO_GROUND) {
        secret_start(a);
        return;
    }
    if (g_active && !exiting && first && def && (D_FLAGS(def) & DF_START)) g_drop = a;   /* trapdoor_skip_drop */
    if (!g_active || !exiting || !def || !(D_FLAGS(def) & DF_START)) return;
    x = *G_RESTARTX; y = *G_RESTARTY;
    if (i >= 0 && g_arr[i].tsec == *G_LOADEDSECTION && g_arr[i].tsub == *G_CURSUBLEVEL) {
        g = (int)Plane_FindGroundY(*G_PCURPLANE, g_arr[i].ax, g_arr[i].ay - 8, 4);
        x = g_arr[i].ax;
        y = g != NO_GROUND ? g : g_arr[i].ay;
    }
    *G_RESTARTX = x;
    *G_RESTARTY = y;
    *G_PLAYERX = x; *G_PLAYERXFIXED = x << 16;
    *G_PLAYERY = y; *G_PLAYERYFIXED = y << 16;
    *G_PLAYERVX = 0; *G_PLAYERVY = 0;
    *G_PLAYERGROUNDY = y;
    P_CAMX(*G_PCURPLANE) = x - 0xA0;                                     /* the camera */
    P_CAMY(*G_PCURPLANE) = y - *G_CAMERAPLAYEROFFSETY;
    *G_BLEVELEXITREACHED = 0;
    *G_BLEVELENDING = 0;
    *G_BINPUTLOCKED = 0;
    *(int *)(a + 0xA8) = 0;
    *(int *)(a + 0x90) = -1;     /* no "Let's go" (Trapdoor_Init set 10 ticks to it: sound 26 and Insert_Show(1)) */
    flog("arrived at (%d,%d)%s", x, y, i >= 0 ? "" : " (the Start's floor: no arrival)");
}

static void __cdecl exit_draw_none(BYTE *a) { (void)a; }

/* Leaves for the Exit's arrival (+0x88: its index in g_arr + 1, 0 = none): Level_Start (0x40F670),
   which Game_UpdateFade calls when the fade-out ends, adds 1 to g_CurSubLevel and loads that
   sub-level while g_bLevelExitReached is set, so g_CurSubLevel becomes the target's - 1 (it can be
   an earlier sub-level, a side room or the same map). Without an arrival it stays: the next map. */
static void exit_leave(BYTE *a)
{
    int i = *(int *)(a + 0x88) - 1;
    *G_BLEVELEXITREACHED = 1;
    g_arrival = i;
    if (i >= 0) *G_CURSUBLEVEL = g_arr[i].tsub - 1;
}

/* LevelExit_Update, firing also at the Exit itself: within 24 px of it, or past it when it is in
   the map's last 320 px (Flink walks into its exits). Actor: +0x80 reached, +0x84 ticks to the
   fade-out, +0x88 the arrival + 1. Lomax then walks off (Player_Update walks him on while
   g_bLevelExitReached is set) and the fade starts 30 ticks later. A door (Flink's 'look up' Exit
   point) fires as Flink's does: Up pressed (after being released since the map loaded, so that
   holding Up through an arrival next to a door doesn't go straight back), on the ground, within 16 px
   of it and 0..80 px above its y. Lomax stops (the input lock: g_bLevelEnding, which
   Actors_UpdateAll copies into g_bInputLocked), walks to the door's centre and into the door,
   shrinking (walk_start, walk_tick), the fade starts 10 ticks after he turns to it and g_bLevelExitReached is set only as it ends
   (g_FadeStep -64 -> 0), so that he doesn't walk off. The
   music fades only if the map it leads to plays another track (music.c). */
static void __cdecl exit_update(BYTE *a)
{
    int *reached = (int *)(a + 0x80), *ticks = (int *)(a + 0x84), i = *(int *)(a + 0x88) - 1;
    int door = i >= 0 && g_arr[i].door;
    if (!(*G_INPUTHELD & INPUT_UP)) g_upReleased = 1;
    if (!*reached) {
        int ex = A_X(a), ey = A_Y(a), px = *G_PLAYERX, py = *G_PLAYERY;
        int right = P_W(*G_PCURPLANE) * 16 - 0x32, fire;
        if (A_PLANE(a) != *G_PLAYERPLANE || (*G_PLAYERSTATE & 1)) return;
        if (door)
            fire = g_upReleased && (*G_INPUTHELD & INPUT_UP) && !*G_BINPUTLOCKED && !*G_BLEVELENDING &&
                   abs(px - ex) <= DOOR_RANGE && py <= ey && py >= ey - DOOR_ABOVE;
        else
            fire = abs(py - ey) < 0x30 && (px > right || abs(px - ex) < 24 || (ex > right - 320 && px > ex));
        if (!fire) return;
        *reached = 1;
        *G_BLEVELENDING = 1;
        *G_BINPUTLOCKED = 1;
        if (*G_BEXITFADESMUSIC && music_changes(i >= 0 ? g_arr[i].tsec : -1, i >= 0 ? g_arr[i].tsub : -1))
            *G_BFADEOUTMUSIC = 1;
        if (door) { walk_start(ex); walk_tick(); *ticks = DOOR_TICKS; }
        else exit_leave(a);
        flog("%s at (%d,%d) taken at (%d,%d), arrival %d%s", door ? "door" : "exit", ex, ey, px, py, i,
             *G_BFADEOUTMUSIC ? ", music fades" : "");
    } else {
        *G_BINPUTLOCKED = 1;
        if (door && g_walk && walk_tick()) return;     /* still walking to the door's centre */
        if (*ticks > 0) --*ticks;
        if (!*ticks && !*G_FADESTEP) *G_FADESTEP = -0x40;
        if (door && *G_FADESTEP < 0 && *G_FADESTEP >= -2 && !*G_BLEVELEXITREACHED) exit_leave(a);
    }
}

/* The walk off an Exit. While g_bLevelExitReached is set, Player_Update walks Lomax on and skips its
   ground search (0x410C4C: the call of Plane_FindGroundY that sets g_PlayerGroundY, 0x52C558), so he
   kept the Exit's height: Lomax's own exits are at the map's flat right end, Flink's stand anywhere,
   and past 10-1's, where the ground steps down, he ran in the air. Before
   Player_Update the ground under him is looked up as that call would (the current plane, his x, y,
   4 cells); none found, the old one stays. In Flink's maps only (doors set the flag only once the
   screen is black). */
void exit_walk_before(void)
{
    unsigned g;
    if (!g_active || !*G_BLEVELEXITREACHED || *G_BPAUSED || !*G_PCURPLANE) return;
    g = Plane_FindGroundY(*G_PCURPLANE, *G_PLAYERX, *G_PLAYERY, 4);
    if (g != NO_GROUND) *G_PLAYERGROUNDY = (int)g;
}

/* the Exit (object 2) */
void __cdecl exit_spawn(BYTE *a, BYTE *def)
{
    int i;
    LevelExit_Spawn(a, def);
    if (!g_active) return;
    A_UPDATE(a) = (void *)exit_update;
    i = find_arrival(*G_LOADEDSECTION, *G_CURSUBLEVEL, A_X(a), A_Y(a));
    *(int *)(a + 0x88) = i + 1;
    if (i >= 0 && g_arr[i].door)
        A_DRAW(a) = (void *)exit_draw_none;      /* a door: Flink's tiles draw it, no Exit sign */
}
