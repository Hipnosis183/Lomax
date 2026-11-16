/*
 * hand.c - Flink's Hand (3-3, 6-2): a wooden hand on an arm, a platform going up and down that closes
 * into a fist on a timer (README "Hands").
 *
 * Flink (init 0x28696 = the Mega platform's 0x286B2 plus: +0x52 / +0x54 = param 2, the period;
 * +0x5A = checkbox 4, 'deadly'; think 0x2876A, shared with the Mega platform, whose +0x52 is 0):
 * - It moves as a Mega platform: base + sin(phase) * amplitude, phase += speed a frame (params 0, 1;
 *   checkbox 2 = left/right instead of up/down), its arm (APPE) drawn down to the screen's bottom.
 * - A countdown (+0x54) from the period; at 0 it starts again from the period and the hand closes
 *   for 90 frames (+0x56): MEGA's 3 frames, open to fist, at speed 64 (a frame every 4), held on the
 *   last (+3 bit 0: no loop); at the end frame 0 again. Closing, it plays sound 0x36 when on screen
 *   and, if the player stood on it the frame before (+0x58), hurts him (Player_TakeHit 0x1A7DC).
 *   With 'deadly' it also hurts on touch while closed (+6 bit 3).
 * - While closed it is no platform (0x1A272 is skipped): the player falls off or through it; landing
 *   on the fist only plays sound 0x18 (Actor_CheckStompedByPlayer 0x1A352).
 * - Hit by a spell or a slide (+3 bit 6, from 0x1A6D2 / 0x1A514) it opens and stays open 420 frames.
 * Here: a 'Mega Hand' (Lomax's MovingPlatform, object 64, whose spawn platforms.c wraps): MAIN =
 * MEGA's 3 frames, PART = APPE (the arm, as the Mega's chain). The converter puts the period in the
 * speed of the axis it doesn't move on (amplitude 0 there), bit 15 = 'deadly'. hand_update runs
 * MovingPlatform_Update; while closed, with player state 0x20000 set around it, which makes
 * Platform_MoveAndCarryPlayer (0x413510) move it without carrying or landing Lomax. Not done: the
 * clank of a landing on the fist.
 * Lomax's way: while closed the fist is flagged hurts + hittable (AF_HURTS |
 * AF_HITTABLE), so Player_Update's contact loop bounces him off it when he falls onto it, as off an
 * enemy (Player_BounceOffEnemy: sound and poof); touching it from the side hurts only a 'deadly' one
 * (hand_harmless, from contact.c's contact_hit). His thrown helmet hitting it (+0x58 bit 0x20000, set
 * by the helmet, read by Actor_CheckHitByPlayer) opens it, and it stays open 420 frames, as Flink's
 * after a spell or slide.
 */
#include "flink.h"

#define HAND_CLOSED       90
#define HAND_FRAME_TICKS  4            /* speed 64 */
#define HAND_FRAMES       3
#define HAND_DEADLY       0x8000
#define HAND_HIT_OPEN     420          /* 0x28778 */
#define AF_STRUCK         0x20000u     /* hit by the thrown helmet */
#define SND_OPEN          SND_HIT
#define PS_NO_PLATFORMS   0x20000u     /* Platform_MoveAndCarryPlayer returns 0 at once */
#define VIEW_W            512          /* Lomax+'s widest view */
#define VIEW_H            240

/* per actor slot: its type, the period, the countdown, ticks left closed, deadly, stood on last tick */
static struct { BYTE *type; int period, t, closed, deadly, on; } g_hand[MAX_ACTORS];

void hands_reset(void) { memset(g_hand, 0, sizeof(g_hand)); }

static int hand_slot(const BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_hand[i].type && g_hand[i].type == A_TYPE(a) ? i : -1;
}

static int on_screen(const BYTE *a)
{
    const BYTE *pl = PLANE(A_PLANE(a));
    int x = A_X(a) - P_CAMX(pl), y = A_Y(a) - P_CAMY(pl);
    return x >= 0 && x < VIEW_W && y >= 0 && y < VIEW_H;
}

/* a fist that only bounces Lomax: touched from the side it doesn't hurt (contact.c) */
int hand_harmless(const BYTE *a)
{
    int i = a ? hand_slot(a) : -1;
    return i >= 0 && !g_hand[i].deadly;
}

static void hand_open(BYTE *a, int i)
{
    g_hand[i].closed = 0;
    A_FLAGS(a) &= ~(AF_HURTS | AF_HITTABLE | AF_STRUCK);
}

static void __cdecl hand_update(BYTE *a)
{
    int i = hand_slot(a), f = 0;
    if (i < 0) { MovingPlatform_Update(a); return; }
    if (g_hand[i].period && --g_hand[i].t <= 0) {                 /* closes (0x287A0) */
        g_hand[i].t = g_hand[i].period;
        g_hand[i].closed = HAND_CLOSED;
        if (on_screen(a)) flink_sound(a, 0x36);
        if (g_hand[i].on && !*G_PLAYERHURTTIMER && !*G_PLAYERDEATHTIMER) {
            flog("hand at (%d,%d) closes on Lomax", A_X(a), A_Y(a));
            Player_TakeHit(A_X(a), 0, 0);
        }
    }
    if (g_hand[i].closed && (A_FLAGS(a) & AF_STRUCK)) {             /* the helmet opens it */
        hand_open(a, i);
        g_hand[i].t = HAND_HIT_OPEN;
        Sound_PlayEffect(a, SND_OPEN);
        flog("hand at (%d,%d) opened by the helmet", A_X(a), A_Y(a));
    }
    if (g_hand[i].closed) {
        DWORD st = *G_PLAYERSTATE;
        f = (HAND_CLOSED - g_hand[i].closed) / HAND_FRAME_TICKS;
        if (f >= HAND_FRAMES) f = HAND_FRAMES - 1;
        A_FLAGS(a) |= AF_HURTS | AF_HITTABLE;
        if (!--g_hand[i].closed) hand_open(a, i);
        *G_PLAYERSTATE = st | PS_NO_PLATFORMS;
        MovingPlatform_Update(a);
        *G_PLAYERSTATE = (*G_PLAYERSTATE & ~PS_NO_PLATFORMS) | (st & PS_NO_PLATFORMS);
        g_hand[i].on = 0;
    } else {
        MovingPlatform_Update(a);
        g_hand[i].on = *G_PRIDINGPLATFORM == a;
    }
    hold_frame(a, f);
    Actor_AdvanceAnim(a);
}

/* (from mega_spawn) the period is the speed of the axis with amplitude 0 (params 0 / 2: x, 1 / 3: y) */
void hand_spawn(BYTE *a, BYTE *def, int i)
{
    int p = def_param(a, def, 0, 0) ? def_param(a, def, 3, 0) : def_param(a, def, 2, 0);
    memset(&g_hand[i], 0, sizeof(g_hand[i]));
    g_hand[i].type = A_TYPE(a);
    g_hand[i].deadly = (p & HAND_DEADLY) != 0;
    g_hand[i].period = g_hand[i].t = p & 0x7FFF;
    A_FLAGS(a) &= ~(AF_HURTS | AF_HITTABLE | AF_STRUCK);
    hold_frame(a, 0);
    A_UPDATE(a) = (void *)hand_update;
}
