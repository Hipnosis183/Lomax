/*
 * stomp.c - the high bounce off an enemy's head, Flink's way (README "How FLINK.DLL works", Stomp
 * bounce).
 *
 * Flink. A stomp (Actor_CheckStompedByPlayer 0x1A352, which enemies, chests and the boss call) sets
 * g_PlayerBounceReq (0x8290); the player's next frame (0x1B94E) bounces him: VelX at least 2.0 the
 * way he faces, and VelY 5.0 up, or, with Jump (C) held in g_Joypad (0x870A) that frame, a full
 * jump: Player_StartJump (0x1CE2E, no jump sound: the WithSfx entry 0x1CE22 isn't used) gives VelY
 * 4.0 up and the jump boost 0x9000 (0x6000 carrying something heavy), which keeps lifting him while
 * Jump stays held, as a jump from the ground. So holding Jump as he lands on the head (or since his
 * jump) bounces him as high as a full jump (68 px), letting go early cuts it short.
 *
 * Lomax. Player_Update's contact loop (0x40FEDD) calls Player_BounceOffEnemy (0x412D90; contact.c
 * passes it through contact_bounce): vy -4.0, g_JumpHoldForce 0, so the bounce has no hold. Here,
 * with Jump held at the bounce, stomp_bounce gives it the start force of a full jump (move.c's
 * move_jump_force: JUMP_HEIGHT, HEAVY_JUMP_HEIGHT carrying something heavy, WATER_JUMP's share under
 * water), which Player_Update takes off vy each tick while Jump is held, as for a jump from the
 * ground. Lomax's vy -4.0 is Flink's start speed for the jump. Without Jump the bounce is Flink's
 * 5.0 up (BOUNCE_VY; Lomax's is 4.0), except under water, where it stays Lomax's.
 *
 * The air spin. In Lomax a Jump press in the air (with the force under 0x9000) starts the SPIN
 * (Player_Update 0x410726: g_bAirSpin), an attack: Actor_CheckHitByPlayer (0x413E40) reads the SPIN
 * anim. Pressing Jump just before landing on an enemy, as Flink's timing asks, would spin into it
 * and hit it as an attack instead of stomping it (no bounce for the Firemonster or Thief, which die
 * of it). So a Jump press in the air that would land Lomax on a stompable actor within STOMP_WINDOW
 * ticks (stomp_ahead: his fall simulated with Player_Update's gravity, against the hitboxes of the
 * actors the contact loop bounces him off) doesn't spin: it is taken out of g_InputPressed (as
 * move.c's strokes do) and, with Jump held, the bounce is the high one. If no bounce comes by
 * STOMP_GRACE ticks after the predicted landing (the enemy moved away), the press is given back to
 * Player_Update and he spins then.
 * A press earlier than that is Lomax's spin as before. Not under water, where move.c's strokes take
 * Jump presses.
 */
#include "flink.h"

#define STOMP_WINDOW       10            /* ticks before the landing in which a Jump press is held back */
#define STOMP_GRACE        3             /* ticks past the predicted landing before it is given back (the
                                            contact loop bounces him a tick or two after his feet reach
                                            the head: seen in 1-1) */
#define BOUNCE_VY          0x50000       /* Flink's plain bounce (0x1B9B0: VelY 5.0) */
#define GRAVITY            0x6000        /* Player_Update's, and the fastest fall */
#define FALL_MAX           0x60000

static int g_heldBack;                   /* ticks left for the Jump press held back, 0 none */

void stomp_reset(void) { g_heldBack = 0; }

/* the contact loop bounces Lomax off it (0x40FEDD: flagged hurts and hittable, on his plane) */
static int stompable(const BYTE *a, int slot)
{
    return actor_active(slot) && A_TYPE(a) && A_LAYER(a) != KILLED_LAYER && A_PLANE(a) == *G_PLAYERPLANE &&
           (A_FLAGS(a) & (AF_HURTS | AF_HITTABLE)) == (AF_HURTS | AF_HITTABLE) && !(A_FLAGS(a) & AF_HIDDEN);
}

/* Lomax, falling as he does now, lands on a stompable actor's top within STOMP_WINDOW ticks: the
   ticks until then (1 = next tick), 0 if not */
static int stomp_ahead(void)
{
    int px1, py1, px2, py2, k, best = 0;
    if (!actor_box(G_PLAYERACTOR, &px1, &py1, &px2, &py2)) return 0;
    for (k = 0; k < MAX_ACTORS; k++) {
        BYTE *a = actor_at(k);
        int x1, y1, x2, y2, t, fy = *G_PLAYERYFIXED, fx = 0, vy = *G_PLAYERVY, feet = py2;
        if (!stompable(a, k) || !actor_box(a, &x1, &y1, &x2, &y2)) continue;
        if (feet > y1) continue;                     /* already lower than its top */
        for (t = 0; t < STOMP_WINDOW; t++) {
            int nfeet;
            vy += GRAVITY;
            if (vy > FALL_MAX) vy = FALL_MAX;
            fy += vy;
            fx += *G_PLAYERVX;
            nfeet = py2 + ((fy - *G_PLAYERYFIXED) >> 16);
            if (vy > 0 && feet <= y1 && nfeet >= y1 &&
                px1 + (fx >> 16) <= x2 && px2 + (fx >> 16) >= x1) {
                if (!best || t + 1 < best) best = t + 1;
                break;
            }
            feet = nfeet;
        }
    }
    return best;
}

/* before Player_Update (player_tick, grab.c), after move_before */
void stomp_before(void)
{
    WORD jump = (WORD)*G_JUMPMASK;
    int free, t;
    if (!g_active || *G_BPAUSED || *G_PLAYERSTATE & PS_FROZEN) return;
    free = *G_PLAYERSTATE & PS_AIR && !(*G_PLAYERSTATE & (PS_ROPE | PS_FLY)) && !*G_JUMPTAKEOFFTIMER &&
           !*G_PLAYERDEATHTIMER && *G_PLAYERHURTTIMER <= 0 && !*G_BINPUTLOCKED && !move_under_water();
    if (!free) { g_heldBack = 0; return; }
    if (g_heldBack && !--g_heldBack) {               /* no bounce came: the spin after all */
        *G_INPUTPRESSED |= jump;
        flog("stomp: no bounce, the held-back Jump press spins");
        return;
    }
    if (*G_INPUTPRESSED & jump && !g_heldBack && (t = stomp_ahead()) != 0) {
        *G_INPUTPRESSED &= (WORD)~jump;
        g_heldBack = t + STOMP_GRACE;
        flog("stomp: Jump pressed %d ticks before landing on a head, no spin", t);
    }
}

/* Lomax bounced off an actor's head (contact_bounce, after Player_BounceOffEnemy): with Jump held, a
   full jump from there (Flink's 0x1B9C6) */
void stomp_bounce(void)
{
    int held;
    if (!g_active) return;
    held = (*G_INPUTHELD & *G_JUMPMASK) != 0;
    if (held) *G_JUMPHOLDFORCE = move_jump_force();
    else if (!move_under_water()) *G_PLAYERVY = -BOUNCE_VY;   /* Lomax's is 4.0 */
    flog("stomp bounce at (%d,%d)%s%s", *G_PLAYERX, *G_PLAYERY, held ? ": Jump held, full jump" : "",
         g_heldBack ? " (pressed in the window)" : "");
    g_heldBack = 0;
}
