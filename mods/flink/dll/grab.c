/*
 * grab.c - picking up, carrying and throwing, Flink's way, with Lomax's Ability button (README
 * "Grabbing and throwing"). The helmet powers are off in Flink's maps (powers.c), so the button is
 * free: Flink's ACTION button (g_BtnGrabBit 0x8144).
 *
 * Flink (Player_HandleButtons 0x32E0E, Actor_CheckPickedUp / Actor_TryPickUp 0x1ABB4 / 0x1ABDE, the
 * carry hooks 0x1B5B2, Actor_ThrownThink 0x323C2 and the shot 0x32630; the Flink notes' 10.9):
 * - The button is armed (g_GrabArmed 0x8148) whenever it isn't held, and disarmed by a pick-up or a
 *   throw: each needs a fresh press. Holding it while walking up to something picks it up.
 * - Pick-up: an object that can be picked up asks every tick, from its think. It succeeds when the
 *   button is held and armed, Flink carries nothing, isn't hanging, and his grab box overlaps the
 *   object's hitbox. The grab box is the second box of his current animation record (0x332A0):
 *   standing (5, -26, 13, -18) from his feet, in front of him; ducking (6, -18, 15, -9). A chest's or a
 *   small stone's hitbox top is 14 px up, under the standing box: he must duck for those. A stunned
 *   dummy (top 27 px up) or a big stone (23) can be taken standing.
 * - Carried, the object doesn't think. It is drawn at the hand (an attach point of his pose: light
 *   objects in front at chest height, heavy ones overhead) plus its own offset (+0x10, +0x12),
 *   facing his way. A heavy one cuts his speed to 1/4 when lifted, keeps him at the slow speed row
 *   (1.0 px a frame on flat ground) and lowers his jump (boost 0x6000, not 0x9000). A light one
 *   that meets a wall or a ceiling falls out of his hands. One with a fuse (a dummy's stun timer)
 *   jumps out at 0 (4.0 px up) and hurts him.
 * - Throw: a new press while carrying. Hurt, he drops it with his speed (at least 4.0 up); in the
 *   water he drops it (Player_DropCarried 0x1A778). Otherwise, with vx = his speed / 4:
 *     heavy: vx +- 1.5, vy = his vy / 16 + 1.5 up, and a boost of 0x7800 (0x5000 standing on a
 *     seesaw) that, while the button is held, shrinks by 0x800 a frame and adds to vy and half to vx
 *     (a longer throw); lifetime 30. The flight is Actor_ThrownThink's: the boost first, then
 *     0x1B0E2's gravity 0.25 (at most 4.0) and air friction 0x800 on vx (0x6000 once vy is 0);
 *     light, standing still with Up held: straight up at his vy / 16 + 8.0; lifetime 1000;
 *     light: a shot (below), vy 1.5 up.
 * - A thrown object (Actor_ThrownThink) falls at 0.25 px a frame (at most 4.0), bounces off walls and
 *   ceilings, is slowed to 1.0 in water (with a splash), and can be picked up again, even in the air.
 *   Landing at the full 4.0 it bounces once (2.0 up, vx halved). Landed, one with a fuse gets its own
 *   think back (a dummy walks on once its stun is over); one without stays a thrown object at rest,
 *   sliding down 45 degree slopes at 2.0. While it moves it hits any stompable actor it touches
 *   (their hit flag: every Flink enemy dies of it) and then breaks (a puff; one with a fuse dies,
 *   Enemy_Die); once its lifetime is over it also hurts Flink if it falls on him.
 * - A shot (0x32630): Flink's Shot SPIN (or SMAL for a small object), straight on at 4.0 px a frame,
 *   falling at 0.1875 (at most 4.0), bouncing off walls, ceilings and the ground (half its speed when
 *   it lands faster than 1.0). It kills the enemies it touches (a puff each) and flies on; after 14
 *   frames it hurts Flink if it touches him; it blinks from 420 frames on and goes at 540, or when it
 *   leaves the screen. The thrown object's map entry is forgotten (a small stone keeps it).
 *
 * Here: Game_Frame's call of Player_Update (0x404C74) goes through player_tick, which runs
 * grab_before (the button, pick-up, throw; Flink's Player_HandleButtons and the objects' pick-up
 * checks, which run before Flink's own update), Player_Update, then grab_after (the carried object
 * at the hand: Flink's carry hooks); groundtype.c's groundtype_before / groundtype_after go around Player_Update too. A carried object's update is carried_update (nothing), a thrown
 * one's thrown_update, a shot's shot_update; the object's own update, flags (no contact while held:
 * Lomax neither stomps it nor is hurt by it), draw layer and cull are kept in g_grab and given back when
 * it lands. A thrown object or shot never hurts Lomax, falling on him or coming back off a wall or the
 * boss's trunks (Flink's does once its lifetime is out). Lomax has no carrying poses yet: the hand is Flink's (HAND_HEAVY,
 * HAND_LIGHT: his poses' attach points, standing, ducking or in the air, whatever Lomax's own pose),
 * and DUCK is his duck. What can be picked up comes from enemy_grab_kind (the stunned dummy), chest_grab_kind (an
 * open chest), bigapple_grab_kind (an App2 at rest), the stones (stone_spawn) and the Throw platforms (throwplat.c), the last two thrown objects at rest
 * from their spawn on (grab_rest).
 */
#include "enemy.h"

#define CALL_PLAYER_UPDATE 0x00404C74u   /* Game_Frame: call Player_Update */
#define CALL_TRY_HANG      0x00429FE0u   /* Rope_WaitForGrab: call Player_TryGrabHangPoint */
#define Player_TryGrabHangPoint ((int (__cdecl *)(BYTE *, int))0x00413D60u)   /* (rope, length): 1 = he hangs on it */

enum { GS_NONE, GS_CARRIED, GS_THROWN, GS_SHOT };

/* Flink's player record second boxes (MAIN.WLD Hero), from the feet, facing right */
static const short GRAB_STAND[4] = { 5, -26, 13, -18 }, GRAB_DUCK[4] = { 6, -18, 15, -9 };

/* Flink's hand, from his feet facing right: the attach points of his pose where the carry hooks
   (0x1B5B2 / 0x1B5BE through PlayerDrawHookTable 0x3405E) draw the carried object (MAIN.WLD Hero:
   the hook pieces of group 3 overhead, group 2 in front), standing (STAY; WALK and RUN within 2 px),
   ducking (DUCK) and in the air (JUMP, frames 1-4). Not Lomax's hitbox: his jump pose is 11 px
   shorter than his standing one, and a heavy throw from it started that much lower than Flink's. */
static const short HAND_HEAVY[3][2] = { { -4, -37 }, { -2, -29 }, { -3, -39 } };
static const short HAND_LIGHT[3][2] = { { 8, -22 }, { 10, -13 }, { 9, -24 } };

#define HEAVY_SPEED        0x18000       /* on the ground with a heavy object: half Lomax's walk (Flink 1.0 of 2.0),
                                            scaled with his speed (move_speed); the lower jump is move.c's */
#define SHOT_SPEED         0x40000
#define HEAVY_BOOST        0x7800        /* the heavy throw's boost (Flink +0x60, 0x33090) */
#define HEAVY_BOOST_SEESAW 0x5000        /* standing on a seesaw (0x330B0) */

/* per actor slot: grab.c runs it while state != GS_NONE and its update is one of grab.c's */
static struct {
    BYTE *type;
    int state, src;
    GrabKind k;
    void *update;                        /* its own (Flink +0x3E) */
    DWORD flags;
    short layer, cull;
    int boost, life;                     /* Flink +0x60, +0x64 */
} g_grab[MAX_ACTORS];

static int g_carried = -1;               /* the carried actor's slot (Flink's g_CarriedActorOfs 0x8304) */
static int g_armed;                      /* g_GrabArmed */
static int g_hurt;                       /* last tick's hurt timer */

static void __cdecl carried_update(BYTE *a) { (void)a; }
static void __cdecl thrown_update(BYTE *a);
static void __cdecl shot_update(BYTE *a);

void grab_reset(void)
{
    memset(g_grab, 0, sizeof(g_grab));
    g_carried = -1;
    g_armed = 0;
    g_hurt = 0;
}

int grab_held(const BYTE *a)
{
    int i = slot_of(a);
    void *u = A_UPDATE(a);
    return i >= 0 && g_grab[i].state != GS_NONE && g_grab[i].type == A_TYPE(a) &&
           (u == (void *)carried_update || u == (void *)thrown_update || u == (void *)shot_update);
}

static int alive(const BYTE *a, int slot) { return actor_active(slot) && A_TYPE(a) && A_LAYER(a) != KILLED_LAYER; }

/* the control scheme's Ability button (g_AbilityButtonMask is 0 in Flink's maps, powers.c) */
static DWORD ability_mask(void)
{
    int s = *G_CONTROLSCHEME;
    return s >= 0 && s < 8 ? G_SCHEMEMASKS[s * 4 + SCHEME_ABILITY] : 0x80;
}

static int ability_held(void) { return (*G_INPUTHELD & ability_mask()) != 0; }

static int player_left(void) { return (*G_PLAYERFLAGS & AF_LEFT) != 0; }

static int player_ducks(void)
{
    const BYTE *pa = *G_PLAYERANIM;
    return pa && *(const DWORD *)(pa + 4) == FOURCC("DUCK");
}

static int overlap(int ax1, int ay1, int ax2, int ay2, int bx1, int by1, int bx2, int by2)
{
    return ax1 <= bx2 && bx1 <= ax2 && ay1 <= by2 && by1 <= ay2;
}


/* a solid cell that blocks from below (0x80 | 0x08) at x, y on the actor's plane */
static int ceiling_at(const BYTE *a, int x, int y)
{
    const BYTE *pl = PLANE(A_PLANE(a)), *coll = P_COLL(pl);
    BYTE b;
    if (P_EMPTY(pl) || !coll || x < 0 || y < 0 || x >= P_W(pl) * 16 || y >= P_H(pl) * 16) return 0;
    b = coll[(x >> 4) + (y >> 4) * P_W(pl)];
    return (b & 0xC0) == 0x80 && (b & 0x08);
}

/* the x of its hitbox's side in the way it moves (Flink's wall check 0x1AD4C tests there) */
static int front_x(const BYTE *a, int right)
{
    int x1, y1, x2, y2;
    if (!actor_box(a, &x1, &y1, &x2, &y2)) return A_X(a);
    return right ? x2 : x1;
}

static int top_y(const BYTE *a)
{
    int x1, y1, x2, y2;
    return actor_box(a, &x1, &y1, &x2, &y2) ? y1 : A_Y(a) - 16;
}

/* a wall in the way of vx: steps back and turns (sound 0x29, Lomax's pot bounce); 1 = it did */
static int bounce_off_wall(BYTE *a)
{
    int vx = A_VX(a);
    if (!vx || !wall_at(a, front_x(a, vx > 0), vx > 0)) return 0;
    A_FX(a) -= vx;
    A_VX(a) = -vx;
    A_FLAGS(a) ^= AF_LEFT;
    set_pos(a);
    Sound_PlayEffect(a, SND_POTBOUNCE);
    return 1;
}

/* Flink's puff (0x3365A) at its front: Lomax's stomp poof */
static void puff(BYTE *a)
{
    int x1, y1, x2, y2;
    if (!actor_box(a, &x1, &y1, &x2, &y2)) { Effect_SpawnPoof(A_X(a), A_Y(a) - 8); return; }
    Effect_SpawnPoof(A_VX(a) < 0 ? x1 : x2, (y1 + y2) / 2);
}

/* Actor_FindTouchingActor (0x1A5A0): the first Flink enemy its hitbox touches is hit (enemy_struck).
   The boss's trunks come after the enemies, the boss's body included: Flink's boss spawns its two
   NODEs (0x32CD8, the first free slots) right after taking its own slot, so the search, in slot order,
   meets the body first, and a thrown object touching the head and a trunk hits the head. It meets the
   body even while it can't be hit (its guard: +6 bit 3 stays set), and then stops there too: a puff,
   and no trunk turns the object back. */
static int strike_at(BYTE *a, BYTE *t, int k, int x1, int y1, int x2, int y2, int shot, int small)
{
    int tx1, ty1, tx2, ty2, e;
    if (t == a || !alive(t, k) || A_PLANE(t) != A_PLANE(a) || grab_held(t)) return 0;
    if (wurm_struck(t, x1, y1, x2, y2)) return 1;           /* a Raketen wurm: its head or any segment */
    if (apple_struck(t, x1, y1, x2, y2)) return 1;          /* a Falling apple bursts (apple.c) */
    if (bigapple_struck(t, x1, y1, x2, y2)) return 1;       /* an App2 breaks (bigapple.c) */
    if (donk_struck(t, a, x1, y1, x2, y2, shot)) return 1;  /* the Donkey monkey (donkey.c) */
    if (bos1_struck(t, a, x1, y1, x2, y2)) return 1;        /* the final boss (bos1.c) */
    if (mboss_struck(t, x1, y1, x2, y2)) return 1;          /* the Meta Boss (metaboss.c) */
    e = enemy_slot(t);
    if (!(A_FLAGS(t) & AF_HITTABLE) && (e < 0 || (!g_enemy[e].boss && !g_enemy[e].cycl))) return 0;   /* (the flying
                                                   Cyclin' Dude can't be stomped, +6 bit 3 is set: cyclindude.c) */
    if (!actor_box(t, &tx1, &ty1, &tx2, &ty2) || !overlap(x1, y1, x2, y2, tx1, ty1, tx2, ty2)) return 0;
    if (yeti_deflects(t, a)) return 1;                      /* a Yeti turns it back (yeti.c) */
    if (moun_shield(t, a, shot)) return 1;                  /* the Mountain dummy's shield turns a shot back (mountain.c) */
    return enemy_struck(t, small) || (e >= 0 && g_enemy[e].boss);
}

static int strike(BYTE *a)
{
    int x1, y1, x2, y2, k, self = slot_of(a), small = self >= 0 && g_grab[self].k.small;
    int shot = self >= 0 && g_grab[self].state == GS_SHOT;
    if (!actor_box(a, &x1, &y1, &x2, &y2)) return 0;
    if (vboss_struck(a, x1, y1, x2, y2, shot)) return 1;    /* the Village Boss's men (villageboss.c) */
    for (k = 0; k < MAX_ACTORS; k++)
        if (strike_at(a, actor_at(k), k, x1, y1, x2, y2, shot, small)) return 1;
    return boss_trunk_bounce(a, x1, y1, x2, y2) ||           /* the boss's trunks turn it back */
           rock_bounce(a, x1, y1, x2, y2);                  /* and so do the rocks (rock.c) */
}

/* A spell's hit (spellfx.c): Flink's spell actors set an actor's hit flag (0x1A6D2), as a thrown
   object does, either the first one their box touches (Actor_FindTouchingActor 0x1A5A0) or their
   own target once their box touches it (0x1A65A). Here the box x1..y2 of src hits t (NULL: the first
   one it touches) as a thrown object would; 1 if something was hit. The trunks and rocks that turn a
   thrown object back are not actors Flink's search finds (no +6 bit 3). */
int spell_strike(BYTE *src, BYTE *t, int x1, int y1, int x2, int y2)
{
    int k;
    if (boss_fight()) return 0;                              /* (no spell hits in a boss fight) */
    if (t) return strike_at(src, t, slot_of(t), x1, y1, x2, y2, 0, 0);
    if (vboss_struck(src, x1, y1, x2, y2, 0)) return 1;
    for (k = 0; k < MAX_ACTORS; k++)
        if (strike_at(src, actor_at(k), k, x1, y1, x2, y2, 0, 0)) return 1;
    return 0;
}

/* its own update, flags, layer and cull back (Flink 0x32532: think = +0x3E) */
static void give_back(BYTE *a, int i)
{
    A_UPDATE(a) = g_grab[i].update;
    A_FLAGS(a) = (A_FLAGS(a) & ~(AF_CONTACT | AF_HIDDEN)) | (g_grab[i].flags & AF_CONTACT);
    A_LAYER(a) = g_grab[i].layer;
    A_CULL(a) = g_grab[i].cull;
    g_grab[i].state = GS_NONE;
    if (g_carried == i) g_carried = -1;
}

/* landed (an object with a fuse): its own think again, moving at vy */
static void resume(BYTE *a, int i, int vy)
{
    give_back(a, i);
    A_VY(a) = vy;
    if (g_grab[i].src == SRC_ENEMY) enemy_grab_resume(a);
}

/* it hit something (0x325DE): a puff, and one with a fuse dies */
static int break_up(BYTE *a, int i)
{
    puff(a);
    if (g_grab[i].k.fuse) {
        give_back(a, i);
        if (g_grab[i].src == SRC_ENEMY) enemy_grab_break(a);
        return 1;
    }
    return 0;
}

/* out of his hands, flying (think Actor_ThrownThink) */
static void release(BYTE *a, int i, int vx, int vy, int life)
{
    g_grab[i].state = GS_THROWN;
    g_grab[i].boost = 0;
    g_grab[i].life = life;
    A_UPDATE(a) = (void *)thrown_update;
    A_LAYER(a) = g_grab[i].layer;
    A_CULL(a) = g_grab[i].cull;
    A_VX(a) = vx;
    A_VY(a) = vy;
    if (g_carried == i) g_carried = -1;
}

/* fell out of his hands against a wall or ceiling, or jumped out (0x1B68A): back against his
   speed, life 1000 */
static void fall_out(BYTE *a, int i)
{
    int vx = -*G_PLAYERVX;
    A_FX(a) += vx;
    set_pos(a);
    if (vx < 0) A_FLAGS(a) |= AF_LEFT;
    else A_FLAGS(a) &= ~AF_LEFT;
    release(a, i, vx, A_VY(a), 1000);
}

/* Actor_TryPickUp (0x1ABDE) succeeded */
static void pick_up(BYTE *a, int i, const GrabKind *k, int src)
{
    if (!grab_held(a)) {                 /* (a thrown object keeps what it had) */
        memset(&g_grab[i], 0, sizeof(g_grab[i]));
        g_grab[i].type = A_TYPE(a);
        g_grab[i].src = src;
        g_grab[i].k = *k;
        g_grab[i].update = A_UPDATE(a);
        g_grab[i].flags = A_FLAGS(a);
        g_grab[i].layer = A_LAYER(a);
        g_grab[i].cull = A_CULL(a);
    }
    g_grab[i].state = GS_CARRIED;
    g_grab[i].boost = g_grab[i].life = 0;
    g_armed = 0;
    g_carried = i;
    A_UPDATE(a) = (void *)carried_update;
    A_FLAGS(a) &= ~(AF_CONTACT | AF_HIDDEN);
    A_LAYER(a) = 2;                      /* in front of Lomax (layer 2 draws 8 over its plane, he 6) */
    A_CULL(a) = CULL_NEVER;
    A_CLIPY(a) = NO_CLIP;                /* (a chest's clip line was its placement) */
    A_VX(a) = A_VY(a) = 0;
    if (k->heavy) *G_PLAYERVX /= 4;
    *G_BAIRSPIN = 0;                     /* (no spin while carrying: grab_before) */
    if (k->picked) k->picked(a);
    flog("Lomax picks up %s at (%d,%d)%s", T_NAME(A_TYPE(a)), A_X(a), A_Y(a), k->heavy ? " (heavy)" : "");
}

/* Lomax's grab box (Flink's record second box, 0x332A0) */
static void grab_box(int *x1, int *y1, int *x2, int *y2)
{
    const short *b = player_ducks() ? GRAB_DUCK : GRAB_STAND;
    int px = *G_PLAYERX, py = *G_PLAYERY;
    *y1 = py + b[1]; *y2 = py + b[3];
    if (player_left()) { *x1 = px - b[2]; *x2 = px - b[0]; }
    else { *x1 = px + b[0]; *x2 = px + b[2]; }
}

/* Lomax takes hold of an object by touching it, as of his ropes (the Chopper Bike, cyclindude.c; Flink
   needs the grab button there, Actor_CheckPickedUp 0x1ABB4; as for the lianas):
   Lomax carrying nothing, not hanging or riding, input not locked, not dying, and its hitbox touching
   his (his animation's, mirrored with him). 1 = he takes it. */
int grab_can_touch(const BYTE *a)
{
    return g_active && *G_PLAYERANIM && g_carried < 0 && !ride_active() && !*G_BINPUTLOCKED && !*G_PLAYERDEATHTIMER &&
           !(*G_PLAYERSTATE & (PS_ROPE | PS_FROZEN)) && A_PLANE(a) == *G_PLAYERPLANE;
}

int grab_touch(BYTE *a)
{
    const BYTE *pa = *G_PLAYERANIM;
    int px = *G_PLAYERX, py = *G_PLAYERY, lx1, lx2, x1, y1, x2, y2;
    if (!grab_can_touch(a)) return 0;
    if (player_left()) { lx1 = px - HB_X2(pa); lx2 = px - HB_X1(pa); }
    else { lx1 = px + HB_X1(pa); lx2 = px + HB_X2(pa); }
    return actor_box(a, &x1, &y1, &x2, &y2) && overlap(lx1, py + HB_Y1(pa), lx2, py + HB_Y2(pa), x1, y1, x2, y2);
}

/* the objects in Lomax's grab box (Actor_CheckPickedUp, as each object's think calls it); 1 if he
   picked one up */
static int try_pick_up(void)
{
    int gx1, gx2, gy1, gy2, k;
    int water_air = (*G_PLAYERSTATE & (PS_WATER | PS_AIR)) == (PS_WATER | PS_AIR);
    grab_box(&gx1, &gy1, &gx2, &gy2);
    for (k = 0; k < MAX_ACTORS; k++) {
        BYTE *a = actor_at(k);
        GrabKind kk;
        int src, x1, y1, x2, y2;
        if (!alive(a, k) || A_PLANE(a) != *G_PLAYERPLANE) continue;
        if (grab_held(a)) {
            if (g_grab[k].state != GS_THROWN) continue;
            kk = g_grab[k].k;
            src = g_grab[k].src;
        } else if (enemy_grab_kind(a, &kk)) src = SRC_ENEMY;
        else if (chest_grab_kind(a, &kk)) src = SRC_CHEST;
        else if (bigapple_grab_kind(a, &kk)) src = SRC_APPLE;
        else continue;
        if (kk.heavy && water_air) continue;         /* (0x1AC68) */
        if (!actor_box(a, &x1, &y1, &x2, &y2) || !overlap(gx1, gy1, gx2, gy2, x1, y1, x2, y2)) continue;
        pick_up(a, k, &kk, src);
        return 1;
    }
    return 0;
}

/* Flink's throw (0x32FC0) */
static void throw_it(BYTE *a, int i)
{
    int pvx = *G_PLAYERVX, up = -*G_PLAYERVY, d0 = pvx >> 2, left = player_left();
    const GrabKind *k = &g_grab[i].k;
    g_armed = 0;
    if (*G_PLAYERSTATE & PS_WATER) {             /* Player_DropCarried (0x1A778) */
        release(a, i, pvx, -(up > 0x20000 ? up : 0x20000), 120);
        flog("Lomax drops it in the water");
        return;
    }
    if (k->heavy) {                              /* an arc, longer while the button is held (0x33080):
                                                    vx = his vx / 4 +- 1.5, vy = his vy / 16 + 1.5 up,
                                                    boost 0x7800, 0x5000 standing on a seesaw (0x330A4) */
        release(a, i, d0 + (left ? -0x18000 : 0x18000), -((up >> 4) + 0x18000), 30);
        g_grab[i].boost = seesaw_under_lomax() ? HEAVY_BOOST_SEESAW : HEAVY_BOOST;
        flink_sound(NULL, 0x1A);
        flog("Lomax throws it (heavy)");
        return;
    }
    if (*G_INPUTHELD & INPUT_UP && !pvx) {       /* straight up */
        release(a, i, d0, -((up >> 4) + 0x80000), 1000);
        flink_sound(NULL, 0x1A);
        flog("Lomax throws it up");
        return;
    }
    /* a shot (0x33154) */
    release(a, i, d0 + (left ? -SHOT_SPEED : SHOT_SPEED), -0x18000, 14);
    g_grab[i].state = GS_SHOT;
    A_UPDATE(a) = (void *)shot_update;
    A_CULL(a) = 1;                               /* gone when it leaves the screen */
    {
        DWORD tag = FOURCC(k->small ? "SHSM" : "SPIN");
        if (record_of(a, tag)) Actor_SetAnim(a, tag);
    }
    if (!k->respawns) forget_placement(A_DEF(a));
    if (g_grab[i].src == SRC_ENEMY) enemy_grab_shot(a);
    {                                            /* not inside the ground (0x33206) */
        unsigned g = Plane_FindGroundY(PLANE(A_PLANE(a)), A_X(a), A_Y(a) - 8, 3);
        if (g != NO_GROUND && (int)g < A_Y(a)) place_y(a, g);
    }
    flink_sound(NULL, 0x19);
    flog("Lomax throws a shot from (%d,%d)", A_X(a), A_Y(a));
}

/* The cheat's fly mode (Player_Update 0x40FD43): with g_bCheatsEnabled, Ability pressed with one of
   the low buttons held (g_InputHeld & 0xF: LookDown, Run, LookUp) and the helicopter chosen toggles
   player state 0x20000, stopping him. In Flink's maps g_AbilityButtonMask is 0, so it is done here
   with the scheme's Ability button, without the helicopter (no power can be chosen there), under the
   same conditions (not on a rope, input not locked, not frozen: Player_Update clears the input then).
   1 = toggled: the press neither grabs nor throws. */
static int fly_cheat(void)
{
    DWORD st = *G_PLAYERSTATE;
    if (!*G_BCHEATSENABLED || !(*G_INPUTPRESSED & ability_mask()) || !(*G_INPUTHELD & 0xF)) return 0;
    if (st & PS_ROPE || *G_BINPUTLOCKED || (st & 0x18000) == 0x18000) return 0;
    *G_PLAYERVX = *G_PLAYERVY = 0;
    *G_PLAYERSTATE = (st ^ PS_FLY) | PS_AIR;
    flog("fly mode %s", *G_PLAYERSTATE & PS_FLY ? "on" : "off");
    return 1;
}

/* carrying: no helmet throw (the Helm button, Player_Update 0x4107ED) and no air spin (Jump pressed
   again in the air, 0x410425; Jump pressed is read for nothing else there out of the water) */
static void carry_input(void)
{
    *G_INPUTPRESSED &= (WORD)~*G_HELMMASK;
    if ((*G_PLAYERSTATE & (PS_AIR | PS_WATER)) == PS_AIR) *G_INPUTPRESSED &= (WORD)~*G_JUMPMASK;
}

/* before Player_Update: Flink's Player_HandleButtons and the pick-up checks */
static void grab_before(void)
{
    int held, hurt, newhit;
    BYTE *a;
    if (!g_active || *G_BPAUSED || *G_PLAYERSTATE & PS_FROZEN) return;
    held = ability_held();
    if (!held) g_armed = 1;
    if (ride_active()) return;                   /* riding: no grab, throw or fly cheat (0x32FC0, +0x828E bit 4) */
    if (fly_cheat()) g_armed = 0;
    hurt = *G_PLAYERHURTTIMER;
    newhit = hurt > 0 && hurt > g_hurt;        /* (after the knock-back's landing it counts up from below 0: no hit) */
    g_hurt = hurt;
    a = actor_at(g_carried);
    if (a && (!grab_held(a) || g_grab[g_carried].state != GS_CARRIED)) { g_carried = -1; a = NULL; }
    if (a) {
        int i = g_carried, up = -*G_PLAYERVY;
        if (newhit) {                            /* hurt: dropped with his speed (0x33038) */
            release(a, i, *G_PLAYERVX, -(up > 0x40000 ? up : 0x40000), 14);
            flog("Lomax is hurt and drops it");
        } else if (g_armed && held && !*G_BINPUTLOCKED && !*G_PLAYERDEATHTIMER)
            throw_it(a, i);
        else if (g_grab[i].k.heavy && (*G_PLAYERSTATE & (PS_WATER | PS_AIR)) == (PS_WATER | PS_AIR))
            release(a, i, *G_PLAYERVX, -(up > 0x20000 ? up : 0x20000), 120);   /* (0x32E46) */
        if (g_carried >= 0) carry_input();
        if (g_carried >= 0 && g_grab[i].k.heavy && !(*G_PLAYERSTATE & PS_AIR)) {
            int cap = move_speed(HEAVY_SPEED);
            if (*G_PLAYERVX > cap) *G_PLAYERVX = cap;
            if (*G_PLAYERVX < -cap) *G_PLAYERVX = -cap;
        }
    } else if (g_armed && held && !*G_BINPUTLOCKED && !*G_PLAYERDEATHTIMER && !(*G_PLAYERSTATE & PS_ROPE)) {
        int pressed = (*G_INPUTPRESSED & ability_mask()) != 0;
        if (pressed && (plant_grow() || chest_unlock() || spell_area_cast()))
            g_armed = 0;                         /* a spell: a Plant (plant.c), a locked chest (chests.c) */
        else if (!try_pick_up() && pressed && spell_cast())
            g_armed = 0;                         /* nothing to pick up: the spell cast anywhere (spellfx.c) */
    }
}

/* after Player_Update: the carried object at the hand (Flink's carry hooks 0x1B5D6) */
static void grab_after(void)
{
    int i = g_carried, left = player_left(), duck, pose, hx, hy;
    BYTE *a;
    if (!g_active || *G_BPAUSED || *G_PLAYERSTATE & PS_FROZEN) return;
    a = actor_at(i);
    if (a && (!grab_held(a) || g_grab[i].state != GS_CARRIED)) { g_carried = -1; a = NULL; }
    if (!a) return;                              /* (the lower jump carrying something heavy: move.c) */
    duck = player_ducks();
    pose = duck ? 1 : *G_PLAYERSTATE & PS_AIR ? 2 : 0;
    hx = g_grab[i].k.heavy ? HAND_HEAVY[pose][0] : HAND_LIGHT[pose][0];
    hy = g_grab[i].k.heavy ? HAND_HEAVY[pose][1] : HAND_LIGHT[pose][1];
    hx += g_grab[i].k.dx;
    A_X(a) = *G_PLAYERX + (left ? -hx : hx);
    A_Y(a) = *G_PLAYERY + hy + g_grab[i].k.dy;
    A_FX(a) = A_X(a) << 16;
    A_FY(a) = A_Y(a) << 16;
    A_PLANE(a) = *G_PLAYERPLANE;
    if (left) A_FLAGS(a) |= AF_LEFT;
    else A_FLAGS(a) &= ~AF_LEFT;
    if (!g_grab[i].k.heavy) {                    /* a light one falls out at a wall or a ceiling (0x1B648) */
        if (wall_at(a, front_x(a, !left), !left)) { fall_out(a, i); flog("it falls out of Lomax's hands (wall)"); return; }
        if (ceiling_at(a, A_X(a), top_y(a))) { A_VY(a) = 0x20000; fall_out(a, i); flog("it falls out of Lomax's hands (ceiling)"); return; }
    }
    if (g_grab[i].k.fuse && --*g_grab[i].k.fuse <= 0) {   /* jumps out and hurts him (0x1B676) */
        A_VY(a) = -0x40000;
        Player_TakeHit(A_X(a), 0, 0);
        fall_out(a, i);
        flog("it jumps out of Lomax's hands");
        return;
    }
    if (g_grab[i].src == SRC_ENEMY) enemy_grab_frame(a);
    else Actor_AdvanceAnim(a);
}

/* Actor_ThrownThink (0x323C2) */
static void __cdecl thrown_update(BYTE *a)
{
    int i = slot_of(a), prev, g, v, wl, fx0 = A_FX(a), fy0 = A_FY(a);
    if (i < 0 || !grab_held(a)) return;
    if (g_grab[i].k.think && g_grab[i].k.think(a)) {
        if (A_LAYER(a) != KILLED_LAYER) Actor_AdvanceAnim(a);   /* (the think may despawn it) */
        else g_grab[i].state = GS_NONE;
        return;
    }
    if (g_grab[i].k.fuse) --*g_grab[i].k.fuse;
    if (g_grab[i].boost) {                       /* the heavy throw's boost, while the button is held */
        int b = g_grab[i].boost - 0x800;
        if (b < 0 || !ability_held()) b = 0;
        g_grab[i].boost = b;
        A_VY(a) -= b;
        A_VX(a) += A_FLAGS(a) & AF_LEFT ? -(b >> 1) : b >> 1;
    }
    bounce_off_wall(a);
    prev = A_Y(a);
    fall_and_land(a, A_VY(a) ? 0x800 : 0x6000, 0);
    if (A_VY(a) < 0) {                           /* rising: a ceiling sends it down at 2.0 */
        if (ceiling_at(a, A_X(a), top_y(a))) { A_VY(a) = 0x20000; Sound_PlayEffect(a, SND_POTBOUNCE); }
    } else {
        wl = *G_WATERLEVELY;
        if (A_Y(a) >= wl) {                      /* in the water: 1.0 at most, a splash going in */
            if (A_VY(a) > 0x10000) A_VY(a) = 0x10000;
            if (prev < wl) Water_SpawnSplash(a);
        }
        g = ground_y(a, A_X(a), prev);
        if (g != NO_GROUND && A_Y(a) >= g) {     /* landed (0x324C2) */
            v = A_VY(a);
            place_y(a, g);
            A_VY(a) = 0;
            if (v > 0x8000) Sound_PlayEffect(a, SND_LAND);   /* (not at rest: 0x4000 a tick) */
            if (v >= 0x40000) {                  /* at the full 4.0: one bounce */
                A_VY(a) = -0x20000;
                A_VX(a) /= 2;
                if (g_grab[i].k.fuse && *g_grab[i].k.fuse <= 0) { resume(a, i, -0x20000); return; }
            } else if (g_grab[i].k.fuse) { resume(a, i, 0); return; }
            if (*G_GROUNDSLOPECLASS == 2) {      /* slides down a 45 degree slope (0x3253C) */
                A_VX(a) = *G_GROUNDMIRRORED ? -0x20000 : 0x20000;
                A_VY(a) = 0;
            }
        }
    }
    if (g_grab[i].k.platform) {                  /* Lomax stands on it (the Throw platform: 0x27AE6 moves it
                                                    back and then by its speed, carrying him) */
        int vx = A_VX(a), vy = A_VY(a), dx = A_FX(a) - fx0;
        A_VY(a) = A_FY(a) - fy0;
        A_VX(a) = dx == 1 ? 0 : dx;              /* (vx 1 means "don't move") */
        A_FX(a) = fx0;
        A_FY(a) = fy0;
        set_pos(a);
        Platform_MoveAndCarryPlayer(a, NULL);
        A_VX(a) = vx;
        A_VY(a) = vy;
    }
    if (A_VY(a) && !(A_VY(a) & 1)) {             /* moving: it hits (never Lomax;
                                                    Flink's hurts him once its life is out); an odd vy
                                                    (lying on a platform: grab_land_on) doesn't (0x3256A) */
        g_grab[i].life--;
        if (strike(a) && break_up(a, i)) return;
    }
    /* below the map: gone, unless the water's surface is still below it (11-2's low tide: the Water
       at the map's bottom, +-72 px, puts the surface + 5 at y 381, under below_map's 368), where a
       Throw platform comes to float (Flink's thrown think has no such end) */
    if (below_map(a) && !(water_present() && A_Y(a) <= *G_WATERLEVELY + 64)) {
        g_grab[i].state = GS_NONE; Actor_Kill(a); return;
    }
    if (g_grab[i].k.after) {                     /* Flink's +0x5C hook (0x325CA) */
        g_grab[i].k.after(a);
        if (!grab_held(a)) return;
    }
    if (g_grab[i].src == SRC_ENEMY) enemy_grab_frame(a);
    else Actor_AdvanceAnim(a);
}

int grab_is_carried(const BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && i == g_carried && grab_held(a) && g_grab[i].state == GS_CARRIED;
}

/* its +0x5C hook, Flink's +6 bit 6 (the Village Boss's stones: villageboss.c) */
void grab_set_after(BYTE *a, void (__cdecl *after)(BYTE *a))
{
    int i = slot_of(a);
    if (i >= 0 && g_grab[i].type == A_TYPE(a) && g_grab[i].state != GS_NONE) g_grab[i].k.after = after;
}

/* a thrown object (or a carried one) gets its own update, flags, layer and cull back, moving on at its
   speed (Flink's +0x5C hook setting the think to +0x3E: App2's 0x242B2, bigapple.c) */
void grab_let_go(BYTE *a)
{
    int i = slot_of(a);
    if (i >= 0 && grab_held(a)) give_back(a, i);
}

/* out of play while on (Flink's think Hook_Nop, 0x34130: the Village Boss's stones held or waiting in the
   ceiling); off, a thrown object again. Not for a carried one. */
static void __cdecl frozen_update(BYTE *a) { (void)a; }
void grab_freeze(BYTE *a, int on)
{
    int i = slot_of(a);
    if (i < 0 || g_grab[i].type != A_TYPE(a) || g_grab[i].state != GS_THROWN) return;
    if (on && A_UPDATE(a) == (void *)thrown_update) A_UPDATE(a) = (void *)frozen_update;
    else if (!on && A_UPDATE(a) == (void *)frozen_update) A_UPDATE(a) = (void *)thrown_update;
}

/* the shot (0x32630) */
static void __cdecl shot_update(BYTE *a)
{
    int i = slot_of(a), prev, g;
    if (i < 0 || !grab_held(a)) return;
    A_VY(a) += 0x3000;
    if (A_VY(a) > 0x40000) A_VY(a) = 0x40000;
    A_VX(a) = A_FLAGS(a) & AF_LEFT ? -SHOT_SPEED : SHOT_SPEED;
    A_FX(a) += A_VX(a);
    set_pos(a);
    if (wall_at(a, front_x(a, A_VX(a) > 0), A_VX(a) > 0)) {
        A_FX(a) -= A_VX(a);
        A_FLAGS(a) ^= AF_LEFT;
        Sound_PlayEffect(a, SND_POTBOUNCE);
    }
    prev = A_Y(a);
    A_FY(a) += A_VY(a);
    set_pos(a);
    if (A_VY(a) < -0x8000) {
        if (ceiling_at(a, A_X(a), top_y(a))) { A_VY(a) = 0x20000; Sound_PlayEffect(a, SND_POTBOUNCE); }
    } else if ((g = ground_y(a, A_X(a), prev)) != NO_GROUND && A_Y(a) >= g) {
        place_y(a, g);
        A_VY(a) = A_VY(a) > 0x10000 ? -(A_VY(a) >> 1) : 0;
    }
    g_grab[i].life--;                            /* (it never hurts Lomax) */
    if (strike(a)) puff(a);
    if (g_grab[i].life <= -420) A_FLAGS(a) ^= AF_HIDDEN;     /* blinks */
    if (g_grab[i].life <= -540 || below_map(a)) { g_grab[i].state = GS_NONE; Actor_Kill(a); return; }
    Actor_AdvanceAnim(a);
}

/* Flink's Stone (0x2E6DC): a thrown object at rest from the start, with MAIN.WLD's Stones records.
   Big (BIGG): heavy, held at the hand. Small (checkbox 1, SMAL; param 0 here): light, held 4 px
   forward and 5 down, thrown as the small shot, and it comes back (think 0x2E76A: its map entry is
   kept). Neither has a fuse. */
void grab_rest(BYTE *a, const GrabKind *k, int src)
{
    int i = slot_of(a);
    if (i < 0) return;
    memset(&g_grab[i], 0, sizeof(g_grab[i]));
    A_VX(a) = A_VY(a) = 0;
    g_grab[i].type = A_TYPE(a);
    g_grab[i].state = GS_THROWN;
    g_grab[i].src = src;
    g_grab[i].k = *k;
    g_grab[i].update = A_UPDATE(a);
    g_grab[i].flags = A_FLAGS(a);
    g_grab[i].layer = A_LAYER(a);
    g_grab[i].cull = A_CULL(a);
    A_UPDATE(a) = (void *)thrown_update;
}

void stone_spawn(BYTE *a, BYTE *def)
{
    int small = def_param(a, def, 0, 0) != 0;
    GrabKind k;
    memset(&k, 0, sizeof(k));
    A_FLAGS(a) &= ~(0x200000u | AF_CONTACT);     /* (Deco_Spawn made it hurt) */
    A_CULL(a) = 1;
    A_LAYER(a) = 1;
    Actor_SetAnim(a, FOURCC(small ? "SMAL" : "BIGG"));
    k.heavy = !small;
    k.small = small;
    k.respawns = small;
    k.dx = small ? 4 : 0;
    k.dy = small ? 5 : 0;
    grab_rest(a, &k, SRC_STONE);
}

/* Lomax carries something (Flink's 0x8304 >= 0: he can't push a rock, rock.c) */
int grab_carrying(void)
{
    BYTE *a = actor_at(g_carried);
    return a && grab_held(a) && g_grab[g_carried].state == GS_CARRIED;
}

/* Lomax carries a heavy object (the seesaw's throw is lower then: Flink's 0x8304, +6 bit 0) */
int grab_carrying_heavy(void)
{
    BYTE *a = actor_at(g_carried);
    return a && grab_held(a) && g_grab[g_carried].state == GS_CARRIED && g_grab[g_carried].k.heavy;
}

/* the seesaw's check (0x2BD54): the first thrown object (think Actor_ThrownThink, not a shot or a
   carried one) that is falling (vy > 0), not hidden, on the seesaw's plane, and whose hitbox
   overlaps x1..x2, y1..y2 */
BYTE *grab_falling_on(const BYTE *s, int x1, int y1, int x2, int y2)
{
    int k;
    for (k = 0; k < MAX_ACTORS; k++) {
        BYTE *a = actor_at(k);
        int ax1, ay1, ax2, ay2;
        if (!alive(a, k) || !grab_held(a) || g_grab[k].state != GS_THROWN || A_UPDATE(a) != (void *)thrown_update) continue;
        if (A_VY(a) <= 0 || A_FLAGS(a) & AF_HIDDEN || A_PLANE(a) != A_PLANE(s)) continue;
        if (actor_box(a, &ax1, &ay1, &ax2, &ay2) && overlap(x1, y1, x2, y2, ax1, ay1, ax2, ay2)) return a;
    }
    return NULL;
}

/* Flink's Platform_CarryThrownObjects (0x1A1B2, called by the Mega platform with 'dont move',
   0x288D0): every heavy (+6 bit 0) thrown object (think Actor_ThrownThink) that isn't hidden or
   rising faster than 1/65536 px, whose y is within reach (4) of the platform's top (y + its hitbox
   top) and whose x is inside the hitbox's x, is put on that top with vy 1/65536 down (Flink -1, up
   being +) and vx halved; the hook it calls there is Hook_Nop (0x34130). The odd vy keeps it from
   hitting anything (Actor_ThrownThink 0x3256A skips the hit test for an odd vy, as thrown_update).
   It falls a little each tick and is put back by the platform's next think: it lies on the platform
   and the platform carries it. Not the Throw platform (its own think, 0x27AE6). 1 = any object lies there. */
int grab_land_on(const BYTE *p, int reach)
{
    const BYTE *r = A_RECORD(p);
    int k, top, x1, x2, any = 0;
    if (!g_active || !r) return 0;
    top = A_Y(p) + HB_Y1(r);
    if (A_FLAGS(p) & AF_LEFT) { x1 = A_X(p) - HB_X2(r); x2 = A_X(p) - HB_X1(r); }
    else { x1 = A_X(p) + HB_X1(r); x2 = A_X(p) + HB_X2(r); }
    for (k = 0; k < MAX_ACTORS; k++) {
        BYTE *a = actor_at(k);
        if (!alive(a, k) || !grab_held(a) || g_grab[k].state != GS_THROWN || A_UPDATE(a) != (void *)thrown_update) continue;
        if (!g_grab[k].k.heavy || g_grab[k].k.platform || A_FLAGS(a) & AF_HIDDEN || A_PLANE(a) != A_PLANE(p)) continue;
        if (A_VY(a) < -1 || abs(A_Y(a) - top) > reach || A_X(a) < x1 || A_X(a) > x2) continue;
        place_y(a, top);
        A_VY(a) = 1;
        A_VX(a) /= 2;
        any = 1;
    }
    return any;
}

/* Flink's 0x1A1BA as the Fountain calls it (fountain.c): every heavy thrown object, the Throw platforms
   included (Flink's think 0x323C2, +6 bit 0), not going up faster than 1/65536 (vy <= 1, up positive),
   whose y is within reach px of the line top and its x within x1..x2, is put on the line, vy -1
   (here 1: down), vx halved; then the Fountain's hook (0x26DC8): fling (16.16, up negative) not 0,
   it is thrown up at that speed. 1 if any. */
int grab_lift(const BYTE *p, int top, int x1, int x2, int reach, int fling)
{
    int k, any = 0;
    if (!g_active) return 0;
    for (k = 0; k < MAX_ACTORS; k++) {
        BYTE *a = actor_at(k);
        if (!alive(a, k) || !grab_held(a) || g_grab[k].state != GS_THROWN || A_UPDATE(a) != (void *)thrown_update) continue;
        if (!g_grab[k].k.heavy || A_FLAGS(a) & AF_HIDDEN || A_PLANE(a) != A_PLANE(p)) continue;
        if (A_VY(a) < -1 || abs(A_Y(a) - top) > reach || A_X(a) < x1 || A_X(a) > x2) continue;
        place_y(a, top);
        A_VY(a) = fling ? fling : 1;
        A_VX(a) /= 2;
        any = 1;
    }
    return any;
}

/* Game_Frame's call of Player_Update */
static void __cdecl player_tick(void)
{
    if (scene_hides_player()) return;            /* Flink's intro and ending: no Lomax (scenes.c) */
    razorback_before();
    grab_before();
    ride_before();
    ropeslide_before();
    contact_before();
    move_before();
    stomp_before();
    groundtype_before();
    scroll_before();
    secret_before();
    exit_walk_before();
    worldmap_before();
    title_before();
    rock_before();
    wagon_before();
    steer_before();
    storm_before();
    Player_Update();
    spirits_hurt_after();
    steer_after();
    wagon_after();
    move_after();
    rock_after();
    title_after();
    worldmap_after();
    grab_after();
    ride_after();
    ropeslide_after();
    groundtype_after();
    scroll_after();
    secret_after();
    trunks_after();
    donk_after();
    boss_quiet_after();
    shake_after();
    razorback_after();
    spells_tick();
    sfx_tick();
}

/* Rope_WaitForGrab's hang check: Lomax carrying something doesn't take hold of a rope */
static int __cdecl try_hang(BYTE *rope, int length)
{
    BYTE *a = actor_at(g_carried);
    if (g_active && a && grab_held(a) && g_grab[g_carried].state == GS_CARRIED) return 0;
    if (g_active && ride_active()) return 0;     /* (hanging from the Chopper Bike) */
    return Player_TryGrabHangPoint(rope, length);
}

int grab_retail(void)
{
    return is_call(CALL_PLAYER_UPDATE, (DWORD)Player_Update) && is_call(CALL_TRY_HANG, (DWORD)Player_TryGrabHangPoint);
}

void grab_install(void)
{
    patch_call(CALL_PLAYER_UPDATE, (const void *)player_tick);
    patch_call(CALL_TRY_HANG, (const void *)try_hang);
}
