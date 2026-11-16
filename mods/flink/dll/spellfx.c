/*
 * spellfx.c - Flink's spells cast anywhere: SPIRIT BOMB, DUST DEVIL, LIGHTNING, PLATFORM, SHIELD and
 * DEMON (README "Spells").
 *
 * Flink (decompilation.md §10.10). The spell button on the ground calls SpellHandlerTable[g_SelectedSpell]
 * (0x32F0A). A handler stops Flink (vx 0, anything carried dropped) and starts the cast gesture, the
 * timer 0x830C, which Player_Update counts while Flink stands (0x1CBA2; walking holds it, leaving the
 * ground clears it, 0x1C7E2): MAG1 down to 0, then, unless g_CastNoFollowThrough (0x8310), MAG2 from
 * -95 up to 0, the cast sound 0x20 at -40. Its actors are spawned from MAIN.WLD's NMY records
 * (Actor_SpawnFromNmy 0x32BE0). A cost is paid with the spirits held (spells.c, spell_pay).
 *
 * Lomax has no casting pose, so nothing waits for Flink's MAG1 / MAG2: PLATFORM, SHIELD and DEMON,
 * which Flink's code holds until the gesture's -30 (85 frames after the press), take effect on the
 * press, with the cast sound (cast_now), as QUICK GROW does (plant.c). SPIRIT BOMB, DUST DEVIL and
 * LIGHTNING act on the press in Flink too; their gesture (no follow-through) still runs, as Flink's
 * count: it keeps another spell from being cast meanwhile and paces SPIRIT BOMB's spirits.
 *
 * Here Ability on the ground with nothing in reach to pick up (grab.c's grab_before) calls spell_cast;
 * spells_tick (after Player_Update) counts the gesture as Player_Update does. Lomax has no casting
 * pose: he stands. The actors are 'Spik Spells' (the converter's spells type: FLA1-3, DUST, BLAD),
 * 'Spik Cloud' (the Platform's WOLK as its MAIN record, the standing surface, WARN, PUFF) and 'Spik
 * Spirit' (spirits.c). Their hits are a thrown object's (grab.c, spell_strike: Flink's actors set the
 * hit flag, 0x1A6D2). Flink's screen flash (main command 0x28) has no Lomax counterpart.
 *
 * - SPIRIT BOMB (Spell0_HomingSpirits 0x30C66, 4 each): none while one of its spirits flies. A spirit
 *   (0x30CD4) for the first enemy on the screen (stompable, not frozen, not a Rock) not yet taken
 *   (g_SpiritTargetMask 0x839C), for 4 spirits, while more than 4 are held (here: 4 or more): from
 *   Flink's x, y - 20 at vy -4.0, sound 0x2B; gesture 0x4F, no follow-through. Each spirit, once,
 *   when the gesture timer & 15 is 0, sends the next one out (0x30E50), so one leaves every 16 frames
 *   while it lasts and every frame after. It homes in on its enemy's middle (Actor_MoveToward 0x1B008:
 *   up to 2.5, 0x3000 a frame) and hits it on touching it (0x1A65A, 0x1A6D2), then plays TAIL and goes
 *   (0x2EEEC). An enemy gone, off the screen or no longer stompable: the spirit turns into the one a
 *   defeated enemy sets free (0x2EE2C), homing in on Flink, collected for 1, life 1200.
 * - DUST DEVIL (Spell1_Tornado 0x30F12, 8): none while one blows. Four Tornados (DUST), 0x40 px apart
 *   from 0x40 past the screen's right edge, 64 .. 191 px above it (0x30FF0), each with its top speed
 *   2.0 .. 2.875, vy 1.0 .. 2.875, a height on the screen 32 .. 159, four blades (BLAD, frames 0/1 and
 *   2/3 by turns every 16 frames, the last two mirrored) flying left; sound 0x2C once on the screen;
 *   gesture 0x3C, no follow-through. Think (0x31142):
 *   - no enemy yet: takes the first one on the screen not yet taken (g_TornadoTargetMask 0x8388; the
 *     slots of one parity a frame), flies toward 140 px from the screen's left at its height (0x800
 *     a frame), and past that x it is done;
 *   - an enemy: for 300 frames, toward its feet (0xC00 a frame); touching it, hits it once, and done
 *     120 frames later; a dying one (Enemy_Die's fall 0x1B1AA) it catches: the body rides 8 px above
 *     it (0x313E6) and it is done;
 *   - done: toward 200 px left of the screen and its top (0x800 a frame), freed (with its body) once
 *     off the screen.
 *   The screen is Flink's 320 px; here Lomax's view, the x's scaled to its width.
 * - LIGHTNING (Spell2_Lightning 0x31CC6, 10): none while a bolt strikes. Five bolts one after the
 *   other (0x31D36, 0x31D7C): each tries 10 frames for a place, x 32 .. 287 px into the screen, the
 *   ground below a random point of the screen's top 128 px (Map_GetGroundHeight 0x1AEEA, 20 cells),
 *   on the screen (the 10th try: 96 px lower down, no ground), its point 62 px above that, unseen
 *   meanwhile. Then FLA1 once; FLA2 once, sound 0x2D and the shake (0x874E = 0x23 when still),
 *   hitting the first actor its box touches each frame (Actor_FindTouchingActor 0x1A5A0); FLA3 once,
 *   the next bolt starting with it. Gesture 0x3C, no follow-through.
 * - PLATFORM (Spell8_CloudPlatform 0x3140E, 8): a cloud, waiting unseen while the gesture runs
 *   (0x31474: gone if it stops), at -30 paid (or the cost flashes) and placed 40 px in front of Flink
 *   and 20 up, a puff there 8 px lower (Puff, PLOF; Lomax's poof), sound 0x42. Think (0x31508), 480
 *   frames: the first 15 unseen (under the puff), WARN from 60 frames left, then PUFF once (sound 0x42)
 *   and gone. A platform (0x1A28A): landed on, it dips at 0.5 px a frame for 15 frames, then comes back
 *   up at 0.5 to its height; left before, it comes back at once. Gesture 20 and the follow-through
 *   (here none: on the press); any number of clouds.
 * - SHIELD (Spell9_OrbitingSpirits 0x31606, 10): none while its spirits circle. Three spirits (SPIR)
 *   wait while the gesture runs (0x31726: gone if it stops); at -30 the cost is paid (or flashes) and
 *   they circle Flink (0x31772), 120 degrees apart, 10/1024 of a turn a frame, around his x, y - 20,
 *   the radius growing by 1 a frame to 31 (27 up and down). Each hits the first actor it touches and
 *   goes (TAIL); one touching a Rock or an actor whose timer +0x38 runs stops it instead (here a
 *   Rock isn't one a spell hits, and the timer is not looked at). Their count
 *   (g_OrbitSpiritCount 0x814E) comes back circling at full radius when a map starts
 *   (Spell9_RestoreOrbitSpirits 0x316F0), and is lost when Flink dies (Player_Die). Gesture 20 and
 *   the follow-through (here none: on the press). In Flink a gesture stopped before -30 frees the
 *   waiting spirits but keeps their count (a restart brings them back unpaid).
 * - DEMON (Spell10_Demon 0x31856, 8 to summon, 6 to grow): a demon (MAIN.WLD's Demon) that follows
 *   Flink and attacks the enemies near him, 5 hits (g_DemonActive 0x814C, its hits left).
 *   (Flink's gesture, 20 and the follow-through, is none here: what it does at -30 is on the press.)
 *   - No demon: the gesture, the demon waiting unseen (0x318E0, think 0x31992; the gesture stopped:
 *     gone, no demon); at -30 paid (or the cost flashes and it goes),
 *     placed 30 px in front of Flink and 15 up, sparkling (0x2ECA6).
 *   - Its demon there, not grown (+0x4E): the gesture; at -30, with DEMON still chosen, paid 6 (or
 *     the cost flashes) and the demon grows (0x31A3A): 5 hits more, FLY2 / ATK2 for FLY1 / ATK1
 *     (g_DemonGrown 0x814B), sparkling. Grown: the press does nothing.
 *   - Its think (0x31A1E), every frame: with an enemy taken, still standing (+6 bit 3), the same
 *     one and within 100 px of Flink (x, y + 20), it flies toward the enemy's middle (Actor_MoveToward
 *     0x1B008: up to 4.0, 0x4000 a frame) in ATK1, in front of Flink (draw list 1); touching it, hits
 *     it (0x1A65A, 0x1A6D2), rests 90 frames and, its hits run out, sparkles and goes (0x31CBA:
 *     unseen 120 frames). Otherwise FLY1, behind Flink (draw list 2): resting, or else it takes the
 *     first standing enemy (not a Rock, 0x291EC) within 80 px of Flink's x and 60 of y + 20 (the
 *     slots of one parity a frame), and flies toward 30 px behind Flink and 35 up (up to 2.5, 0x2000
 *     a frame). FLY and ATK keep the animation's frame; it turns at more than 1.0 px a frame.
 *   - It comes back when a map starts (Spell10_RestoreDemon 0x3193A: 30 px left of Flink and 35 up,
 *     its hits and its form), and is lost when Flink dies (Player_Die), at the title and on a new
 *     game. Flink's spells 1, 2, 8 and 10 share one graphics slot (Spell_LoadGraphics 0x344A4): one
 *     of the others chosen, the demon goes (0x814A); Lomax has no such slot and keeps it. Flink's
 *     summon that can't be paid at -30 keeps its hits (the next map brings the demon unpaid); here
 *     it doesn't. No sound but the gesture's.
 */
#include "enemy.h"

#define SPELL_SPIRIT_BOMB  0
#define SPELL_DUST_DEVIL   1
#define SPELL_LIGHTNING    2
#define SPELL_PLATFORM     8
#define SPELL_SHIELD       9
/* SPELL_DEMON (flink.h) 10 */

/* the gesture (0x830C) */
#define CAST_BOMB          0x4F
#define CAST_SPELL         0x3C          /* DUST DEVIL, LIGHTNING */

#define FLINK_VIEW_W       320
#define VIEW_H             240           /* (enemy.c's) */

/* SPIRIT BOMB */
#define BOMB_COST          4
#define BOMB_VY            (-0x40000)
#define BOMB_MAX           0x28000
#define BOMB_ACCEL         0x3000
#define BOMB_HOME_LIFE     1200
#define BOMB_FROM_Y        20

/* DUST DEVIL */
#define DEVILS             4
#define DEVIL_GAP          0x40
#define DEVIL_DONE_X       0x8C          /* of Flink's 320 */
#define DEVIL_AWAY_X       200
#define DEVIL_CHASE        300
#define DEVIL_HOLD         120
#define DEVIL_SEEK_ACCEL   0x800
#define DEVIL_HIT_ACCEL    0xC00
#define DEVIL_BODY_DY      8
#define BLADES             4
#define BLAD_FRAMES        4

/* LIGHTNING */
#define BOLTS              5
#define BOLT_TRIES         10
#define BOLT_ABOVE         0x3E
#define BOLT_LAST_DY       0x60
#define BOLT_CELLS         20
#define BOLT_SHAKE         0x23

/* PLATFORM */
#define CLOUD_LIFE         480
#define CLOUD_UNSEEN       15            /* (life > 0x1D1) */
#define CLOUD_WARN         60
#define CLOUD_DX           40
#define CLOUD_DY           20
#define CLOUD_DIP_VY       0x8000
#define CLOUD_DIP          15
#define CLOUD_LAYER        FLINK_LAYER(2)

/* SHIELD */
#define SHIELDS            3
#define SHIELD_STEP        10            /* of 1024 a turn, a frame */
#define SHIELD_RADIUS      31
#define SHIELD_FROM_Y      20

/* DEMON */
#define DEMON_COST         8
#define DEMON_GROW_COST    6
#define DEMON_HITS         5
#define DEMON_REST         0x5A
#define DEMON_GONE         0x78
#define DEMON_PLACE_DX     0x1E          /* at -30, in front of Flink */
#define DEMON_PLACE_DY     0x0F
#define DEMON_BEHIND_DX    0x1E          /* following, behind him */
#define DEMON_ABOVE        0x23
#define DEMON_FROM_Y       0x14          /* the enemies' y + 20 against Flink's */
#define DEMON_SEEK_X       0x50
#define DEMON_SEEK_Y       0x3C
#define DEMON_KEEP         0x64
#define DEMON_MAX          0x28000
#define DEMON_ACCEL        0x2000
#define DEMON_ATK_MAX      0x40000
#define DEMON_ATK_ACCEL    0x4000
#define DEMON_TURN         0x10000
#define DEMON_SPARKLE_DY   0x0F          /* (0x2ECA6's a3) */

enum { FX_NONE, FX_BOMB, FX_DEVIL, FX_BOLT, FX_CLOUD, FX_SHIELD, FX_DEMON };
enum { DEMON_WAITING, DEMON_FOLLOWING, DEMON_GOING };

typedef struct {
    int kind;
    BYTE *type;                          /* the actor's type when it was made (a reused slot is not it) */
    /* SPIRIT BOMB: its enemy (slot, type, placement), sent the next one; DUST DEVIL: the same,
       state (0 seeking, 1 done, 2 an enemy), hit (-1 not yet, 1 hit, 0 carrying), hold, chase */
    int target, chained, state, hit, hold, chase;
    BYTE *ttype, *tdef;
    int vmax, ty, cnt, sound;            /* DUST DEVIL: top speed, height on the screen, counter, sound played */
    int bx[BLADES], by[BLADES], bvx[BLADES], bvy[BLADES], bacc[BLADES];
    /* LIGHTNING: tries left, phase (FLA1..FLA3), bolts left, the animation's 8.8 frame */
    int tries, phase, left, acc;
    /* PLATFORM: life, its height, dip ticks, waiting for the gesture; SHIELD: waiting, angle, radius;
       DEMON: phase, its hits left (left), grown (state), rest (hold), going's ticks (life), its
       enemy (target) */
    int life, rest, dip, vy, waiting, angle, radius;
} Fx;
static Fx g_fx[MAX_ACTORS];

static int g_cast;                       /* the gesture (0x830C) */
static int g_castSpell = -1;             /* PLATFORM, SHIELD or DEMON taking effect (cast_now) */
int g_shieldCount;                       /* g_OrbitSpiritCount (0x814E); also cleared by spirits.c's new game */
static int g_shieldRestore;              /* a map started: the shield comes back */
int g_demonHits;                         /* g_DemonActive (0x814C); also cleared by spirits.c's new game */
static int g_demonGrown;                 /* 0x814B */
static int g_demonRestore;               /* a map started: the demon comes back */
static BYTE g_bombTaken[MAX_ACTORS];     /* SPIRIT BOMB's enemies taken (g_SpiritTargetMask 0x839C) */
static BYTE g_devilTaken[MAX_ACTORS];    /* DUST DEVIL's (0x8388) */
static unsigned g_rand = 0x1F3A5C77u;

static int rnd(void) { g_rand = g_rand * 1103515245u + 12345u; return (int)(g_rand >> 16) & 0x7FFF; }

static Fx *fx_of(BYTE *a, int kind)
{
    int k = a ? slot_of(a) : -1;
    if (k < 0 || g_fx[k].kind != kind || g_fx[k].type != A_TYPE(a) || A_LAYER(a) == KILLED_LAYER || !actor_active(k)) return NULL;
    return &g_fx[k];
}

static int any_fx(int kind, int waiting)
{
    int k;
    for (k = 0; k < MAX_ACTORS; k++) {
        Fx *f = fx_of(actor_at(k), kind);
        if (f && (waiting < 0 || f->waiting == waiting)) return 1;
    }
    return 0;
}

static void fx_free(BYTE *a)
{
    int k = slot_of(a);
    if (k >= 0) g_fx[k].kind = FX_NONE;
    if (*G_PRIDINGPLATFORM == a) *G_PRIDINGPLATFORM = NULL;
    Actor_Despawn(a);
}

static Fx *fx_new(BYTE *a, int kind)
{
    int k = slot_of(a);
    if (k < 0) return NULL;
    memset(&g_fx[k], 0, sizeof(g_fx[k]));
    g_fx[k].kind = kind;
    g_fx[k].type = A_TYPE(a);
    g_fx[k].target = -1;
    return &g_fx[k];
}

/* a spells actor (type name) at x, y on Lomax's plane, never culled, harmless */
static BYTE *fx_alloc(const char *type, int x, int y, int layer)
{
    BYTE *a;
    if (!ObjType_FindByName(type) || !(a = Actor_Alloc(NULL, type))) return NULL;
    A_PLANE(a) = *G_PLAYERPLANE;
    A_FLAGS(a) = 0;
    A_CULL(a) = CULL_NEVER;
    A_LAYER(a) = layer;
    A_FX(a) = x << 16;
    A_FY(a) = y << 16;
    A_VX(a) = A_VY(a) = 0;
    set_pos(a);
    return a;
}

static BYTE *cur_plane(void) { return PLANE(*G_PLAYERPLANE); }

static void __cdecl draw_none(BYTE *a) { (void)a; }
static void __cdecl draw_plain(BYTE *a) { draw_actor(a); }

/* the view's width (Lomax+ widens it), at least Flink's */
static int view_w(void)
{
    const BYTE *pl = cur_plane();
    int w = P_W(pl) * 16 - P_MAXX(pl);
    return P_MAXX(pl) <= 0 || w < FLINK_VIEW_W ? FLINK_VIEW_W : w;
}

/* Actor_MoveToward (0x1B008) on one axis: v toward the target by accel (2 * accel while going the
   other way), not past max */
static int toward(int pos, int target, int v, int max, int accel)
{
    if (target == pos && (v < 0 ? -v : v) <= accel) return 0;
    if (target >= pos) { if (v < max) v += v < 0 ? 2 * accel : accel; }
    else if (-v < max) v -= v > 0 ? 2 * accel : accel;
    return v;
}

static void move_to(BYTE *a, int x, int y, int max, int accel)
{
    A_VX(a) = toward(A_X(a), x, A_VX(a), max, accel);
    A_VY(a) = toward(A_Y(a), y, A_VY(a), max, accel);
    A_FX(a) += A_VX(a);
    A_FY(a) += A_VY(a);
    set_pos(a);
}

/* Actor_SetAnim then the record once (Actor_AdvanceAnim with +3 bit 0: it stops on its last frame):
   1 while it plays */
static void once_start(BYTE *a, Fx *f, const char *tag) { Actor_SetAnim(a, FOURCC(tag)); f->acc = 0; }

static int once_step(BYTE *a, Fx *f)
{
    const BYTE *r = A_RECORD(a);
    int n = A_NFRAMES(a), fr = f->acc >> 8;
    if (!r || fr >= n) { hold_frame(a, n > 0 ? n - 1 : 0); Actor_AdvanceAnim(a); return 0; }
    hold_frame(a, fr);
    Actor_AdvanceAnim(a);
    f->acc += *(const WORD *)(r + 0x0C);     /* (the record's speed: +8 entries, +0xA frames, +0xC speed) */
    return 1;
}

/* the actor stands and the spell can be aimed at it: Flink's +6 bits 3 and 4 (stompable, on the
   screen), not frozen, not a Rock; here a live Flink enemy on the view that a thrown object hits */
static int is_target(BYTE *t, int k)
{
    int e = enemy_slot(t);
    if (e < 0 || !actor_active(k) || A_LAYER(t) == KILLED_LAYER || A_PLANE(t) != *G_PLAYERPLANE) return 0;
    if (!g_enemy[e].type || g_enemy[e].dead || g_enemy[e].razo || grab_held(t) || !enemy_on_screen(t)) return 0;
    return (A_FLAGS(t) & AF_HITTABLE) || g_enemy[e].boss || g_enemy[e].cycl || g_enemy[e].wurm || g_enemy[e].donk;
}

/* its enemy still the one it took (Flink: the slot's NMY and GNMY bit) and on the screen; standing
   (a target still), unless any */
static BYTE *target_of(Fx *f, int any)
{
    BYTE *t = actor_at(f->target);
    if (!t || A_TYPE(t) != f->ttype || A_DEF(t) != f->tdef || !actor_active(f->target) || A_LAYER(t) == KILLED_LAYER) return NULL;
    return (any ? enemy_on_screen(t) : is_target(t, f->target)) ? t : NULL;
}

static void take(Fx *f, BYTE *t, int k, BYTE *taken)
{
    f->target = k;
    f->ttype = A_TYPE(t);
    f->tdef = A_DEF(t);
    if (taken) taken[k] = 1;
}

/* the gesture starts (no follow-through: 0x8310 set): Flink stops (0x1A778 drops what he carries;
   here he carries nothing) */
static void cast_start(int t)
{
    g_cast = t;
    *G_PLAYERVX = 0;
}

static void cast_now(int spell);

/* ---- SPIRIT BOMB ---------------------------------------------------------------------------------- */

static void __cdecl bomb_update(BYTE *d);

/* 0x30CD4: a spirit for the first enemy not taken; 0 if none (or too few spirits held) */
static int bomb_spawn(void)
{
    int k;
    BYTE *d;
    Fx *f;
    if (g_spirits < BOMB_COST) return 0;
    for (k = 0; k < MAX_ACTORS; k++) {
        BYTE *t = actor_at(k);
        if (g_bombTaken[k] || !is_target(t, k)) continue;
        if (!(d = spirit_release(NULL, *G_PLAYERX, *G_PLAYERY - BOMB_FROM_Y)) || !(f = fx_new(d, FX_BOMB))) return 0;
        take(f, t, k, g_bombTaken);
        A_VY(d) = BOMB_VY;
        A_LAYER(d) = FLINK_LAYER(1);
        A_UPDATE(d) = (void *)bomb_update;
        g_spirits -= BOMB_COST;
        flink_sound(d, 0x2B);
        flog("SPIRIT BOMB: a spirit for the enemy at (%d,%d), %d spirits left", A_X(t), A_Y(t), g_spirits);
        return 1;
    }
    return 0;
}

/* 0x30E06 */
static void __cdecl bomb_update(BYTE *d)
{
    Fx *f = fx_of(d, FX_BOMB);
    BYTE *t;
    int x1, y1, x2, y2;
    if (!f) return;
    spirit_trail(d);
    if (!f->chained && !(g_cast & 15)) { f->chained = 1; bomb_spawn(); }
    if (!(t = target_of(f, 0))) {                /* the spirit a defeated enemy sets free (0x2EE2C) */
        f->kind = FX_NONE;
        spirit_home(d, BOMB_HOME_LIFE);
        return;
    }
    if (actor_box(d, &x1, &y1, &x2, &y2) && spell_strike(d, t, x1, y1, x2, y2)) {
        flog("SPIRIT BOMB: the spirit hits its enemy at (%d,%d)", A_X(t), A_Y(t));
        f->kind = FX_NONE;
        spirit_vanish(d);
        return;
    }
    {
        const BYTE *r = A_RECORD(t);
        move_to(d, A_X(t), A_Y(t) + (r ? (HB_Y1(r) + HB_Y2(r)) / 2 : 0), BOMB_MAX, BOMB_ACCEL);
    }
    Actor_AdvanceAnim(d);
}

/* 0x30C66 */
static int bomb_cast(void)
{
    if (any_fx(FX_BOMB, -1)) return 1;
    memset(g_bombTaken, 0, sizeof(g_bombTaken));
    if (!spell_affordable(SPELL_SPIRIT_BOMB)) return 1;   /* (the cost flashes) */
    if (!bomb_spawn()) return 1;
    cast_start(CAST_BOMB);
    return 1;
}

/* ---- DUST DEVIL ----------------------------------------------------------------------------------- */

/* one blade: BLAD's frame (one piece), mirrored or not */
static void blade_draw(BYTE *a, int frame, int x, int y, int mirror)
{
    const BYTE *e = record_of(a, FOURCC("BLAD")), *bank;
    if (!e) return;
    e += (frame % BLAD_FRAMES) * 8;
    if (!(bank = G_ANIMBANKS[e[0] | (T_BANKS(A_TYPE(a)) & 0x100)])) return;
    Sprite_QueueWorld(PLANE(A_PLANE(a)), bank + e[1] * 0x38 + 0x10, x + *(const short *)(e + 2), y + *(const short *)(e + 4),
                      (unsigned)Actor_GetDrawDepth(a) << 24 | ((e[6] ^ (mirror ? 0x08 : 0)) & 0x18), 20000);
}

/* 0x31378: the blades (frames 0/1 and 2/3 by turns every 16 frames, the 3rd and 4th mirrored), then
   the Tornado */
static void __cdecl devil_draw(BYTE *a)
{
    Fx *f = fx_of(a, FX_DEVIL);
    int n, odd;
    if (f) {
        odd = (f->cnt & 16) != 0;
        for (n = 0; n < BLADES; n++)
            blade_draw(a, (n & 1 ? 2 : 0) + odd, f->bx[n] >> 16, f->by[n] >> 16, n >= 2);
    }
    draw_actor(a);
}

/* the body it carries (0x313E6): moved by the Tornado */
static void __cdecl body_update(BYTE *a) { (void)a; }

static void devil_free(BYTE *a, Fx *f)
{
    BYTE *t = actor_at(f->target);
    if (f->hit == 0 && t && A_TYPE(t) == f->ttype && A_UPDATE(t) == (void *)body_update) {
        int e = enemy_slot(t);
        if (e >= 0) g_enemy[e].type = NULL;
        Actor_Kill(t);
    }
    fx_free(a);
}

/* Enemy_Die's fall (0x1B1AA): dead, DEAD falling away (not one that breaks up) */
static int dying(BYTE *t)
{
    int e = enemy_slot(t);
    const BYTE *r = A_RECORD(t);
    return e >= 0 && g_enemy[e].dead && !g_enemy[e].worm && !g_enemy[e].fire && r && *(const DWORD *)(r + 4) == FOURCC("DEAD");
}

/* 0x31142 */
static void __cdecl devil_update(BYTE *a)
{
    Fx *f = fx_of(a, FX_DEVIL);
    const BYTE *pl = cur_plane();
    int cx = P_CAMX(pl), cy = P_CAMY(pl), vw = view_w(), n, x1, y1, x2, y2;
    BYTE *t;
    if (!f) return;
    for (n = 0; n < BLADES; n++) {               /* (0x313A2) */
        f->bvy[n] -= f->bacc[n];
        f->bx[n] += f->bvx[n];
        f->by[n] += f->bvy[n];
    }
    if (!f->sound && enemy_on_screen(a)) { f->sound = 1; flink_sound(a, 0x2C); }
    f->cnt++;
    if (f->hold >= 0 && --f->hold == 0) f->state = 1;
    if (f->state == 2 && f->hit != 0) {          /* an enemy */
        t = target_of(f, f->hit > 0);            /* (hit: it may be dying) */
        if (--f->chase < 0 || !t) f->state = 1;
        else if (f->hit > 0) {
            if (dying(t)) {                      /* caught */
                f->hit = 0;
                f->hold = -1;
                f->state = 1;
                A_UPDATE(t) = (void *)body_update;
                A_LAYER(t) = FLINK_LAYER(2);
                A_CULL(t) = CULL_NEVER;
                flog("DUST DEVIL: carries the enemy from (%d,%d) away", A_X(t), A_Y(t));
            }
        } else if (actor_box(a, &x1, &y1, &x2, &y2) && spell_strike(a, t, x1, y1, x2, y2)) {
            f->hit = 1;
            if (f->hold < 0) f->hold = DEVIL_HOLD;
            flog("DUST DEVIL: hits the enemy at (%d,%d)", A_X(t), A_Y(t));
        }
        if (f->state == 2) {
            const BYTE *r = A_RECORD(t);
            move_to(a, A_X(t), A_Y(t) + (r ? HB_Y2(r) : 0), f->vmax, DEVIL_HIT_ACCEL);
        }
    } else if (f->state == 0) {                  /* (0x3129E) */
        for (n = f->cnt & 1; n < MAX_ACTORS; n += 2) {
            t = actor_at(n);
            if (g_devilTaken[n] || !is_target(t, n)) continue;
            take(f, t, n, g_devilTaken);
            f->state = 2;
            f->hit = -1;
            break;
        }
        if (cx + DEVIL_DONE_X * vw / FLINK_VIEW_W >= A_X(a)) f->state = 1;
        else move_to(a, cx + DEVIL_DONE_X * vw / FLINK_VIEW_W, cy + f->ty, f->vmax, DEVIL_SEEK_ACCEL);
    }
    if (f->state == 1 || (f->state == 2 && f->hit == 0)) {   /* done (0x31280) */
        if (!enemy_on_screen(a)) { devil_free(a, f); return; }
        move_to(a, cx - DEVIL_AWAY_X, cy, f->vmax, DEVIL_SEEK_ACCEL);
    }
    if (f->hit == 0 && (t = actor_at(f->target)) && A_UPDATE(t) == (void *)body_update) {
        A_X(t) = A_X(a); A_FX(t) = A_X(t) << 16;
        A_Y(t) = A_Y(a) - DEVIL_BODY_DY; A_FY(t) = A_Y(t) << 16;
        Actor_AdvanceAnim(t);
    }
    Actor_AdvanceAnim(a);
}

/* 0x30FF0: Tornado k */
static void devil_spawn(int k)
{
    const BYTE *pl = cur_plane();
    int x = P_CAMX(pl) + view_w() + (k + 1) * DEVIL_GAP, y = P_CAMY(pl) - 0x40 - (rnd() & 0x7F), n;
    BYTE *a = fx_alloc("Spik Spells", x, y, FLINK_LAYER(1));
    Fx *f;
    if (!a || !(f = fx_new(a, FX_DEVIL))) return;
    f->vmax = ((rnd() & 7) + 0x10) << 13;
    A_VY(a) = ((rnd() & 15) + 8) << 13;
    f->ty = (rnd() & 0x7F) + 0x20;
    f->cnt = rnd();
    f->hold = -1;
    f->hit = -1;
    f->chase = DEVIL_CHASE;
    for (n = 0; n < BLADES; n++) {
        f->bx[n] = (x + (rnd() & 0x7F)) << 16;
        f->by[n] = (y + 0x1E + (rnd() & 0x7F)) << 16;
        f->bvx[n] = -(((rnd() & 7) + 13) << 13);
        f->bvy[n] = ((rnd() & 7) + 12) << 13;
        f->bacc[n] = ((rnd() & 3) + 3) << 8;
    }
    Actor_SetAnim(a, FOURCC("DUST"));
    A_UPDATE(a) = (void *)devil_update;
    A_DRAW(a) = (void *)devil_draw;
}

/* 0x30F12 */
static int devil_cast(void)
{
    int k;
    if (any_fx(FX_DEVIL, -1)) return 1;
    memset(g_devilTaken, 0, sizeof(g_devilTaken));
    if (!spell_pay(SPELL_DUST_DEVIL)) return 1;
    cast_start(CAST_SPELL);
    for (k = 0; k < DEVILS; k++) devil_spawn(k);
    flog("DUST DEVIL: %d Tornados", DEVILS);
    return 1;
}

/* ---- LIGHTNING ------------------------------------------------------------------------------------ */

static void __cdecl bolt_update(BYTE *a);

/* 0x31D36 */
static void bolt_spawn(int left)
{
    BYTE *a = fx_alloc("Spik Spells", *G_PLAYERX, *G_PLAYERY, FLINK_LAYER(1));
    Fx *f;
    if (!a || !(f = fx_new(a, FX_BOLT))) return;
    f->tries = BOLT_TRIES;
    f->left = left;
    once_start(a, f, "FLA1");
    A_UPDATE(a) = (void *)bolt_update;
    A_DRAW(a) = (void *)draw_none;           /* (unseen until placed) */
}

/* 0x31D7C */
static void __cdecl bolt_update(BYTE *a)
{
    Fx *f = fx_of(a, FX_BOLT);
    const BYTE *pl = cur_plane();
    int x1, y1, x2, y2;
    if (!f) return;
    if (f->tries) {                              /* a place */
        int x = P_CAMX(pl) + 0x20 + rnd() % (view_w() - 0x40), y = P_CAMY(pl) + (rnd() & 0x7F), g;
        if (--f->tries == 0) g = y + BOLT_LAST_DY;
        else {
            g = (int)Plane_FindGroundY(PLANE(A_PLANE(a)), x, y, BOLT_CELLS);
            if (g > P_H(pl) * 16) g = P_H(pl) * 16;
            if (g > P_CAMY(pl) + VIEW_H) return;
        }
        A_X(a) = x; A_FX(a) = x << 16;
        A_Y(a) = g - BOLT_ABOVE; A_FY(a) = A_Y(a) << 16;
        f->tries = 0;
        A_DRAW(a) = (void *)draw_plain;
        flog("LIGHTNING: a bolt at (%d,%d)", A_X(a), g);
        return;
    }
    if (!once_step(a, f)) {
        static const char *const TAGS[3] = { "FLA1", "FLA2", "FLA3" };
        if (++f->phase >= 3) { fx_free(a); return; }
        if (f->phase == 2 && --f->left > 0) bolt_spawn(f->left);
        if (f->phase == 1) {
            flink_sound(a, 0x2D);
            if (!flink_quake()) flink_quake_set(BOLT_SHAKE);
        }
        once_start(a, f, TAGS[f->phase]);
        once_step(a, f);
    }
    if (f->phase == 1 && actor_box(a, &x1, &y1, &x2, &y2) && spell_strike(a, NULL, x1, y1, x2, y2))
        flog("LIGHTNING: the bolt at (%d,%d) hits", A_X(a), A_Y(a));
}

/* 0x31CC6 */
static int bolt_cast(void)
{
    if (any_fx(FX_BOLT, -1)) return 1;
    if (!spell_pay(SPELL_LIGHTNING)) return 1;
    cast_start(CAST_SPELL);
    bolt_spawn(BOLTS);
    return 1;
}

/* ---- PLATFORM ------------------------------------------------------------------------------------- */

/* 0x31508 */
static void __cdecl cloud_update(BYTE *a)
{
    Fx *f = fx_of(a, FX_CLOUD);
    BYTE *was;
    int on;
    if (!f || f->waiting) return;
    if (--f->life > 0) {
        if (f->life > CLOUD_LIFE - CLOUD_UNSEEN) return;
        if (f->life == CLOUD_LIFE - CLOUD_UNSEEN) A_DRAW(a) = (void *)draw_plain;
        if (f->life == CLOUD_WARN) Actor_SetAnim(a, FOURCC("WARN"));
        if (f->dip) { if (--f->dip == 0) f->vy = -f->vy; }
        else if (A_Y(a) == f->rest) f->vy = 0;
        was = *G_PRIDINGPLATFORM;
        A_VX(a) = 0;                             /* (its speed only for the move: plant.c's way) */
        A_VY(a) = f->vy;
        on = Platform_MoveAndCarryPlayer(a, NULL);
        A_VY(a) = 0;
        if (on) {
            if (was != a) { f->vy = CLOUD_DIP_VY; f->dip = CLOUD_DIP; }   /* landed on */
        } else if (f->dip) { f->dip = 0; f->vy = -f->vy; }
        Actor_AdvanceAnim(a);
        return;
    }
    if (f->life == 0) {
        if (*G_PRIDINGPLATFORM == a) *G_PRIDINGPLATFORM = NULL;
        once_start(a, f, "PUFF");
        flink_sound(a, 0x42);
    }
    if (!once_step(a, f)) fx_free(a);
}

/* 0x31474 at -30: paid, placed in front of Lomax, the puff */
static void cloud_place(BYTE *a, Fx *f)
{
    int x = *G_PLAYERX + (*G_PLAYERFLAGS & AF_LEFT ? -CLOUD_DX : CLOUD_DX), y = *G_PLAYERY - CLOUD_DY;
    f->waiting = 0;
    f->life = CLOUD_LIFE;
    f->rest = y;
    A_X(a) = x; A_FX(a) = x << 16;
    A_Y(a) = y; A_FY(a) = y << 16;
    A_PLANE(a) = *G_PLAYERPLANE;
    Effect_SpawnPoof(x, y + 8);
    flink_sound(a, 0x42);
    flog("PLATFORM: a cloud at (%d,%d)", x, y);
}

/* 0x3140E: the cloud waits for the gesture */
static int cloud_cast(void)
{
    BYTE *a;
    Fx *f;
    if (!spell_affordable(SPELL_PLATFORM)) return 1;
    if (!(a = fx_alloc("Spik Cloud", *G_PLAYERX, *G_PLAYERY, CLOUD_LAYER)) || !(f = fx_new(a, FX_CLOUD))) return 1;
    f->waiting = 1;
    Actor_SetAnim(a, FOURCC("MAIN"));
    A_UPDATE(a) = (void *)cloud_update;
    A_DRAW(a) = (void *)draw_none;
    cast_now(SPELL_PLATFORM);
    return 1;
}

/* ---- SHIELD --------------------------------------------------------------------------------------- */

static void shield_place(BYTE *d, Fx *f)
{
    int r = f->radius, x = *G_PLAYERX + G_SINTABLE[f->angle & 1023] * r / 16384;
    int y = *G_PLAYERY - SHIELD_FROM_Y + G_SINTABLE[(f->angle + 256) & 1023] * (r - 4) / 16384;
    A_X(d) = x; A_FX(d) = x << 16;
    A_Y(d) = y; A_FY(d) = y << 16;
    A_PLANE(d) = *G_PLAYERPLANE;
}

/* 0x31772 */
static void __cdecl shield_update(BYTE *d)
{
    Fx *f = fx_of(d, FX_SHIELD);
    int x1, y1, x2, y2;
    if (!f) return;
    if (f->waiting) { Actor_AdvanceAnim(d); return; }
    f->angle = (f->angle + SHIELD_STEP) & 1023;
    if (f->radius < SHIELD_RADIUS) f->radius++;
    shield_place(d, f);
    spirit_trail(d);
    if (actor_box(d, &x1, &y1, &x2, &y2) && spell_strike(d, NULL, x1, y1, x2, y2)) {
        if (g_shieldCount > 0) g_shieldCount--;
        f->kind = FX_NONE;
        flog("SHIELD: a spirit hits, %d left", g_shieldCount);
        spirit_vanish(d);
        return;
    }
    Actor_AdvanceAnim(d);
}

/* 0x31698: a spirit of the shield, at angle a (1024 a turn) */
static Fx *shield_spawn(int angle, int radius, int waiting)
{
    BYTE *d = spirit_release(NULL, *G_PLAYERX, *G_PLAYERY - SHIELD_FROM_Y);
    Fx *f;
    if (!d || !(f = fx_new(d, FX_SHIELD))) return NULL;
    f->angle = angle;
    f->radius = radius;
    f->waiting = waiting;
    A_VY(d) = 0;
    A_LAYER(d) = FLINK_LAYER(1);
    A_UPDATE(d) = (void *)shield_update;
    if (waiting) A_FLAGS(d) |= AF_HIDDEN;
    else shield_place(d, f);
    return f;
}

/* 0x31606: three spirits wait for the gesture (Flink's angles 0x554, 0x2AA, 0 of 0x800) */
static int shield_cast(void)
{
    int n;
    if (any_fx(FX_SHIELD, 0)) return 1;
    if (!spell_affordable(SPELL_SHIELD)) return 1;
    for (n = 0; n < SHIELDS; n++) shield_spawn((SHIELDS - 1 - n) * 1024 / SHIELDS, 0, 1);
    cast_now(SPELL_SHIELD);
    return 1;
}

/* 0x316F0: a map starts: the shield's spirits circling at full radius */
static void shield_restore(void)
{
    int n, have = 0;
    for (n = 0; n < MAX_ACTORS; n++) if (fx_of(actor_at(n), FX_SHIELD)) have++;
    for (n = have; n < g_shieldCount; n++) shield_spawn(n * 1024 / SHIELDS, SHIELD_RADIUS, 0);
    if (g_shieldCount > have) flog("SHIELD: %d spirits back", g_shieldCount - have);
}

/* ---- DEMON --------------------------------------------------------------------------------------- */

/* its demon (following), or NULL */
static BYTE *demon_find(Fx **pf)
{
    int k;
    for (k = 0; k < MAX_ACTORS; k++) {
        BYTE *a = actor_at(k);
        Fx *f = fx_of(a, FX_DEMON);
        if (f && f->phase == DEMON_FOLLOWING) { if (pf) *pf = f; return a; }
    }
    return NULL;
}

int spellfx_demon_cost(void)
{
    Fx *f;
    return demon_find(&f) && !f->state ? DEMON_GROW_COST : DEMON_COST;
}

static int dist(int a, int b) { return a > b ? a - b : b - a; }

/* the hits left (g_DemonActive), not while Lomax dies (Player_Die clears it) */
static void demon_hits(int n) { if (!*G_PLAYERDEATHTIMER) g_demonHits = n; }

/* 0x31A1E */
static void __cdecl demon_update(BYTE *a)
{
    Fx *f = fx_of(a, FX_DEMON);
    BYTE *t;
    int px = *G_PLAYERX, py = *G_PLAYERY, left = (*G_PLAYERFLAGS & AF_LEFT) != 0, k, x1, y1, x2, y2;
    short frame;
    if (!f || f->phase == DEMON_WAITING) return;
    if (f->phase == DEMON_GOING) { if (--f->life < 0) fx_free(a); return; }   /* (0x31CBA) */
    A_PLANE(a) = *G_PLAYERPLANE;
    frame = *(short *)(a + 0x6A);                /* (+0x30 kept through the records) */
    if (f->target >= 0) {
        t = target_of(f, 0);
        if (t && dist(A_X(t), px) <= DEMON_KEEP && dist(A_Y(t) + DEMON_FROM_Y, py) <= DEMON_KEEP) {
            const BYTE *r = A_RECORD(t);
            move_to(a, A_X(t), A_Y(t) + (r ? (HB_Y1(r) + HB_Y2(r)) / 2 : 0), DEMON_ATK_MAX, DEMON_ATK_ACCEL);
            A_LAYER(a) = FLINK_LAYER(1);
            set_anim(a, f->state ? "ATK2" : "ATK1");
            if (!actor_box(a, &x1, &y1, &x2, &y2) || !spell_strike(a, t, x1, y1, x2, y2)) goto done;
            f->hold = DEMON_REST;
            demon_hits(--f->left);
            flog("DEMON: hits the enemy at (%d,%d), %d hits left", A_X(t), A_Y(t), f->left);
            if (!f->left) {
                f->phase = DEMON_GOING;
                f->life = DEMON_GONE;
                g_demonGrown = 0;
                A_DRAW(a) = (void *)draw_none;
                spirit_sparkle(a, DEMON_SPARKLE_DY);
                return;
            }
        }
        f->target = -1;
    }
    A_LAYER(a) = FLINK_LAYER(2);
    set_anim(a, f->state ? "FLY2" : "FLY1");
    if (f->hold) f->hold--;
    else for (k = ++f->cnt & 1; k < MAX_ACTORS; k += 2) {
        t = actor_at(k);
        if (!is_target(t, k) || dist(A_X(t), px) > DEMON_SEEK_X || dist(A_Y(t) + DEMON_FROM_Y, py) > DEMON_SEEK_Y) continue;
        take(f, t, k, NULL);
        break;
    }
    move_to(a, px + (left ? DEMON_BEHIND_DX : -DEMON_BEHIND_DX), py - DEMON_ABOVE, DEMON_MAX, DEMON_ACCEL);
done:
    *(short *)(a + 0x6A) = frame;
    if (A_VX(a) > DEMON_TURN) A_FLAGS(a) &= ~AF_LEFT;
    else if (A_VX(a) < -DEMON_TURN) A_FLAGS(a) |= AF_LEFT;
    Actor_AdvanceAnim(a);
}

/* 0x318E0: the demon, its hits, grown or not */
static BYTE *demon_spawn(int phase, int hits, int grown)
{
    BYTE *a = fx_alloc("Spik Spells", *G_PLAYERX, *G_PLAYERY, FLINK_LAYER(2));
    Fx *f;
    if (!a || !(f = fx_new(a, FX_DEMON))) { if (a) Actor_Despawn(a); return NULL; }
    f->phase = phase;
    f->left = hits;
    f->state = grown;
    Actor_SetAnim(a, FOURCC(grown ? "FLY2" : "FLY1"));
    A_UPDATE(a) = (void *)demon_update;
    A_DRAW(a) = (void *)(phase == DEMON_WAITING ? draw_none : draw_plain);
    return a;
}

static void demon_place(BYTE *a, int x, int y)
{
    A_X(a) = x; A_FX(a) = x << 16;
    A_Y(a) = y; A_FY(a) = y << 16;
    A_PLANE(a) = *G_PLAYERPLANE;
}

/* Flink's -30 (0x31992, 0x31A3A), here on the press */
static void demon_takes_effect(void)
{
    int k;
    for (k = 0; k < MAX_ACTORS; k++) {
        BYTE *a = actor_at(k);
        Fx *f = fx_of(a, FX_DEMON);
        if (!f) continue;
        if (f->phase == DEMON_WAITING) {
            if (!spell_pay(SPELL_DEMON)) {                       /* (0x319B0) */
                g_demonHits = g_demonGrown = 0;
                fx_free(a);
                continue;
            }
            f->phase = DEMON_FOLLOWING;
            demon_place(a, *G_PLAYERX + (*G_PLAYERFLAGS & AF_LEFT ? -DEMON_PLACE_DX : DEMON_PLACE_DX), *G_PLAYERY - DEMON_PLACE_DY);
            A_DRAW(a) = (void *)draw_plain;
            spirit_sparkle(a, DEMON_SPARKLE_DY);
            flog("DEMON: a demon at (%d,%d), %d hits", A_X(a), A_Y(a), f->left);
        } else if (f->phase == DEMON_FOLLOWING && !f->state && spell_pay(SPELL_DEMON)) {
            f->state = g_demonGrown = 1;
            f->left += DEMON_HITS;
            demon_hits(f->left);
            spirit_sparkle(a, DEMON_SPARKLE_DY);
            flog("DEMON: grown, %d hits", f->left);
        }
    }
}

/* 0x31856 */
static int demon_cast(void)
{
    Fx *f;
    if (demon_find(&f)) {
        if (f->state) return 1;                  /* (0x34130) */
        if (spell_affordable(SPELL_DEMON)) cast_now(SPELL_DEMON);
        return 1;
    }
    if (!spell_affordable(SPELL_DEMON) || !demon_spawn(DEMON_WAITING, DEMON_HITS, 0)) return 1;
    g_demonGrown = 0;
    g_demonHits = DEMON_HITS;
    cast_now(SPELL_DEMON);
    return 1;
}

/* 0x3193A: a map starts: the demon, left of Lomax */
static void demon_restore(void)
{
    BYTE *a;
    if (demon_find(NULL) || !(a = demon_spawn(DEMON_FOLLOWING, g_demonHits, g_demonGrown))) return;
    demon_place(a, *G_PLAYERX - DEMON_BEHIND_DX, *G_PLAYERY - DEMON_ABOVE);
    flog("DEMON: back, %d hits%s", g_demonHits, g_demonGrown ? ", grown" : "");
}

/* ---- The gesture ---------------------------------------------------------------------------------- */

/* PLATFORM's cloud and SHIELD's spirits at Flink's -30 (0x31474, 0x31726), here on the press:
   paid, or the cost flashes and they go */
static void cast_takes_effect(void)
{
    int k, paid = spell_pay(g_castSpell), n = 0;
    for (k = 0; k < MAX_ACTORS; k++) {
        BYTE *a = actor_at(k);
        Fx *f = fx_of(a, FX_CLOUD);
        if (!f) f = fx_of(a, FX_SHIELD);
        if (!f || !f->waiting) continue;
        if (!paid) { f->kind = FX_NONE; Actor_Despawn(a); continue; }
        if (f->kind == FX_CLOUD) cloud_place(a, f);
        else { f->waiting = 0; A_FLAGS(a) &= ~AF_HIDDEN; n++; }
    }
    if (paid && n) { g_shieldCount += n; flog("SHIELD: %d spirits circle Lomax", g_shieldCount); }
}

/* what Flink's gesture does at -30, on the press: the cast sound (0x20, Flink's at -40), then the
   spell's actors take effect */
static void cast_now(int spell)
{
    g_castSpell = spell;
    flink_sound(NULL, 0x20);
    if (spell == SPELL_DEMON) demon_takes_effect();
    else cast_takes_effect();
    g_castSpell = -1;
}

/* Player_Update's count (0x1CBA2): standing, MAG1 down to 0 (the spells here have no follow-through);
   walking holds it, off the ground it stops (0x1C7E2) */
static void cast_tick(void)
{
    if (!g_cast) return;
    if (*G_PLAYERSTATE & PS_AIR || *G_PLAYERHURTTIMER > 0 || *G_PLAYERDEATHTIMER) g_cast = 0;
    else if (!*G_PLAYERVX) g_cast--;
}

/* every tick, after Player_Update (spells_tick) */
void spellfx_tick(void)
{
    if (!g_active || *G_BPAUSED) return;
    if (*G_PLAYERDEATHTIMER && g_shieldCount) { g_shieldCount = 0; flog("SHIELD: lost"); }   /* (Player_Die) */
    if (*G_PLAYERDEATHTIMER && g_demonHits) { g_demonHits = g_demonGrown = 0; flog("DEMON: lost"); }
    if (g_shieldRestore && !*G_PLAYERDEATHTIMER) { g_shieldRestore = 0; shield_restore(); }
    if (g_demonRestore && !*G_PLAYERDEATHTIMER) { g_demonRestore = 0; if (g_demonHits > 0) demon_restore(); }
    cast_tick();
}

/* Ability on the ground with nothing to pick up (grab.c): the chosen spell's handler; 1 if it took
   the press */
int spell_cast(void)
{
    if (!g_active || g_cast || !spell_can_cast()) return 0;
    g_rand ^= GetTickCount();                    /* (Rand_GetNext 0x19C8A mixes in the stopwatch) */
    if (spell_chosen(SPELL_SPIRIT_BOMB)) return bomb_cast();
    if (spell_chosen(SPELL_DUST_DEVIL)) return devil_cast();
    if (spell_chosen(SPELL_LIGHTNING)) return bolt_cast();
    if (spell_chosen(SPELL_PLATFORM)) return cloud_cast();
    if (spell_chosen(SPELL_SHIELD)) return shield_cast();
    if (spell_chosen(SPELL_DEMON)) return demon_cast();
    if (spell_chosen(SPELL_HELMET)) return spell_helmet_cast();   /* (spells.c) */
    return 0;
}

/* a map loads (world_load) */
void spellfx_reset(void)
{
    memset(g_fx, 0, sizeof(g_fx));
    memset(g_bombTaken, 0, sizeof(g_bombTaken));
    memset(g_devilTaken, 0, sizeof(g_devilTaken));
    g_cast = 0;
    g_castSpell = -1;
    g_shieldRestore = g_shieldCount > 0;
    g_demonRestore = g_demonHits > 0;
}

/* a new game (levels.c: the title; spirits.c: Game_ResetNewGame) */
void spellfx_new_game(void) { g_shieldCount = 0; g_demonHits = g_demonGrown = 0; }
