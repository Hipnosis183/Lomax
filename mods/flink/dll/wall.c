/*
 * wall.c - Flink's Wall platform (init 0x2897E, think 0x289BC; VILLAGE: 13-1 and others; README
 * "Wall platforms"): a brick that slides out of the wall and back on timers.
 *
 * Flink's:
 * - Init: params 'in' (+0x46) and 'out' (+0x48) frames, 'pre-run'; the countdown (+0x4A) starts at
 *   'in', the phase (+0x4C) at 0 (going in), the frame (+0x30) at 0, in the wall; then the step runs
 *   'pre-run' times (0x289A8). The record is WALL, whose frames 0..4 go from in to out.
 * - Each frame (0x289BC): at frame 3 or 4 it is a platform the player can stand on (0x1A28A: its
 *   hitbox's top, landed on from above); then the step (0x289CC): the countdown runs out -> the phase
 *   flips, the countdown 'in' frames going in or 'out' frames coming out; every 4th frame (the
 *   countdown & 3 = 0) the frame goes one toward 0 going in, toward 4 coming out.
 * Here: the converter's 'Mega Wall' (Lomax's MovingPlatform, object 64, static, whose spawn
 * platforms.c wraps), MAIN = WALL's frames 0-4, params Lomax's five and in, out, pre-run.
 * wall_update runs MovingPlatform_Update, at frames 0-2 with player state 0x20000 set around it, as
 * hand.c's closed fist: Platform_MoveAndCarryPlayer neither carries nor lands Lomax, so he falls off
 * or through it.
 */
#include "flink.h"

#define WALL_FRAMES       5
#define WALL_STANDABLE    3              /* from this frame on (0x289C2) */
#define PS_NO_PLATFORMS   0x20000u       /* Platform_MoveAndCarryPlayer returns 0 at once */

/* per actor slot: its type, the phases' frames, the countdown, the phase (1: coming out), the frame */
static struct { BYTE *type; int in, out, t, phase, frame; } g_wall[MAX_ACTORS];

void walls_reset(void) { memset(g_wall, 0, sizeof(g_wall)); }

/* 0x289CC */
static void wall_step(int i)
{
    if (--g_wall[i].t <= 0) {
        g_wall[i].phase = !g_wall[i].phase;
        g_wall[i].t = g_wall[i].phase ? g_wall[i].out : g_wall[i].in;
    }
    if (g_wall[i].t & 3) return;
    if (!g_wall[i].phase) { if (g_wall[i].frame > 0) g_wall[i].frame--; }
    else if (g_wall[i].frame < WALL_FRAMES - 1) g_wall[i].frame++;
}

static void __cdecl wall_update(BYTE *a)
{
    int i = slot_of(a);
    if (i < 0 || g_wall[i].type != A_TYPE(a)) { MovingPlatform_Update(a); return; }
    if (g_wall[i].frame >= WALL_STANDABLE)
        MovingPlatform_Update(a);
    else {
        DWORD st = *G_PLAYERSTATE;
        *G_PLAYERSTATE = st | PS_NO_PLATFORMS;
        MovingPlatform_Update(a);
        *G_PLAYERSTATE = (*G_PLAYERSTATE & ~PS_NO_PLATFORMS) | (st & PS_NO_PLATFORMS);
    }
    wall_step(i);
    hold_frame(a, g_wall[i].frame);
    Actor_AdvanceAnim(a);
}

/* (from mega_spawn) params 5-7: in, out, pre-run */
void wall_spawn(BYTE *a, BYTE *def, int i)
{
    int k;
    memset(&g_wall[i], 0, sizeof(g_wall[i]));
    g_wall[i].type = A_TYPE(a);
    g_wall[i].in = g_wall[i].t = def_param(a, def, 5, 50);
    g_wall[i].out = def_param(a, def, 6, 75);
    for (k = def_param(a, def, 7, 0); k > 0; k--) wall_step(i);
    hold_frame(a, g_wall[i].frame);
    A_UPDATE(a) = (void *)wall_update;
    flog("wall platform at (%d,%d): in %d, out %d, pre-run %d, frame %d", A_X(a), A_Y(a), g_wall[i].in, g_wall[i].out,
         def_param(a, def, 7, 0), g_wall[i].frame);
}
