/*
 * villageboss.c - Flink's Village Boss (actor 'Boss', init 0x2948C -> 0x29A9E for
 * VILLAGE, think 0x29CCC), in 16-2 (VILLAGE map 17) (README "Enemies" (The Village Boss)). The
 * converter makes it a 'Muta Village n' type (vboss_records): RUN, WALK, STAY (the Village Boss), PRUN, PWAL,
 * PSTA (each frame's three men's points), MEN1 .. MEN5 (the men), BAL1 / BAL3 / BL2L / BL2R (their
 * chains' links, the middle's spiked head and the sides' hands, drawn with their top-left at the
 * point), DEAD and BOOM; params 0, the music at its defeat. Its placement links to the arena's Finish.
 *
 * Flink's Village Boss:
 * - Init (0x29A9E): the map wraps around from now on and the camera scrolls on past its edges (0x8726,
 *   Camera_Update 0x19EAE; the map's Scroll Point moves it 1 px a frame); the screen's edges are no
 *   walls for Flink (0x871E). Three child men (0x32CD8) at +0x4C (left, MEN4), +0x4E (middle, MEN1)
 *   and +0x50 (right, MEN4, its animation 5/4 as fast), each on a chain (+0x48 its length, +0x4A its
 *   speed). The Village Boss starts at x -500 running (+0x1C = 2), on draw list 3 (behind everything).
 *   0x8282 holds who is left: bit 3 the left man, bit 2 the right one, bit 1 the middle one's first
 *   hit to come, bit 0 the Village Boss (15 at each level start).
 * - Each frame (0x29CCC): every +0x54 frames (450 first) it drops in, 40 px past the screen's right
 *   edge on the ground (0x828A: y 208), a big stone ('Ston', 0x32BD4) and then a Jumppad ('Jump',
 *   draw list 1), turn about: 100 + (rand & 63) frames after a stone, 300 + (rand & 127) after a
 *   Jumppad. Then by +0x1C:
 *     2 running: RUN, x + 2 a frame, until it is more than 260 px into the screen at a frame 0;
 *     1 walking: WALK, x + 1 (the screen's pace), for +0x52 = 200 + (rand & 127) frames;
 *     0 standing: STAY, for +0x52 = 60 + (rand & 127) frames, then walking if more than 60 px into the
 *       screen, else running. Each stop shoots a chain up: half the time the middle one, else (if the
 *       left one is down) both side ones (+0x4A = 2).
 * - A side man (0x29F80): its chain grows 2 px a frame; at 76 px it hits the ceiling and goes back:
 *   if the Village Boss holds no stone, the screen shakes (0x874E = 35), sound 0x2A, and a big stone is
 *   taken from the ceiling (spawned at the man's x, y 20, no think, hidden: +0x64), held between the
 *   two hands (the left hand's top + (21, 8), 0x2A442), both chains bending to it. Coming back the
 *   chains stop at 16 px; while they wait there, Flink within 25 px of the Village Boss's x makes both go
 *   down, and the stone is let go onto him: a thrown object again (hook 0x2A122), whose lifetime (0)
 *   is out, so it hurts Flink as it falls (0x32588).
 * - The middle man (0x2A13C): its chain grows to 72 px (the shake, sound 0x2A), back down to 20, up to
 *   72 again (the shake) and down. At the first 72 three stones come out of the ceiling at its x - 64
 *   + d, d = rand & 127, then d + 32 + (rand & 31) twice (mod 128), at y 20 (0x2A2A6); each sinks
 *   1 px a frame to y 35 and falls at the next shake (0x2A2F2): a thrown object with vy 1/65536 up
 *   and vx ((rand & 31) - 16) / 8, both odd (no hits while odd, 0x3256A), falling at half the gravity
 *   (hook 0x2A33E: + 0x2000 a frame) and no faster than 3.75 above y 90 (0x2A346).
 * - The men are drawn at their frame's points (the hook table 0x2A534): each chain from its man's
 *   point (middle x - 11, sides + 2 / - 16, y + 38 / + 39) up by its length along the curve at
 *   0x14B1E (eased below 32 px, then 1.5 px a px), a link every 6 px of length, its head first (the
 *   middle one's next links 4 px right and down of it, the sides' 4 px in, 10 with a stone), then the
 *   man (MEN4, MEN1 / MEN2) behind his chain: a side man only while he can be hit, the middle one
 *   while any is left. The first sprite drawn is in front: the chains, then the men, then the Village Boss.
 * - Hit (+3 bit 6 by a thrown object, think 0x323C2; a side man any time, the middle one once both
 *   are gone): the object bounces up at 3.0 (odd: no more hits) and falls no faster than 3.75 above
 *   y 90. A side man falls dead (MEN5, Enemy_Die 0x1B13A) with a spirit. The middle one's first hit
 *   only hurts him (MEN2); the second (not while Flink dies) defeats the Village Boss: 0x8282 = 0, the
 *   camera stops (0x8723, its speed 0), MEN3 falls dead, the shake, sound 0x2A, music index 21.
 * - Defeated (0x29CD6): Flink can't be hurt, STAY shaking (frame |(t & 15) - 8|), for 161 frames a
 *   spirit every 32 (at y - 50 - (rand & 31)), with a flash and sound 0x1A; then the men stop, its
 *   crystal drops (Item_Spawn 0x18), DEAD's frame 1 flies off (0x29906), its body breaks up (0x1B1F8),
 *   sound 0x53, and the level ends.
 * The Village Boss itself never hurts Flink and can't be stomped; nor can the men.
 *
 * Here: the same, a Flink frame a Lomax tick, with Lomax's own map wrap (scroll.c: scroll_wrap,
 * player state 0x20000000), except:
 * - Lomax+'s view (the map's width - nMaxScrollX) is wider than Flink's 320 px: the stones and Jumppads come in 40 px past its right
 *   edge, and the Village Boss runs until view - 60 px into it (Flink's 260 of 320); the 60 from its left
 *   edge stays.
 * - the stones and Jumppads are the map's own never-spawned 'Spik Stone' and 'Mega Jumppad' placements
 *   (Flink places one of each only to load the types), spawned where Flink spawns them; gone again
 *   160 px left of the screen (Flink's widest activation window), unless Lomax carries them.
 * - The stones it lets fall hurt Lomax as in Flink (the rule that thrown objects don't hurt
 *   him is about his own throws): while falling at an even speed, until they first come to rest.
 * - Lomax can't be hurt while it dies (boss_untouchable, forestboss.c); no flashes and no crystal; the shake is Lomax's g_ScreenShake (10, as forestboss.c's); the fanfare is
 *   played once (music_play_once); the men and the Village Boss count as defeated (spirits, the HUD's
 *   count: enemy_defeated); and 100 ticks after the break-up the Finish appears in the middle of the
 *   screen (as forestboss.c's). Defeated, it stays gone for the level
 *   visit (gone.c): its arena then has the Finish where the converter put it, with no wrap.
 * - Not only a heavy thrown object hits a man (Flink's 0x29F8E): a shot does too, and so do Lomax's
 *   stomp on a man (Flink's stomp test on its box, flink_stomp_box) and his spin, roll or helmet
 *   reaching one (player_attack_box), 30 ticks apart (lomax_hits). Only HELMET while it runs
 *   (boss_fighting, forestboss.c).
 * - Sounds: Flink's 0x2A, 0x22, 0x1A and 0x53 (sfx.c).
 */
#include "enemy.h"

#define START_X          (-500)          /* 0xFE0C */
#define FIRST_DROP       450             /* +0x54 (0x29C6E) */
#define DROP_PAST        40              /* 0x168: 360 = 40 past Flink's 320-px screen */
#define DROP_Y           208             /* 0x828A's high word */
#define STOP_FROM_RIGHT  60              /* 0x104 = 260 = 320 - 60 */
#define WALK_FROM_LEFT   60              /* 0x3C */
#define CHAIN_SPEED      2
#define SIDE_HOLD        16              /* 0x2A038 */
#define SIDE_TOP         76              /* 0x2A0AC */
#define MID_TURN         20              /* 0x2A214 */
#define MID_TOP          72              /* 0x2A236 */
#define NEAR_X             25              /* 0x2A062 */
#define CEIL_Y           20              /* 0x2A0EA, 0x2A2BE */
#define CEIL_REST        35              /* 0x2A2F2 */
#define SLOW_ABOVE       90              /* 0x2A346 */
#define SLOW_MAX         0x3BFFF         /* -0xFFFC4001, Flink's up being + */
#define BOUNCE_VY        (-0x30001)      /* 0x30001 up: odd, so it hits nothing more */
#define DYING            161             /* 0xA1 */
#define SPIRIT_DY        50              /* 0x29D06 */
#define LOMAX_GUARD      30              /* ticks after Lomax's stomp or attack on a man (enemy.c's GUARD) */
#define SHAKE            10              /* g_ScreenShake as forestboss.c's (Flink's 35 frames) */
#define FINISH_WAIT      100             /* ticks from the break-up to the Finish (forestboss.c's) */
#define GONE_LEFT        160             /* despawned this far left of the screen */

enum { LEFT, MID, RIGHT };
enum { RUNNING = 2, WALKING = 1, STANDING = 0 };
enum { S_WAIT = 1, S_SLOW = 2, S_HALF = 4, S_DROP = 8, S_DANGER = 16 };   /* a stone's state (g_stone) */

/* the arm's rise for lengths under 32 (0x14B1E: (sin - 0x4000) * 0x80 >> 16); from 32 on, 1.5 a px */
static const signed char CURVE[32] = {
    0, -1, -1, -1, -1, -1, -2, -2, -3, -4, -4, -5, -6, -7, -8, -9,
    -10, -11, -12, -13, -15, -16, -17, -19, -20, -22, -23, -25, -26, -28, -29, -31 };

static struct {
    BYTE *a, *type, *finish, *stone_def, *jumppad_def;
    BYTE *man[3], *held;                 /* the men, the stone in its hands (+0x64) */
    int h[3], v[3], back;                /* the chains' length and speed (+0x48, +0x4A); the middle's +0x4C */
    int alive;                           /* Flink's 0x8282 */
    int state, t, drop, stone_next;      /* +0x1C, +0x52, +0x54, +0x56 */
    int win, view, broken, quake;
    int guard;                           /* ticks no stomp or attack of Lomax's counts (LOMAX_GUARD) */
    int px[3], py[3], order[3];          /* this frame's men, in the frame's order */
} M;
static BYTE g_stone[MAX_ACTORS];         /* per slot, the Village Boss's stones (S_*) */

/* Drawing for Lomax+'s FPS Override (frames between the ticks): it moves what each Actor_Draw call
   queued by the motion of that call's point (the actor's x, y), matching the calls of two ticks by
   actor and by the n-th call of that actor in the frame. Drawn whole, the Village Boss's legs, which move
   on their own (a planted foot stays put while the body goes on), slid with the body between ticks
   and jumped back at the tick; and one drawer for the three chains, in the frame's order of the men,
   paired a chain's links with another's when that order changed. So each part of the Village Boss is
   drawn on its own from its own point, by a stand-in actor (a copy of the Village Boss's struct, not in
   the game's slots: Actor_Draw reads only the struct, Lomax+ keys by its address) kept for that part:
   one per part kind (its sprite bank and side) and rank in the frame (along the axis that kind's
   pieces spread on), and one per chain. The parts come from XRUN / XWAL / XSTA, the body's pieces
   each its own one-piece frame drawn around the middle of its drawn pixels (the converter), each
   stand-in at that middle: a limb turns by a new frame each tick, whose box (and corner) jumps while
   the limb's middle moves as the limb does, so from the corner the limbs would shake between ticks. */
#define MAX_PARTS        40
static BYTE g_part[MAX_PARTS][ACTOR_STRIDE], g_chain[3][ACTOR_STRIDE];
static int g_part_key[MAX_PARTS], g_nparts;
static BYTE *g_fin_def;                  /* the Finish's placement and its x as converted: vboss_break moves */
static short g_fin_x;                    /*   it to the screen's middle, a restart puts it back */

static BYTE *finish_of(const BYTE *def)
{
    BYTE *f = def && D_FLAGS(def) & 0x8000 ? D_NEXT(def) : NULL;
    if (f && f != g_fin_def) { g_fin_def = f; g_fin_x = D_X(f); }
    else if (f) D_X(f) = g_fin_x;
    return f;
}
static BYTE *g_stone_type;

void villageboss_reset(void) { memset(&M, 0, sizeof(M)); memset(g_stone, 0, sizeof(g_stone)); g_stone_type = NULL; g_nparts = 0; }

static int vboss_rand(void) { static unsigned r = 0x2C9E5F31u; r = r * 1103515245u + 12345u; return (int)(r >> 16) & 0x7FFF; }

static int live(BYTE *a) { int k = slot_of(a); return a && k >= 0 && actor_active(k) && A_TYPE(a) && A_LAYER(a) != KILLED_LAYER; }

static int cam_x(void) { return *G_PCURPLANE ? P_CAMX(*G_PCURPLANE) : 0; }

static int is_stone(BYTE *s) { return live(s) && g_stone_type && A_TYPE(s) == g_stone_type; }

static void __cdecl nothing(BYTE *a) { (void)a; }
static void __cdecl plain_draw(BYTE *a)
{
    if (A_TYPE(a)) draw_actor(a);
}
static void *g_held_draw;                /* the held stone's own draw */

/* a child of the Village Boss (no placement, never culled), not an enemy */
static BYTE *child(ActorFn update, ActorFn draw, const char *tag)
{
    BYTE *d = Actor_Alloc(M.a, NULL);
    int k;
    if (!d || (k = slot_of(d)) < 0) return NULL;
    g_enemy[k].type = NULL;
    A_UPDATE(d) = (void *)update;
    A_DRAW(d) = (void *)draw;
    A_CULL(d) = CULL_NEVER;
    A_FLAGS(d) = 0;
    A_LAYER(d) = A_LAYER(M.a);
    A_VX(d) = A_VY(d) = 0;
    if (tag) Actor_SetAnim(d, FOURCC(tag));
    return d;
}

/* ---- The stones and Jumppads ---------------------------------------------------------------------- */

/* the plane's first placement of a type named so (its list: Plane +0, placement +0 next, +8 type) */
static BYTE *placement_named(int plane, const char *prefix)
{
    BYTE *d;
    for (d = *(BYTE **)PLANE(plane); d; d = *(BYTE **)d) {
        BYTE *t = *(BYTE **)(d + 8);
        if (t && !strncmp(T_NAME(t), prefix, strlen(prefix))) return d;
    }
    return NULL;
}

/* Flink's 0x32BD4 (a type spawned by name at x, y): the placement spawned there */
static BYTE *spawn_at(BYTE *def, int x, int y)
{
    short ox, oy;
    BYTE *a;
    if (!def) return NULL;
    ox = D_X(def); oy = D_Y(def);
    D_X(def) = (short)x; D_Y(def) = (short)y;
    D_STATE(def) &= ~DS_SPAWNED;
    a = Object_TrySpawn(def);
    D_X(def) = ox; D_Y(def) = oy;
    if (a) A_CULL(a) = CULL_NEVER;           /* (vboss_update despawns them) */
    return a;
}

/* Flink's +0x5C hooks of the Village Boss's stones (grab.c runs it last in the thrown object's tick):
   0x2A33E (S_HALF: half the gravity), 0x2A346 (S_SLOW: no faster than 3.75 above y 90), 0x2A122 (S_DROP:
   half the gravity until it stops rising or falling); and S_DANGER, Flink's lifetime out (0x32580):
   falling at an even speed it hurts Lomax, until it first comes to rest */
static void __cdecl stone_after(BYTE *s)
{
    int k = slot_of(s), f;
    if (k < 0) return;
    f = g_stone[k];
    if (f & S_DROP) { if (A_VY(s)) A_VY(s) -= 0x2000; else f &= ~S_DROP; }
    if (f & S_HALF) A_VY(s) -= 0x2000;
    if (f & (S_HALF | S_SLOW)) {
        if (A_Y(s) >= SLOW_ABOVE) f &= ~(S_HALF | S_SLOW);
        else if (A_VY(s) > SLOW_MAX) A_VY(s) = SLOW_MAX;
    }
    if (f & S_DANGER) {
        const BYTE *pa = *G_PLAYERANIM;
        int x1, y1, x2, y2;
        if (!A_VY(s)) f &= ~S_DANGER;
        else if (!(A_VY(s) & 1) && pa && !*G_PLAYERHURTTIMER && !*G_PLAYERDEATHTIMER && actor_box(s, &x1, &y1, &x2, &y2) &&
                 x2 >= *G_PLAYERX + HB_X1(pa) && x1 <= *G_PLAYERX + HB_X2(pa) &&
                 y2 >= *G_PLAYERY + HB_Y1(pa) && y1 <= *G_PLAYERY + HB_Y2(pa)) {
            flog("the Village Boss's stone at (%d,%d) hits Lomax", A_X(s), A_Y(s));
            Player_TakeHit(A_X(s), 0, 0);
            Effect_SpawnPoof(A_X(s), y1);
        }
    }
    g_stone[k] = (BYTE)f;
    if (!(f & ~S_WAIT)) grab_set_after(s, NULL);
}

static void stone_hooks(BYTE *s, int f)
{
    int k = slot_of(s);
    if (k < 0) return;
    g_stone[k] = (BYTE)f;
    grab_set_after(s, f ? stone_after : NULL);
}

/* a stone out of the ceiling or the hands: no think, drawn (or not, held) */
static BYTE *stone_take(int x, int y, int held)
{
    BYTE *s = spawn_at(M.stone_def, x, y);
    int k = s ? slot_of(s) : -1;
    if (k < 0) return NULL;
    g_stone_type = A_TYPE(s);
    grab_freeze(s, 1);
    g_stone[k] = held ? 0 : S_WAIT;
    if (held) { g_held_draw = A_DRAW(s); A_DRAW(s) = (void *)nothing; A_LAYER(s) = A_LAYER(M.a); }
    return s;
}

/* the held stone let go onto Lomax (0x2A086) */
static void stone_drop(void)
{
    BYTE *s = M.held;
    M.held = NULL;
    if (!is_stone(s)) return;
    A_DRAW(s) = g_held_draw;
    A_LAYER(s) = 1;                              /* (the stones' list 2, 0x2E6F6) */
    A_VX(s) = A_VY(s) = 0;
    grab_freeze(s, 0);
    stone_hooks(s, S_DROP | S_DANGER);
    flog("the Village Boss lets its stone fall at (%d,%d)", A_X(s), A_Y(s));
}

/* ---- The men --------------------------------------------------------------------------------------- */

static int arm_offset(int d) { return d >= 32 ? -((d - 32) * 3 / 2 + 32) : CURVE[d]; }

/* the chain's start and its links' step (the draw hooks 0x2A3F8, 0x2A36E, 0x2A478) */
static void arm_base(int k, int *x, int *y, int *dx, int *dy)
{
    int held = M.held != NULL;
    *dy = 0;
    if (k == MID) { *x = M.px[k] - 11; *y = M.py[k] + 38; *dx = 4; *dy = 4; }
    else if (k == LEFT) { *x = M.px[k] + 2 + (held ? 6 : 0); *y = M.py[k] + 39; *dx = held ? -10 : -4; }
    else { *x = M.px[k] - 16 - (held ? 6 : 0); *y = M.py[k] + 39; *dx = held ? 10 : 4; }
}

/* the held stone at the left hand's top + (21, 8) (0x2A442) */
static void place_held(void)
{
    int x, y, dx, dy;
    if (!is_stone(M.held)) { M.held = NULL; return; }
    arm_base(LEFT, &x, &y, &dx, &dy);
    A_X(M.held) = x + 21;
    A_Y(M.held) = y + arm_offset(M.h[LEFT]) + 8;
    A_FX(M.held) = A_X(M.held) << 16;
    A_FY(M.held) = A_Y(M.held) << 16;
}

static int man_visible(int k) { return k == MID ? M.alive != 0 : (M.alive & (k == LEFT ? 8 : 4)) != 0; }
static int man_hittable(int k) { return k == MID ? M.alive && !(M.alive & 0xC) : man_visible(k); }

static void __cdecl man_update(BYTE *m) { Actor_AdvanceAnim(m); }

/* the men at this frame's points (the hooks 0x2A504, 0x2A4C4: middle - 12, left - 6, right - 8) */
static void men_place(void)
{
    BYTE *a = M.a;
    const BYTE *r = A_RECORD(a), *e;
    DWORD cc;
    int n, k, xs[3], ys[3], lo = 0, hi = 0;
    if (!r) return;
    cc = *(const DWORD *)(r + 4);
    e = record_of(a, cc == FOURCC("RUN ") ? FOURCC("PRUN") : cc == FOURCC("WALK") ? FOURCC("PWAL") : FOURCC("PSTA"));
    if (!e) return;
    e += A_FRAME(a) * 3 * 8;
    for (n = 0; n < 3; n++) {
        xs[n] = *(const short *)(e + n * 8 + 2);
        ys[n] = *(const short *)(e + n * 8 + 4);
        if (xs[n] < xs[lo]) lo = n;
        if (xs[n] > xs[hi]) hi = n;
    }
    for (n = 0; n < 3; n++) {
        k = n == lo ? LEFT : n == hi ? RIGHT : MID;
        M.order[n] = k;
        M.px[k] = A_X(a) + xs[n] - (k == MID ? 12 : k == LEFT ? 6 : 8);
        M.py[k] = A_Y(a) + ys[n];
        if (live(M.man[k])) {
            A_X(M.man[k]) = M.px[k]; A_Y(M.man[k]) = M.py[k];
            A_FX(M.man[k]) = M.px[k] << 16; A_FY(M.man[k]) = M.py[k] << 16;
        }
    }
    if (M.held) place_held();
}

/* Enemy_Die (0x1B13A) for a man's dead copy: thrown up at 4.0, falling through everything (0x1B1AA) */
static void __cdecl dead_update(BYTE *d)
{
    fall_and_land(d, 0x2AA, 0);
    Actor_AdvanceAnim(d);
    if (++*(int *)(d + 0x80) > 150 || below_map(d)) Actor_Kill(d);
}

static void man_dies(int k, const char *tag)
{
    BYTE *d = child(dead_update, plain_draw, tag);
    if (!d) return;
    A_X(d) = M.px[k]; A_Y(d) = M.py[k];
    A_FX(d) = A_X(d) << 16; A_FY(d) = A_Y(d) << 16;
    A_VY(d) = -0x40000;
    A_LAYER(d) = FLINK_LAYER(1);
    *(int *)(d + 0x80) = 0;
    flink_sound(d, 0x22);
    enemy_defeated(d);
}

/* a side man's chain (0x2A024) */
static void side_step(int k)
{
    M.h[k] += M.v[k];
    if (!M.h[k]) { M.v[k] = 0; return; }
    if (M.h[k] == SIDE_HOLD) {
        if (M.v[k] > 0) return;
        M.v[k] = 0;
        if (abs(A_X(M.a) - *G_PLAYERX) >= NEAR_X) return;
        M.v[LEFT] = M.v[RIGHT] = -CHAIN_SPEED;
        if (M.held) stone_drop();
    } else if (M.h[k] == SIDE_TOP) {
        M.v[k] = -M.v[k];
        if (M.held) return;
        flink_sound(M.a, 0x2A);
        *G_SCREENSHAKE = SHAKE;
        M.quake = 1;
        M.held = stone_take(M.px[k], CEIL_Y, 1);
        if (M.held) { place_held(); flog("the Village Boss's hands take a stone from the ceiling at x %d", M.px[k]); }
    }
}

/* the middle one's chain (0x2A1FC) and its three stones (0x2A2A6) */
static void mid_step(void)
{
    M.h[MID] += M.v[MID];
    if (!M.h[MID]) { M.v[MID] = 0; M.back = 0; return; }
    if (M.h[MID] == MID_TURN) {
        if (M.v[MID] >= 0) return;
        M.back = !M.back;
        if (M.back) M.v[MID] = -M.v[MID];
    } else if (M.h[MID] == MID_TOP) {
        flink_sound(M.a, 0x2A);
        *G_SCREENSHAKE = SHAKE;
        M.quake = 1;
        M.v[MID] = -M.v[MID];
        if (!M.back) {
            int d = vboss_rand() & 127, n;
            for (n = 0; n < 3; n++) {
                stone_take(M.px[MID] + d - 64, CEIL_Y, 0);
                d = (d + 32 + (vboss_rand() & 31)) & 127;
            }
        }
    }
}

/* the stones in the ceiling (0x2A2F2): down to y 35, then they fall at the next shake */
static void ceiling_stones(void)
{
    int k;
    for (k = 0; k < MAX_ACTORS; k++) {
        BYTE *s = actor_at(k);
        if (!(g_stone[k] & S_WAIT)) continue;
        if (!is_stone(s)) { g_stone[k] = 0; continue; }
        if (A_Y(s) < CEIL_REST) { A_Y(s)++; A_FY(s) = A_Y(s) << 16; continue; }
        if (!M.quake) continue;
        A_VY(s) = -1;                            /* (Flink's 1, up: odd) */
        A_VX(s) = (((vboss_rand() & 31) - 16) << 16 >> 3) | 1;
        grab_freeze(s, 0);
        stone_hooks(s, S_HALF | S_SLOW | S_DANGER);
    }
}

/* ---- The Village Boss ----------------------------------------------------------------------------------- */

/* 0x29ED2: it stops, and a chain shoots up */
static void stand(void)
{
    M.state = STANDING;
    M.t = 60 + (vboss_rand() & 127);
    if (!(vboss_rand() & 1)) M.v[MID] = CHAIN_SPEED;
    else if (!M.h[LEFT]) M.v[LEFT] = M.v[RIGHT] = CHAIN_SPEED;
}

static void walk(BYTE *a)
{
    int cx = cam_x();
    if (--M.drop == 0) {                         /* a stone or a Jumppad comes in (0x29DEC) */
        BYTE *s;
        M.stone_next = !M.stone_next;
        if (M.stone_next) {
            M.drop = 100 + (vboss_rand() & 63);
            spawn_at(M.stone_def, cx + M.view + DROP_PAST, DROP_Y);
            flog("a stone comes in at x %d (the screen %d..%d)", cx + M.view + DROP_PAST, cx, cx + M.view);
        } else {
            M.drop = 300 + (vboss_rand() & 127);
            if ((s = spawn_at(M.jumppad_def, cx + M.view + DROP_PAST, DROP_Y))) A_LAYER(s) = FLINK_LAYER(1);
        }
    }
    Actor_AdvanceAnim(a);
    switch (M.state) {
    case RUNNING:
        set_anim(a, "RUN ");
        A_FX(a) += 2 << 16;
        set_pos(a);
        if (!A_FRAME(a) && A_X(a) - cx > M.view - STOP_FROM_RIGHT) stand();
        break;
    case WALKING:
        set_anim(a, "WALK");
        A_FX(a) += 1 << 16;
        set_pos(a);
        if (--M.t < 0 && !A_FRAME(a)) stand();
        break;
    default:
        set_anim(a, "STAY");
        if (--M.t >= 0 || A_FRAME(a)) break;
        if (A_X(a) - cx > WALK_FROM_LEFT) { M.state = WALKING; M.t = 200 + (vboss_rand() & 127); }
        else M.state = RUNNING;
    }
}

/* the end (0x29D78): the men stop, a cluster flies off, the body breaks up; then the Finish */
static void vboss_break(BYTE *a)
{
    BYTE *d;
    int k;
    M.broken = 1;
    for (k = 0; k < 3; k++) if (live(M.man[k])) Actor_Kill(M.man[k]);
    if (is_stone(M.held)) Actor_Kill(M.held);
    M.held = NULL;
    forget_placement(A_DEF(a));
    if ((d = child(NULL, plain_draw, "DEAD"))) {   /* 0x29906: DEAD's frame 1 */
        A_UPDATE(d) = (void *)dead_update;
        hold_frame(d, 1);
        Actor_AdvanceAnim(d);
        A_X(d) = A_X(a); A_Y(d) = A_Y(a);
        A_FX(d) = A_X(d) << 16; A_FY(d) = A_Y(d) << 16;
        A_VX(d) = (((vboss_rand() & 31) - 15) << 16) >> 3;
        A_VY(d) = -(0x10000 + ((vboss_rand() & 31) << 13));
        A_LAYER(d) = FLINK_LAYER(1);
        *(int *)(d + 0x80) = 0;
    }
    if ((d = child(NULL, plain_draw, "BOO2"))) {   /* the body's 18th piece: Lomax shatters 17 at most */
        A_UPDATE(d) = (void *)dead_update;
        A_X(d) = A_X(a); A_Y(d) = A_Y(a);
        A_FX(d) = A_X(d) << 16; A_FY(d) = A_Y(d) << 16;
        A_VX(d) = (((vboss_rand() & 31) - 15) << 16) >> 3;
        A_VY(d) = -(0x20000 + ((vboss_rand() & 31) << 13));
        A_LAYER(d) = FLINK_LAYER(1);
        *(int *)(d + 0x80) = 0;
    }
    if (M.finish) {                              /* in the middle of the screen */
        D_X(M.finish) = (short)(cam_x() + M.view / 2);
        boss_finish_later(a, M.finish, FINISH_WAIT);
    }
    *G_SCREENSHAKE = SHAKE;
    enemy_defeated(a);
    flink_sound(a, 0x53);
    Actor_ShatterIntoPieces(a, 0xC00);
    flog("the Village Boss at (%d,%d) breaks up", A_X(a), A_Y(a));
}

/* despawned 160 px left of the screen: its stones and Jumppads (not one Lomax holds) */
static void sweep(void)
{
    int k, left = cam_x() - GONE_LEFT;
    for (k = 0; k < MAX_ACTORS; k++) {
        BYTE *s = actor_at(k);
        if (!live(s) || (A_DEF(s) != M.stone_def && A_DEF(s) != M.jumppad_def) || A_X(s) >= left) continue;
        if (s == M.held || grab_is_carried(s)) continue;
        g_stone[k] = 0;
        Actor_Kill(s);
    }
}

static void lomax_hits(void);

static void __cdecl vboss_update(BYTE *a)
{
    int k;
    if (a != M.a) return;
    if (M.broken) return;
    M.quake = 0;
    if (!M.alive) {                              /* defeated (0x29CD6) */
        boss_untouchable();                      /* (Flink can't be hurt) */
        set_anim(a, "STAY");
        if (!(M.t & 31)) {
            spirit_release(a, A_X(a), A_Y(a) - SPIRIT_DY - (vboss_rand() & 31));
            flink_sound(a, 0x1A);
        }
        hold_frame(a, abs((M.t & 15) - 8));
        Actor_AdvanceAnim(a);
        if (--M.t <= 0) { vboss_break(a); return; }
    } else walk(a);
    boss_fighting();                             /* (only HELMET: spells.c) */
    men_place();
    side_step(LEFT);                             /* (the men's thinks, in their slots' order) */
    mid_step();
    side_step(RIGHT);
    ceiling_stones();
    lomax_hits();
    for (k = 0; k < MAX_ACTORS; k++)             /* picked up: Lomax's now */
        if (g_stone[k] && is_stone(actor_at(k)) && grab_is_carried(actor_at(k))) stone_hooks(actor_at(k), 0);
    sweep();
}

/* man k is hit (0x29F9A on) */
static void man_hit(int k)
{
    int n;
    if (k != MID) {
        man_dies(k, "MEN5");
        M.alive &= ~(k == LEFT ? 8 : 4);
        flog("the Village Boss's %s man is hit (left: %d)", k == LEFT ? "left" : "right", M.alive);
    } else if (M.alive & 2) {
        M.alive &= ~2;
        Actor_SetAnim(M.man[MID], FOURCC("MEN2"));
        flog("the Village Boss's middle man is hit once");
    } else if (!*G_PLAYERDEATHTIMER) {       /* 0x2A188 */
        M.alive = 0;
        M.t = DYING;
        scroll_stop_x();
        flink_sound(M.a, 0x2A);
        *G_SCREENSHAKE = SHAKE;
        man_dies(MID, "MEN3");
        music_play_once(M.win);
        for (n = 0; n < MAX_ACTORS; n++) if (g_stone[n] & S_DANGER) g_stone[n] &= ~S_DANGER;   /* (Flink can't be hurt) */
        flog("the Village Boss is defeated");
    }
}

/* the men in Flink's slot order (left, middle, right) */
static int man_k(int n) { return n == 0 ? LEFT : n == 1 ? MID : RIGHT; }

/* a thrown object's box hit a man (from grab.c's strike, before the enemies: Flink's men come right
   after the Village Boss in the slots); 1 = it did (a puff) */
int vboss_struck(BYTE *o, int x1, int y1, int x2, int y2, int shot)
{
    int n, k;
    (void)shot;                                  /* (a shot counts too: Flink's 0x29F8E lets only a heavy throw hit) */
    if (!g_active || !live(M.a) || M.broken || A_PLANE(o) != A_PLANE(M.a)) return 0;
    for (n = 0; n < 3; n++) {
        int tx1, ty1, tx2, ty2;
        k = man_k(n);
        if (!man_hittable(k) || !live(M.man[k]) || !actor_box(M.man[k], &tx1, &ty1, &tx2, &ty2)) continue;
        if (x2 < tx1 || tx2 < x1 || y2 < ty1 || ty2 < y1) continue;
        A_VY(o) = BOUNCE_VY;                     /* 0x29F9A */
        if (slot_of(o) >= 0) stone_hooks(o, S_SLOW);
        man_hit(k);
        return 1;
    }
    return 0;
}

/* Lomax's stomp on a man (Flink's stomp test on its box, with his bounce) or his spin, roll or helmet
   reaching one (Flink's think asks only for a thrown object): a hit as a thrown object's, then
   LOMAX_GUARD ticks in which neither counts again */
static void lomax_hits(void)
{
    int n, k;
    if (M.guard) { M.guard--; return; }
    if (!M.alive || *G_PLAYERDEATHTIMER) return;
    for (n = 0; n < 3; n++) {
        int x1, y1, x2, y2, plane;
        k = man_k(n);
        if (!man_hittable(k) || !live(M.man[k]) || !actor_box(M.man[k], &x1, &y1, &x2, &y2)) continue;
        plane = A_PLANE(M.man[k]);
        if (flink_stomp_box(plane, x1, y1, x2, y2)) {
            Player_BounceOffEnemy();
            stomp_bounce();
        } else if (!player_attack_box(plane, x1, y1, x2, y2)) continue;
        flog("the Village Boss's man %d is hit by Lomax", n);
        man_hit(k);
        M.guard = LOMAX_GUARD;
        return;
    }
}

/* the Village Boss's draw (0x29994 and its hooks): each man's chain and the man, in the frame's order,
   then the Village Boss; the first drawn in front, in Lomax's buckets as in Flink's sprite list */
/* a stand-in for the Village Boss: its struct (type, plane, layer, flags), no blink, nothing hidden */
static void stand_in(BYTE *s, const char *tag)
{
    memcpy(s, M.a, ACTOR_STRIDE);
    A_FLAGS(s) &= ~AF_HIDDEN;
    A_HIDE(s) = 0;
    *(int *)(s + 0x78) = 0;                      /* (Actor_Draw's blink counter) */
    if (tag) Actor_SetAnim(s, FOURCC(tag));
}

static BYTE *part_for(int key)
{
    int n;
    for (n = 0; n < g_nparts; n++) if (g_part_key[n] == key) return g_part[n];
    if (g_nparts >= MAX_PARTS) return NULL;
    g_part_key[g_nparts] = key;
    stand_in(g_part[g_nparts], NULL);
    return g_part[g_nparts++];
}

/* the body: the current frame's pieces in their order (the first drawn in front), each by its part's
   stand-in at the piece's middle (XRUN's entry of the same index: the sprite around its middle) */
static void body_draw(BYTE *a)
{
    const BYTE *r = A_RECORD(a), *e, *x;
    DWORD cc;
    int first, n = 0, p, q;
    if (!r) return;
    cc = *(const DWORD *)(r + 4);
    x = record_of(a, cc == FOURCC("RUN ") ? FOURCC("XRUN") : cc == FOURCC("WALK") ? FOURCC("XWAL") : FOURCC("XSTA"));
    if (!x || (cc != FOURCC("RUN ") && cc != FOURCC("WALK") && cc != FOURCC("STAY"))) { draw_actor(a); return; }
    first = *(const short *)(a + 0x6E);          /* the frame's first entry (Actor_Draw's) */
    e = r + 0x20 + first * 8;
    while (n < 64 && !(*(const WORD *)(e + n * 8 + 6) & 0x80)) n++;
    n++;
    for (p = 0; p < n; p++) {
        const BYTE *ep = e + p * 8;
        int dx = *(const short *)(ep + 2), dy = *(const short *)(ep + 4), kind = ep[0] * 2 + (dx < 0), rank = 0;
        int x0 = dx, x1 = dx, y0 = dy, y1 = dy, u, v;
        BYTE *s;
        /* its rank among the frame's pieces of its kind, along the axis they spread on: a thigh is
           three copies of one sprite laid along it, nearly level (x; their y order changes, and turns
           round where Flink flips the sprite, RUN frame 23), a shin two copies one above the other (y);
           ranked another way a stand-in would jump between copies and the in-between frames would
           slide them across each other */
        for (q = 0; q < n; q++) {
            const BYTE *eq = e + q * 8;
            int qx = *(const short *)(eq + 2), qy = *(const short *)(eq + 4);
            if (eq[0] * 2 + (qx < 0) != kind) continue;
            x0 = qx < x0 ? qx : x0; x1 = qx > x1 ? qx : x1;
            y0 = qy < y0 ? qy : y0; y1 = qy > y1 ? qy : y1;
        }
        u = x1 - x0 > y1 - y0;                   /* 1: rank by x */
        for (q = 0; q < n; q++) {
            const BYTE *eq = e + q * 8;
            int qx = *(const short *)(eq + 2), qy = *(const short *)(eq + 4), qa, pa;
            if (q == p || eq[0] * 2 + (qx < 0) != kind) continue;
            qa = u ? qx : qy; pa = u ? dx : dy; v = u ? qy - dy : qx - dx;
            if (qa < pa || (qa == pa && (v < 0 || (v == 0 && q < p)))) rank++;
        }
        if (!(s = part_for(kind * 16 + rank))) continue;
        *(BYTE **)(s + 0x08) = (BYTE *)x - 0x20;
        *(short *)(s + 0x6E) = (short)(first + p);
        /* at the middle of the piece's drawn pixels (XRUN's entry: the sprite at minus that middle) */
        A_X(s) = A_X(a) + dx - *(const short *)(x + (first + p) * 8 + 2);
        A_Y(s) = A_Y(a) + dy - *(const short *)(x + (first + p) * 8 + 4);
        A_FX(s) = A_X(s) << 16; A_FY(s) = A_Y(s) << 16;
        A_LAYER(s) = A_LAYER(a);
        draw_actor(s);
    }
}

/* the Village Boss's draw (0x29994 and its hooks): each man's chain and the man, in the frame's order,
   then the Village Boss; the first drawn in front, in Lomax's buckets as in Flink's sprite list */
static void __cdecl vboss_draw(BYTE *a)
{
    int n;
    if (a != M.a || M.broken || !A_TYPE(a)) return;   /* (broken up: Actor_ShatterIntoPieces cleared its type) */
    for (n = 0; n < 3; n++) {
        int k = M.order[n], x, y, dx, dy, d;
        BYTE *r = g_chain[k];
        arm_base(k, &x, &y, &dx, &dy);
        for (d = M.h[k]; d >= 0; d -= 6) {
            set_anim(r, d != M.h[k] ? "BAL1" : k == MID ? "BAL3" : k == LEFT ? "BL2L" : "BL2R");
            A_X(r) = x; A_Y(r) = y + arm_offset(d);
            draw_actor(r);
            if (d == M.h[k]) {
                if (k == LEFT && is_stone(M.held)) draw_actor(M.held);
                x += dx; y += dy;
            }
        }
        if (man_visible(k) && live(M.man[k])) draw_actor(M.man[k]);
    }
    body_draw(a);
}

/* (from enemy_spawn) param 1: the music at its defeat */
void vboss_spawn(BYTE *a, int i, const BYTE *def)
{
    BYTE *pl = PLANE(A_PLANE(a));
    (void)i;
    villageboss_reset();
    M.a = a;
    M.type = A_TYPE(a);
    M.finish = finish_of(def);
    M.win = def_param(a, def, 1, 0);
    M.view = P_W(pl) * 16 - P_MAXX(pl);         /* (Lomax+'s view: nMaxScrollX is the map's width - the view's,
                                                    0 for a 512-px view on this 512-px map) */
    if (M.view < 320) M.view = 320;
    M.stone_def = placement_named(A_PLANE(a), "Spik Stone");
    M.jumppad_def = placement_named(A_PLANE(a), "Mega Jumppad");
    M.alive = 15;
    M.state = RUNNING;
    M.drop = FIRST_DROP;
    A_UPDATE(a) = (void *)vboss_update;
    A_DRAW(a) = (void *)vboss_draw;
    A_FLAGS(a) &= ~(AF_CONTACT | AF_LEFT);       /* (it neither hurts nor is hit) */
    A_CULL(a) = CULL_NEVER;
    A_LAYER(a) = FLINK_LAYER(3);
    A_VX(a) = A_VY(a) = 0;
    A_X(a) = START_X;
    A_FX(a) = START_X * 65536;
    A_FY(a) = A_Y(a) << 16;
    Actor_SetAnim(a, FOURCC("RUN "));
    M.man[LEFT] = child(nothing, nothing, "MEN4");
    M.man[MID] = child(nothing, nothing, "MEN1");
    M.man[RIGHT] = child(nothing, nothing, "MEN4");
    if (M.man[RIGHT]) *(short *)(M.man[RIGHT] + 0x68) = (short)(*(short *)(M.man[RIGHT] + 0x68) * 5 >> 2);   /* (0x29C2C) */
    for (i = 0; i < 3; i++) stand_in(g_chain[i], "BAL1");
    for (i = 0; i < 3; i++) if (M.man[i]) A_UPDATE(M.man[i]) = (void *)man_update;
    men_place();
    scroll_wrap();
    flog("the Village Boss at y %d, view %d, stone %s, jumppad %s, Finish %s", A_Y(a), M.view,
         M.stone_def ? "yes" : "no", M.jumppad_def ? "yes" : "no", M.finish ? "yes" : "no");
}

/* (from enemy_spawn) defeated already in this visit: only its Finish, where the converter put it */
void vboss_gone(BYTE *a, const BYTE *def)
{
    BYTE *f = finish_of(def);
    if (f) boss_finish_later(a, f, 1);
}
