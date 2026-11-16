/*
 * rock.c - Flink's Rock, the block of stones Flink pushes (init 0x2917C, think 0x291EC; 11-1, 11-2,
 * MOUNTAIN 8 and 16, 21-1's two breakable ones; README "Rocks").
 *
 * Flink's:
 * - Init: EINS, ZWEI or DREI (one, two or three stones high; checkboxes 2, 4), draw list 2, hit by
 *   thrown objects (+6 bit 3), param 0 its hits (+0x4C) with checkbox 8 ('breakable').
 * - Each frame (0x291EC):
 *   - hit by a thrown object (+3 bit 6, the hitter at +0x34): at its top (|the rock's hitbox top -
 *     the object's y| <= 8) and falling, the object gets vy 2.0 up and vx 1.5 x its own, at least
 *     2.0; anywhere else it is turned back (mirrored, vx negated, moved 2 x the new vx); sound 0x29
 *     both times. With checkbox 8 ('razorback-check', 21-1's two) any hitter but a running Razorback
 *     (think 0x275BA) counts down the hits (+0x4C), and below 0 it breaks (0x293BC: TOD1 / TOD2 /
 *     TOD3, Actor_ForgetForever 0x1B1E8 breaking that frame up, with the pending bit set back, so it
 *     is there again after a lost life). A Razorback that runs into it has already died in its own think
 *     (0x27656) when the rock's think sees the hit, so every Razorback counts, and is turned back by it as
 *     a thrown object (0x292AC); Flink sliding into it counts too. The player as the hitter (0x81E2)
 *     breaks it only while he casts a spell (0x828E bit 7). Checkbox 16 ('touch down': breaks when it
 *     lands) isn't converted.
 *   - it takes Flink's facing and tests for a wall at that side (0x1AD4C: the column at x + its
 *     hitbox's right, mirrored x - right, from y - 1 up |top| / 16 + 2 cells, a wall cell blocking
 *     that way): a wall sets vx 1, "it doesn't move", else vx 0.
 *   - falling (vy set): 0x1AE3E, vy + 0.25 a frame up to 4.0, landing on ground met between the old
 *     and new y, sound 0x35; on the ground: 0x1AE96,
 *     the ground under its x within 3 cells and 3 px (the table 0x1AEDA at vx 0), else it starts
 *     falling at 0.25 (not moving that frame).
 *   - Flink stands on it (0x1A28A), then the push check, falling or not.
 * - The push check, Actor_CheckPushedByPlayer (0x1AA56; rock_check): not while dying; the side is the
 *   rock's x against Flink's (right if greater); on the ground with the pad held away from it, no; the
 *   point 16 px in front of Flink (one more px plus his speed's whole px when both he and the rock
 *   move, the table 0x1AA96) inside the hitbox across, his feet not above its top + |its vy| + 1 and
 *   his feet - 32 not below its bottom: it is g_PushedActor (0x8300). Met for the first time
 *   (g_PushedActor of the last frame, 0x8302) with vx 0, Flink's speed is divided by 16; with vx not 0
 *   on his left and nothing carried, he faces left.
 * - Flink's Player_Update (0x1C31C, after his x moved; rock_after): the offset from him is 16 px from
 *   the hitbox's near side. In the air (0x828E bit 0): his feet - 30 at or below its bottom (under it)
 *   push him down 2 x |vy| + 2 px, vy 2.0 down; else rising he is put at its side and it is let go;
 *   falling it is let go and he moves 2 px away from it, his speed toward it taken off. On the
 *   ground: carrying something, he is put at its side and it is let go; with vx not 0 he is put at
 *   its side; else the rock is moved to him + the offset, and let go if he doesn't move. Then the
 *   animation (0x1CB16): PUSH while it is pushed.
 * The push speed: PUSH's animation code (0x1CB20) restarts Flink's 60-frame turn timer 0x828F every
 * frame, and while it runs his movement takes the timer rows of PlayerSpeedTable (0x1D004 on flat
 * ground: -1.0 / 1.0, accel 0x2400), so he pushes at 1.0 px a frame, not his 2.0 walk. Here
 * rock_check sets g_PlayerSpeedCapOverride to 1.0 for this tick's Player_Update while Lomax pushes on
 * the ground (rock_after clears it).
 * Not Flink's: a touch in the air while Lomax moves away from the rock doesn't count. In Flink a
 * jump away from a rock he stands against meets it again in the air (his x hardly moved), which puts
 * him back at its side and, a 'first' touch every frame (the rising case lets go), divides his speed
 * by 16 each frame: Lomax, slower to turn in the air, was held there.
 * Here: the converter's 'Mega Rock' (MovingPlatform, object 64, static, whose spawn platforms.c wraps)
 * runs rock_update as its think, before Player_Update as Flink's thinks run before his; rock_after
 * runs after Player_Update (player_tick). Lomax's own push (Actor_CheckPlayerPush, g_pPushBlock) is
 * not used: it pulls him 2 px a tick toward a block he jumps against and slows pushing to 1.0. Only
 * his PUSH pose (kept running across ticks, as Player_Update picks his walk) is Lomax's. The sound:
 * Flink's rock think (0x2935C), pushed with Flink in PUSH, starts the rolling 0x38 (E41, endless)
 * once (+0x50) when on screen, and stops it (Pcm_StopEffect 0x3FE4A, 0x293A8) when it isn't pushed;
 * here sfx_loop keeps it going while Lomax pushes it in his PUSH pose (sfx.c). The Apple (big) has
 * no push sound, as in Flink. The map's edge stops it as a wall would (Flink's test ignores it).
 * The push check and Player_Update's push are Flink's for every pushable object: push_check and
 * rock_after serve the Apple (big) too (bigapple.c), with its own hitbox and vx.
 */
#include "enemy.h"

#define ROCK_GRAVITY      0x4000         /* 0x1AE42 */
#define ROCK_VMAX         0x40000
#define ROCK_STICK        3              /* 0x1AEDA[0]: the ground within 3 px */
#define ROCK_TOP_REACH    8              /* 0x29252 */
#define PUSH_FRONT        16             /* 0x1AA60 */
#define PUSH_HEAD         32             /* 0x1AAEC: his feet - 32 */
#define PUSH_UNDER        30             /* 0x1C3A8 */
#define PUSH_NUDGE        2              /* 0x1C404 */
#define PUSH_FOURCC       0x50555348u    /* 'PUSH' */
#define ROCK_LAYER        FLINK_LAYER(2) /* draw list 2 (0x29184: 0x7D6E) */
#define PUSH_SPEED        0x10000        /* 0x1D004: the timer row's limit on flat ground */

/* per actor slot: its type, its fall speed (16.16, 0 on the ground), vx 1 = blocked (Flink's +0x1C),
   breakable (checkbox 8) and the hits it still takes (+0x4C) */
static struct { BYTE *type; int vy, blocked, breakable, hits; } g_rock[MAX_ACTORS];
static int g_pushed = -1, g_prev = -1;   /* Flink's g_PushedActor 0x8300 and last frame's 0x8302 (slots) */
static BYTE *g_pushType;                 /* the pushed actor's type, */
static const int *g_pushVx;              /*   its vx (Flink's +0x1C: a rock's 'blocked', an Apple (big)'s speed) */
static int g_pushX1, g_pushX2, g_pushY2; /*   and its hitbox from its point at the check */
static int g_pushAir;                    /* Lomax was in the air at the check (0x828E bit 0) */
static int g_animPos = -1;               /* the PUSH pose's position last tick, -1: not pushing */

void rocks_reset(void) { memset(g_rock, 0, sizeof(g_rock)); g_pushed = g_prev = g_animPos = -1; g_pushType = NULL; }

static int rock_slot(const BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_rock[i].type && g_rock[i].type == A_TYPE(a) ? i : -1;
}

/* 0x1AD4C: a wall at the rock's side toward `right`, or the map's edge */
static int rock_wall(const BYTE *a, int right)
{
    const BYTE *pl = PLANE(A_PLANE(a)), *coll = P_COLL(pl), *r = A_RECORD(a);
    int w = P_W(pl), h = P_H(pl), x, y = A_Y(a) - 1, n;
    if (!r) return 1;
    x = right ? A_X(a) + HB_X2(r) : A_X(a) - HB_X2(r);
    if (x < 0 || x >= w * 16) return 1;
    if (P_EMPTY(pl) || !coll) return 0;
    for (n = abs(HB_Y1(r)) / 16 + 2; n > 0 && y >= 0; n--, y -= 16) {
        BYTE b;
        if (y >= h * 16) continue;
        b = coll[(x >> 4) + (y >> 4) * w];
        if ((b & 0xC0) == 0x80 && b & (right ? 0x20 : 0x10)) return 1;
    }
    return 0;
}

/* Lomax can push: not dying, on its plane, not hanging, flying or riding */
static int can_push(const BYTE *a)
{
    return !*G_PLAYERDEATHTIMER && *G_PLAYERPLANE == A_PLANE(a) && !(*G_PLAYERSTATE & (PS_ROPE | PS_FLY)) &&
           !ride_active() && !ropeslide_active() && !*G_PLANETRANSITIONSCALE;
}

/* 0x1AA56, for a rock or an Apple (big) (bigapple.c): its hitbox x1..y2, its vx (16.16; read again
   by push_after) and its vy's whole px. 1 = it is g_PushedActor. With vx not 0 and moving toward
   Lomax's side of it (vx's whole px against the side, 0x1AB8C), carrying nothing, he faces against
   vx: a rock's vx 1 is "it doesn't move", which turns him left when it is on his left. */
int push_check(BYTE *a, int x1, int y1, int x2, int y2, const int *vx, int vy)
{
    int px = *G_PLAYERX, py = *G_PLAYERY, pvx = *G_PLAYERVX, air = (*G_PLAYERSTATE & PS_AIR) != 0;
    int right = A_X(a) > px, front = px, i = slot_of(a);
    WORD held = *G_INPUTHELD;
    if (i < 0 || !can_push(a)) return 0;
    if (!air && !*G_BINPUTLOCKED && held & (right ? 0x8000 : 0x2000)) return 0;   /* walking away (left / right) */
    if (*vx && pvx) {
        int v = pvx >> 16, t = (v < 0 ? -v : v) + 1;     /* the table 0x1AA96 */
        front += pvx < 0 ? -t : t;
    }
    front += right ? PUSH_FRONT : -PUSH_FRONT;
    if (front < x1 || front > x2) return 0;
    if (py < y1 + abs(vy) + 1 || py - PUSH_HEAD > y2) return 0;
    if (air && (right ? pvx < 0 : pvx > 0)) return 0;    /* (not Flink's: jumping away from it) */
    g_pushed = i;
    g_pushType = A_TYPE(a);
    g_pushVx = vx;
    g_pushX1 = A_X(a) - x1;
    g_pushX2 = x2 - A_X(a);
    g_pushY2 = y2 - A_Y(a);
    g_pushAir = air;
    if (!air) *G_PLAYERSPEEDCAP = PUSH_SPEED;            /* (0x1CB20: the turn timer's rows) */
    if (!*vx) {
        if (i != g_prev) *G_PLAYERVX = pvx >> 4;
    } else if (((*vx >> 16) < 0) != !right && !grab_carrying()) {
        if (*vx >> 16 < 0) *G_PLAYERFLAGS &= ~AF_LEFT;
        else *G_PLAYERFLAGS |= AF_LEFT;
    }
    return 1;
}

static void rock_check(BYTE *a, int i)
{
    int x1, y1, x2, y2;
    if (actor_box(a, &x1, &y1, &x2, &y2)) push_check(a, x1, y1, x2, y2, &g_rock[i].blocked, g_rock[i].vy >> 16);
}

static void __cdecl rock_update(BYTE *a)
{
    int i = rock_slot(a), y = A_Y(a), g, landed = 0;
    if (i < 0) { MovingPlatform_Update(a); return; }
    g_rock[i].blocked = rock_wall(a, !(*G_PLAYERFLAGS & AF_LEFT));    /* the side Lomax faces */
    A_FY(a) = y << 16;
    A_VX(a) = 0;
    if (g_rock[i].vy) {                                  /* 0x1AE3E */
        int ny;
        g_rock[i].vy += ROCK_GRAVITY;
        if (g_rock[i].vy > ROCK_VMAX) g_rock[i].vy = ROCK_VMAX;
        ny = (A_FY(a) + g_rock[i].vy) >> 16;
        g = ground_y(a, A_X(a), ny);
        if (g != NO_GROUND && g <= ny && g >= y) { A_VY(a) = (g - y) << 16; g_rock[i].vy = 0; landed = 1; }
        else A_VY(a) = g_rock[i].vy;
    } else {                                             /* 0x1AE96 */
        g = ground_y(a, A_X(a), y);
        if (g == NO_GROUND || abs(g - y) > ROCK_STICK) {
            g_rock[i].vy = ROCK_GRAVITY;
            A_VY(a) = 0;
            flog("rock falls at (%d,%d), ground %d", A_X(a), y, g);
        }
        else A_VY(a) = (g - y) << 16;
    }
    Platform_MoveAndCarryPlayer(a, NULL);                /* (0x1A28A: Lomax stands on it) */
    A_VY(a) = 0;
    if (landed) {
        flink_sound(a, 0x35);
        flog("rock lands at (%d,%d)", A_X(a), A_Y(a));
    }
    if (below_map(a)) { g_rock[i].type = NULL; Actor_Despawn(a); return; }   /* its placement spawns it again */
    rock_check(a, i);
    Actor_AdvanceAnim(a);
}

static void set_player_x(int x) { *G_PLAYERX = x; ((volatile short *)G_PLAYERXFIXED)[1] = (short)x; }

/* before Player_Update (player_tick): the PUSH pose's position, which Player_Update replaces by his walk */
void rock_before(void)
{
    const BYTE *r = *G_PLAYERANIM;
    g_animPos = g_active && r && *(const DWORD *)(r + 4) == PUSH_FOURCC ? *(short *)(G_PLAYERACTOR + 0x6A) : -1;
}

/* after Player_Update (player_tick): 0x1C31C, then the PUSH pose (0x1CB16) */
void rock_after(void)
{
    BYTE *a = actor_at(g_pushed);
    int i = g_pushed, px, off, y2;
    g_prev = g_pushed;
    g_pushed = -1;
    if (g_active && *G_PLAYERSPEEDCAP == PUSH_SPEED) *G_PLAYERSPEEDCAP = 0;
    if (!g_active || !a || !g_pushType || A_TYPE(a) != g_pushType || A_LAYER(a) == KILLED_LAYER) return;
    px = *G_PLAYERX;
    off = A_X(a) > px ? PUSH_FRONT + g_pushX1 : -PUSH_FRONT - g_pushX2;
    y2 = A_Y(a) + g_pushY2;
    if (g_pushAir) {
        if (*G_PLAYERY - PUSH_UNDER >= y2) {             /* under it */
            int vy = *G_PLAYERVY;
            *G_PLAYERYFIXED += 2 * abs(vy) + (2 << 16);
            *G_PLAYERY = *G_PLAYERYFIXED >> 16;
            *G_PLAYERVY = 0x20000;
            g_prev = i;
            return;
        }
        g_prev = -1;
        if (*G_PLAYERVY <= 0) set_player_x(A_X(a) - off);   /* rising: at its side */
        else if (off >= 0) { set_player_x(px - PUSH_NUDGE); if (*G_PLAYERVX > 0) *G_PLAYERVX = 0; }
        else { set_player_x(px + PUSH_NUDGE); if (*G_PLAYERVX < 0) *G_PLAYERVX = 0; }
        return;
    }
    if (grab_carrying()) { g_prev = -1; set_player_x(A_X(a) - off); return; }
    if (*g_pushVx) set_player_x(A_X(a) - off);
    else {
        A_X(a) = px + off;
        ((short *)&A_FX(a))[1] = (short)A_X(a);
        if (!*G_PLAYERVX) { g_prev = -1; return; }
    }
    if (*G_PLAYERSTATE & PS_AIR) return;
    {                                                    /* the PUSH pose, kept running */
        BYTE *p = G_PLAYERACTOR;
        int was = *G_PLAYERANIM && *(const DWORD *)(*G_PLAYERANIM + 4) == PUSH_FOURCC;
        Actor_SetAnimIfChanged(p, PUSH_FOURCC);
        if (!was && g_animPos >= 0) *(short *)(p + 0x6A) = (short)g_animPos;
        Actor_AdvanceAnim(p);
        if (rock_slot(a) >= 0) sfx_loop(a, 0x38);
    }
}

/* 0x293BC: TOD (BOOM here) broken up; actor_break_up clears its placement, so Lomax's placements
   spawn it again after a lost life, as Flink's pending bit set back */
static void rock_break(BYTE *a, int i)
{
    if (g_pushed == i) g_pushed = -1;
    if (g_prev == i) g_prev = -1;
    if (*G_PRIDINGPLATFORM == a) *G_PRIDINGPLATFORM = NULL;
    g_rock[i].type = NULL;
    flog("rock at (%d,%d) breaks", A_X(a), A_Y(a));
    set_anim(a, "BOOM");
    hold_frame(a, 0);
    Actor_AdvanceAnim(a);
    actor_break_up(a);
}

/* a hit from anything but a running Razorback (0x291FE): a breakable rock counts it; 1 = it broke */
static int rock_count_hit(BYTE *a, int i)
{
    if (!g_rock[i].breakable) return 0;
    flog("rock at (%d,%d) hit, %d hits left", A_X(a), A_Y(a), g_rock[i].hits);
    if (--g_rock[i].hits >= 0) return 0;
    rock_break(a, i);
    return 1;
}

/* a rock (for razorback.c's touch test, Actor_FindTouchingActor 0x1A5A0: the rock has +6 bit 3) */
int rock_is(BYTE *a)
{
    int i = a ? rock_slot(a) : -1;
    return i >= 0;
}

/* a Razorback b ran into rock r and died (razorback.c): the rock's think sees a hitter that is no running Razorback
   (0x291FE: counts it), not the player; away from the top (|the rock's hitbox top - b's y| > 8) it
   turns b back (0x292AC: mirrored, vx negated, moved 2 x the new vx), sound 0x29 */
void rock_hit(BYTE *r, BYTE *b)
{
    int i = rock_slot(r), x1, y1, x2, y2;
    if (i < 0 || !actor_box(r, &x1, &y1, &x2, &y2)) return;
    if (abs(y1 - A_Y(b)) > ROCK_TOP_REACH) {
        A_FLAGS(b) ^= AF_LEFT;
        A_VX(b) = -A_VX(b);
        A_FX(b) += 2 * A_VX(b);
        set_pos(b);
        Sound_PlayEffect(r, SND_STOMP);
    }
    rock_count_hit(r, i);
}

/* a thrown object (or shot) o with hitbox x1..y2 met a rock: Flink's 0x291F6. 1 = it hit one */
int rock_bounce(BYTE *o, int x1, int y1, int x2, int y2)
{
    int k;
    if (!g_active) return 0;
    for (k = 0; k < MAX_ACTORS; k++) {
        BYTE *r = actor_at(k);
        int rx1, ry1, rx2, ry2, vx = A_VX(o);
        if (!g_rock[k].type || g_rock[k].type != A_TYPE(r) || !actor_active(k) || A_LAYER(r) == KILLED_LAYER ||
            A_PLANE(r) != A_PLANE(o) || !actor_box(r, &rx1, &ry1, &rx2, &ry2)) continue;
        if (!(x1 <= rx2 && rx1 <= x2 && y1 <= ry2 && ry1 <= y2)) continue;
        if (abs(ry1 - A_Y(o)) <= ROCK_TOP_REACH) {       /* at its top (0x29252) */
            int v;
            if (A_VY(o) <= 0) return 0;                  /* (rising or still: nothing; Flink's vy is
                                                            up-positive there: tst.l, bge) */
            v = abs(vx) * 3 / 2;
            if (v < 0x20000) v = 0x20000;
            A_VX(o) = vx < 0 ? -v : v;
            A_VY(o) = -0x20000;                          /* (bounced up at 2.0: Flink's +0x20000) */
        } else {
            if ((vx > 0) != ((x1 + x2) / 2 < A_X(r)) || !vx) continue;   /* (already going away from it) */
            A_VX(o) = vx = -vx;
            A_FLAGS(o) ^= AF_LEFT;
            A_FX(o) += vx * 2;
            set_pos(o);
        }
        Sound_PlayEffect(o, SND_STOMP);                  /* (Flink's 0x29, as the boss's trunks) */
        flog("thrown object bounces off the rock at (%d,%d)", A_X(r), A_Y(r));
        rock_count_hit(r, k);                            /* (counted once a bounce: Flink counts each
                                                            frame of the hit, which the bounce ends) */
        return 1;
    }
    return 0;
}

/* (from mega_spawn) params Lomax's 5, then breakable, hits */
void rock_spawn(BYTE *a, BYTE *def, int i)
{
    memset(&g_rock[i], 0, sizeof(g_rock[i]));
    g_rock[i].type = A_TYPE(a);
    g_rock[i].breakable = def_param(a, def, 5, 0);
    g_rock[i].hits = def_param(a, def, 6, 0);
    A_LAYER(a) = ROCK_LAYER;
    A_UPDATE(a) = (void *)rock_update;
    flog("rock at (%d,%d)%s", A_X(a), A_Y(a), g_rock[i].breakable ? ", breakable" : "");
}
