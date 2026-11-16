/*
 * water.c - Flink's tide, and swimming (README "Water").
 *
 * The Water object's update (Water_Update 0x431A20, stored by Water_Init at 0x431881) is pointed at
 * water_update, which acts only in Flink's levels:
 *  - Tide. Flink's water rises and falls: surface = y + amplitude * sin(phase), the phase stepping
 *    5 or 3 (of 1024 a turn) a frame. The converter writes amplitude and step as the Water placement's
 *    two params (Lomax's own Water_Init ignores params); the water actor's y moves accordingly every
 *    tick, and so do the Throw platforms (DriftingBoat actors, behaviour 65) and 'Mega Float' platforms
 *    (float_track, from platforms.c), which then carry Lomax with them. A sinking one (a lilypad)
 *    carries him down to the bottom, the water leaving him alone meanwhile (riding_lilypad).
 *  - Swim. Lomax drowns once his feet are 32 px under the surface unless g_WaterEscapeTimer
 *    (0x59E95C) still runs: then he stays at that depth and can jump out. Level_Start sets it to 120
 *    ticks, it counts down while he is in the water and re-entering the water zeroes it, so in the
 *    game he survives only his first dip. While he is in the water Water_Update also sets the input
 *    lock (g_bInputLocked 0x52C52C) every tick, and its jump-out test needs it clear, so he gets out
 *    only by holding jump as he lands. With Swim=1 the timer is topped up and, while he is in the water
 *    (player state 0x200), the lock cleared before every update: he floats at that depth, rides the
 *    tide, and jumps out whenever jump is pressed, as often as he likes. With Swim=2 (the default)
 *    he doesn't swim: Water_Update runs with player state 0x20000 set, which skips its whole player
 *    part (catch, float, drown, input lock, the 3.0 fall cap), so he sinks to the bottom and walks
 *    there; move.c slows him down under water. Only the player part's splashes are kept, redone by
 *    player_splash after the call (the same tests, with Water_Update's 30-tick cooldown in +0xF8).
 *  - Tint. Water_Draw (0x431D80) draws 'WAV3', the water body, translucent with the alpha in the
 *    actor's +0xE8 (bits 16-23 of its sprite flags; Water_Init sets 0x8A). The converter makes
 *    'WAV3' a strip of one colour that, blended at the alpha in the placement's 3rd param, comes
 *    nearest to Flink's palette tint below the surface; that alpha is stored in +0xE8 every tick.
 *    Buckets (the top bytes of +0x114, front waves, and +0xF4, the strip; Water_Draw only rewrites
 *    +0xF4's alpha byte): Water_Init puts the waves 10 below the water's plane (behind its tiles)
 *    and the strip at the plane + 14. As in Flink, the waves are drawn over the ground: they go to
 *    the front plane's + 14 and the strip to + 15: the water's plane, or Flink's priority plane
 *    (plane 0, 0xA0 over the play plane's 0x90) when that is in front, since Flink's waves are
 *    high-priority sprites and its tint a palette change, both over its high-priority tiles too
 *    (behind plane 0, 4-2's priority rocks would cover the waves).
 *    Without real blending (g_bD3DTranslucency 0x469A00 clear: a software driver draws translucent
 *    sprites as a checkerboard, and Lomax+'s Solid setting as opaque) the strip's blank 2nd frame
 *    (bank + 2 * 0x38 + 0x10, Actor_GetCurSprite 0x4163A0) is drawn instead of +0xF0's 1st: no tint.
 *    Height: Water_Draw puts the strip at y + 6, under the waves' grey rows, which Flink tints. The
 *    Water's draw (stored by Water_Init at 0x431888) is water_draw, which raises the strip by the
 *    placement's 4th param: it draws with y that much higher, and the waves' frame offsets, cached
 *    by Water_Init in the actor's link block (+0x80; shorts at +0x88, 'WAV1', and +0xC8, 'WAV2',
 *    12 each), that much lower, so the waves stay put. The lift applied is kept in the actor's +0xEC
 *    (zeroed by Water_Init, used by neither Water_Update nor Water_Draw), so a water actor that
 *    Water_Init sets up again (a lost life) is lifted again.
 *  - Lava. The placement's 5th param is Flink's checkbox 1 'lava' (MOUNTAIN's three lava maps). Lava
 *    has no swimming, floating or drowning: Water_Update runs with player state 0x20000 set whatever
 *    SWIM_MODE says, its splashes redone by player_splash, and lava.c's lava_player does Flink's
 *    touch (Water think 0x2E976): hurt below the surface on the ground, dead 25 px under it.
 */
#include "flink.h"

#define SET_WATER_UPDATE   0x00431881u   /* Water_Init: mov dword [esi+0x18], Water_Update */
static const BYTE setWaterOrig[7] = { 0xC7, 0x46, 0x18, 0x20, 0x1A, 0x43, 0x00 };
#define SET_WATER_DRAW     0x00431888u   /* Water_Init: mov dword [esi+0x1C], Water_Draw */
static const BYTE setWaterDrawOrig[7] = { 0xC7, 0x46, 0x1C, 0x80, 0x1D, 0x43, 0x00 };

#define G_PLAYERTICKSTARTY ((volatile short *)0x0052C6CCu)  /* Lomax's y at the tick's start (Actors_UpdateAll) */
#define G_PLAYERNEXTFEETY  ((volatile short *)0x0059C710u)  /* and his feet's y after this tick's move */

static int g_live;                       /* ticks the water's update last ran (water_present) */
static int g_lava;                       /* its 'lava' checkbox (water_lava) */
static struct { BYTE *actor; int base, y, phase; } g_tide;
static struct { BYTE *actor; DWORD frame; } g_tint;
/* per actor slot: platforms riding the tide (their type, the tide offset applied, a Mega?) */
static struct { BYTE *type; int off, mega; } g_float[MAX_ACTORS];

void water_reset(void)
{
    g_tide.actor = NULL;
    g_tint.actor = NULL;
    g_live = 0;
    g_lava = 0;
    memset(g_float, 0, sizeof(g_float));
}

/* Moves a platform riding the tide by d px: a boat (DriftingBoat, moved by its vy) and a sinking Mega
   (elevator mode, +0xA8) by its y; a Mega on its sine path by its base y (+0x9C), from which
   MovingPlatform_Update moves it, carrying Lomax. new: just spawned, put there at once. */
static void float_move(BYTE *a, int d, int mega, int new)
{
    if (mega) *(int *)(a + 0x9C) += d;
    if (!mega || new || *(int *)(a + 0xA8)) {
        A_Y(a) += d;
        A_FY(a) += d << 16;
    }
}

static int tide_offset(void) { return g_tide.actor ? g_tide.y - g_tide.base : 0; }

/* a platform (mega: a Mega, else a boat) that rides the tide from now on */
void float_track(BYTE *a, int mega)
{
    int i = slot_of(a);
    if (i < 0) return;
    g_float[i].type = A_TYPE(a);
    g_float[i].mega = mega;
    g_float[i].off = tide_offset();
    float_move(a, g_float[i].off, mega, 1);
}

/* Lomax stands on a sinking platform riding the tide (a lilypad: 'Mega Float' with MovingPlatform's
   sinking mode, +0xA8). Water_Update's player part (0x431A20) catches him once his feet are 32 px
   under the surface: it holds him there (Swim) or drowns him, and he drops off the platform, which,
   left, rises back. Flink's water leaves a player standing on a Sinkplatform (think 0x2F1D4) alone:
   it keeps sinking 0.5 px a frame while he stands on it, down to the bottom, where he dies. So while
   he rides one, Water_Update runs with player state 0x20000 set, which its player part skips (the
   state of a player the water must not touch); the actors' splashes and the water's own counters
   still run. */
#define PS_WATER_IGNORES   0x20000u
static int riding_lilypad(void)
{
    BYTE *r = *G_PRIDINGPLATFORM;
    int i;
    if (!g_active || !r || (i = slot_of(r)) < 0 || !g_float[i].type || g_float[i].type != A_TYPE(r) || !g_float[i].mega) return 0;
    return *(int *)(r + 0xA8) != 0;
}

/* the map's water is running (its update ran within the last 3 player ticks): g_WaterLevelY is its
   surface. water_tick counts the player's ticks (move_after). */
int water_present(void) { return g_live > 0; }
int water_lava(void) { return g_live > 0 && g_lava; }
void water_tick(void) { if (g_live > 0) g_live--; }

/* Water_Update's splashes for Lomax (0x431A20's player part, run without it with Swim=2): in the air,
   his feet going under the surface, his head (feet - 16) going under, or coming back out */
static void player_splash(BYTE *water)
{
    int s = A_Y(water), feet = *G_PLAYERNEXTFEETY, start = *G_PLAYERTICKSTARTY;
    int *cool = (int *)(water + 0xF8);
    if (!(*G_PLAYERSTATE & PS_AIR) || *cool) return;
    if ((s < feet && start <= s) || (s < feet - 16 && start - 16 <= s) || (feet - 18 <= s && s <= start - 16)) {
        Water_SpawnSplash(G_PLAYERACTOR);
        *cool = 30;
    }
}

/* Water actor: placement params amplitude and step (see the top of the file) */
static void __cdecl water_update(BYTE *water)
{
    const BYTE *def = A_DEF(water);
    int amp, step, y, off, i, lava;
    if (g_active) {
        g_live = 3;
        if (SWIM_MODE == 1 && !(def && T_NPARAMS(A_TYPE(water)) >= 5 && (D_PARAM(def, 4) & 1))) {
            *G_WATERESCAPETIMER = 120;
            if (*G_PLAYERSTATE & 0x200) *G_BINPUTLOCKED = 0;
        }
        if (def && T_NPARAMS(A_TYPE(water)) >= 3) {
            /* Flink's waves are high-priority sprites (Water's init sets +3 bit 7, 0x2E8D0) and its tint
               a palette change below the surface: both in front of its high-priority tiles too, so
               over the priority plane (plane 0) when it is in front of the water's (4-2's rocks) */
            int bucket = P_DEPTH(PLANE(A_PLANE(water)));
            if (P_DEPTH(PLANE(0)) > bucket) bucket = P_DEPTH(PLANE(0));
            if (g_tint.actor != water) {             /* the strip's own frame, cached by Water_Init */
                g_tint.actor = water;
                g_tint.frame = *(DWORD *)(water + 0xF0);
            }
            /* no real blending: the strip's blank 2nd frame (a bank's frames are 0x38 bytes apart) */
            *(DWORD *)(water + 0xF0) = g_tint.frame + (*G_BD3DTRANSLUCENCY ? 0 : 0x38);
            *(int *)(water + 0xE8) = D_PARAM(def, 2) & 0xFF;        /* the tint strip's alpha */
            /* in front of the water's plane and its actors (layers +2..+14): the waves, then the strip */
            *(water + 0x117) = (BYTE)(bucket + 14 > 0xFF ? 0xFF : bucket + 14);
            *(water + 0xF7) = (BYTE)(bucket + 15 > 0xFF ? 0xFF : bucket + 15);
        }
        if (def && T_NPARAMS(A_TYPE(water)) >= 2 && (amp = D_PARAM(def, 0)) != 0) {
            step = D_PARAM(def, 1);
            if (g_tide.actor != water) {             /* a new water actor: its y is the mean level */
                g_tide.actor = water;
                g_tide.base = g_tide.y = A_Y(water);
                g_tide.phase = 0;
            }
            g_tide.phase = (g_tide.phase + step) & 0x3FF;
            y = g_tide.base + amp * G_SINTABLE[g_tide.phase] / 16384;
            g_tide.y = y;
            place_y(water, y);
            off = tide_offset();
            for (i = 0; i < MAX_ACTORS; i++) {       /* the Throw platforms and floating platforms ride it */
                BYTE *a = actor_at(i);
                if (g_float[i].type && A_TYPE(a) == g_float[i].type && off != g_float[i].off) {
                    float_move(a, off - g_float[i].off, g_float[i].mega, 0);
                    g_float[i].off = off;
                }
            }
        }
    }
    lava = g_active && def && T_NPARAMS(A_TYPE(water)) >= 5 && (D_PARAM(def, 4) & 1);
    g_lava = lava;
    if (g_active && (SWIM_MODE == 2 || lava || riding_lilypad())) {   /* the water leaves him alone (and to the lilypad) */
        DWORD was = *G_PLAYERSTATE & PS_WATER_IGNORES;  /* (the fly cheat's bit: kept) */
        *G_PLAYERSTATE |= PS_WATER_IGNORES;
        Water_Update(water);
        *G_PLAYERSTATE = (*G_PLAYERSTATE & ~PS_WATER_IGNORES) | was;
        if ((SWIM_MODE == 2 || lava) && !was) player_splash(water);
        if (lava) lava_player(water);
    } else
        Water_Update(water);
}

/* Water actor's draw: the tint strip raised by the placement's 4th param (see the top of the file) */
static void __cdecl water_draw(BYTE *water)
{
    const BYTE *def = A_DEF(water);
    int lift = g_active && def && T_NPARAMS(A_TYPE(water)) >= 4 ? D_PARAM(def, 3) : 0, i;
    if (lift <= 0 || lift > 64) {
        Water_Draw(water);
        return;
    }
    if (*(int *)(water + 0xEC) != lift) {            /* the waves' cached offsets, once per Water_Init */
        short *off = (short *)(*(BYTE **)(water + 0x80) + 0x88);
        for (i = 0; i < 24; i++) off[i] = (short)(off[i] + lift - *(int *)(water + 0xEC));
        *(int *)(water + 0xEC) = lift;               /* 'WAV1' and 'WAV2'; +0xEC: 0 from Water_Init, unused */
    }
    A_Y(water) -= lift;
    Water_Draw(water);
    A_Y(water) += lift;
}

int water_retail(void)
{
    return is_bytes(SET_WATER_UPDATE, setWaterOrig, sizeof(setWaterOrig)) &&
           is_bytes(SET_WATER_DRAW, setWaterDrawOrig, sizeof(setWaterDrawOrig));
}

void water_install(void)
{
    patch_dword(SET_WATER_UPDATE + 3, (DWORD)water_update);
    patch_dword(SET_WATER_DRAW + 3, (DWORD)water_draw);
}
