/*
 * steer.c - Flink's Steer platform (README "Platforms", "Scrolling screens"): 27-1's way down the
 * tree's shaft, under the screen scrolling down (scroll.c).
 *
 * Flink (init 0x28CBC: record CONT, frame 1; a platform, 0x1A28A moves it by its speeds +0x1C / +0x20
 * and carries the player standing on it):
 * - At rest (0x28CE6) until the player stands on it; then its y speed is 0.5 px a frame (0x8000), for
 *   good: it sinks through everything.
 * - Then every frame (0x28D06):
 *   - its x speed goes toward the steering speed (+0x46) by 0x800; facing left while it is negative;
 *     a wall at its front side (0x1AD4C: the column at x +- the hitbox's right side, 29 px, its height)
 *     stops it;
 *   - the standing surface (0x1A29A) is the hitbox top, lower on the side the player stands when the
 *     last frame was a tilt: + (dx >> 3) at frame 0, - (dx >> 3) at frame 2 (dx his x - its x, an
 *     arithmetic shift: 1 to 8 px left is 1 px lower);
 *   - player on it: frame 0 when he is 8 px or more right of its x, 2 when 8 px or more left, else 1;
 *     the steering speed is (dx + 12) / 16 px a frame, (dx - 12) / 16 when dx < 0 (dx his x - its x);
 *   - player off: frame 1, the steering speed falls back to 0 by 0x100 a frame (0x1D448).
 * - The player stands on it while his x is inside the hitbox across (0x1A304: -26 .. 29 px), and it
 *   carries him: his x moves with it and his feet are put on its surface every frame.
 * - He walks on it as on flat ground (0x1C4A6 sets slope class 1 on a platform). Flink's walk (0x1BDB6,
 *   table 0x1CFE4) is at most 1.0 px a frame for the first 60 frames a direction is held (0x828F, set
 *   again while no direction is held and at each turn), then speeds up by 0x800 a frame to 2.0. The
 *   leaf's speed follows him by at most 0x800 a frame, so that slow first second is what lets the
 *   player steer it by small steps.
 * Here: a static 'Mega Steer' (Lomax's MovingPlatform, object 64, whose spawn platforms.c wraps) with
 * CONT's 3 frames as MAIN. Its hitbox is CONT's widened by the converter's PLATFORM_EDGE (6 px each
 * side, -32 .. 35 for Lomax's x), as every converted platform's; the wall test takes the 6 px off
 * again (STEER_EDGE), so that it stops where Flink's does.
 * Lomax's walk on it is Flink's (steer_before / steer_after, around Player_Update). Lomax's own walk
 * (2.4 px a tick, reached at 0.125 a tick: MOVE_SPEED) would take him off its 61 px in about 15 ticks,
 * before the leaf has picked up 0.5 px a tick. While he stands on it, g_PlayerSpeedCapOverride (the
 * walking limit, as rock.c's push) is 1.0 for the first STEER_WALK_SLOW ticks a direction is held,
 * then rises by 0x800 a tick to 2.0, Flink's limits; his acceleration and friction stay Lomax's.
 * The map's Raketen wurms (wurm.c) stay in their mud until Lomax has stood on it since the last start
 * (steer_waiting). In Flink they come out by Lomax's x alone (0x25B94), and those that come out
 * before the ride are below Flink's screen: Flink's camera is the player's y - 160 (0x872E), 224 px
 * high, Lomax's his y - 112, 240 px high, so the shaft's walls under the ledge show here (the Raketen
 * wurm at (140, 298) comes out with Lomax at x 20-80, and at 200-260 on the jump to the leaf). steer_rest / steer_move replace MovingPlatform_Update;
 * Platform_MoveAndCarryPlayer (0x413510) is Flink's 0x1A28A, with steer_surface. The wall check reads
 * Lomax's collision cells as Actor_CheckWallCollision does (a wall cell, 0x80 without 0x40, with bit
 * 0x10 for a wall met going left, 0x20 going right). Under the map it is despawned (Actor_Despawn),
 * so its placement spawns it again.
 */
#include "flink.h"

#define STEER_VY           0x8000        /* 0.5 px a frame */
#define STEER_ACCEL        0x800
#define STEER_DECAY        0x100
#define STEER_TILT         8             /* px off its x for a tilt */
#define STEER_BIAS         12
#define STEER_EDGE         6             /* the converter's PLATFORM_EDGE, off the hitbox for the wall test */
#define STEER_WALK_SLOW    60            /* Flink's 0x828F */
#define STEER_WALK_START   0x10000       /* 1.0 px a tick (row 0x1D004) */
#define STEER_WALK_MAX     0x20000       /* 2.0 (row 0x1CFC4) */
#define STEER_WALK_ACCEL   0x800         /* (row 0x1CFC4's acceleration) */
#define IN_LEFT            0x8000
#define IN_RIGHT           0x2000

/* per actor slot: its type, the steering speed and its x speed (16.16), its frame (0-2) */
static struct { BYTE *type; int target, vx, frame; } g_steer[MAX_ACTORS];

static int g_waiting;                    /* a Steer platform not stood on since the last start */
static int g_walkDir, g_slow, g_cap;     /* Lomax's walk on it: direction held, ticks left at 1.0, the limit */

void steers_reset(void) { memset(g_steer, 0, sizeof(g_steer)); g_waiting = 0; g_walkDir = 0; g_cap = 0; }

int steer_waiting(void) { return g_active && g_waiting; }

static void __cdecl steer_move(BYTE *a);
static void __cdecl steer_rest(BYTE *a);

static int steer_slot(const BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_steer[i].type && g_steer[i].type == A_TYPE(a) ? i : -1;
}

/* 0x28D5C: the surface after a tilt, + (dx >> 3) at frame 0, - (dx >> 3) at frame 2 */
static int __cdecl steer_surface(BYTE *a, int relx)
{
    int i = steer_slot(a), d = (short)relx >> 3;
    if (i < 0 || g_steer[i].frame == 1) return 0;
    return g_steer[i].frame ? -d : d;
}

/* 0x1AD4C: a wall at its front side, over its height */
static int steer_wall(const BYTE *a, int vx)
{
    const BYTE *r = A_RECORD(a), *pl = PLANE(A_PLANE(a));
    const BYTE *coll = P_COLL(pl);
    int x, y, n, h, c, side = vx < 0 ? 0x10 : 0x20;
    if (!r || !coll || !vx) return 0;
    x = A_X(a) + (vx < 0 ? -(HB_X2(r) - STEER_EDGE) : HB_X2(r) - STEER_EDGE);
    h = HB_Y1(r) < 0 ? -HB_Y1(r) : HB_Y1(r);
    if (x < 0 || x >= P_W(pl) * 16) return 0;
    for (y = A_Y(a) - 1, n = (h >> 4) + 1; n >= 0 && y >= 0; n--, y -= 16) {
        if (y >= P_H(pl) * 16) continue;
        c = coll[(x >> 4) + (y >> 4) * P_W(pl)];
        if ((c & 0xC0) == 0x80 && c & side) return 1;
    }
    return 0;
}

static int carry(BYTE *a, int vx, int vy)
{
    int on;
    A_VX(a) = vx;
    A_VY(a) = vy;
    on = Platform_MoveAndCarryPlayer(a, steer_surface);
    A_VX(a) = A_VY(a) = 0;
    return on;
}

/* ridden down (0x28D06) */
static void __cdecl steer_move(BYTE *a)
{
    int i = steer_slot(a), t, dx;
    if (i < 0) return;
    t = g_steer[i].target;
    if (g_steer[i].vx < t) g_steer[i].vx = g_steer[i].vx + STEER_ACCEL < t ? g_steer[i].vx + STEER_ACCEL : t;
    else if (g_steer[i].vx > t) g_steer[i].vx = g_steer[i].vx - STEER_ACCEL > t ? g_steer[i].vx - STEER_ACCEL : t;
    if (steer_wall(a, g_steer[i].vx)) g_steer[i].vx = 0;
    if (carry(a, g_steer[i].vx, STEER_VY)) {
        dx = *G_PLAYERX - A_X(a);
        g_steer[i].frame = dx >= STEER_TILT ? 0 : dx <= -STEER_TILT ? 2 : 1;
        g_steer[i].target = ((dx < 0 ? dx - STEER_BIAS : dx + STEER_BIAS) << 16) >> 4;
    } else {
        g_steer[i].frame = 1;
        t = g_steer[i].target;
        g_steer[i].target = t > 0 ? (t > STEER_DECAY ? t - STEER_DECAY : 0) : (t < -STEER_DECAY ? t + STEER_DECAY : 0);
    }
    hold_frame(a, g_steer[i].frame);
    Actor_AdvanceAnim(a);
    if (below_map(a)) {
        g_steer[i].type = NULL;
        Actor_Despawn(a);
    }
}

/* at rest (0x28CE6): stood on, it goes */
static void __cdecl steer_rest(BYTE *a)
{
    int i = steer_slot(a);
    if (i < 0 || !carry(a, 0, 0)) return;
    A_UPDATE(a) = (void *)steer_move;
    g_waiting = 0;
    flog("steer platform at (%d,%d) goes", A_X(a), A_Y(a));
}

/* before Player_Update (player_tick): standing on it, Flink's walking limit (0x1BDB6) */
void steer_before(void)
{
    BYTE *p = *G_PRIDINGPLATFORM;
    WORD held;
    int dir, v;
    if (!g_active || !p || steer_slot(p) < 0 || *G_PLAYERSTATE & PS_AIR) { g_walkDir = 0; return; }
    held = *G_INPUTHELD & (IN_LEFT | IN_RIGHT);
    dir = held == IN_LEFT ? -1 : held == IN_RIGHT ? 1 : 0;
    if (!dir || dir != g_walkDir) {              /* (no direction 0x1BF8C, a turn 0x1BF0A) */
        g_slow = STEER_WALK_SLOW;
        g_cap = STEER_WALK_START;
    } else if (g_slow > 0) g_slow--;
    else {
        v = abs(*G_PLAYERVX);
        if (g_cap < v) g_cap = v;
        g_cap = g_cap + STEER_WALK_ACCEL < STEER_WALK_MAX ? g_cap + STEER_WALK_ACCEL : STEER_WALK_MAX;
    }
    g_walkDir = dir;
    *G_PLAYERSPEEDCAP = g_cap;
}

/* after Player_Update: the limit off again (unless something else set its own meanwhile) */
void steer_after(void)
{
    if (g_active && g_cap && *G_PLAYERSPEEDCAP == g_cap) *G_PLAYERSPEEDCAP = 0;
}

/* 'Mega Steer' (from mega_spawn) */
void steer_spawn(BYTE *a, int i)
{
    memset(&g_steer[i], 0, sizeof(g_steer[i]));
    g_steer[i].type = A_TYPE(a);
    g_steer[i].frame = 1;
    g_waiting = 1;
    A_FX(a) = A_X(a) << 16;
    A_FY(a) = A_Y(a) << 16;
    A_VX(a) = A_VY(a) = 0;
    hold_frame(a, 1);
    Actor_AdvanceAnim(a);
    A_UPDATE(a) = (void *)steer_rest;
}
