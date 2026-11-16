/*
 * spike.c - Flink's Spikes, 'Spike' (init 0x2856E, think 0x28608; VILLAGE: 13-1 and others;
 * README "Spikes"): a spike that shoots out of the ground on a timer. Flink's:
 * - Init: params 'hidden' (+0x46), 'out' (+0x48, + 30) and 'pre-run'; the VERT record moves its y
 *   (checkbox 1: HORI, its x; checkbox 2: the other way; neither is used in the converted maps), 4 px
 *   a frame (+0x58). Out is the placement (+0x4E), peeking 24 px under it (+0x52), hidden 32 px under
 *   it (+0x50), where it starts. The countdown (+0x4A) starts at 'hidden', the phase (+0x4C) at 0,
 *   the peek's wait (+0x54) at 30; then the think's step runs 'pre-run' times (0x285FE).
 * - Each frame (0x28608): fully out, touching Flink hurts him (0x1A462); then the step (0x2861C):
 *   the countdown runs out -> the phase flips, the countdown 'hidden' frames for hiding, 'out' + 30
 *   for coming out. Hiding, it goes down 4 px a frame to hidden. Coming out, it goes up to the peek
 *   (2 frames), waits there 30 frames, plays sound 0x36 (when on screen, +6 bit 4) and shoots up to
 *   out (6 frames), where it stays the rest of the phase ('out' - 8 frames).
 * - The ground hides what is below the placement (the sprite is drawn behind the ground's tiles).
 * Here: the converter's 'Spik Spike' (Lomax's decoration, object 51) with the VERT record; params
 * hidden, out, pre-run. The actor is moved to the spike's y, clipped at its placement (+0x7C, as the
 * chests), and touches nothing itself: the hurt is Flink's test, Lomax's hitbox against the spike's,
 * then Player_TakeHit. Most are spawned by Generators, whose groups despawn them.
 */
#include "flink.h"

#define SPIKE_STEP        4              /* px a frame (+0x58) */
#define SPIKE_PEEK        24             /* px under the placement (+0x52) */
#define SPIKE_HIDDEN      32             /* (+0x50) */
#define SPIKE_WAIT        30             /* frames peeking (+0x54) */
#define SPIKE_OUT_EXTRA   30             /* added to 'out' (0x28586) */
#define VIEW_W            512            /* Lomax+'s widest view */
#define VIEW_H            240

typedef struct {
    BYTE *type;
    int base;                            /* the placement's y: out */
    int hidden, out;                     /* the phases' frames (+0x46, +0x48) */
    int t, phase, off, wait;             /* +0x4A, +0x4C, px under the placement, +0x54 */
} Spike;
static Spike g_spike[MAX_ACTORS];

void spikes_reset(void) { memset(g_spike, 0, sizeof(g_spike)); }

static Spike *spike_of(const BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_spike[i].type && g_spike[i].type == A_TYPE(a) ? &g_spike[i] : NULL;
}

/* where it comes out (its placement: the actor itself is still SPIKE_PEEK px under it then, below
   the screen's bottom where the floor is at the screen's edge, as in 25-1) within the view */
static int on_screen(const BYTE *a, int base)
{
    const BYTE *pl = PLANE(A_PLANE(a));
    int x = A_X(a) - P_CAMX(pl), y = base - P_CAMY(pl);
    return x >= 0 && x < VIEW_W && y >= 0 && y < VIEW_H;
}

/* 0x2861C (a = NULL: no sound, the pre-run) */
static void spike_step(Spike *s, BYTE *a)
{
    if (--s->t <= 0) {
        s->phase = !s->phase;
        s->t = s->phase ? s->out : s->hidden;
    }
    if (!s->phase) {
        if (s->off != SPIKE_HIDDEN) s->off += SPIKE_STEP;
    } else if (s->off == SPIKE_PEEK) {
        if (--s->wait == 0) {
            if (a && on_screen(a, s->base)) flink_sound(a, 0x36);
            s->wait = SPIKE_WAIT;
            s->off -= SPIKE_STEP;
        }
    } else if (s->off != 0)
        s->off -= SPIKE_STEP;
}

static void __cdecl spike_update(BYTE *a)
{
    Spike *s = spike_of(a);
    const BYTE *pa = *G_PLAYERANIM;
    int x1, y1, x2, y2, px = *G_PLAYERX, py = *G_PLAYERY;
    if (!s) return;
    if (!s->off && pa && A_PLANE(a) == *G_PLAYERPLANE && !*G_PLAYERHURTTIMER && actor_box(a, &x1, &y1, &x2, &y2) &&
        x2 >= px + HB_X1(pa) && x1 <= px + HB_X2(pa) && y2 >= py + HB_Y1(pa) && y1 <= py + HB_Y2(pa)) {
        flog("spike at (%d,%d) hurts Lomax at (%d,%d)", A_X(a), s->base, px, py);
        Player_TakeHit(A_X(a), 0, 0);
    }
    spike_step(s, a);
    place_y(a, s->base + s->off);
}

/* (from spik_spawn) params: hidden, out, pre-run */
void spike_spawn(BYTE *a, BYTE *def)
{
    int i = slot_of(a), k;
    Spike *s;
    if (i < 0 || !def || T_NPARAMS(A_TYPE(a)) < 3) return;
    s = &g_spike[i];
    memset(s, 0, sizeof(*s));
    s->base = A_Y(a);
    s->hidden = D_PARAM(def, 0);
    s->out = D_PARAM(def, 1) + SPIKE_OUT_EXTRA;
    s->t = s->hidden;
    s->off = SPIKE_HIDDEN;
    s->wait = SPIKE_WAIT;
    s->type = A_TYPE(a);
    for (k = D_PARAM(def, 2); k > 0; k--) spike_step(s, NULL);
    A_FLAGS(a) &= ~(0x200000u | AF_CONTACT);     /* the actor itself touches nothing */
    A_CLIPY(a) = (short)s->base;                 /* the ground hides what is below its placement */
    A_UPDATE(a) = (void *)spike_update;
    place_y(a, s->base + s->off);
    flog("spike at (%d,%d): hidden %d, out %d, pre-run %d", A_X(a), s->base, s->hidden, s->out, D_PARAM(def, 2));
}
