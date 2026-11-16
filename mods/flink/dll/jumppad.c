/*
 * jumppad.c - Flink's Jumppads (README "Jumppads").
 *
 * Jumppad (Flink 0x2BF8E, think 0x2BFBC; no params): a 'Mega Jumppad' type (a static Mega, object 64,
 * whose update is replaced: platforms.c calls jumppad_spawn) with Flink's record JPAD: frame 0
 * squashed, 1 at rest, 2 stretched. Flink:
 * - Its standing surface (Platform_Stand 0x1A29A, the player's x within the hitbox's sides) is the
 *   hitbox top raised by JUMPPAD_RISE[frame] (0x2C0E0), so it moves with the frame.
 * - Landed on (not already standing on it), it keeps the player's falling speed and its timer starts
 *   at 48. Each frame the timer counts down and the frame is JUMPPAD_FRAMES[timer / 4] (0x2C0D4): a
 *   squash, a stretch and a wobble back to rest. While stood on, Jump does nothing (the jump boost
 *   0x8306 is cleared: Flink can't jump off it).
 * - At timer 28 (20 frames after landing) it plays its sound (0x2E) and, if he is still on it, throws
 *   him up at the landing speed (at least 4.0) * 100 / 70, at most 7.0: a short hop. With Jump held
 *   at that frame, * 100 / 50 instead, and the jump boost 0x7000, which (as for a jump) is added to
 *   his speed while Jump stays held, shrinking by 0x800 a frame: the full jump. At most 5.0 instead
 *   while carrying a heavy object (0x8304 >= 0 with +6 bit 0: grab_carrying_heavy).
 * Lomax's player physics are Flink's (gravity 0.375 px a tick, at most 6.0; a jump is 4.0 plus a hold
 * force g_JumpHoldForce that Player_Update shrinks by 0x800 and adds while Jump is held, and resets to
 * -1 'ready' when it isn't, 0x4105A0), so the numbers carry over. The throw sets what Lomax's own
 * bounce pad sets (BouncePad_Update 0x433F40: airborne | standing jump, vy, the hold force) but not
 * 0x400, which keeps the hold force going with Jump released; and, as Player_BounceOffEnemy, no
 * take-off delay. Platform_MoveAndCarryPlayer (0x413510) carries Lomax with a surface offset from
 * jumppad_surface. Lomax's bounce pad's launch sound (0x35) plays for Flink's 0x2E.
 *
 * Bouncy platform (Flink 0x2BE16, think 0x2BE6A; TREE's leaf on a stalk; params x-speed, checkbox 1
 * 'look right'): the Jumppad's throw driven by its 'JUMP' animation (29 frames at speed 182/256 a
 * frame, played once: +3 bit 0) instead of a timer. Its init starts the animation on its last frame
 * (at rest) and keeps an x speed of x-speed * 0x28F (1/100 px a frame), to the left (muls.w #-0x28F),
 * to the right and flipped (+3 bit 3) with 'look right'. Each frame, in Flink's order:
 * - on frame 7 it plays sound 0x2E and, if the player stood on it last frame, throws him as the
 *   Jumppad does (the same speeds and caps) and also sets his x speed (0x81FE) to its own: straight
 *   up for x-speed 0, else at that fixed angle;
 * - the animation advances (Actor_AdvanceAnim 0x337EA), holding on the last frame;
 * - its standing surface is the hitbox top + BOUNCY_SINK[frame] (0x2BF7C) for frames 0-7, the top
 *   itself after: the leaf sinks 10 px, springs up 15 px and the throw comes at the top;
 * - landed on (Platform_Stand 0x1A29A, not already standing), it keeps his falling speed and the
 *   animation restarts at frame 0, so the throw comes 11 frames after landing; while stood on, the
 *   jump boost is cleared (no jumping off it).
 * Frame 7 lasts one or two frames (182/256 a frame); the sound is played on its first, since Lomax's
 * Sound_PlayEffect would start the same sample again a tick later. The converter's 'Mega Bouncy' has
 * all of JUMP's frames (mirrored for 'look right'); params 5-7: the signed x speed in Flink's units,
 * JUMP's frame count and speed.
 */
#include "flink.h"

#define JUMPPAD_TICKS       48
#define JUMPPAD_LAUNCH      28
static const BYTE JUMPPAD_FRAMES[JUMPPAD_TICKS / 4] = { 1, 2, 1, 0, 1, 2, 2, 1, 0, 0, 0, 1 };
static const int JUMPPAD_RISE[3] = { -5, 0, 7 };
#define BOUNCY_LAUNCH       7
static const int BOUNCY_SINK[8] = { 0, 2, 4, 7, 10, 4, -9, -15 };
#define BOUNCY_UNIT         0x28F        /* x-speed 1 = 0.01 px a frame */

/* per actor slot: their type, timer, frame, stood on last tick, and the player's speed when he landed;
   a Bouncy platform's animation position (frame << 8 | fraction, Flink's +0x30 word), frame count and
   speed, and its x speed */
static struct { BYTE *type; int timer, frame, on, vy, bouncy, pos, frames, speed, vx; } g_jumppad[MAX_ACTORS];

void jumppads_reset(void) { memset(g_jumppad, 0, sizeof(g_jumppad)); }

static int jumppad_slot(BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_jumppad[i].type && g_jumppad[i].type == A_TYPE(a) ? i : -1;
}

static int __cdecl jumppad_surface(BYTE *a, int relx)
{
    int i = jumppad_slot(a);
    (void)relx;
    if (i < 0) return 0;
    if (g_jumppad[i].bouncy) return g_jumppad[i].frame < 8 ? BOUNCY_SINK[g_jumppad[i].frame] : 0;
    return -JUMPPAD_RISE[g_jumppad[i].frame];
}

/* the throw (0x2C000 / 0x2BE9C), shared with the Bouncy platform */
static void jumppad_launch(int i)
{
    int v = abs(g_jumppad[i].vy), div = 70, cap = grab_carrying_heavy() ? 0x50000 : 0x70000;
    if (v < 0x40000) v = 0x40000;
    *G_JUMPHOLDFORCE = 0;
    if (*G_INPUTHELD & *G_JUMPMASK) { *G_JUMPHOLDFORCE = 0x7000; div = 50; }
    v = v / div * 100;                       /* (divu.w, then mulu.w #100) */
    if (v > cap) v = cap;
    *G_PLAYERVY = -v;
    if (g_jumppad[i].bouncy) *G_PLAYERVX = g_jumppad[i].vx;
    *G_PLAYERSTATE |= 3;
    *G_JUMPTAKEOFFTIMER = 0;
    *G_BAIRSPIN = 0;
    flog("%s throws Lomax at %d.%02d%s (landed at %d.%02d), x speed %d/65536",
         g_jumppad[i].bouncy ? "bouncy platform" : "jumppad", v >> 16, (v & 0xFFFF) * 100 >> 16,
         *G_JUMPHOLDFORCE ? " with Jump held" : "", abs(g_jumppad[i].vy) >> 16, (abs(g_jumppad[i].vy) & 0xFFFF) * 100 >> 16,
         g_jumppad[i].bouncy ? g_jumppad[i].vx : 0);
}

static void __cdecl jumppad_update(BYTE *a)
{
    int i = jumppad_slot(a);
    if (i < 0) return;
    if (g_jumppad[i].timer && --g_jumppad[i].timer == JUMPPAD_LAUNCH) {
        flink_sound(a, 0x2E);
        if (g_jumppad[i].on) jumppad_launch(i);
    }
    g_jumppad[i].frame = JUMPPAD_FRAMES[g_jumppad[i].timer >> 2];
    hold_frame(a, g_jumppad[i].frame);
    Actor_AdvanceAnim(a);
    if (Platform_MoveAndCarryPlayer(a, jumppad_surface)) {
        *G_JUMPHOLDFORCE = 0;                /* no jumping off it */
        if (!g_jumppad[i].on) {
            g_jumppad[i].on = 1;
            g_jumppad[i].vy = *G_PLAYERVY;
            g_jumppad[i].timer = JUMPPAD_TICKS;
        }
    } else
        g_jumppad[i].on = 0;
}

/* Flink's Bouncy platform think (0x2BE6A) */
static void __cdecl bouncy_update(BYTE *a)
{
    int i = jumppad_slot(a), was;
    if (i < 0) return;
    was = g_jumppad[i].frame;
    if (was == BOUNCY_LAUNCH) {
        if (g_jumppad[i].pos >> 8 == BOUNCY_LAUNCH && (g_jumppad[i].pos & 0xFF) < g_jumppad[i].speed)
            flink_sound(a, 0x2E);         /* (its first frame on frame 7) */
        if (g_jumppad[i].on) jumppad_launch(i);
    }
    g_jumppad[i].pos += g_jumppad[i].speed;          /* Actor_AdvanceAnim 0x337EA, played once */
    if (g_jumppad[i].pos >> 8 >= g_jumppad[i].frames) g_jumppad[i].pos -= 0x100;
    g_jumppad[i].frame = g_jumppad[i].pos >> 8;
    hold_frame(a, g_jumppad[i].frame);
    Actor_AdvanceAnim(a);
    if (Platform_MoveAndCarryPlayer(a, jumppad_surface)) {
        *G_JUMPHOLDFORCE = 0;                        /* no jumping off it */
        if (!g_jumppad[i].on) {
            g_jumppad[i].on = 1;
            g_jumppad[i].vy = *G_PLAYERVY;
            g_jumppad[i].pos = g_jumppad[i].frame = 0;   /* (clr.w +0x30) */
            hold_frame(a, 0);
        }
    } else
        g_jumppad[i].on = 0;
}

/* Flink's Bouncy platform init (0x2BE16); params 5-7: x speed (signed, 1/100 px), frames, speed */
void bouncy_spawn(BYTE *a, BYTE *def, int i)
{
    memset(&g_jumppad[i], 0, sizeof(g_jumppad[i]));
    g_jumppad[i].type = A_TYPE(a);
    g_jumppad[i].bouncy = 1;
    g_jumppad[i].vx = (short)def_param(a, def, 5, 0) * BOUNCY_UNIT;
    g_jumppad[i].frames = def_param(a, def, 6, 1);
    g_jumppad[i].speed = def_param(a, def, 7, 0x100);
    if (g_jumppad[i].frames < 1) g_jumppad[i].frames = 1;
    g_jumppad[i].pos = (g_jumppad[i].frames - 1) << 8;   /* at rest: its last frame */
    g_jumppad[i].frame = g_jumppad[i].frames - 1;
    hold_frame(a, g_jumppad[i].frame);
    A_VX(a) = A_VY(a) = 0;
    A_UPDATE(a) = (void *)bouncy_update;
}

void jumppad_spawn(BYTE *a, int i)
{
    memset(&g_jumppad[i], 0, sizeof(g_jumppad[i]));
    g_jumppad[i].type = A_TYPE(a);
    g_jumppad[i].frame = JUMPPAD_FRAMES[0];
    A_VX(a) = A_VY(a) = 0;
    A_UPDATE(a) = (void *)jumppad_update;
}
