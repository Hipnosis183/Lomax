/*
 * chests.c - Flink's chests (README "Chests").
 *
 * Chest (Flink 0x2E000, see the converter's chest_content): a 'Pots Chest' type (object 85, whose
 * spawn is wrapped: pots_spawn) with Flink's records CHES, OPEN, LOCK (the padlock) and ERDE (the
 * clods), param 0 the content as a Lomax pot's (ClayPot_Spawn 0x42BEE0): 0 a helmet, 1 a 1UP, -n n
 * coins, or CHEST_SPIRITS + n for n spirits (Flink's magic key: the MAGIC KEY spell costs 10
 * spirits here, spells.c; chest_drop), and param 1 = locked. Flink's behaviour:
 * - Bursting out of the ground (0x2E000 / 0x2E138), every time it spawns: from 16 px under its
 *   placement, hidden behind the ground, it is thrown up at 2.5 px a frame and falls back (gravity
 *   0x2000 a frame, at most 4.0) onto it. Here the ground hides it through the bottom clip line
 *   (+0x7C, as Lomax's pots that burst out of the ground). Meanwhile 5 clods (Fx_SpawnDebris 0x2E0BC,
 *   table 0x2E106: a delay, x/y offsets, vx, vy) fly off, each for 60 frames once its delay is over,
 *   with gravity 0x2000 and friction 0x400; Lomax's debris sound (0x3D, Effect_SpawnDebrisBurst)
 *   plays for Flink's 0x49.
 * - Closed (0x2E184): stomping it opens it. Here Lomax's own stomp does (Player_Update's contact loop,
 *   as for the enemies: contact.c calls chest_bounce; it doesn't hurt), and so do his attacks
 *   (Actor_CheckHitByPlayer), as they open Lomax's pots.
 * - Opening: OPEN, Lomax's pot-hit sound (0x11) for Flink's 0x26, and the content comes out as from
 *   an opened pot (ClayPot_UpdateOpened 0x42C2E0). The open chest stays; nothing opens it again
 *   (Lomax's pots break at the second hit).
 * - Locked (Flink checkbox 1; spawn 0x2E058 keeps LOCK's frame in +0x52, which the draw hook 0x2E2B2
 *   draws as a bare sprite, its top-left at x - 10, y - 19: lock_origin): a stomp opens it only with the MAGIC KEY spell selected, using up a key
 *   (0x2E196), else it plays the locked sound 0x18. Opened, the padlock flies off (0x2E1D6: an actor
 *   with LOCK at x - 10, y - 19, thrown up at 3.0 and 1.5 the way Flink faces; here from where it was
 *   drawn). Here the MAGIC KEY is chosen on the spell wheel (spells.c), Lomax has no keys, and a
 *   locked chest opens only by Ability (the grab button, as the spells are cast: plant.c), not by a
 *   stomp or an attack: Lomax on the ground with its hitbox within CHEST_REACH px of his presses it
 *   (grab.c's grab_before calls chest_unlock before a pick-up); with the MAGIC KEY chosen the chest
 *   opens for the spell's cost in spirits (spell_pay; with fewer the cost flashes on the HUD), with
 *   another spell chosen the press plays the locked sound (Lomax's ROCKHITX). A stomp on it plays
 *   that sound too and bounces Lomax off it; an attack does nothing, silently (Lomax's spin hits it
 *   every tick, and a sound would repeat) (chest_try).
 * - Opened for good: once it leaves the screen it never comes back (forget_placement).
 * - Open, it is a thrown object at rest: Lomax can pick it up (crouching: its OPEN hitbox is too low
 *   for his standing grab box) and throw it (grab.c, chest_grab_kind).
 * A chest holding an information note is a hint hat instead (hints.c).
 */
#include "flink.h"

enum { CHEST_RISING = 1, CHEST_CLOSED, CHEST_OPEN };
#define SND_LOCKED  38                           /* ROCKHITX for Flink's 0x18 (statue.c) */
#define CHEST_REACH 8                            /* px either side of Lomax's hitbox for Ability */
#define CHEST_SPIRITS 100                        /* content 100 + n: n spirits (the converter's CHEST_SPIRITS) */
#define CHEST_SPIRITS_MAX 20                     /* at most this many from one chest (actor slots) */

/* per actor slot: their type, placement y, state, content, locked and the game's draw function */
static struct { BYTE *type; int base, state, content, locked; void (__cdecl *draw)(BYTE *); } g_chest[MAX_ACTORS];

void chests_reset(void) { memset(g_chest, 0, sizeof(g_chest)); }

static int chest_slot(BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_chest[i].type && g_chest[i].type == A_TYPE(a) ? i : -1;
}

int is_chest(BYTE *a) { return a && chest_slot(a) >= 0; }

/* a clod (Flink 0x2E33C): +0x80 its counter (from minus its delay; shown 0..60) */
static void __cdecl clod_update(BYTE *d)
{
    int c = ++*(int *)(d + 0x80), *vx = &A_VX(d), *vy = &A_VY(d);
    if (c < 0) return;
    if (c > 60) { Actor_Despawn(d); return; }
    *vy += 0x2000;
    if (*vy > 0x40000) *vy = 0x40000;
    A_FY(d) += *vy;
    A_FX(d) += *vx;
    *vx = *vx > 0 ? (*vx > 0x400 ? *vx - 0x400 : 0) : (*vx < -0x400 ? *vx + 0x400 : 0);
    set_pos(d);
}

static void __cdecl clod_draw(BYTE *d)
{
    if (*(int *)(d + 0x80) >= 0) draw_actor(d);
}

/* (also the Statue's, statue.c: Fx_SpawnDebris is shared) */
void ground_clods(BYTE *a, int x, int y)
{
    /* 0x2E106: delay, dx, dy, vx and vy (<< 12, y up) of each clod; they use ERDE's frames in turn */
    static const short CLODS[5][5] = { { 0, -5, 1, -24, 44 }, { -9, -3, 1, -24, 52 }, { -20, 2, 0, 24, 48 },
                                       { -3, 0, -2, -8, 48 }, { -17, 7, -1, 16, 44 } };
    int k;
    for (k = 0; k < 5; k++) {
        BYTE *d = Actor_Alloc(a, NULL);
        if (!d) return;
        A_FX(d) = (x + CLODS[k][1]) << 16;
        A_FY(d) = (y + CLODS[k][2]) << 16;
        set_pos(d);
        A_VX(d) = CLODS[k][3] * 0x1000;
        A_VY(d) = -CLODS[k][4] * 0x1000;
        *(int *)(d + 0x80) = CLODS[k][0];
        A_CULL(d) = CULL_NEVER;
        A_UPDATE(d) = (void *)clod_update;
        A_DRAW(d) = (void *)clod_draw;
        Actor_SetAnim(d, FOURCC("ERDE"));
        hold_frame(d, k & 1);
    }
}

/* the content, as ClayPot_UpdateOpened (0x42C2E0) drops it: coins thrown from the table at 0x4645E8,
   or a helmet or 1UP ('Items') coming up out of the chest, clipped at its top until it falls. Item
   fields: +0x80 ticks before it can be picked up, +0x84 the content, +0x88 / +0x90 its sparkles.
   Content CHEST_SPIRITS + n (not a Lomax pot's) sets n spirits free instead, fanned out from the
   chest's top, each homing on Lomax as a defeated enemy's (spirits.c). */
static void chest_drop(BYTE *a, int content)
{
    int y = A_Y(a), top = A_RECORD(a) ? *(const short *)(A_RECORD(a) + 0x12) : -14, k;
    BYTE *it;
    if (content >= CHEST_SPIRITS) {
        int n = content - CHEST_SPIRITS;
        if (n > CHEST_SPIRITS_MAX) n = CHEST_SPIRITS_MAX;
        for (k = 0; k < n; k++)
            if ((it = spirit_release(a, A_X(a), y + top))) {
                A_VX(it) = n > 1 ? (2 * k - (n - 1)) * 0x18000 / (n - 1) : 0;   /* -1.5 .. 1.5 px */
                A_VY(it) = k & 1 ? -0x30000 : -0x20000;
            }
        return;
    }
    if (content < 0) {
        for (k = (-content > 8 ? 8 : -content) - 1; k >= 0; k--)
            if ((it = Item_SpawnCoin(a))) {
                A_VX(it) = COIN_THROWS[k * 2];
                A_VY(it) = COIN_THROWS[k * 2 + 1];
                A_CLIPY(it) = (short)(y - 0x14);
            }
        return;
    }
    if (!(it = Actor_Alloc(a, S_ITEMS))) return;
    A_LAYER(it) = 1;
    A_CLIPY(it) = (short)(y + top);
    *(int *)(it + 0x80) = 0x28;
    *(int *)(it + 0x84) = content;
    A_Y(it) = y - 16;
    *(short *)(it + 0x4A) = (short)(y - 16);     /* (the 16.16 y's whole part) */
    A_VX(it) = *G_PLAYERVX < 0 ? -0x14000 : 0x14000;
    Actor_SetAnim(it, FOURCC("TAIL"));           /* the sparkles' record */
    Sparkles_Init(it, it + 0x90, 4);
    *(int *)(it + 0x88) = 1;
    A_DRAW(it) = (void *)Item_DrawWithSparkles;
    A_CULL(it) = 2;
    if (content == 0) {                          /* a helmet */
        A_UPDATE(it) = (void *)Item_UpdateCoinOrHelmet;
        A_VY(it) = -0x58000;
        A_FLAGS(it) |= 0x80000;
        Actor_SetAnim(it, FOURCC("HELM"));
    } else {                                     /* a 1UP balloon */
        A_UPDATE(it) = (void *)Item_UpdateOneUp;
        A_VY(it) = -0x8000;
        A_VX(it) = 0;
        *(int *)(it + 0x84) = 0x8000;
        Actor_SetAnim(it, FOURCC("UP  "));
    }
}

/* where the padlock actor goes for LOCK's sprite to have its top-left at x - 10, y - 19, as Flink's
   draw hook 0x2E2B2 draws the bare frame (Actor_Draw adds the record's piece offset) */
static void lock_origin(BYTE *a, int *x, int *y)
{
    const BYTE *e = record_of(a, FOURCC("LOCK"));
    *x = A_X(a) - 10 - (e ? *(const short *)(e + 2) : 0);
    *y = A_Y(a) - 19 - (e ? *(const short *)(e + 4) : 0);
}

/* the padlock flying off (0x2E1D6): +0x80 its ticks */
static void __cdecl padlock_update(BYTE *d)
{
    int *vy = &A_VY(d);
    if (++*(int *)(d + 0x80) > 120) { Actor_Despawn(d); return; }
    *vy += 0x2000;
    if (*vy > 0x40000) *vy = 0x40000;
    A_FY(d) += *vy;
    A_FX(d) += A_VX(d);
    set_pos(d);
}

static void __cdecl padlock_draw(BYTE *d) { draw_actor(d); }

static void padlock_fly(BYTE *a)
{
    BYTE *d = Actor_Alloc(a, NULL);
    int x, y;
    if (!d) return;
    lock_origin(a, &x, &y);
    A_FX(d) = x << 16;
    A_FY(d) = y << 16;
    set_pos(d);
    A_VX(d) = *G_PLAYERFLAGS & AF_LEFT ? -0x18000 : 0x18000;
    A_VY(d) = -0x30000;
    *(int *)(d + 0x80) = 0;
    A_CULL(d) = CULL_NEVER;
    A_UPDATE(d) = (void *)padlock_update;
    A_DRAW(d) = (void *)padlock_draw;
    Actor_SetAnim(d, FOURCC("LOCK"));
    hold_frame(d, 0);
}

static void chest_open(BYTE *a, int i)
{
    if (g_chest[i].locked) { g_chest[i].locked = 0; padlock_fly(a); }
    g_chest[i].state = CHEST_OPEN;
    A_FLAGS(a) &= ~AF_CONTACT;                   /* no longer touched or hit */
    forget_placement(A_DEF(a));
    Actor_SetAnim(a, FOURCC("OPEN"));
    flink_sound(a, 0x26);
    chest_drop(a, g_chest[i].content);
    flog("chest at (%d,%d) opened: %s", A_X(a), A_Y(a),
         g_chest[i].content >= CHEST_SPIRITS ? "spirits" : g_chest[i].content < 0 ? "coins" :
         g_chest[i].content ? "1UP" : "helmet");
}

/* The open chest becomes a thrown object at rest (0x2E250: think Actor_ThrownThink 0x323C2), which
   Lomax can pick up (grab.c): heavy (+6 bit 0, 0x2E03C), no fuse (bit 2), held at the hand itself */
int chest_grab_kind(BYTE *a, GrabKind *k)
{
    int i = a ? chest_slot(a) : -1;
    if (i < 0 || g_chest[i].state != CHEST_OPEN || grab_held(a)) return 0;
    memset(k, 0, sizeof(*k));
    k->heavy = 1;
    return 1;
}

/* a stomp or an attack on a closed chest opens it; a locked one only plays the locked sound when
   stomped (Flink's 0x18; an attack would repeat it every tick) and opens with Ability (chest_unlock) */
static void chest_try(BYTE *a, int i, int stomp)
{
    if (g_chest[i].locked) {
        if (stomp) Sound_PlayEffect(a, SND_LOCKED);
        return;
    }
    chest_open(a, i);
}

/* Lomax's hitbox, widened by CHEST_REACH px each side, overlaps the chest's */
static int chest_in_reach(BYTE *a)
{
    const BYTE *pa = *G_PLAYERANIM;
    int x1, y1, x2, y2;
    if (!pa || A_PLANE(a) != *G_PLAYERPLANE || !actor_box(a, &x1, &y1, &x2, &y2)) return 0;
    return x1 < *G_PLAYERX + HB_X2(pa) + CHEST_REACH && *G_PLAYERX + HB_X1(pa) - CHEST_REACH < x2 &&
           y1 < *G_PLAYERY + HB_Y2(pa) && *G_PLAYERY + HB_Y1(pa) < y2;
}

/* Ability pressed (grab.c's grab_before, before a pick-up): a closed locked chest within reach of
   Lomax on the ground takes the press. With the MAGIC KEY chosen and paid it opens (0x2E196's
   effect); too few spirits flash the cost; another spell chosen plays the locked sound. 1: taken */
int chest_unlock(void)
{
    int k, i;
    if (!spell_can_cast()) return 0;
    for (k = 0; k < MAX_ACTORS; k++) {
        BYTE *a = actor_at(k);
        if (!actor_active(k) || A_LAYER(a) == KILLED_LAYER || (i = chest_slot(a)) < 0) continue;
        if (!g_chest[i].locked || g_chest[i].state != CHEST_CLOSED || !chest_in_reach(a)) continue;
        if (!spell_chosen(SPELL_MAGIC_KEY)) Sound_PlayEffect(a, SND_LOCKED);
        else if (spell_pay(SPELL_MAGIC_KEY)) {
            flog("locked chest at (%d,%d) opened with the MAGIC KEY", A_X(a), A_Y(a));
            chest_open(a, i);
        }
        return 1;
    }
    return 0;
}

void chest_bounce(BYTE *a)
{
    int i = a ? chest_slot(a) : -1;
    if (i >= 0 && g_chest[i].state == CHEST_CLOSED) chest_try(a, i, 1);
}

static void __cdecl chest_update(BYTE *a)
{
    int i = chest_slot(a), *vy = &A_VY(a);
    if (i < 0) return;
    if (g_chest[i].state == CHEST_RISING) {     /* 0x2E138 */
        *vy += 0x2000;
        if (*vy > 0x40000) *vy = 0x40000;
        A_FY(a) += *vy;
        set_pos(a);
        if (*vy >= 0 && A_Y(a) >= g_chest[i].base) {
            place_y(a, g_chest[i].base);
            *vy = 0;
            g_chest[i].state = CHEST_CLOSED;
            A_FLAGS(a) |= AF_HURTS | AF_HITTABLE;    /* stomped (chest_bounce) or hit */
        }
    } else if (g_chest[i].state == CHEST_CLOSED && Actor_CheckHitByPlayer(a))
        chest_try(a, i, 0);
}

/* the chest, and while locked LOCK's frame over it (lock_origin: Flink's draw hook 0x2E2B2),
   clipped as the chest (its bottom clip line). Drawn through Actor_Draw with the record switched for
   the call (as monkey.c's body_draw), so that Lomax+'s FPS Override moves it with the chest. */
static void __cdecl chest_draw(BYTE *a)
{
    int i = chest_slot(a), x = A_X(a), y = A_Y(a), lx, ly;
    if (i < 0) return;
    if (g_chest[i].locked && record_of(a, FOURCC("LOCK"))) {
        /* first: within a draw bucket the sprite queued first is drawn in front */
        lock_origin(a, &lx, &ly);
        Actor_SetAnim(a, FOURCC("LOCK"));
        hold_frame(a, 0);
        Actor_AdvanceAnim(a);
        A_X(a) = lx;
        A_Y(a) = ly;
        draw_actor(a);
        A_X(a) = x;
        A_Y(a) = y;
        Actor_SetAnim(a, FOURCC("CHES"));      /* (one frame) */
        hold_frame(a, 0);
        Actor_AdvanceAnim(a);
    }
    if (g_chest[i].draw) g_chest[i].draw(a);
    else draw_actor(a);                          /* (none: Actors_DrawAndCull's default) */
}

static void chest_spawn(BYTE *a, BYTE *def, int i)
{
    int x = A_X(a), y = A_Y(a);
    memset(&g_chest[i], 0, sizeof(g_chest[i]));
    g_chest[i].type = A_TYPE(a);
    g_chest[i].base = y;
    g_chest[i].content = def_param(a, def, 0, -5);
    g_chest[i].locked = def_param(a, def, 1, 0) != 0;
    g_chest[i].draw = (void (__cdecl *)(BYTE *))A_DRAW(a);
    A_UPDATE(a) = (void *)chest_update;
    if (g_chest[i].locked) A_DRAW(a) = (void *)chest_draw;
    A_FLAGS(a) &= ~AF_CONTACT;
    A_CLIPY(a) = (short)y;                       /* the ground hides what is below its placement */
    A_VX(a) = A_VY(a) = 0;
    if (placement_gone(def)) { g_chest[i].type = NULL; spawn_gone(a); return; }
    g_chest[i].state = CHEST_RISING;
    Actor_SetAnim(a, FOURCC("CHES"));
    place_y(a, y + 16);
    A_VY(a) = -0x28000;
    ground_clods(a, x, y);
    flink_sound(a, 0x49);
}

void __cdecl pots_spawn(BYTE *a, BYTE *def)
{
    int i = slot_of(a);
    if (g_active && i >= 0 && is_type(a, "Pots Chest")) chest_spawn(a, def, i);
    else ClayPot_Spawn(a, def);
}

int chests_retail(void) { return !memcmp(S_ITEMS, "Items", 6); }
