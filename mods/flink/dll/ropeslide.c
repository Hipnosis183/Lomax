/*
 * ropeslide.c - Flink's Rope slide (`Rope slide`, init 0x27EB8; VILLAGE maps 7, 8, 15 = 10-4
 * (two), 11-1, 15-2; README "Rope slides"): a handle on a cable that is part of the background; Flink
 * takes hold of it and it carries him down the cable. Flink's:
 * - Init: home x0, y0 = the placement (+0x4E, +0x50); param 0 the angle (+0x4A: the cable drops
 *   1 px every 'angle' px), param 1 the length; the step (+0x46) 0x1000 (1/16 px a frame), and with
 *   checkbox 1 ('left') the step and the length negated and the record mirrored; the end x (+0x4C)
 *   = x0 + length; record ROPE, draw list 3 ((a5) = 0x7D70: behind Flink).
 * - Waiting (0x27F0C): its speed (+0x1C, 16.16), when not 0, goes down by the step (a handle let go
 *   on the way coasts to a stop); x + speed, clamped at the end (speed 0 there); y = |x - x0| /
 *   angle + y0; then Player_CheckGrab (0x1A9B0) with a span of 24: the grab button pressed and
 *   armed, carrying nothing, Flink's x within +-2 px of the handle's, his head (feet - 0x2B)
 *   between y - 0x20 and y - 8. Taken: Flink hangs (+0x828E bits 4, 5), at the handle's x, still.
 * - Ridden (0x27F8C): speed + step until |speed| is 3.0; x + speed; at the end x = the end and Flink
 *   lets go (0x28002) with the handle's speed as his vx, vy 0 and no jump boost; the handle stays
 *   there. Hurt (0x82E0 > 0), pushing (0x8300) or landed (+0x828E bit 0 clear) he lets go the same way.
 *   Jump pressed after it was released (g_JumpBoost < 0) jumps off: Player_StartJumpWithSfx, VelY
 *   halved, vx the handle's speed (0x28018). Otherwise Flink is held at x, y + 0x1C, the handle's y
 *   from the cable as above, and all buttons but Jump and Start are taken out.
 * Here: the converter's 'Spik Slide' (Lomax's decoration, object 51), record ROPE with Flink's hitbox;
 * params angle, length, left. Lomax takes hold of the handle by touching it, without a button (as
 * for the lianas and the Chopper Bike): ropeslide_reach, Player_CheckGrab's window
 * without the button (grab_can_touch: carrying nothing, not hanging or riding; his feet within the
 * span of 24 from Flink's highest grab, y + 11, both moved LOMAX_LOWER down with his hang, so landing
 * on it from above doesn't take it; across, his hitbox on the handle's), not again before he has been off it since he let go
 * (or he would take it again at once, standing where he landed), not for REGRAB ticks after jumping
 * off (the handle coasts on with him), and not at the end (Flink would let go there at once). He hangs in his rope pose (player state 0x1010, as on
 * the Chopper Bike, cyclindude.c) around Player_Update (ropeslide_before / ropeslide_after, from player_tick), his
 * buttons but Jump taken out; walls (x +- 8) and the ground under his feet are tested here, as
 * Flink's player code tests them. At the end he drops off with the handle's speed (he keeps
 * the momentum).
 */
#include "enemy.h"

#define ROPESLIDE_STEP        0x1000           /* 1/16 px a frame */
#define ROPESLIDE_MAX         0x30000          /* 3.0 */
/* Flink's feet hang at the handle's y + 0x1C, his HANG glove on the bottom of the handle's stem;
   Lomax's rope pose reaches higher (its hand at the wheel at 0x1C), so he
   hangs LOMAX_LOWER px lower, his hand on the stem's bottom as Flink's */
#define LOMAX_LOWER     10
#define ROPESLIDE_HANG        (0x1C + LOMAX_LOWER)
#define ROPESLIDE_FEET_MIN    (11 + LOMAX_LOWER)   /* his feet at least this far under the handle's y to take it (Flink's highest grab) */
#define ROPESLIDE_SPAN        24                   /* ... and at most this much further (Player_CheckGrab's d0) */
#define REGRAB          30               /* ticks after a jump off before touching it takes it again */

#define IN_LEFT         0x8000
#define IN_RIGHT        0x2000
#define IN_UP           0x1000
#define IN_DOWN         0x4000
#define IN_LOOK         0x0005           /* LookDown, LookUp */

typedef struct {
    BYTE *type;
    int x0, y0, end, angle, step;        /* +0x4E, +0x50, +0x4C, +0x4A, +0x46 */
    int fx, v;                           /* x and speed, 16.16 (+0x14, +0x1C) */
    int t;                               /* REGRAB */
    int clear;                           /* Lomax has been off it since he let go */
} RopeSlide;
static RopeSlide g_ropeslide[MAX_ACTORS];

/* the ride: the handle's slot, Jump released since, the hurt timer, the buttons taken out */
static struct { int slot, jumpok, hurt, masked; WORD held, pressed; } Z = { -1, 0, 0, 0, 0, 0 };

void ropeslides_reset(void)
{
    memset(g_ropeslide, 0, sizeof(g_ropeslide));
    Z.slot = -1;
    Z.masked = 0;
}

int ropeslide_active(void) { return Z.slot >= 0; }

static RopeSlide *ropeslide_of(const BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_ropeslide[i].type && g_ropeslide[i].type == A_TYPE(a) ? &g_ropeslide[i] : NULL;
}

/* x + speed, clamped at the end (1: there) */
static int ropeslide_move(RopeSlide *z)
{
    z->fx += z->v;
    if (z->step < 0 ? z->fx >> 16 <= z->end : z->fx >> 16 >= z->end) { z->fx = z->end << 16; return 1; }
    return 0;
}

/* the handle's y on the cable, from its x */
static void ropeslide_place(RopeSlide *z, BYTE *a)
{
    A_FX(a) = z->fx;
    A_X(a) = z->fx >> 16;
    place_y(a, abs(A_X(a) - z->x0) / z->angle + z->y0);
}

static void hang(void)
{
    *G_PLAYERSTATE = (*G_PLAYERSTATE & ~(ROPE_CLEAR | ROPE_CLIMB)) | ROPE_STATE | PS_AIR;
    *G_BAIRSPIN = 0;
    *G_JUMPHOLDFORCE = -1;
    *G_PLAYERVX = *G_PLAYERVY = 0;
}

/* Lomax at the handle: x, feet at y + ROPESLIDE_HANG */
static void at_handle(const BYTE *a)
{
    *G_PLAYERX = A_X(a); *G_PLAYERXFIXED = A_FX(a);
    *G_PLAYERY = A_Y(a) + ROPESLIDE_HANG; *G_PLAYERYFIXED = *G_PLAYERY << 16;
}

enum { END_DROP, END_JUMP, END_HURT, END_LAND };

/* Flink lets go (0x28002, 0x28018): the handle's speed as his vx; the handle waits (0x27F0C again) */
static void ropeslide_end(int how)
{
    BYTE *a = actor_at(Z.slot);
    RopeSlide *z = a ? ropeslide_of(a) : NULL;
    int v = z ? z->v : 0;
    Z.slot = -1;
    *G_PLAYERSTATE &= ~(ROPE_STATE | ROPE_CLIMB);
    if (how == END_LAND) *G_PLAYERSTATE &= ~PS_AIR;
    else *G_PLAYERSTATE |= PS_AIR;
    if (how != END_HURT) *G_PLAYERVX = v;
    *G_PLAYERVY = 0;
    *G_JUMPHOLDFORCE = 0;                        /* (0x2800E: no jump boost) */
    if (how == END_JUMP) move_jump_off();
    if (z) {                                     /* (0x2800A: it stops, but after a jump it coasts) */
        if (how == END_JUMP) z->t = REGRAB;
        else z->v = 0;
        z->clear = 0;
    }
    flog("Lomax %s the rope slide at (%d,%d), vx %d/65536 (hurt %d, dying %d)",
         how == END_DROP ? "drops off" : how == END_JUMP ? "jumps off" : how == END_HURT ? "is hurt and lets go of" : "lands with",
         *G_PLAYERX, *G_PLAYERY, how != END_HURT ? v : 0, *G_PLAYERHURTTIMER, *G_PLAYERDEATHTIMER);
}

static int new_hit(void)
{
    int h = *G_PLAYERHURTTIMER;
    return (h > 0 && h > Z.hurt) || *G_PLAYERDEATHTIMER;
}

/* Player_CheckGrab (0x1A9B0) without the button: his feet within Flink's span of 24 from his highest
   grab (moved LOMAX_LOWER down with the hang), and across, his hitbox (mirrored with him) on the
   handle's, as a touch (Flink: within +-2 px of its x) */
static int ropeslide_reach(const BYTE *a)
{
    const BYTE *pa = *G_PLAYERANIM;
    int px = *G_PLAYERX, py = *G_PLAYERY, lx1, lx2, x1, y1, x2, y2;
    if (!grab_can_touch(a) || py < A_Y(a) + ROPESLIDE_FEET_MIN || py > A_Y(a) + ROPESLIDE_FEET_MIN + ROPESLIDE_SPAN) return 0;
    if (*G_PLAYERFLAGS & AF_LEFT) { lx1 = px - HB_X2(pa); lx2 = px - HB_X1(pa); }
    else { lx1 = px + HB_X1(pa); lx2 = px + HB_X2(pa); }
    return actor_box(a, &x1, &y1, &x2, &y2) && lx1 <= x2 && lx2 >= x1;
}

/* 0x27F0C, waiting */
static void __cdecl ropeslide_update(BYTE *a)
{
    RopeSlide *z = ropeslide_of(a);
    int i = slot_of(a), there;
    if (!z || i == Z.slot) return;               /* (ridden: ropeslide_before moves it) */
    if (z->v) z->v -= z->step;
    there = ropeslide_move(z);
    if (there) z->v = 0;
    ropeslide_place(z, a);
    if (z->t > 0) { z->t--; return; }
    if (ropeslide_active() || !ropeslide_reach(a)) { z->clear = 1; return; }
    if (!z->clear || (there && !z->v)) return;   /* (touched since he let go; at the end) */
    {
        Z.slot = i;
        Z.jumpok = 0;
        Z.hurt = *G_PLAYERHURTTIMER;
        hang();
        at_handle(a);
        flog("Lomax takes the rope slide at (%d,%d), speed %d/65536, end x %d", A_X(a), A_Y(a), z->v, z->end);
    }
}

/* before Player_Update: the ride (0x27F8C) */
void ropeslide_before(void)
{
    BYTE *a = actor_at(Z.slot);
    RopeSlide *z;
    int held, there, g, oy;
    if (Z.slot < 0) return;
    z = a && actor_active(Z.slot) ? ropeslide_of(a) : NULL;
    if (!g_active || !z) { ropeslide_end(END_HURT); return; }
    if (*G_BPAUSED || *G_PLAYERSTATE & PS_FROZEN) return;
    if (new_hit()) { ropeslide_end(END_HURT); return; }
    Z.hurt = *G_PLAYERHURTTIMER;
    held = *G_INPUTHELD;
    if (!(held & *G_JUMPMASK)) Z.jumpok = 1;     /* (0x28028: g_JumpBoost = -1) */
    else if (Z.jumpok) { ropeslide_end(END_JUMP); return; }
    if (abs(z->v) != ROPESLIDE_MAX) z->v += z->step;
    there = ropeslide_move(z);
    ropeslide_place(z, a);
    oy = *G_PLAYERY;
    at_handle(a);
    if (there) { ropeslide_end(END_DROP); return; }
    if (wall_at(G_PLAYERACTOR, *G_PLAYERX + (z->v > 0 ? 8 : -8), z->v > 0)) { ropeslide_end(END_DROP); return; }   /* (pushing) */
    g = ground_y(G_PLAYERACTOR, *G_PLAYERX, oy);
    if (g != NO_GROUND && *G_PLAYERY >= g) {
        *G_PLAYERY = g; *G_PLAYERYFIXED = g << 16;
        ropeslide_end(END_LAND);
        return;
    }
    hang();
    {
        WORD mask = (WORD)(IN_LEFT | IN_RIGHT | IN_UP | IN_DOWN | IN_LOOK | *G_JUMPMASK | *G_HELMMASK);
        Z.held = *G_INPUTHELD & mask; Z.pressed = *G_INPUTPRESSED & mask;
        *G_INPUTHELD &= (WORD)~mask; *G_INPUTPRESSED &= (WORD)~mask;
        Z.masked = 1;
    }
}

void ropeslide_after(void)
{
    BYTE *a = actor_at(Z.slot);
    if (Z.masked) { *G_INPUTHELD |= Z.held; *G_INPUTPRESSED |= Z.pressed; Z.masked = 0; }
    if (Z.slot < 0) return;
    if (!a || !ropeslide_of(a)) { ropeslide_end(END_HURT); return; }
    if (new_hit()) { ropeslide_end(END_HURT); return; }   /* (his knock-back stays) */
    *G_PLAYERSTATE &= ~ROPE_CLIMB;
    *G_PLAYERVX = *G_PLAYERVY = 0;
    at_handle(a);
}

/* (from spik_spawn) params: angle, length, left */
void ropeslide_spawn(BYTE *a, BYTE *def)
{
    int i = slot_of(a), len;
    RopeSlide *z;
    if (i < 0 || !def || T_NPARAMS(A_TYPE(a)) < 3) return;
    z = &g_ropeslide[i];
    memset(z, 0, sizeof(*z));
    z->type = A_TYPE(a);
    z->x0 = A_X(a);
    z->y0 = A_Y(a);
    z->angle = D_PARAM(def, 0) > 0 ? D_PARAM(def, 0) : 1;
    len = D_PARAM(def, 1);
    z->step = ROPESLIDE_STEP;
    if (D_PARAM(def, 2)) { z->step = -ROPESLIDE_STEP; len = -len; A_FLAGS(a) |= AF_LEFT; }
    z->end = z->x0 + len;
    z->fx = z->x0 << 16;
    z->clear = 1;
    A_FLAGS(a) &= ~(0x200000u | AF_CONTACT);     /* the actor itself touches nothing */
    A_LAYER(a) = (short)FLINK_LAYER(3);          /* (a5) = 0x7D70: list 3 */
    A_UPDATE(a) = (void *)ropeslide_update;
    set_anim(a, "ROPE");
    flog("rope slide at (%d,%d): angle %d, to x %d", z->x0, z->y0, z->angle, z->end);
}
