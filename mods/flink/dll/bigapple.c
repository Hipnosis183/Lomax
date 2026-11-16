/*
 * bigapple.c - Flink's Apple (big) (init 0x24242) and App2 (init 0x24204), TREE's rolling apples
 * (26-1, 27-2, 28-1; README "Apple (big) and App2"). Both run the think 0x242CE and the draw 0x245FE; +0x4C
 * tells them apart (set: the Apple (big)).
 *
 * Flink's:
 * - Init: draw list 1 ((a5) = 0x7D6C), vx = 'start speed' * 0x28F (1/100 px a frame), the phase +0x46
 *   0, ROLL. App2 also gets +6 bits 2 (no fuse), 3 (thrown objects and Flink's slide reach it) and 6
 *   (the hook +0x5C, 0x242B2, while it is a thrown object) and its carry offset (5, 8). The first
 *   placement of each type in a map (spawn flag bit 6, 0x24724) only builds the rotated frames and is
 *   freed (the converter leaves it out); its checkbox 1 ('16 phases') picks 16 phases or 8.
 * - Spawned after the level's start (0x879E set) its first draw is 0x245C0: drawn on the screen, it is
 *   hidden and freed 30 frames later (0x328BA). Not here: Lomax spawns by proximity off the screen, and
 *   generator.c lets every Generator-spawned object appear (see there).
 * - Each frame (0x242CE), in this order:
 *   - hit (+3 bit 6: App2 only, by a thrown object, the slide below or a rolling apple's touch test)
 *     -> DEAD, sound 0x48 when on screen, and broken up (0x1B1F8 with +3 bit 5: not its sound 0x2A; not
 *     forgotten).
 *   - 0x1B0E2: vy - 0x2000 (at most 4.0 down; vy is up-positive), y - vy, x + vx.
 *   - not rising: Actor_SnapToGround (0x1AE96: Map_GetGroundHeight from y - 16, 4 cells, and the
 *     ground within the table 0x1AEDA's reach for vx's whole px, 3 px at 0) -> y on it, and landing
 *     faster than 2.0 bounces: the Apple (big) at 4.0 up (so it bounces on as long as it keeps
 *     landing that fast: it rises 62 px and lands at 3.875), shaking the screen 0x13 frames (0x874E)
 *     with sound 0x18 when on screen and the screen isn't shaking; App2 at 3/4 of its speed, sound 0x29
 *     when on screen. Else vy 0. Then on flat ground (Map_GetGroundHeight's 1): above 1.0 it slows by
 *     0x400 a frame; under 0.625 it stops, at phase 0 only (it rests upright); on a slope it speeds up
 *     downhill by 0x800 a frame (0x8000 the frame it bounces), twice while it still rolls uphill, up
 *     to 1.5.
 *   - the phase + vx.
 *   - Actor_CheckStompedByPlayer (0x1A352): Flink bounces off it, sound 0x18; nothing else that frame.
 *   - otherwise, in the air: the touch test (0x1A462: its hitbox against Flink's, Player_TakeHit; App2
 *     sliding (SL45) into it is knocked away at 2.0 and hit). On the ground, App2: at rest it can be
 *     picked up (Actor_CheckPickedUp 0x1ABB4), rolling it touches. The Apple (big): rolling and not
 *     pushed last frame, it touches while within 20 px of Flink across or rolling toward him;
 *     otherwise the push check (Actor_CheckPushedByPlayer 0x1AA56, kept in +0x4A): pushed, with Flink
 *     on the ground faster than 0.625, it rolls at his speed (at most 1.0) unless it already rolls
 *     faster or the other way.
 *   - facing (+3 bit 3) from vx; moving, the touch test on other actors (Actor_FindTouchingActor
 *     0x1A5A0: every other slot by the frame's parity, the first one with +6 bit 3 touched is hit); a
 *     wall at its front (0x1AD4C) turns it back.
 * - The hook 0x242B2 (App2 thrown): landed (vy 0), it gets the think 0x242CE back, rolling on at its
 *   speed.
 * - The draw (0x245FE): phase (+0x46's whole px / 4) & 15, 8 phases taking phase / 2. The first half
 *   of the turn is the frames rotated at 0x1DC56 in 22.5 (45) degree steps, phase 0 being ROLL itself;
 *   the second half the same drawn upside down (sprite flags 0x1800).
 * Here: the converter's 'Spik Appl' and 'Spik App2' (Lomax's decoration, object 51), whose ROLL has the
 * 16 frames (see the converter's bigapple_records), App2's also DEAD (its break-up) and MAIN.WLD's Shot
 * SPIN (thrown as a shot); params start speed and phases (16 or 8). Its update is the think and it holds
 * ROLL's frame of the phase. The actor touches nothing itself (no Lomax contact): the touch and the stomp
 * are Flink's tests against Lomax's hitbox on its plane, a stomp Lomax's enemy bounce
 * (Player_BounceOffEnemy, stomp_bounce) and a touch Player_TakeHit; Lomax's TUMB (his roll) is the slide,
 * as for the Falling apple. Flink's facing mirrors its hitbox, not the drawing (0x245FE clears +3 bit 3):
 * it is kept here (left) with AF_LEFT clear. The push is rock.c's push_check and rock_after (Flink's
 * Player_Update push, its PUSH pose and 1.0 limit). App2 is picked up and thrown through grab.c
 * (bigapple_grab_kind), its hook app2_after; thrown as a shot it is grab.c's shot. The touch test on
 * other actors reaches the Flink enemies (enemy_struck), the Falling apples, the Raketen wurms and App2s
 * as grab.c's thrown objects do; the screen shake is razorback.c's (Flink's counter). The Rocks (+6 bit 3)
 * are not tested (none in TREE). Sounds: Lomax's ROCKHITX (38) for 0x18, the pot bounce for 0x29, the
 * enemy hit for 0x48. "On screen" (+6 bit 4) is enemy_on_screen.
 */
#include "enemy.h"

#define AP_SPEED        0x28F            /* 0x24296: 'start speed' / 100 px a frame */
#define AP_GRAVITY      0x2000           /* 0x24328 */
#define AP_VMAX         0x40000          /* 0x1B0E6: 4.0 down at most */
#define AP_BOUNCE       0x20000          /* 0x24360: landing faster than 2.0 bounces */
#define AP_BIG_BOUNCE   0x40000          /* 0x24372 */
#define AP_QUAKE        0x13             /* 0x2438E */
#define AP_ROLL_MAX     0x10000          /* 0x243EC: flat ground slows it above 1.0 ... */
#define AP_ROLL_SLOW    0x400            /*   by this a frame */
#define AP_ROLL_STOP    0xA000           /* 0x24410: and stops it under 0.625 at phase 0 */
#define AP_SLOPE        0x800            /* 0x24434: downhill a frame, x 16 the frame it bounces */
#define AP_SLOPE_MAX    0x18000          /* 0x2444A */
#define AP_NEAR         20               /* 0x244E4 */
#define AP_PUSH_MIN     0xA000           /* 0x24526 */
#define AP_PUSH_MAX     0x10000          /* 0x24530 */
#define AP_SLIDE_VX     0x20000          /* 0x1A4CA */
#define AP_SLIDE_PUFF   0x14             /* 0x1A4EC: the puff 20 px in front of Flink, 8 up */
#define AP_CELLS        4                /* 0x1AE9E: d2 = 3, dbra */
#define AP_CARRY_DX     5                /* 0x24232: App2's +0x10, +0x12 */
#define AP_CARRY_DY     8
#define SND_THUD        38               /* ROCKHITX for Flink's 0x18 (statue.c) */

/* 0x1AECA: the ground's reach by vx's whole px, -8 .. 7 (razorback.c, wagon.c) */
static const BYTE STICK[16] = { 10, 10, 10, 10, 9, 7, 6, 4, 3, 4, 6, 7, 10, 10, 10, 10 };

typedef struct {
    BYTE *type;
    int small;                           /* App2 (+0x4C clear) */
    int frames8;                         /* 8 phases ('16 phases' clear) */
    int vy;                              /* +0x20, 16.16, up-positive (vx is A_VX) */
    int phase;                           /* +0x46 */
    int pushed;                          /* +0x4A */
    int left;                            /* +3 bit 3 */
    int hit;                             /* +3 bit 6 */
} Appl;
static Appl g_appl[MAX_ACTORS];

void bigapples_reset(void) { memset(g_appl, 0, sizeof(g_appl)); }

static void __cdecl bigapple_update(BYTE *a);

static Appl *appl_of(const BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_appl[i].type && g_appl[i].type == A_TYPE(a) ? &g_appl[i] : NULL;
}

static const char *kind(const Appl *p) { return p->small ? "app2" : "apple (big)"; }

/* 0x1A170: its record's hitbox, mirrored by its facing */
static int ap_box(const BYTE *a, const Appl *p, int *x1, int *y1, int *x2, int *y2)
{
    const BYTE *r = A_RECORD(a);
    if (!r) return 0;
    if (p->left) { *x1 = A_X(a) - HB_X2(r); *x2 = A_X(a) - HB_X1(r); }
    else { *x1 = A_X(a) + HB_X1(r); *x2 = A_X(a) + HB_X2(r); }
    *y1 = A_Y(a) + HB_Y1(r);
    *y2 = A_Y(a) + HB_Y2(r);
    return 1;
}

/* 0x1AE96: the ground under it (*g), within reach. Map_GetGroundHeight's kind: 1 flat, > 0 a slope
   going down to the right, < 0 to the left; 0 no ground */
static int ap_ground(BYTE *a, int *g)
{
    int v = A_VX(a) >> 16, y = A_Y(a);
    unsigned r = Plane_FindGroundY(PLANE(A_PLANE(a)), A_X(a), y - 16, AP_CELLS);
    if (v < -8) v = -8;
    if (v > 7) v = 7;
    if (r == NO_GROUND || abs((int)r - y) > STICK[v + 8]) return 0;
    *g = (int)r;
    if (!*G_GROUNDSLOPECLASS) return 1;
    return *G_GROUNDMIRRORED ? -2 : 2;
}

/* 0x1AD4C: a wall at its front (x + its hitbox's right, mirrored x - right), from y - 1 up
   |top| / 16 + 2 cells; none outside the map */
static int ap_wall(const BYTE *a, const Appl *p)
{
    const BYTE *pl = PLANE(A_PLANE(a)), *coll = P_COLL(pl), *r = A_RECORD(a);
    int w = P_W(pl), h = P_H(pl), x, y = A_Y(a) - 1, n;
    if (!r || P_EMPTY(pl) || !coll) return 0;
    x = p->left ? A_X(a) - HB_X2(r) : A_X(a) + HB_X2(r);
    if (x < 0 || x >= w * 16 || y < 0 || y >= h * 16) return 0;
    for (n = abs(HB_Y1(r)) / 16 + 2; n > 0 && y >= 0; n--, y -= 16) {
        BYTE b = coll[(x >> 4) + (y >> 4) * w];
        if ((b & 0xC0) == 0x80 && b & (p->left ? 0x10 : 0x20)) return 1;
    }
    return 0;
}

/* 0x1A352: in the air, not in the water, vy under 2.0 up, not hurt, dying or riding, his box across
   its hitbox and his feet from its top - 2 to its top + half its height (at most 20 px) */
static int ap_stomped(const BYTE *a, const Appl *p)
{
    const BYTE *pa = *G_PLAYERANIM;
    int px = *G_PLAYERX, py = *G_PLAYERY, x1, y1, x2, y2, deep;
    if (!pa || A_PLANE(a) != *G_PLAYERPLANE || !ap_box(a, p, &x1, &y1, &x2, &y2)) return 0;
    if ((*G_PLAYERSTATE & (PS_AIR | PS_WATER | PS_ROPE | PS_FLY)) != PS_AIR || *G_PLAYERVY <= -0x20000) return 0;
    if (*G_PLAYERDEATHTIMER || *G_PLAYERHURTTIMER > 0 || ride_active()) return 0;
    if (x1 > px + HB_X2(pa) || x2 < px + HB_X1(pa)) return 0;
    deep = (y2 - y1) / 2;
    if (deep > 20) deep = 20;
    return py >= y1 - 2 && py <= y1 + deep;
}

/* 0x1A462: touching Lomax hurts him; App2 is knocked away by his roll (Flink's slide, +6 bit 3) */
static void ap_touch(BYTE *a, Appl *p)
{
    const BYTE *pa = *G_PLAYERANIM;
    int x1, y1, x2, y2, px = *G_PLAYERX, py = *G_PLAYERY;
    if (!pa || A_PLANE(a) != *G_PLAYERPLANE || !ap_box(a, p, &x1, &y1, &x2, &y2)) return;
    if (x2 < px + HB_X1(pa) || x1 > px + HB_X2(pa) || y2 < py + HB_Y1(pa) || y1 > py + HB_Y2(pa)) return;
    if (p->small && *(const DWORD *)(pa + 4) == FOURCC("TUMB")) {
        int left = (*G_PLAYERFLAGS & AF_LEFT) != 0;
        if (left ? A_X(a) < px : A_X(a) > px) {
            A_VX(a) = left ? -AP_SLIDE_VX : AP_SLIDE_VX;
            Effect_SpawnPoof(px + (left ? -AP_SLIDE_PUFF : AP_SLIDE_PUFF), py - 8);
            p->hit = 1;
            flog("%s at (%d,%d) knocked away by Lomax's roll", kind(p), A_X(a), A_Y(a));
            return;
        }
    }
    if (*G_PLAYERHURTTIMER) return;
    flog("%s at (%d,%d) hurts Lomax", kind(p), A_X(a), A_Y(a));
    Player_TakeHit(A_X(a), 0, 0);
}

/* 0x24500: the push check; pushed by Lomax on the ground it rolls at his speed */
static void ap_push(BYTE *a, Appl *p)
{
    int x1, y1, x2, y2, d0, v = A_VX(a), was = p->pushed;
    p->pushed = ap_box(a, p, &x1, &y1, &x2, &y2) && push_check(a, x1, y1, x2, y2, &A_VX(a), p->vy >> 16);
    if (p->pushed && !was) flog("%s at (%d,%d) pushed", kind(p), A_X(a), A_Y(a));
    if (!p->pushed || *G_PLAYERSTATE & PS_AIR) return;
    d0 = abs(*G_PLAYERVX);
    if (d0 <= AP_PUSH_MIN) return;
    if (d0 > AP_PUSH_MAX) d0 = AP_PUSH_MAX;
    if (d0 < abs(v)) return;
    if (*G_PLAYERVX < 0) d0 = -d0;
    if (v && (v ^ d0) < 0) return;
    A_VX(a) = d0;
}

/* 0x1A5A0: the first actor its hitbox touches among this frame's slots is hit (0x1A6D2) */
static void ap_strike(BYTE *a, const Appl *p)
{
    int x1, y1, x2, y2, k, self = slot_of(a);
    if (!ap_box(a, p, &x1, &y1, &x2, &y2)) return;
    for (k = *G_TICKCOUNTER & 1; k < MAX_ACTORS; k += 2) {
        BYTE *t = actor_at(k);
        int e, tx1, ty1, tx2, ty2;
        if (k == self || !actor_active(k) || !A_TYPE(t) || A_LAYER(t) == KILLED_LAYER || A_PLANE(t) != A_PLANE(a)) continue;
        if (bigapple_struck(t, x1, y1, x2, y2) || apple_struck(t, x1, y1, x2, y2) || wurm_struck(t, x1, y1, x2, y2) ||
            donk_struck(t, a, x1, y1, x2, y2, 0)) return;
        if ((e = enemy_slot(t)) < 0 || g_enemy[e].dead || !(A_FLAGS(t) & AF_HITTABLE) || grab_held(t)) continue;
        if (!actor_box(t, &tx1, &ty1, &tx2, &ty2) || x1 > tx2 || tx1 > x2 || y1 > ty2 || ty1 > y2) continue;
        flog("%s at (%d,%d) hits an enemy at (%d,%d)", kind(p), A_X(a), A_Y(a), A_X(t), A_Y(t));
        enemy_struck(t, 0);
        return;
    }
}

/* the hit (0x242CE's first test): DEAD broken up */
static void ap_break(BYTE *a, Appl *p)
{
    flog("%s at (%d,%d) breaks", kind(p), A_X(a), A_Y(a));
    p->type = NULL;
    if (enemy_on_screen(a)) Sound_PlayEffect(a, SND_HIT);
    set_anim(a, "DEAD");
    hold_frame(a, 0);
    Actor_AdvanceAnim(a);
    actor_break_up_sound(a, 0);
}

/* ROLL's frame of the phase (0x245FE) */
static void ap_frame(BYTE *a, const Appl *p)
{
    int q = (p->phase >> 18) & 15;
    if (p->frames8) q &= ~1;
    hold_frame(a, q);
    Actor_AdvanceAnim(a);
}

/* 0x242CE */
static void __cdecl bigapple_update(BYTE *a)
{
    Appl *p = appl_of(a);
    int g, d1, on;
    if (!p) return;
    A_FLAGS(a) &= ~AF_LEFT;
    if (p->hit) { ap_break(a, p); return; }
    on = enemy_on_screen(a);
    p->vy -= AP_GRAVITY;                             /* 0x1B0E2 */
    if (p->vy < -AP_VMAX) p->vy = -AP_VMAX;
    A_FY(a) -= p->vy;
    A_FX(a) += A_VX(a);
    set_pos(a);
    if (p->vy <= 0 && (d1 = ap_ground(a, &g)) != 0) {
        int av = abs(p->vy);
        place_y(a, g);
        if (av <= AP_BOUNCE) p->vy = 0;
        else if (!p->small) {
            p->vy = AP_BIG_BOUNCE;
            if (on && !flink_quake()) {
                flink_quake_set(AP_QUAKE);
                Sound_PlayEffect(a, SND_THUD);
                flog("%s bounces at (%d,%d), the screen shakes", kind(p), A_X(a), A_Y(a));
            }
        } else {
            p->vy = (int)(((unsigned)av * 3) >> 2);
            if (on) Sound_PlayEffect(a, SND_POTBOUNCE);
        }
        if (d1 == 1) {                               /* flat */
            int v = abs(A_VX(a));
            if (v > AP_ROLL_MAX) { v -= AP_ROLL_SLOW; A_VX(a) = p->left ? -v : v; }
            else if (v < AP_ROLL_STOP && !((p->phase >> 18) & 15)) {
                if (A_VX(a)) flog("%s stops at (%d,%d)", kind(p), A_X(a), A_Y(a));
                p->phase = 0;
                A_VX(a) = 0;
            }
        } else {                                     /* a slope: downhill */
            int d2 = p->vy ? AP_SLOPE << 4 : AP_SLOPE;
            if (d1 > 0) { if (A_VX(a) < AP_SLOPE_MAX && (A_VX(a) += d2) < 0) A_VX(a) += d2; }
            else if (A_VX(a) > -AP_SLOPE_MAX && (A_VX(a) -= d2) > 0) A_VX(a) -= d2;
        }
    }
    p->phase += A_VX(a);
    if (ap_stomped(a, p)) {
        flog("%s at (%d,%d) stomped", kind(p), A_X(a), A_Y(a));
        Player_BounceOffEnemy();
        stomp_bounce();
        Sound_PlayEffect(a, SND_THUD);
    } else if (p->vy) ap_touch(a, p);
    else if (p->small) { if (A_VX(a)) ap_touch(a, p); }   /* (at rest: picked up, bigapple_grab_kind) */
    else if (!p->pushed && A_VX(a)) {
        int dx = A_X(a) - *G_PLAYERX;
        if (abs(dx) < AP_NEAR || (dx < 0) != (A_VX(a) >> 16 < 0)) ap_touch(a, p);
        else ap_push(a, p);
    } else ap_push(a, p);
    if (A_VX(a) > 0) p->left = 0;
    else if (A_VX(a) < 0) p->left = 1;
    if (A_VX(a) || p->vy) ap_strike(a, p);
    if (ap_wall(a, p)) { A_VX(a) = -A_VX(a); p->left ^= 1; }
    if (below_map(a)) {
        flog("%s fell out of the map at x %d", kind(p), A_X(a));
        p->type = NULL;
        Actor_Despawn(a);                            /* (its placement spawns it again) */
        return;
    }
    ap_frame(a, p);
}

/* 0x242B2, App2's +0x5C hook while it is a thrown object: landed, it rolls on (its think again) */
static void __cdecl app2_after(BYTE *a)
{
    Appl *p = appl_of(a);
    if (!p || A_VY(a)) return;
    p->vy = 0;
    p->left = A_VX(a) < 0;
    grab_let_go(a);
    A_FLAGS(a) &= ~AF_LEFT;
    A_UPDATE(a) = (void *)bigapple_update;
    flog("app2 lands at (%d,%d), rolling at %d/65536", A_X(a), A_Y(a), A_VX(a));
}

/* grab.c: an App2 at rest (0x244B8: vx and vy 0, its own think) can be picked up; light, no fuse */
int bigapple_grab_kind(BYTE *a, GrabKind *k)
{
    Appl *p = appl_of(a);
    if (!p || !p->small || p->hit || A_VX(a) || p->vy || A_UPDATE(a) != (void *)bigapple_update) return 0;
    memset(k, 0, sizeof(*k));
    k->dx = AP_CARRY_DX;
    k->dy = AP_CARRY_DY;
    k->after = app2_after;
    return 1;
}

/* a thrown object's (or a rolling apple's) box x1..y2 touches an App2 (+6 bit 3): it is hit. Not one
   held by grab.c (Flink's thrown one would be hit too: Actor_ThrownThink gives it its think back) */
int bigapple_struck(BYTE *t, int x1, int y1, int x2, int y2)
{
    Appl *p = appl_of(t);
    int tx1, ty1, tx2, ty2;
    if (!p || !p->small || p->hit || grab_held(t) || !ap_box(t, p, &tx1, &ty1, &tx2, &ty2)) return 0;
    if (x2 < tx1 || x1 > tx2 || y2 < ty1 || y1 > ty2) return 0;
    flog("app2 at (%d,%d) hit", A_X(t), A_Y(t));
    p->hit = 1;
    return 1;
}

/* (from spik_spawn) params: start speed, phases (16 or 8) */
void bigapple_spawn(BYTE *a, BYTE *def, int small)
{
    int i = slot_of(a);
    Appl *p;
    if (i < 0 || !def) return;
    p = &g_appl[i];
    memset(p, 0, sizeof(*p));
    p->type = A_TYPE(a);
    p->small = small;
    p->frames8 = def_param(a, def, 1, 16) == 8;
    A_VX(a) = def_param(a, def, 0, 0) * AP_SPEED;
    A_VY(a) = 0;
    A_FLAGS(a) &= ~(0x200000u | AF_CONTACT | AF_LEFT);   /* the actor itself touches nothing */
    A_LAYER(a) = (short)FLINK_LAYER(1);              /* (a5) = 0x7D6C: list 1 */
    A_UPDATE(a) = (void *)bigapple_update;
    A_FX(a) = A_X(a) << 16;
    place_y(a, A_Y(a));
    Actor_SetAnim(a, FOURCC("ROLL"));
    ap_frame(a, p);
    flog("%s at (%d,%d): start speed %d/65536, %d phases", kind(p), A_X(a), A_Y(a), A_VX(a), p->frames8 ? 8 : 16);
}
