/*
 * enemy.c - Flink's enemies on Lomax's 'Muta' (object 74, Enemy_ZombieRunner_Spawn 0x42E1B0, whose
 * spawn is wrapped: enemy_spawn), and what they share (README "Enemies").
 *
 * The converter makes each a 'Muta <name> n' type with Flink's records and hitboxes. Flink's enemies
 * run at 60 frames a second, as Lomax's ticks. Lomax actor fields: game.h.
 *
 * Lomax's stomp. Player_Update's contact loop (0x40FEDD) bounces Lomax off an actor flagged 0x20
 * (hurts) | 0x10000 (hittable) when he falls onto its hitbox from above (Player_BounceOffEnemy), and
 * otherwise hurts him (Player_TakeHit), but the actor isn't told. contact.c passes the actor to
 * enemy_bounce and enemy_harmless: the bounce is Flink's Actor_CheckStompedByPlayer (0x1A352)
 * succeeding (enemy_stomped), and a stunned or dying enemy doesn't hurt. Lomax's attacks (spin,
 * tumble, the thrown helmet: Actor_CheckHitByPlayer) kill the one-hit enemies (Firemonster, Thief, Visje) as
 * Flink's slide and spells do, but count as a stomp on the two-hit ones (Forest Dummy, Ingredient
 * Holder), as Lomax's zombies take two hits of any kind (enemy_attacked). The boss (forestboss.c) takes
 * stomps, attacks and thrown objects as hits.
 */
#include "enemy.h"

Enemy g_enemy[MAX_ACTORS];

void enemies_reset(void) { memset(g_enemy, 0, sizeof(g_enemy)); }

int enemy_slot(BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_enemy[i].type && g_enemy[i].type == A_TYPE(a) ? i : -1;
}

/* a wall cell (0x80 + a side bit: 0x20 met moving right, 0x10 moving left) at x in the n cells up from
   the feet */
static int wall_in(const BYTE *a, int x, int right, int n)
{
    const BYTE *pl = PLANE(A_PLANE(a)), *coll = P_COLL(pl);
    int w = P_W(pl), h = P_H(pl), y = A_Y(a) - 1, k;
    if (P_EMPTY(pl) || !coll || x < 0 || x >= w * 16) return 0;
    for (k = 0; k < n && y >= 16; k++, y -= 16) {
        BYTE b;
        if (y >= h * 16) continue;
        b = coll[(x >> 4) + (y >> 4) * w];
        if ((b & 0xC0) == 0x80 && b & (right ? 0x20 : 0x10)) return 1;
    }
    return 0;
}

/* in the 3 cells up from the feet, as Actor_CheckWallCollision (0x412F50) tests it, without its side
   effects */
int wall_at(const BYTE *a, int x, int right) { return wall_in(a, x, right, 3); }

/* Flink's 0x1AD4C: at x + its hitbox's right (- it facing left), in |its hitbox's top| / 16 + 1 cells up
   from the feet (its loop enters at the dbra) */
int flink_wall_ahead(const BYTE *a, int right)
{
    const BYTE *rec = A_RECORD(a);
    int top;
    if (!rec) return 0;
    top = HB_Y1(rec) < 0 ? -HB_Y1(rec) : HB_Y1(rec);
    return wall_in(a, A_X(a) + (right ? HB_X2(rec) : -HB_X2(rec)), right, (top >> 4) + 1);
}

/* Actor_FallAndLand (0x1B0E2): gravity 0.25 px a frame, at most 4.0, vx braked by friction; then
   (falling, with land) the ground under it, within 3..7 px; 1 = landed */
int fall_and_land(BYTE *a, int friction, int land) { return fall_and_land_g(a, 0x4000, friction, land); }

/* the same at another gravity (the Little Dude's 0.125: cyclindude.c) */
int fall_and_land_g(BYTE *a, int gravity, int friction, int land)
{
    int *vx = &A_VX(a), *vy = &A_VY(a), g, y;
    *vy += gravity;
    if (*vy > 0x40000) *vy = 0x40000;
    A_FY(a) += *vy;
    A_FX(a) += *vx;
    *vx = *vx > 0 ? (*vx > friction ? *vx - friction : 0) : (*vx < -friction ? *vx + friction : 0);
    set_pos(a);
    if (!land || *vy < 0) return 0;
    y = A_Y(a);
    g = ground_y(a, A_X(a), y);
    if (g == NO_GROUND || y < g || y - g > 3 + (*vy >> 16)) return 0;
    place_y(a, g);
    *vy = 0;
    return 1;
}

/* Actor_Walk (0x1D0C4) as the Forest Dummy uses it (flags 0x1000, speed 0.5: no jumps, turns at
   ledges) and the Firemonster (flags 0x9800, speed 0x5555: bit 15 makes it turn on a 45 degree slope
   either way, and fall where the ground under it ends instead of turning back). It speeds up by
   0x1000 a frame. A wall at x +-8 turns it at once. With no ground ahead (none within 4 cells below
   x +-8) it stops and turns: it never walks off a ledge. Snapped to the ground under it (within 3 px
   at its speed, 0x1AE96); with none there it turns back. Going down a 45 degree slope the dummy
   slides (SL45, up to 1.5 px at 0x400 a frame); it won't walk up one. */
void enemy_walk(BYTE *a, int i)
{
    static const int SNAP[8] = { 3, 4, 6, 7, 10, 10, 10, 10 };   /* 0x1AEDA, by speed in whole px */
    DWORD *fl = &A_FLAGS(a);
    int *vx = &A_VX(a), x = A_X(a), y = A_Y(a);
    int target = g_enemy[i].fire ? 0x5555 : g_enemy[i].city || g_enemy[i].moun ? g_enemy[i].speed : g_enemy[i].cycl ? 0x10000 : 0x8000, step = 0x1000, speed, g, downhill;
    if (ground_y(a, *fl & AF_LEFT ? x - 8 : x + 8, y) == NO_GROUND) g_enemy[i].turning = 1;   /* a ledge ahead */
    if (wall_at(a, *fl & AF_LEFT ? x - 8 : x + 8, !(*fl & AF_LEFT))) { *fl ^= AF_LEFT; g_enemy[i].turning = 0; }
    if (g_enemy[i].slide) { target = 0x18000; step = 0x400; }
    g_enemy[i].slide = 0;
    speed = abs(*vx);
    if (g_enemy[i].turning) {                /* stops, then turns (divu.w #6 of the speed: the remainder
                                                in the high word stops it at once unless it divides) */
        step = speed < 6 * 0x10000 ? (speed % 6) << 16 | speed / 6 : speed;
        target = 0;
        if (!*vx) { g_enemy[i].turning = 0; *fl ^= AF_LEFT; }
    }
    if (speed < target) speed = speed + step > target ? target : speed + step;
    else if (speed > target) speed = speed - step < target ? target : speed - step;
    *vx = *fl & AF_LEFT ? -speed : speed;
    A_FX(a) += *vx;
    set_pos(a);
    x = A_X(a);
    g = ground_y(a, x, y);
    if (g == NO_GROUND || abs(g - y) > SNAP[speed >> 16 < 7 ? speed >> 16 : 7]) {   /* no ground: back */
        if (g_enemy[i].fire) { g_enemy[i].air = 1; return; }                     /* (or falls) */
        *fl ^= AF_LEFT;
        g_enemy[i].turning = 0;
        return;
    }
    place_y(a, g);
    A_VY(a) = 0;
    if (*G_GROUNDSLOPECLASS == 2) {          /* 45 degrees: slides down, won't go up */
        downhill = *fl & AF_LEFT ? *G_GROUNDMIRRORED : !*G_GROUNDMIRRORED;
        if (downhill && !g_enemy[i].fire) { g_enemy[i].slide = 1; set_anim(a, "SL45"); return; }
        *fl ^= AF_LEFT;
        g_enemy[i].turning = 0;
        return;
    }
    set_anim(a, g_enemy[i].charge ? "ATCK" : "WALK");   /* (the City dummy's charge: city.c) */
}

/* Hud_Draw: mov [esi+0x3C], 0x82, the lemming icon's x (Lomax+ moves it for widescreen) */
#define HUD_LEMMING_ICON   0x0042A5E8u
static const BYTE lemmingIconMov[3] = { 0xC7, 0x46, 0x3C };

/* A 1UP for 100 defeated enemies, as Item_UpdateCoinOrHelmet (0x42C809) gives one for 100 coins: the
   'Items' 1UP flies along the HUD from the count (here the lemming icon, there the coin's) to the
   lives, which Item_UpdateCoinLifeToHud adds the life to on arrival (g_bPending1Up). One already on
   its way (a coin's): the life is added at once instead, the flag being shared. */
static void enemy_1up(BYTE *a)
{
    BYTE *u;
    if (*G_BPENDING1UP) { if (*G_LIVES < 30) ++*G_LIVES; return; }
    *G_BPENDING1UP = 1;
    if (!(u = Actor_Alloc(a, S_ITEMS))) return;
    A_UPDATE(u) = (void *)Item_UpdateCoinLifeToHud;
    A_DRAW(u) = (void *)Item_DrawWithSparkles;
    A_LAYER(u) = -1;
    A_FLAGS(u) |= AF_SCREEN;
    Actor_SetAnim(u, FOURCC("TAIL"));
    Sparkles_Init(u, u + 0x90, 4);
    A_VY(u) = 0x3800;
    A_VX(u) = 0x8000;
    A_X(u) = is_bytes(HUD_LEMMING_ICON, lemmingIconMov, sizeof(lemmingIconMov)) ? *(int *)(HUD_LEMMING_ICON + 3) : 0x82;
    A_Y(u) = 0x28;
    A_FX(u) = A_X(u) << 16;
    A_FY(u) = A_Y(u) << 16;
    A_CULL(u) = CULL_NEVER;
    Actor_SetAnim(u, FOURCC("UP  "));
    *(int *)(u + 0x88) = 1;
}

/* What Lomax's Zombie_FreeLemming (0x42CD00) does for a zombie, but the freed lemming: the HUD's
   count goes up and flashes for 60 ticks; and a coin when its x is odd, thrown as Zombie_FreeLemming
   throws it. Flink's enemies drop nothing. Unlike a zombie's, 50 don't open the bonus trapdoor
   (Zombie_FreeLemming: at 50 back to 0 and g_bBonusTrapdoorOpen = 1): the count works as the coins
   do, 100 give a 1UP and it starts again from 0. And a spirit flies out of the
   enemy (spirits.c; in Flink only the boss and the spells set them free). */
BYTE *enemy_defeated(BYTE *a)
{
    BYTE *coin;
    int x1, y1, x2, y2;
    if (++*G_LEMMINGSFREED >= 100) {
        *G_LEMMINGSFREED = 0;
        enemy_1up(a);
        flog("100 enemies defeated: a 1UP");
    }
    *G_HUDLEMMINGFLASH = 60;
    if (actor_box(a, &x1, &y1, &x2, &y2)) spirit_release(a, (x1 + x2) / 2, (y1 + y2) / 2);
    else spirit_release(a, A_X(a), A_Y(a) - 16);
    coin = A_X(a) & 1 ? Item_SpawnCoin(a) : NULL;
    if (coin) {
        int vx = *G_PLAYERVX / 2;
        vx += vx < 0 ? -0x8000 : 0x8000;
        A_VX(coin) = vx * 2 / 3;
        A_VY(coin) = -0x60000;
    }
    return coin;
}

/* Flink's Enemy_Die (0x1B12E): DEAD, thrown up at 4.0 px (knock: and away from Lomax), falls through
   everything (dummy_update) until off the screen, and never comes back (Actor_ForgetForever) */
void enemy_die(BYTE *a, int i, int knock)
{
    BYTE *coin;
    g_enemy[i].dead = 1;
    g_enemy[i].timer = 0;
    forget_placement(A_DEF(a));              /* stays dead (gone.c) */
    A_FLAGS(a) &= ~AF_CONTACT;               /* no longer hurts or is hit */
    A_CULL(a) = CULL_NEVER;                  /* not despawned (it would come back) while it falls away */
    A_VY(a) = -0x40000;
    if (knock) A_VX(a) = *G_PLAYERX < A_X(a) ? 0x10000 : -0x10000;
    set_anim(a, "DEAD");
    if (g_enemy[i].city) city_lance(a);      /* its lance flies off (0x2FDE2) */
    if (g_enemy[i].moun) moun_shield_off(a); /* its shield flies off (0x2FFF2) */
    flink_sound(a, 0x22);
    coin = enemy_defeated(a);
    flog("enemy at (%d,%d) dies%s", A_X(a), A_Y(a), coin ? ", drops a coin" : "");
}

/* after enemy_die: falls through everything until off the screen, then is removed */
void enemy_dead_update(BYTE *a, int i)
{
    fall_and_land(a, 0x2AA, 0);
    Actor_AdvanceAnim(a);
    if (++g_enemy[i].timer > 150 || below_map(a)) {
        g_enemy[i].type = NULL;
        Actor_Kill(a);
    }
}

/* For the two-hit enemies (dummy_update, holder_update): Lomax's attack hit it (Actor_CheckHitByPlayer 0x413E40: his SPIN or TUMB anim overlapping its
   hitbox, every tick it overlaps, or the thrown helmet's hit, flag 0x20000, which the call clears).
   A spin passing through would hit on several ticks in a row, so only the first tick of an overlap
   counts, and none in the GUARD ticks after a first hit (stun or scare): Enemy_ZombieRunner_Update
   (0x42E220) clears the zombie's hittable flag for 90 ticks after its first hit for the same
   reason. Here the enemy stays hittable so that a stomp still kills it at once, as in Flink.
   Returns 2 (kills at once, guard or not) when Lomax hit it rolling down a steep slope: his TUMB anim,
   which Player_Update sets (0x411C5A) while the slope-slide state bit 8 is on (set at 0x41078A), as
   Flink's slide kills these enemies in one hit. The helmet's hit (flag 0x20000) is never a roll. */
#define GUARD 30
int enemy_attacked(BYTE *a, int i)
{
    int helmet = (A_FLAGS(a) & 0x20000) != 0;
    int hit = Actor_CheckHitByPlayer(a) != 0, first = hit && !g_enemy[i].hit;
    const BYTE *pa = *G_PLAYERANIM;
    g_enemy[i].hit = hit;
    if (hit && !helmet && pa && *(const DWORD *)(pa + 4) == FOURCC("TUMB")) return 2;
    if (g_enemy[i].guard > 0) { g_enemy[i].guard--; return 0; }
    if (first) g_enemy[i].guard = GUARD;
    return first;
}

/* ---- Lomax's attacks on the bosses ------------------------------------------------------------------
   The bosses that run on their own (villageboss.c, metaboss.c, bos1.c) have no contact flags, so Lomax's
   contact loop and Actor_CheckHitByPlayer (which tests only actors flagged hittable, 0x10000) never
   reach them. These are the same tests on a box of their own. */

/* Lomax's thrown helmet, if one is out (the actor ThrownHelmet_Update runs) */
BYTE *thrown_helmet(void)
{
    int k;
    for (k = 0; k < MAX_ACTORS; k++) {
        BYTE *h = actor_at(k);
        if (A_TYPE(h) && (DWORD)A_UPDATE(h) == ThrownHelmet_Update) return h;
    }
    return NULL;
}

/* Lomax's spin or roll (Actor_CheckHitByPlayer 0x413E40: his SPIN or TUMB anim's hitbox across the
   box, on its plane, no plane change running) or his thrown helmet (ThrownHelmet_Update 0x42F660: in
   EINS, flying out, or DREI, its box across, Lomax alive) reaches the box x1..y2 (inclusive). The
   helmet flying out then turns back, as there after Actor_HitOverlappingActors: ZWEI, vy 3.0 up, vx 0,
   homing (+0x80 1, +0x84 0, +0x68 0x80) and a poof 20 px above it. Every tick it overlaps counts:
   the caller guards. */
int player_attack_box(int plane, int x1, int y1, int x2, int y2)
{
    const BYTE *pa = *G_PLAYERANIM;
    BYTE *h;
    if (pa && plane == *G_PLAYERPLANE && !*G_PLANETRANSITIONSCALE &&
        (*(const DWORD *)(pa + 4) == FOURCC("SPIN") || *(const DWORD *)(pa + 4) == FOURCC("TUMB")) &&
        x1 <= *G_PLAYERX + HB_X2(pa) && *G_PLAYERX + HB_X1(pa) <= x2 &&
        y1 <= *G_PLAYERY + HB_Y2(pa) && *G_PLAYERY + HB_Y1(pa) <= y2)
        return 1;
    if (!*G_PLAYERDEATHTIMER && (h = thrown_helmet()) && A_PLANE(h) == plane && *(int *)(h + 0x80) != -1) {
        const BYTE *r = A_RECORD(h);
        DWORD tag = r ? *(const DWORD *)(r + 4) : 0;
        int hx1, hy1, hx2, hy2;
        if ((tag == FOURCC("EINS") || tag == FOURCC("DREI")) && actor_box(h, &hx1, &hy1, &hx2, &hy2) &&
            hx1 <= x2 && x1 <= hx2 && hy1 <= y2 && y1 <= hy2) {
            if (tag == FOURCC("EINS")) {
                Actor_SetAnim(h, FOURCC("ZWEI"));
                A_VY(h) = -0x30000;
                A_VX(h) = 0;
                *(int *)(h + 0x80) = 1;
                *(int *)(h + 0x84) = 0;
                *(short *)(h + 0x68) = 0x80;
                Effect_SpawnPoof(A_X(h), A_Y(h) - 20);
            }
            return 1;
        }
    }
    return 0;
}

/* Flink's Actor_CheckStompedByPlayer (0x1A352) on the box x1..y2: Lomax in the air, out of the water,
   vy under 2.0 up, not hurt or dying, across the box, his feet from its top - 2 to its top + half its
   height (at most 20). The caller bounces him (Player_BounceOffEnemy, stomp_bounce). */
int flink_stomp_box(int plane, int x1, int y1, int x2, int y2)
{
    const BYTE *pa = *G_PLAYERANIM;
    int px = *G_PLAYERX, py = *G_PLAYERY, deep;
    if (!pa || plane != *G_PLAYERPLANE) return 0;
    if ((*G_PLAYERSTATE & (PS_AIR | PS_WATER | PS_ROPE | PS_FLY)) != PS_AIR || *G_PLAYERVY <= -0x20000) return 0;
    if (*G_PLAYERDEATHTIMER || *G_PLAYERHURTTIMER > 0) return 0;
    if (x1 > px + HB_X2(pa) || x2 < px + HB_X1(pa)) return 0;
    deep = (y2 - y1) / 2;
    if (deep > 20) deep = 20;
    return py >= y1 - 2 && py <= y1 + deep;
}

/* Lomax bounced off it: Flink's Actor_CheckStompedByPlayer succeeded */
static void enemy_stomped(BYTE *a, int i)
{
    if (g_enemy[i].dead) return;
    if (g_enemy[i].boss) boss_stomped(a, i);
    else if (g_enemy[i].fire) fire_die(a, i);
    else if (g_enemy[i].holder) holder_stomped(a, i, 0);
    else if (g_enemy[i].thief) thief_die(a, i, 0);
    else if (g_enemy[i].visje) enemy_die(a, i, 0);
    else if (g_enemy[i].statue) statue_stomped(a);
    else if (g_enemy[i].wurm) wurm_stomped(a);
    else if (g_enemy[i].cycl) cycl_stomped(a, i);
    else if (g_enemy[i].yeti) enemy_die(a, i, 0);   /* (not spinning: contact.c) */
    else if (g_enemy[i].buzzard) buzzard_die(a, i);
    else if (g_enemy[i].worm) worm_stomped(a);
    else if (g_enemy[i].meta) meta_stomped(a, i, 0);
    else if (g_enemy[i].storm) storm_die(a, i);
    else if (g_enemy[i].blitz) blitz_die(a, i, 0);
    else if (g_enemy[i].circ) enemy_die(a, i, 0);
    else dummy_stomped(a, i, 0);          /* (the Little Dude too) */
}

int enemy_bounce(BYTE *a)
{
    int i = a ? enemy_slot(a) : -1;
    if (i < 0) return 0;
    enemy_stomped(a, i);
    return 1;
}

/* a Flink enemy whose think asks Actor_CheckStompedByPlayer (0x1A352): stompable (not the Cyclin' Dude),
   alive, not one with a stomp of its own (the boss's head, the Raketen wurm's), not a Yeti (yeti.c); a
   stunned one too (stomped again it dies) */
int enemy_stompable(BYTE *a)
{
    int i = a ? enemy_slot(a) : -1;
    return i >= 0 && A_FLAGS(a) & AF_HITTABLE && !g_enemy[i].dead && !g_enemy[i].boss && !g_enemy[i].wurm && !yeti_spiked(a) && !g_enemy[i].razo;
}

int enemy_touched(BYTE *a)
{
    int i = a ? enemy_slot(a) : -1;
    return i >= 0 && g_enemy[i].thief && thief_touched(a, i);
}

int enemy_harmless(BYTE *a)
{
    int i = a ? enemy_slot(a) : -1;
    return i >= 0 && (g_enemy[i].timer > 0 || g_enemy[i].dead || g_enemy[i].statue);
}

/* ---- Grabbing and throwing (grab.c; README "Grabbing and throwing") ----------------------------- */

static int is_dummy(int i) { return !g_enemy[i].donk && !g_enemy[i].fire && !g_enemy[i].holder && !g_enemy[i].thief && !g_enemy[i].visje && !g_enemy[i].statue && !g_enemy[i].wurm && !g_enemy[i].puge && !g_enemy[i].boss && !g_enemy[i].cycl && !g_enemy[i].buzzard && !g_enemy[i].razo && !g_enemy[i].worm && !g_enemy[i].storm && !g_enemy[i].blitz && !g_enemy[i].circ; }

/* Flink's stunned enemies call Actor_CheckPickedUp from their shared stun routine (0x1D074): the
   Forest Dummy (and the Meta dummy, metadummy.c) can be picked up while stunned. It is light (+6 bit 0 clear) and its stun timer (+0x38)
   is the fuse: it counts on while carried, and at 0 it jumps out of Lomax's hands and hurts him
   (0x1B66C). Held 6 px forward and 10 px down from the hand (its init, 0x2F95C). */
int enemy_grab_kind(BYTE *a, GrabKind *k)
{
    int i = enemy_slot(a);
    if (i < 0 || !is_dummy(i) || g_enemy[i].dead || g_enemy[i].timer <= 0) return 0;
    memset(k, 0, sizeof(*k));
    k->fuse = &g_enemy[i].timer;
    k->dx = 6;
    k->dy = 10;
    if (g_enemy[i].litt) {                   /* the Little Dude: small (+6 bit 1), held at +4, +4 (0x309D2) */
        k->small = 1;
        k->dx = k->dy = 4;
    }
    if (g_enemy[i].intr) {                   /* the Intree dummy: small, held at +6, +6 (0x2FA34) */
        k->small = 1;
        k->dy = 6;
    }
    if (g_enemy[i].bos3) {                   /* the Bos3 dude: heavy (+6 bit 0), held at +0, +4 (0x21FC4) */
        k->heavy = 1;
        k->dx = 0;
        k->dy = 4;
    }
    return 1;
}

/* Carried or thrown, a stunned enemy keeps animating as in Flink, where the actor loop steps every
   actor's frames whatever its think: stun_frame picks this tick's frame (the bubbles or the wake-up
   lines) and holds it at speed 0, and Actor_AdvanceAnim then puts it on the drawn entries, as
   dummy_update does. Without that call the drawn frame stayed the one it was picked up in. */
void enemy_grab_frame(BYTE *a)
{
    int i = enemy_slot(a);
    if (i >= 0 && is_dummy(i) && g_enemy[i].timer > 0) stun_frame(a, i);
    Actor_AdvanceAnim(a);
}

/* Actor_ThrownThink (0x323C2) gives a landed object its own think back (0x32532): a dummy whose stun
   ran out meanwhile walks on once it lands (dummy_update's hop) */
void enemy_grab_resume(BYTE *a)
{
    int i = enemy_slot(a);
    if (i < 0) return;
    g_enemy[i].turning = g_enemy[i].slide = 0;
    if (g_enemy[i].timer <= 0) { g_enemy[i].timer = 0; g_enemy[i].hop = 1; }
}

/* Thrown as a shot (0x3317C) the enemy becomes Flink's spinning Shot and its map entry is forgotten:
   it is out of the game. Here it counts as defeated then (the HUD's count, maybe a coin). */
void enemy_grab_shot(BYTE *a)
{
    int i = enemy_slot(a);
    if (i < 0) return;
    forget_placement(A_DEF(a));
    enemy_defeated(a);
    g_enemy[i].type = NULL;                  /* no longer an enemy */
    flog("enemy at (%d,%d) thrown as a shot", A_X(a), A_Y(a));
}

/* A thrown enemy that hits Lomax or another enemy breaks (0x325DE: Enemy_Die) */
void enemy_grab_break(BYTE *a)
{
    int i = enemy_slot(a);
    if (i < 0 || g_enemy[i].dead) return;
    g_enemy[i].city = g_enemy[i].moun = 0;   /* (Enemy_Die, not the City dummy's 0x2FDE2 nor the
                                                Mountain dummy's 0x2FFF2: no lance, no shield) */
    enemy_die(a, i, 0);
}

/* A thrown object touching an enemy (Actor_FindTouchingActor 0x1A5A0, for actors with +6 bit 3, the
   stompable ones) sets its hit flag (+3 bit 6), and every Flink enemy's think then dies at once:
   the dummy (0x2F9C8), the Ingredient Holder even on its first hit (0x2F6A4), the Thief (0x2F90A),
   the Visje (0x2817E), the Firemonster shatters (0x2F36A). Here an enemy Lomax's stomp reaches
   (AF_HITTABLE: not a deadly Visje, nor a blinking holder). */
int enemy_struck(BYTE *a, int small)
{
    int i = a ? enemy_slot(a) : -1;
    if (i < 0 || g_enemy[i].dead || g_enemy[i].wurm || g_enemy[i].razo || (!(A_FLAGS(a) & AF_HITTABLE) && !g_enemy[i].cycl)) return 0;   /* (a Raketen wurm: wurm_struck; a Razorback can't be killed: razorback.c) */
    flog("enemy at (%d,%d) hit by a thrown object", A_X(a), A_Y(a));
    if (g_enemy[i].boss) boss_struck(a, i);
    else if (g_enemy[i].cycl) cycl_struck(a, i, small);
    else if (g_enemy[i].fire) fire_die(a, i);
    else if (g_enemy[i].statue) statue_die(a, i);
    else if (g_enemy[i].thief) thief_die(a, i, 1);
    else if (g_enemy[i].buzzard) buzzard_die(a, i);
    else if (g_enemy[i].storm) storm_die(a, i);
    else if (g_enemy[i].blitz) blitz_die(a, i, 1);
    else if (g_enemy[i].worm) worm_kill(a, i);   /* (breaks up: worm.c) */
    else enemy_die(a, i, 1);
    return 1;
}

/* Flink's window 0 (0x32AC2, sizes 0x44148): Flink starts a placement's actor once its point is within
   its camera - (64, 48) .. + (448, 352), the 320 x 224 screen with 64 px on the sides, 48 above and
   80 below. Flink's camera x is the player's x - 0xA4 (0x34D52, Camera_Update 0x19E12) within the map,
   unless a Scroll Point moves or holds it. Lomax spawns placements up to 80..340 px off its own screen
   (Objects_SpawnNearCamera, the margins at 0x464540 by spawn phase), all those in reach at once when a
   map starts, and Lomax+'s view is wider than Flink's: 3-2's three Visje swam in a bunch, jumping
   together. A Flink enemy spawned by proximity outside Flink's window (Flink's camera x as above, or
   the scrolling screen's; the y Lomax's camera's) waits, still, until it is in it: hidden and harmless
   while off Lomax's screen (the plane's width - nMaxScrollX wide, as Lomax+ widens it), seen (and
   touchable) while on it. */
#define FLINK_CAM_DX       0xA4
#define FLINK_VIEW_W       320
#define WIN_SIDE           64
#define WIN_ABOVE          48
#define WIN_BELOW          80
#define VIEW_H             240
#define SEEN_MARGIN        32            /* a sprite's half-width, about */

int flink_cam_x(const BYTE *pl)
{
    int maxx = P_W(pl) * 16 - FLINK_VIEW_W, fc;
    if (scroll_owns_x()) return P_CAMX(pl);
    fc = *G_PLAYERX - FLINK_CAM_DX;
    if (fc > maxx) fc = maxx;
    return fc < 0 ? 0 : fc;
}

static int in_flink_window(const BYTE *a)
{
    const BYTE *pl = PLANE(A_PLANE(a));
    int x = A_X(a) - flink_cam_x(pl), y = A_Y(a) - P_CAMY(pl);
    return x >= -WIN_SIDE && x <= FLINK_VIEW_W + WIN_SIDE && y >= -WIN_ABOVE && y <= VIEW_H + WIN_BELOW;
}

int flink_window(const BYTE *a) { return in_flink_window(a); }

int enemy_on_screen(const BYTE *a)
{
    const BYTE *pl = PLANE(A_PLANE(a));
    int w = P_W(pl) * 16 - P_MAXX(pl), x = A_X(a) - P_CAMX(pl), y = A_Y(a) - P_CAMY(pl);
    if (P_MAXX(pl) <= 0 || w < FLINK_VIEW_W) w = FLINK_VIEW_W;
    return x >= -SEEN_MARGIN && x <= w + SEEN_MARGIN && y >= -SEEN_MARGIN && y <= VIEW_H + SEEN_MARGIN;
}

static void __cdecl enemy_wait(BYTE *a)
{
    int i = enemy_slot(a);
    if (i < 0 || !g_enemy[i].wake) return;
    if (in_flink_window(a)) {
        A_UPDATE(a) = g_enemy[i].wake;
        A_FLAGS(a) = g_enemy[i].wflags;
        g_enemy[i].wake = NULL;
        ((ActorFn)A_UPDATE(a))(a);
        return;
    }
    if (enemy_on_screen(a)) { A_FLAGS(a) = g_enemy[i].wflags; Actor_AdvanceAnim(a); }
    else A_FLAGS(a) = (g_enemy[i].wflags & ~AF_CONTACT) | AF_HIDDEN;
}

/* Params: facing left (bit 0), then each kind's own */
void __cdecl enemy_spawn(BYTE *a, BYTE *def)
{
    int i = slot_of(a);
    int fire = is_type(a, "Muta Fire"), holder = is_type(a, "Muta Holder"), thief = is_type(a, "Muta Thief");
    int visje = is_type(a, "Muta Visje"), statue = is_type(a, "Muta Statue"), wurm = is_type(a, "Muta Wurm");
    int boss = is_type(a, "Muta Boss"), puge = is_type(a, "Muta Puge"), city = is_type(a, "Muta City");
    int cycl = is_type(a, "Muta Cycl"), litt = is_type(a, "Muta Litt"), bike = is_type(a, "Muta Bike");
    int vboss = is_type(a, "Muta Village"), moun = is_type(a, "Muta Moun"), yeti = is_type(a, "Muta Yeti");
    int buzzard = is_type(a, "Muta Buzz"), razo = is_type(a, "Muta Razo"), intr = is_type(a, "Muta Intr");
    int worm = is_type(a, "Muta Worm"), donk = is_type(a, "Muta Donk"), meta = is_type(a, "Muta Meta ");   /* (not 'Muta MetaBoss') */
    int mboss = is_type(a, "Muta MetaBoss"), bos1 = is_type(a, "Muta Bos1"), bos3 = is_type(a, "Muta Bos3");
    int storm = is_type(a, "Muta Storm"), blitz = is_type(a, "Muta Blitz"), circ = is_type(a, "Muta Circ");
    if (!g_active || i < 0 || (!fire && !holder && !thief && !visje && !statue && !wurm && !boss && !puge && !city &&
                               !cycl && !litt && !bike && !vboss && !moun && !yeti && !buzzard && !razo && !intr && !worm && !donk && !meta && !storm && !blitz && !circ && !mboss && !bos1 && !bos3 && !is_type(a, "Muta Dummy"))) {
        Enemy_ZombieRunner_Spawn(a, def);
        return;
    }
    memset(&g_enemy[i], 0, sizeof(g_enemy[i]));
    g_enemy[i].type = A_TYPE(a);
    A_UPDATE(a) = (void *)dummy_update;
    A_FLAGS(a) |= AF_HURTS | AF_HITTABLE;
    A_CULL(a) = 1;                           /* (as Lomax's walking enemies) */
    A_LAYER(a) = 1;
    A_VX(a) = A_VY(a) = 0;
    if (!visje && !statue && !wurm && !puge && !yeti && !buzzard && !razo && !worm && !donk && !meta && !storm && !blitz && !circ) Actor_SetAnim(a, FOURCC("WALK"));   /* (the Visje, the Statue, the Raketen wurm, the tree, the Yeti, the Buzzard, the Razorback, the Worm, the Meta dummy, the Storm Cloud, the Blitz dude and the Circling suckers have none) */
    if (def_param(a, def, 0, 0) & 1) A_FLAGS(a) |= AF_LEFT;
    if (fire) fire_spawn(a, i, def);
    if (holder) holder_spawn(a, i);
    if (thief) thief_spawn(a, i, def);
    if (visje) visje_spawn(a, i, def);
    if (statue) statue_spawn(a, i, def);
    if (wurm) wurm_spawn(a, i, def);
    if (puge) puge_spawn(a, i, def);
    if (city) city_spawn(a, i, def);
    if (moun) moun_spawn(a, i);
    if (intr) intree_spawn(a, i);
    if (worm) worm_spawn(a, i);
    if (yeti) yeti_spawn(a, i);
    if (buzzard) buzzard_spawn(a, i, def);
    if (meta) meta_spawn(a, i);
    if (storm) storm_spawn(a, i);
    if (blitz) blitz_spawn(a, i, def);
    if (circ) circ_spawn(a, i, def);
    if (razo) razo_spawn(a, i, def);
    if (cycl) cycl_spawn(a, i, def);
    if (litt) litt_spawn(a, i);
    if (bike) { bike_spawn(a, i); return; }   /* (not an enemy; only ever placed never to spawn) */
    if (vboss) {                              /* the Village Boss: villageboss.c runs it (not an enemy itself) */
        g_enemy[i].type = NULL;
        if (placement_gone(def)) { vboss_gone(a, def); spawn_gone(a); }
        else vboss_spawn(a, i, def);
        return;
    }
    if (mboss) {                              /* the Meta Boss: metaboss.c runs it (not an enemy itself) */
        g_enemy[i].type = NULL;
        if (placement_gone(def)) { mboss_gone(a, def); spawn_gone(a); }
        else mboss_spawn(a, def);
        return;
    }
    if (bos1) {                               /* the final boss: bos1.c runs him (not an enemy himself) */
        g_enemy[i].type = NULL;
        if (placement_gone(def)) { bos1_gone(a, def); spawn_gone(a); }
        else bos1_spawn(a, def);
        return;
    }
    if (bos3) bos3_spawn(a, i);
    if (boss) boss_spawn(a, i, def);
    if (donk) donk_spawn(a, i, def);
    if (placement_gone(def)) {
        g_enemy[i].type = NULL;
        if (boss) boss_gone(a, def);         /* (its Finish) */
        if (donk) donk_gone(a, i, def);
        spawn_gone(a);
        return;
    }
    if (def && !(D_FLAGS(def) & 0x2000) && !boss && !donk && !in_flink_window(a)) {   /* (not a Generator's; the bosses wait their own way) */
        g_enemy[i].wake = A_UPDATE(a);
        g_enemy[i].wflags = A_FLAGS(a);
        A_UPDATE(a) = (void *)enemy_wait;
        if (!enemy_on_screen(a)) A_FLAGS(a) = (A_FLAGS(a) & ~AF_CONTACT) | AF_HIDDEN;
    }
}
