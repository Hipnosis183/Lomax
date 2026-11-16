/*
 * contact.c - Lomax touching an actor tells the actor (README "Enemies").
 *
 * Player_Update's contact loop (0x40FEDD) calls Player_BounceOffEnemy (0x410092) and Player_TakeHit
 * (0x41009A) for the actor Lomax is on, without telling it. Both calls go through stubs that pass the
 * actor's slot, taken from the loop's frame ([esp+0x30] there: mov eax, [esp+0x30] at 0x40FEF1, before
 * the actor's address), so that Flink's enemies are stunned by a stomp and don't hurt while stunned,
 * Flink's chests open when stomped and never hurt, a Thief steals coins where it would hurt, and
 * a closed Hand (hand.c) only bounces Lomax unless it is a deadly one. A bounce with Jump held is
 * Flink's high one (stomp.c).
 *
 * Several enemies at once (contact_hit, contact_stomped). The loop takes the actors Lomax touches one by
 * one, in slot order, and bounces him off one only if he is in the air, falling (vy > 0) and last
 * tick's feet were above its hitbox's top; any other touch hurts him. The first bounce sets his vy to
 * -4.0, so every other enemy he touches that tick hurts him: landing on a clump of 13-2's Little Dudes
 * always hurt. In Flink each enemy's think asks Actor_CheckStompedByPlayer
 * (0x1A352) on its own, all against the same player: in the air, not in the water, VelY under 2.0 up,
 * not hurt or dying, his box across its hitbox and his feet from 2 px above its top down to half its
 * height (at most 20 px); a stomp only asks for the bounce (g_PlayerBounceReq 0x8290), made the next
 * frame, so all the enemies under him are stomped and none hurts him. Here a touch that would hurt
 * gets Flink's test for a Flink enemy that can be stomped (enemy_stompable), with his vy from before
 * this tick's bounce: passed, it is stomped, and Lomax bounces off it unless he already did this tick.
 */
#include "flink.h"

#define CALL_BOUNCE        0x00410092u   /* Player_Update's contact loop: call Player_BounceOffEnemy */
#define CALL_CONTACT_HIT   0x0041009Au   /*   push ecx (the actor's x); call Player_TakeHit */
#define LOAD_SLOT          0x0040FEF1u   /*   mov eax, [esp+0x30]: the actor's slot */
static const BYTE slotOrig[4] = { 0x8B, 0x44, 0x24, 0x30 }, hitPushOrig[1] = { 0x51 };

/* The stubs pass the slot from the loop's frame: [esp+0x30] at the call, [esp+0x34] in the stub,
   [esp+0x38] after the hit's push of the actor's x; the hit's also Lomax's box ([esp+0x2C] in the
   loop: left, top, right, bottom from his x, y, which 0x40FE6D reads; not his animation's hitbox). */
void __cdecl contact_bounce(int slot) __attribute__((used));
void __cdecl contact_hit(int slot, int ex, const short *box) __attribute__((used));
void contact_bounce_stub(void);
void contact_hit_stub(void);
__asm__(".text\n"
        ".globl _contact_bounce_stub\n"
        "_contact_bounce_stub:\n"
        "  pushl 0x34(%esp)\n"
        "  call _contact_bounce\n"
        "  addl $4, %esp\n"
        "  ret\n"
        ".globl _contact_hit_stub\n"
        "_contact_hit_stub:\n"
        "  pushl 0x34(%esp)\n"
        "  pushl 8(%esp)\n"
        "  pushl 0x40(%esp)\n"
        "  call _contact_hit\n"
        "  addl $12, %esp\n"
        "  ret\n");

static int g_bounced, g_vyBefore, g_yBefore;   /* Lomax bounced this tick, his vy and y before it
                                                  (Player_BounceOffEnemy lifts him) */

/* before Player_Update (player_tick) */
void contact_before(void) { g_bounced = 0; }

/* Lomax fell onto the actor: he bounces off it, a Flink enemy is stomped, a closed chest opens */
void __cdecl contact_bounce(int slot)
{
    BYTE *a = actor_at(slot);
    if (yeti_spiked(a) || razorback_spiked(a)) { Player_TakeHit(A_X(a), 0, 0); return; }   /* a Yeti, a Razorback: no stomp (yeti.c, razorback.c) */
    if (!g_bounced) { g_bounced = 1; g_vyBefore = *G_PLAYERVY; g_yBefore = *G_PLAYERY; }
    Player_BounceOffEnemy();
    stomp_bounce();
    if (!enemy_bounce(a)) chest_bounce(a);
}

/* Flink's Actor_CheckStompedByPlayer (0x1A352) for a Flink enemy Lomax touches (the box across is the
   loop's own overlap test; his feet are his box's bottom): in the air, out of the water, vy (before
   this tick's bounce) under 2.0 up, not hurt or dying, his feet from its hitbox's top - 2 to its top +
   half its height, at most 20 px */
static int contact_stomped(BYTE *a, const short *box)
{
    const BYTE *r = A_RECORD(a);
    int vy = g_bounced ? g_vyBefore : *G_PLAYERVY, top, feet, deep;
    if (!r || !box || !enemy_stompable(a)) return 0;
    if ((*G_PLAYERSTATE & (PS_AIR | PS_WATER)) != PS_AIR || vy <= -0x20000) return 0;
    if (*G_PLAYERHURTTIMER > 0 || *G_PLAYERDEATHTIMER) return 0;
    top = A_Y(a) + HB_Y1(r);
    deep = (HB_Y2(r) - HB_Y1(r)) / 2;
    if (deep > 20) deep = 20;
    feet = (g_bounced ? g_yBefore : *G_PLAYERY) + box[3];
    return feet >= top - 2 && feet <= top + deep;
}

/* Lomax touched the actor at x ex: hurt, unless it is a stunned enemy or a chest, or a Thief steals
   his coins instead, or he stomps it (contact_stomped: Flink's stomp of several enemies at once) */
void __cdecl contact_hit(int slot, int ex, const short *box)
{
    BYTE *a = actor_at(slot);
    if (contact_stomped(a, box)) {
        if (!g_bounced) contact_bounce(slot);
        else enemy_bounce(a);
        flog("stomp: one more enemy under Lomax at (%d,%d)", A_X(a), A_Y(a));
        return;
    }
    if (enemy_harmless(a)) return;
    if (enemy_touched(a)) return;
    if (is_chest(a)) return;
    if (hand_harmless(a)) return;
    Player_TakeHit(ex, 0, 0);
}

int contact_retail(void)
{
    return is_bytes(LOAD_SLOT, slotOrig, sizeof(slotOrig)) && is_call(CALL_BOUNCE, (DWORD)Player_BounceOffEnemy) &&
           is_bytes(CALL_CONTACT_HIT - 1, hitPushOrig, sizeof(hitPushOrig)) && is_call(CALL_CONTACT_HIT, (DWORD)Player_TakeHit);
}

void contact_install(void)
{
    patch_call(CALL_BOUNCE, (const void *)contact_bounce_stub);
    patch_call(CALL_CONTACT_HIT, (const void *)contact_hit_stub);
}
