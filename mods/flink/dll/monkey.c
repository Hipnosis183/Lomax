/*
 * monkey.c - Flink's Monkey (0x2E4F0, think 0x2E562), FOREST (README "Monkeys"): a monkey sitting on
 * a branch with its tail hanging down as a liana. Not an enemy: it never hurts and can't be hit.
 *
 * In Flink the monkey spawns a Liane (0x2B220) 32 px under itself as its tail (param 0: the liana's
 * length, (p + 1) * 32 px; drawn 4 px left of the monkey's x instead of the liana's 8) and keeps it in
 * +0x48. Each frame:
 * - while the tail is held (its think isn't Liane_WaitForGrab) the monkey walks right at 0.5 px a
 *   frame (x += 0x8000) until it is param 1 px from where it started, then stops; the tail follows
 *   its x and Liane_Held puts the player on the tail's x. It shows WALK, otherwise STAY. It never walks
 *   back.
 * - it blinks: STAY's eyelids are a piece with hide selector 1, hidden while bit 8 of the state word
 *   is set (set at init). A timer (first 100 frames) flips the bit: shown for 8 frames, then hidden
 *   for 11, 61, 121 or 181 frames at random (the table at 0x2E5D2).
 *
 * Here the converter makes it a 'Rope Monkey' type: Lomax's rope (object 67, whose spawn is wrapped)
 * placed where the tail is, with a 'ROPE' record of the monkey's MAIN and TAIL sprites, and its STAY
 * and WALK records for the body (pieces with their hide slots). Params: rope segments (Flink's p + 1),
 * the walk distance. Lomax's rope code does the hanging: grabbed by touch (not Flink's grab button,
 * as the lianas), climbing, jumping off. FLINK.DLL wraps its think and draw:
 * - monkey_update runs the rope's own think (Rope_WaitForGrab or Rope_UpdateClimbing, which switch
 *   +0x18 between them; kept in g_monkey.think), then walks while it is Rope_UpdateClimbing and
 *   moves the player with the tail.
 * - monkey_draw draws the tail (Rope_Draw), then the body 32 px above the rope's point (Actor_Draw
 *   with the body's record; see body_draw).
 * Lomax despawns it out of view and respawns it at its placement, as Flink kills both (0x2E58E) and
 * the placement spawns them again.
 */
#include "flink.h"

#define MONKEY_SPEED    0x8000           /* 0.5 px a tick */
#define MONKEY_BODY_DY  (-32)            /* the body is 32 px above the tail's point */
#define BLINK_SLOT      1                /* STAY's eyelids */
#define BLINK_TIME      7                /* (8 ticks shown) */

static const short BLINK_WAIT[8] = { 180, 60, 120, 10, 120, 60, 120, 180 };   /* 0x2E5D2 */

static struct {
    BYTE *type;
    ActorFn think;                       /* the rope's own think */
    int end;                             /* x where it stops (16.16) */
    int timer, blink;                    /* the blink: timer, eyelids shown */
    int phase;                           /* the body's animation, 8.8 frames */
    int walking;
} g_monkey[MAX_ACTORS];

void monkeys_reset(void) { memset(g_monkey, 0, sizeof(g_monkey)); }

static int monkey_rand(void) { static unsigned r = 0x9E3779B9u; r = r * 1103515245u + 12345u; return (int)(r >> 16) & 0x7FFF; }

static int monkey_slot(BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_monkey[i].type == A_TYPE(a) ? i : -1;
}

static void __cdecl monkey_update(BYTE *a)
{
    int i = monkey_slot(a);
    if (i < 0) return;

    A_UPDATE(a) = (void *)g_monkey[i].think;     /* the rope's think, which may switch it */
    g_monkey[i].think(a);
    if (A_UPDATE(a) != (void *)monkey_update) {
        g_monkey[i].think = (ActorFn)A_UPDATE(a);
        A_UPDATE(a) = (void *)monkey_update;
    }

    if (--g_monkey[i].timer < 0) {               /* 0x2E5A4: the blink */
        g_monkey[i].timer = BLINK_TIME;
        if (!(g_monkey[i].blink ^= 1)) g_monkey[i].timer = BLINK_WAIT[(monkey_rand() >> 1) & 7];
    }

    g_monkey[i].walking = g_monkey[i].think == Rope_UpdateClimbing;
    if (g_monkey[i].walking) {
        if (A_FX(a) < g_monkey[i].end) {
            A_FX(a) += MONKEY_SPEED;
            if (A_FX(a) > g_monkey[i].end) A_FX(a) = g_monkey[i].end;
            A_X(a) = A_FX(a) >> 16;
        }
        *G_PLAYERX = A_X(a);                     /* Liane_Held: the player on the tail */
        ((volatile short *)G_PLAYERXFIXED)[1] = (short)A_X(a);
    }
}

/* the body: record tag at frame f, 32 px above the rope's point, after the tail. Drawn through
   Actor_Draw with the actor's record switched for the call (as Rope_Draw moves y and sets the frame
   for its segments), not queued piece by piece: hosts that draw frames between ticks (Lomax+'s FPS
   Override) move what Actor_Draw queued with the actor, and anything else with the camera, so a body
   queued by Sprite_QueueWorld shook against the tail while the monkey walked. The eyelids' slot is
   hidden through +0x70 (Actor_Draw skips slot n while bit n is set). */
static void body_draw(BYTE *a, const char *tag, int speed, int nframes, int blink)
{
    int i = slot_of(a), y = A_Y(a);
    WORD hide = A_HIDE(a);
    if (!record_of(a, FOURCC(tag))) return;
    g_monkey[i].phase += speed;
    Actor_SetAnim(a, FOURCC(tag));
    hold_frame(a, (g_monkey[i].phase >> 8) % nframes);
    Actor_AdvanceAnim(a);
    A_Y(a) = y + MONKEY_BODY_DY;
    A_HIDE(a) = (WORD)(blink ? hide & ~(1u << BLINK_SLOT) : hide | 1u << BLINK_SLOT);
    draw_actor(a);
    A_Y(a) = y;
    A_HIDE(a) = hide;
    Actor_SetAnim(a, FOURCC("ROPE"));           /* (Rope_Draw sets its own frame) */
}

static void __cdecl monkey_draw(BYTE *a)
{
    int i = monkey_slot(a);
    if (i < 0) return;
    Rope_Draw(a);
    if (g_monkey[i].walking) body_draw(a, "WALK", 20, 2, 0);   /* (Flink's WALK speed 20, 2 frames) */
    else body_draw(a, "STAY", 0, 1, g_monkey[i].blink);
}

/* object 67: Lomax's rope. A 'Rope Monkey' is set up as Rope_Spawn does (moved to a high slot, as
   the rope's think must run after the player's update; 'ROPE', +0x80 re-arm timer 0, +0x84 length,
   +0x88 segments), here so that the moved actor is known, and gets its body and its walk. */
void __cdecl rope_spawn(BYTE *a, BYTE *def)
{
    int i, n;
    if (!g_active || !def || !is_type(a, "Rope Monkey") || T_NPARAMS(A_TYPE(a)) < 2) { Rope_Spawn(a, def); return; }
    a = Actor_MoveToHighSlot(a);
    if ((i = slot_of(a)) < 0) return;
    n = D_PARAM(def, 0);
    Actor_SetAnim(a, FOURCC("ROPE"));
    *(int *)(a + 0x80) = 0;
    *(int *)(a + 0x84) = n << 5;
    *(int *)(a + 0x88) = n;
    memset(&g_monkey[i], 0, sizeof(g_monkey[i]));
    g_monkey[i].type = A_TYPE(a);
    g_monkey[i].think = Rope_WaitForGrab;
    g_monkey[i].timer = 100;                     /* 0x2E510 */
    A_FX(a) = A_X(a) << 16;
    g_monkey[i].end = (A_X(a) + D_PARAM(def, 1)) << 16;
    A_UPDATE(a) = (void *)monkey_update;
    A_DRAW(a) = (void *)monkey_draw;
    flog("monkey at (%d,%d): tail %d px, walks %d px when held", A_X(a), A_Y(a), n << 5, D_PARAM(def, 1));
}
