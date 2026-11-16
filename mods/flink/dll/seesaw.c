/*
 * seesaw.c - Flink's seesaws (README "Seesaws").
 *
 * Flink's Seesaw (init 0x2BBE2, think 0x2BC0A; no params; FOREST and VILLAGE) is a plank on a
 * pivot, record SESA: frame 0 left end down (right end up) ... frame 3 right end down. Its tilt is
 * an 8.8 frame position (+0x30, 0 to 0x380) moving by a speed (+0x32) every frame:
 * - Its standing surface (Platform_Stand 0x1A29A) is the hitbox top minus dx * 10 / SLOPE[frame]
 *   (table 0x2BD4C), dx the player's x - the seesaw's x: a slope through the pivot.
 * - Stood on (and not throwing), it tilts toward the player's side at 0x40 a frame.
 * - Every frame, unless in its 60-frame wait (+0x48), it looks for a thrown object (think
 *   Actor_ThrownThink 0x323C2, falling, not hidden) in its hitbox raised 10 px at the top (0x2BD54).
 *   One on the raised end (left of the pivot with frame >= 2, right with frame < 2) starts the throw
 *   (+0x46): the wait, and 0x80 a frame toward that end. Any thrown object counts, heavy or not.
 * - Reaching either end (0 or 0x380) stops it; if it was throwing and the player stood on it, it
 *   throws him (sound 0x34): 9.0 px up (7.0 carrying a heavy object), 3.0 px sideways toward the
 *   end that went down (back over the pivot), the jump boost 0x8306 cleared, as the Jumppad.
 * The thrown object isn't stopped by the seesaw: it falls on through the plank to the ground.
 * Here: a 'Mega Seesaw' (a static Mega, object 64, whose spawn platforms.c wraps) with SESA's four
 * frames; Platform_MoveAndCarryPlayer (0x413510) carries Lomax on the slope that seesaw_surface
 * gives (relx: his x - the seesaw's x), and the throw sets what the Jumppad's sets (jumppad.c).
 * Lomax's bounce pad's launch sound (0x35) plays for Flink's 0x34.
 */
#include "flink.h"

#define TILT_END     0x380
#define TILT_THROW   0x80
#define TILT_STAND   0x40
#define THROW_WAIT   60
#define THROW_VY     0x90000
#define THROW_VY_HEAVY 0x70000
#define THROW_VX     0x30000
static const int SLOPE[4] = { 27, 50, -50, -27 };

/* per actor slot: their type, tilt (8.8 frame), its speed, throwing, the wait, stood on last tick */
static struct { BYTE *type; int tilt, speed, throwing, wait, on; } g_seesaw[MAX_ACTORS];

void seesaws_reset(void) { memset(g_seesaw, 0, sizeof(g_seesaw)); }

static int seesaw_slot(const BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_seesaw[i].type && g_seesaw[i].type == A_TYPE(a) ? i : -1;
}

static int __cdecl seesaw_surface(BYTE *a, int relx)
{
    int i = seesaw_slot(a);
    if (i < 0) return 0;
    return -((short)relx * 10 / SLOPE[g_seesaw[i].tilt >> 8]);   /* (muls.w, divs.w) */
}

static void seesaw_throw(BYTE *a, int right)
{
    int vy = grab_carrying_heavy() ? THROW_VY_HEAVY : THROW_VY;
    flink_sound(a, 0x34);
    *G_JUMPHOLDFORCE = 0;
    *G_PLAYERVY = -vy;
    *G_PLAYERVX = right ? THROW_VX : -THROW_VX;
    *G_PLAYERSTATE |= 3;
    *G_JUMPTAKEOFFTIMER = 0;
    *G_BAIRSPIN = 0;
    flog("seesaw at (%d,%d) throws Lomax %s at %d.%02d up", A_X(a), A_Y(a), right ? "right" : "left",
         vy >> 16, (vy & 0xFFFF) * 100 >> 16);
}

static void __cdecl seesaw_update(BYTE *a)
{
    int i = seesaw_slot(a), t, x1, y1, x2, y2;
    if (i < 0) return;
    if (g_seesaw[i].wait) g_seesaw[i].wait--;
    else if (actor_box(a, &x1, &y1, &x2, &y2)) {
        BYTE *o = grab_falling_on(a, x1, y1 - 10, x2, y2);
        if (o) {
            int side = A_X(a) - A_X(o), f = (g_seesaw[i].tilt >> 8) - 2;
            if ((f ^ side) >= 0) {                   /* on the raised end */
                g_seesaw[i].wait = THROW_WAIT;
                g_seesaw[i].throwing = 1;
                g_seesaw[i].speed = side >= 0 ? -TILT_THROW : TILT_THROW;
                flog("%s lands on the seesaw at (%d,%d), %s end", T_NAME(A_TYPE(o)), A_X(a), A_Y(a), side >= 0 ? "left" : "right");
            }
        }
    }
    t = g_seesaw[i].tilt + g_seesaw[i].speed;
    if (t < 0 || t >= 0x400) {
        t = t < 0 ? 0 : TILT_END;
        g_seesaw[i].speed = 0;
        if (g_seesaw[i].throwing && g_seesaw[i].on) seesaw_throw(a, t != 0);
        g_seesaw[i].throwing = 0;
    }
    g_seesaw[i].tilt = t;
    hold_frame(a, t >> 8);
    Actor_AdvanceAnim(a);
    g_seesaw[i].on = Platform_MoveAndCarryPlayer(a, seesaw_surface);
    if (g_seesaw[i].on && !g_seesaw[i].throwing)
        g_seesaw[i].speed = *G_PLAYERX >= A_X(a) ? TILT_STAND : -TILT_STAND;
}

/* Lomax stands on a seesaw (Flink's g_PlatformActor 0x82FE has the seesaw's think 0x2BC0A): the
   heavy throw's boost is smaller there (grab.c, Flink 0x330A4) */
int seesaw_under_lomax(void)
{
    int k;
    for (k = 0; k < MAX_ACTORS; k++) {
        BYTE *a = actor_at(k);
        if (g_seesaw[k].type && g_seesaw[k].on && actor_active(k) && A_TYPE(a) == g_seesaw[k].type &&
            A_UPDATE(a) == (void *)seesaw_update) return 1;
    }
    return 0;
}

void seesaw_spawn(BYTE *a, int i)
{
    memset(&g_seesaw[i], 0, sizeof(g_seesaw[i]));
    g_seesaw[i].type = A_TYPE(a);
    A_VX(a) = A_VY(a) = 0;
    hold_frame(a, 0);
    A_UPDATE(a) = (void *)seesaw_update;
}
