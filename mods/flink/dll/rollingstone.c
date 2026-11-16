/*
 * rollingstone.c - Flink's Rolling stone ('Rolling stone', type 37, init 0x23E26, think 0x23E74, draw
 * 0x24062; MOUNTAIN 14 = 23-1, five in its shallow lava; README "Rolling stones"): a stone ball the
 * player stands on and rolls by walking on it, to cross the lava. Flink's:
 * - Init: a placement with spawn flag bit 6 only builds the 7 rotated frames (0x24148, as the Apple
 *   (big)'s) and goes; its checkbox 1 ('16 phases') gives every stone 16 phases (else 8). The others:
 *   draw list 3 (0x7D70), window 0, the phase (+0x48) 0, ROLL.
 * - Each frame (0x23E74):
 *   - it faces the way its vx (+0x1C) goes (+3 bit 3 left);
 *   - blocked (+0x4C): a wall ahead (0x1AD4C: the cells at x +- its hitbox's right, from its feet up
 *     |top| / 16 + 1 cells), or another Rolling stone's hitbox inside x +- 28, y - 48 .. y (0x1A65A),
 *     facing that one;
 *   - it is a platform (Platform_Stand 0x1A29A: moved by its vx, carrying the player), its top the
 *     hitbox's top lowered by {0, 0, 1, 1, 2, 2, 3, 4, 4, 5, 6, 7, 8}[|player x - x| / 2] (0x2400A: a
 *     rounded top);
 *   - stood on: just landed (the riding platform changed), the player's vx / 4. Blocked and the pad not
 *     held the way it faces, vx 0. Otherwise vx = -(the player's vx): walking on it rolls it, and him
 *     with it, the other way; with Left or Right held the player's x also moves by that vx, and while
 *     the phase's whole px is a multiple of 8 he is moved 1 px toward its x (kept on its middle). It
 *     sets 0x828F to 60 (Player_Update's slide timer, 0x1BDE0; Lomax has none);
 *   - not stood on: vx slows by 1/32 a frame to 0 (0x1D448);
 *   - the phase + vx.
 * - Its draw (0x24062): the phase's whole px / 4 & 15 (8 phases: / 2), as the Apple (big)'s: ROLL
 *   itself, its 7 rotated frames, then the same upside down.
 * Here: the converter's 'Spik Rolling' (Lomax's decoration, object 51) with ROLL at 16 angles
 * (bigapple_records, the draw offsets of 0x24062: x - 24, y - 47, upside down y - 46); param phases (16
 * or 8). Its platform is Lomax's Platform_MoveAndCarryPlayer (which moves it by its vx and carries Lomax,
 * as Platform_Stand), the rounded top its surface callback, and Flink's blocking. The ride is Lomax's
 * barrel's (Barrel_Update 0x426D20, roll_ride), not Flink's: Flink's sets the stone's vx to minus the
 * player's while he stands on it, and Lomax's walk (faster, with a slide-out after a release) would
 * roll it at his full speed and walk him off it when he stops. As the barrel's, his walk is held to 2.0
 * a tick on it (scaled as his speed, move_speed: 1.6), it slows by 1/64 a tick, and with no Left or
 * Right held he goes 2/3 of its vx with it.
 * Lomax's culling of the decoration stands for Flink's window: a stone out of the view goes back to
 * its placement.
 */
#include "flink.h"
#include "enemy.h"

#define ROLL_FRICTION   0x400            /* Barrel_Update's slow-down a tick */
#define ROLL_CAP        0x20000          /* Barrel_Update's walking limit on it (flat ground), scaled by move_speed */
#define G_TICKCOUNTER   ((volatile int *)0x0052C564u)
#define ROLL_NEAR_X     28               /* another stone's box (0x23EDA) */
#define ROLL_NEAR_UP    48
#define IN_LEFT         0x8000           /* g_InputHeld (wagon.c) */
#define IN_RIGHT        0x2000

static const int ROLL_TOP[13] = { 0, 0, 1, 1, 2, 2, 3, 4, 4, 5, 6, 7, 8 };   /* 0x2400A */

typedef struct {
    BYTE *type;
    int phase, frames8, left;            /* +0x48, '16 phases' clear, +3 bit 3 */
    int rolling, capped, cap;            /* Barrel_Update's +0x88; G_PLAYERSPEEDCAP set last tick, and to what */
} Roll;
static Roll g_roll[MAX_ACTORS];

void rollingstones_reset(void) { memset(g_roll, 0, sizeof(g_roll)); }

static Roll *roll_of(const BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_roll[i].type && g_roll[i].type == A_TYPE(a) ? &g_roll[i] : NULL;
}

/* the rounded top (0x23F1A): relx is Lomax's x - its x */
static int __cdecl roll_surface(BYTE *a, int relx)
{
    int k = abs((short)relx) >> 1;
    (void)a;
    return ROLL_TOP[k < 12 ? k : 12];
}

/* another live Rolling stone's hitbox in its box (0x23E9E); 1 = blocked, facing it */
static int roll_meets(BYTE *a, Roll *r)
{
    int i = slot_of(a), k, x1, y1, x2, y2;
    for (k = 0; k < MAX_ACTORS; k++) {
        BYTE *o = actor_at(k);
        if (k == i || !g_roll[k].type || g_roll[k].type != A_TYPE(o) || !actor_active(k) || A_LAYER(o) == KILLED_LAYER) continue;
        if (!actor_box(o, &x1, &y1, &x2, &y2)) continue;
        if (x2 < A_X(a) - ROLL_NEAR_X || x1 > A_X(a) + ROLL_NEAR_X || y2 < A_Y(a) - ROLL_NEAR_UP || y1 > A_Y(a)) continue;
        r->left = A_X(a) > A_X(o);
        return 1;
    }
    return 0;
}

/* the ride, as Lomax's Barrel_Update (0x426D20) on flat ground: until he first walks on it (r->rolling,
   the barrel's +0x88) Lomax's walk is held to its limit (ROLL_CAP); walking on it, its vx is minus his
   and he is moved back by it, kept on its middle every 8th tick; otherwise (no Left or Right held, or
   it still rolling its own way) he goes 2/3 of its vx with it. Jump pressed: his vx 0. A stone blocked
   the way it would go stays put. */
static void roll_ride(BYTE *a, Roll *r, int blocked)
{
    WORD held = *G_INPUTHELD, pressed = *G_INPUTPRESSED;
    int pv, v = A_VX(a), cap = move_speed(ROLL_CAP);
    if (*G_BINPUTLOCKED) held = pressed = 0;
    if (!r->rolling) {
        if (*G_PLAYERVX > cap) *G_PLAYERVX = cap;
        else if (*G_PLAYERVX < -cap) *G_PLAYERVX = -cap;
    }
    pv = -*G_PLAYERVX;
    *G_PLAYERSPEEDCAP = r->cap = cap;
    if (!r->rolling && ((v > 0 && (pv < 0 || (-v != *G_PLAYERVX && v <= pv))) || (v < 0 && (pv > 0 || pv < v))))
        ;                                            /* still rolling its own way */
    else if (held & (IN_LEFT | IN_RIGHT)) {
        r->rolling = 1;
        if (blocked && pv && (pv < 0) == r->left) pv = 0;   /* rolling into what blocks it */
        A_VX(a) = pv;
        *G_PLAYERXFIXED += pv;
        *G_PLAYERX = *G_PLAYERXFIXED >> 16;
        if (!(*G_TICKCOUNTER & 7) && *G_PLAYERX != A_X(a)) {
            int d = A_X(a) > *G_PLAYERX ? 1 : -1;  /* toward its middle */
            *G_PLAYERX += d;
            *G_PLAYERXFIXED += d << 16;
        }
        goto jump;
    }
    *G_PLAYERXFIXED += v * 2 / 3;
    *G_PLAYERX = *G_PLAYERXFIXED >> 16;
jump:
    if (pressed & *G_JUMPMASK) *G_PLAYERVX = 0;
}

static void __cdecl roll_update(BYTE *a)
{
    Roll *r = roll_of(a);
    const BYTE *rec = A_RECORD(a);
    int vx, blocked, on, q;
    if (!r || !g_active || !rec) return;
    vx = A_VX(a);
    if (vx < 0) r->left = 1;
    else if (vx > 0) r->left = 0;
    blocked = flink_wall_ahead(a, !r->left) || roll_meets(a, r);
    if (blocked && vx && (vx < 0) == r->left) A_VX(a) = 0;
    A_VY(a) = 0;
    on = Platform_MoveAndCarryPlayer(a, roll_surface);   /* (moved by vx: Platform_Stand) */
    if (on) roll_ride(a, r, blocked);
    else {
        r->rolling = 0;
        if (r->capped && *G_PLAYERSPEEDCAP == r->cap) *G_PLAYERSPEEDCAP = 0;
    }
    r->capped = on;
    vx = A_VX(a);                                    /* Barrel_Update's Math_ApproachValue */
    vx = vx > ROLL_FRICTION ? vx - ROLL_FRICTION : vx < -ROLL_FRICTION ? vx + ROLL_FRICTION : 0;
    A_VX(a) = vx;
    r->phase += vx;
    q = (r->phase >> 18) & 15;
    if (r->frames8) q &= ~1;
    hold_frame(a, q);
    Actor_AdvanceAnim(a);
}

/* (from spik_spawn) param: phases (16 or 8) */
void rollingstone_spawn(BYTE *a, BYTE *def)
{
    int i = slot_of(a);
    Roll *r;
    if (i < 0 || !record_of(a, FOURCC("ROLL"))) return;
    r = &g_roll[i];
    memset(r, 0, sizeof(*r));
    r->frames8 = def_param(a, def, 0, 16) == 8;
    r->type = A_TYPE(a);
    A_VX(a) = A_VY(a) = 0;
    A_FX(a) = A_X(a) << 16;
    A_FY(a) = A_Y(a) << 16;
    A_FLAGS(a) &= ~(0x200000u | AF_CONTACT);     /* a platform, touching nothing */
    A_LAYER(a) = (short)FLINK_LAYER(3);          /* (a5) = 0x7D70: list 3 */
    A_UPDATE(a) = (void *)roll_update;
    Actor_SetAnim(a, FOURCC("ROLL"));
    hold_frame(a, 0);
    Actor_AdvanceAnim(a);
    flog("rolling stone at (%d,%d): %d phases", A_X(a), A_Y(a), r->frames8 ? 8 : 16);
}
