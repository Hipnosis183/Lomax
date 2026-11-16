/*
 * apple.c - Flink's Falling apple (init 0x28E02, think 0x28E30; TREE maps 3, 7, 8 = 25-1, 28-1,
 * 25-2; README "Falling apples"): an apple hanging in a tree that shakes when Flink walks under it,
 * then falls and bursts on the ground or on him. Flink's:
 * - Init: draw list 3 ((a5) = 0x7D70: behind Flink), +6 bit 3 (thrown objects and Flink's slide
 *   reach it), 'distance' (+0x46), the state +0x48 = 0, HANG.
 * - Each frame, in this order:
 *   hit (+3 bit 6: a thrown object, Actor_FindTouchingActor 0x1A5A0, a spell or the slide below) ->
 *     the burst;
 *   touching Flink (0x1A462: its record's hitbox against his, inclusive) -> the burst, after: Flink
 *     sliding (SL45) knocks it away (vx 2.0, +3 bit 6), otherwise Player_TakeHit (0x1A7DC); while
 *     g_PlayerHurtTimer (0x82E0) is set it touches nothing (no burst);
 *   the animation advances (0x337EA; HANG and WARN loop);
 *   0 (hanging, 0x28EB0): Flink at least 40 px below it (apple y <= his y - 0x28) and within
 *     'distance' px across (inclusive) -> +0x48 = 30, WARN (it shakes);
 *   positive (warning): - 1 a frame; at 0 -> -1, HANG again;
 *   negative (falling, 0x28E70): not drawn last frame (+6 bit 4 clear) -> Actor_Free (0x32D9E: not
 *     forgotten, so it is back after a lost life); otherwise vy + 0.25 a frame, with no limit, y + vy,
 *     and when Map_GetGroundHeight (0x1AEEA, 101 cells down from y - 16) finds ground at or above y ->
 *     the burst.
 * - The burst (0x28EF8): DEAD, sound 0x48 when on screen, Actor_ForgetForever (0x1B1E8, with +3 bit 5:
 *   not its own sound 0x2A): gone for good, DEAD's frame (five pieces of apple) broken up into its
 *   sprites, which fly up and fall.
 * Here: the converter's 'Spik Apple' (Lomax's decoration, object 51), records HANG, WARN and BOOM
 * (DEAD's frame); param distance. The actor touches nothing itself: the touch is Flink's test, its
 * hitbox against Lomax's on his plane. Lomax's TUMB (his roll) takes the slide's place; his spin
 * is no slide, so a falling apple hurts him while he spins. Thrown objects reach it through grab.c's
 * strike (apple_struck). The burst is forget_placement and Actor_ShatterIntoPieces with BOOM (it sets
 * its own update and draw); Lomax's enemy hit (SND_HIT) for 0x48, as the Buzzard's. "Drawn last frame"
 * is enemy_on_screen (Lomax's view, 32 px margin), and Actor_Free is Actor_Kill (its placement stays
 * spawned until a lost life resets the placements). The ground is searched from the y before the move,
 * so that a fast fall can't step past a floor. The apple has no stomp: landing on it hurts.
 */
#include "enemy.h"

#define APPLE_SEE_DY     0x28
#define APPLE_WARN       30
#define APPLE_FALLING    (-1)
#define APPLE_GRAVITY    0x4000          /* 0.25 a frame */
#define APPLE_CELLS      101             /* 0x1AEEA with d2 = 100: dbra, 101 cells */
#define APPLE_FORCE      0xC00           /* Actor_ShatterIntoPieces, as the Buzzard's */

typedef struct {
    BYTE *type;
    int dist, t;                         /* +0x46, +0x48 */
    int vy;                              /* 16.16 (+0x20) */
} Apple;
static Apple g_apple[MAX_ACTORS];

void apples_reset(void) { memset(g_apple, 0, sizeof(g_apple)); }

static Apple *apple_of(const BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_apple[i].type && g_apple[i].type == A_TYPE(a) ? &g_apple[i] : NULL;
}

/* 0x28EF8 */
static void apple_burst(BYTE *a, Apple *p, const char *why)
{
    flog("falling apple at (%d,%d) bursts: %s", A_X(a), A_Y(a), why);
    p->type = NULL;
    forget_placement(A_DEF(a));
    if (enemy_on_screen(a)) Sound_PlayEffect(a, SND_HIT);
    Actor_ShatterIntoPieces(a, APPLE_FORCE);
}

/* 0x1A462: 1 if it touches Lomax (and so bursts) */
static int apple_touch(BYTE *a)
{
    const BYTE *pa = *G_PLAYERANIM;
    int x1, y1, x2, y2, px = *G_PLAYERX, py = *G_PLAYERY;
    DWORD anim;
    if (!pa || A_PLANE(a) != *G_PLAYERPLANE || !actor_box(a, &x1, &y1, &x2, &y2)) return 0;
    if (x2 < px + HB_X1(pa) || x1 > px + HB_X2(pa) || y2 < py + HB_Y1(pa) || y1 > py + HB_Y2(pa)) return 0;
    anim = *(const DWORD *)(pa + 4);
    if (anim == FOURCC("TUMB")) return 1;            /* his roll, as Flink's slide (SL45) */
    if (*G_PLAYERHURTTIMER) return 0;
    Player_TakeHit(A_X(a), 0, 0);
    return 1;
}

static void __cdecl apple_update(BYTE *a)
{
    Apple *p = apple_of(a);
    int g, y;
    if (!p) return;
    if (apple_touch(a)) { apple_burst(a, p, "it touched Lomax"); return; }
    Actor_AdvanceAnim(a);
    if (!p->t) {
        if (*G_PLAYERY - APPLE_SEE_DY < A_Y(a) || abs(*G_PLAYERX - A_X(a)) > p->dist) return;
        flog("falling apple at (%d,%d) shakes: Lomax at (%d,%d)", A_X(a), A_Y(a), *G_PLAYERX, *G_PLAYERY);
        p->t = APPLE_WARN;
        Actor_SetAnim(a, FOURCC("WARN"));
    } else if (p->t > 0) {
        if (--p->t) return;
        p->t = APPLE_FALLING;
        Actor_SetAnim(a, FOURCC("HANG"));
    } else {
        if (!enemy_on_screen(a)) { p->type = NULL; Actor_Kill(a); return; }
        y = A_Y(a);
        p->vy += APPLE_GRAVITY;
        A_FY(a) += p->vy;
        set_pos(a);
        g = (int)Plane_FindGroundY(PLANE(A_PLANE(a)), A_X(a), y, APPLE_CELLS);
        if (g != NO_GROUND && g <= A_Y(a)) apple_burst(a, p, "it hit the ground");
    }
}

/* grab.c's strike: a thrown object's box x1..y2 touches it (Actor_FindTouchingActor 0x1A5A0) */
int apple_struck(BYTE *t, int x1, int y1, int x2, int y2)
{
    Apple *p = apple_of(t);
    int tx1, ty1, tx2, ty2;
    if (!p || !actor_box(t, &tx1, &ty1, &tx2, &ty2)) return 0;
    if (x2 < tx1 || x1 > tx2 || y2 < ty1 || y1 > ty2) return 0;
    apple_burst(t, p, "hit by a thrown object");
    return 1;
}

/* (from spik_spawn) param: distance */
void apple_spawn(BYTE *a, BYTE *def)
{
    int i = slot_of(a);
    Apple *p;
    if (i < 0 || !def || T_NPARAMS(A_TYPE(a)) < 1) return;
    p = &g_apple[i];
    memset(p, 0, sizeof(*p));
    if (placement_gone(def)) { spawn_gone(a); return; }   /* burst: forgotten for good (0x1B1E8) */
    p->dist = D_PARAM(def, 0);
    p->type = A_TYPE(a);
    A_FLAGS(a) &= ~(0x200000u | AF_CONTACT);         /* the actor itself touches nothing */
    A_LAYER(a) = (short)FLINK_LAYER(3);              /* (a5) = 0x7D70: list 3 */
    A_UPDATE(a) = (void *)apple_update;
    Actor_SetAnim(a, FOURCC("HANG"));
    place_y(a, A_Y(a));
    flog("falling apple at (%d,%d): distance %d", A_X(a), A_Y(a), p->dist);
}
