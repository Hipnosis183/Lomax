/* platforms.c - Flink's platforms as Lomax platform objects (PLATFORMS, plat_* and
   convert_platforms; their comments there explain each kind and its params, as README "Platforms").

   Each plat_* fills a PlatSpec: (behaviour, number of params, number of checkboxes, [(Lomax record,
   [Flink records])], params, flags, (dx, dy) added to the placement, mirrored, variant), or returns 0
   (skipped). The type is named '<behaviour> <variant> <n>'. A Flink record is named by its tag,
   'TAG#n' for only its n-th frame (-1 = the last), a trailing '*2' repeats its sprites twice side by
   side, '&N' draws each frame again N px lower, and '-' is a blank frame. */
#include "convert.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *fourcc; const char *tags[12]; int ntags; } RL;
typedef struct {
    const char *beh; int np, nc;
    RL rl[8]; int nrl;
    int params[16], nparams;
    int flags, dx, dy, mirror;
    const char *variant;
} PlatSpec;

static void spec(PlatSpec *s, const char *beh, int np, int nc, int flags)
{
    memset(s, 0, sizeof(*s));
    s->beh = beh; s->np = np; s->nc = nc; s->flags = flags; s->variant = "Flink";
}
static void rl(PlatSpec *s, const char *fourcc, int n, ...)
{
    va_list ap;
    int i;
    RL *r = &s->rl[s->nrl++];
    r->fourcc = fourcc;
    r->ntags = n;
    va_start(ap, n);
    for (i = 0; i < n; i++) r->tags[i] = va_arg(ap, const char *);
    va_end(ap);
}
static void params(PlatSpec *s, int n, ...)
{
    va_list ap;
    int i;
    s->nparams = n;
    va_start(ap, n);
    for (i = 0; i < n; i++) s->params[i] = va_arg(ap, int);
    va_end(ap);
}

/* Flink platforms as Lomax objects. Each converter fills a PlatSpec or returns 0 (skipped):
     (behaviour, number of params, number of checkboxes, [(Lomax record, [Flink records])], params,
      flags[, (dx, dy) added to the placement[, mirrored[, variant]]])
   The type is named '<behaviour> <variant> <n>'. FLINK.DLL reads the variant of Mega types: 'Float'
   (set here for platforms on the water: they ride the tide), 'Dip' (dips when landed on) and 'Fall'
   (falls for good when landed on); the
   default is 'Flink'.
   A Flink record is named by its tag, 'TAG#n' for only its n-th frame (-1 = the last), a trailing
   '*2' repeats its sprites twice side by side, '&N' draws each frame again N px lower, and '-' is
   a blank frame.

   "Mega": Lomax's MovingPlatform (object 64, MovingPlatform_Spawn 0x429A90). Params: X amplitude,
   Y amplitude, X phase speed, Y phase speed, chain spacing ('PART' drawn down to the screen bottom);
   flags 1 = not always moving, 2 = returns up (with 4), 4 = sinks while stood on at Y speed * 0x28F a
   tick. Position = base + amplitude * sin(phase) / 2, phase += speed a tick, 1024 a turn: exactly
   Flink's Mega platform (0x286B2: same sine scale, same 0x28F), whose stalk is the chain.

   MovingPlatform_DrawWithChain draws 'PART' every 32 px from the platform while above the
   screen's bottom; Flink's stalk hangs 16 px above that point, so up to 16 px at the bottom were
   left empty, flickering as the platform moved. Each piece is drawn twice, 32 px apart. */
static int plat_mega(const Place *p, PlatSpec *s)
{
    int speed = prm(p, 0, 0), amp = prm(p, 1, 0);
    spec(s, "Mega", 5, 5, 0);
    rl(s, "MAIN", 1, "MEGA");
    rl(s, "PART", 1, "APPE&32");
    if (p->check & 1) { params(s, 5, 0, 0, 0, speed, 32); s->flags = 4; }          /* dont move: sinks when stood on */
    else if (p->check & 4) params(s, 5, amp, 0, speed, 0, 32);                      /* left/right */
    else params(s, 5, 0, amp, 0, speed, 32);
    return 1;
}

/* Flink's Hand (init 0x28696: a Mega platform plus param 2, the period, and checkbox 4, 'deadly';
   FLINK.DLL hand.c): MEGA's 3 frames (open to fist), APPE the arm. A 'Mega Hand' moving as plat_mega;
   the period goes into the speed of the axis it doesn't move on (amplitude 0 there), bit 15 'deadly'. */
static int plat_hand(const Place *p, PlatSpec *s)
{
    int speed = prm(p, 0, 0), amp = prm(p, 1, 0), period = prm(p, 2, 0);
    period = (period & 0x7FFF) | (p->check & 0x10 ? 0x8000 : 0);
    spec(s, "Mega", 5, 5, 0);
    rl(s, "MAIN", 1, "MEGA");
    rl(s, "PART", 1, "APPE&32");
    if (p->check & 4) params(s, 5, amp, 0, speed, period, 32);
    else params(s, 5, 0, amp, period, speed, 32);
    s->variant = "Hand";
    return 1;
}

static void plat_static(PlatSpec *s, const char *tag, int flags)
{
    spec(s, "Mega", 5, 5, flags);
    rl(s, "MAIN", 1, tag);
}

/* Flink's Tra2 / Trap platform (0x2765C): falls for good, accelerating, param 0 frames after being
   landed on (or, with -1, when Flink comes near). A static Mega 'Fall' (FLINK.DLL fall_update) with
   the delay as its Y speed, which does nothing with a Y amplitude of 0. */
static int plat_trap(const Place *p, PlatSpec *s)
{
    plat_static(s, p->check & 1 && strcmp(p->name, "Trap platform") ? "SMAL" : "BIG ", 0);
    params(s, 5, 0, 0, 0, prm(p, 0, 1) & 0xFFFF, 0);
    s->variant = "Fall";
    return 1;
}

/* "Rota": Lomax's wheel of platforms (object 21, Rota_RotatingPlatforms_Init 0x41F180). Params:
   radius (px), platform count, speed (phase += speed * 16 a tick, angle = phase / 16 of 1024),
   start phase; flag 2 = turns only while ridden, speeding up / slowing down by 1 phase unit a tick.
   Records: 'TREE' the platforms (separate actors), 'RING' the spokes' links (every 8 px) and 'BLAD'
   (two frames) leaves scattered along them (Draw_RopeSegments 0x425D40), which Flink's chains don't
   have: blank here.
   Flink's wheel (Swingplatform 0x2B3EA with arms > 0): radius (length - 1) * 16 px, arms spaced
   2048 / arms, angle += speed a frame of 2048 a turn (so half the speed in Lomax), 'dont move' =
   turns while ridden, ramping by 0x2000/65536 a frame (= Lomax's 1/16 a tick). With 0 arms it is a
   pendulum (plat_pendulum).

   Lomax's speed param is multiplied by 16, so an odd Flink speed can't be given (16-1's -5 became -2);
   FLINK.DLL's wheel.c (README "Swings", "Wheels") replaces Lomax's update and draw with Flink's
   (the pivot's think 0x2B5E8, the platforms' 0x2B6BA, the chains' draw 0x2B752). Params: radius, arms,
   speed / 2 (Lomax's, without the DLL), phase (Flink's 'pre-run' frames of turning, 0x2B5A6, in
   Lomax's units: angle * 8; none with 'dont move', which doesn't turn unridden), speed * 8 (Lomax's
   phase a tick), first link (0, the pivot's RING; 16 with 'knob', whose KNOB isn't converted), and
   Flink's own for wheel.c: speed, pre-run, the bounce angle (+0x58: 'winkel' * 2, 4000 for 0),
   'stretch-factor' and the flags 1 'stretch' (checkbox 16: META's cloud wheels, whose clouds come in
   toward the pivot and go back out) and 2 (checkbox 32: the arms' stretch staggered). */
static int plat_wheel(const Place *p, PlatSpec *s)
{
    int length = prm(p, 0, 0), speed = s16v(prm(p, 1, 0)), arms = prm(p, 2, 0), winkel = prm(p, 3, 0),
        prerun = prm(p, 4, 0), stretch = prm(p, 5, 0), lspeed, phase, flags;
    long long ph;
    if (!arms || arms > 8) return 0;
    lspeed = speed / 2;                                    /* int(speed / 2): toward 0 */
    if (!lspeed) lspeed = speed > 0 ? 1 : -1;
    ph = (long long)prerun * speed * 8 % 16384;
    if (ph < 0) ph += 16384;
    phase = p->check & 1 ? 0 : (int)ph;
    flags = (p->check & 0x10 ? 1 : 0) | (p->check & 0x20 ? 2 : 0);
    spec(s, "Rota", 11, 2, p->check & 1 ? 2 : 0);
    rl(s, "TREE", 1, p->check & 2 ? "SMAL" : "BIGG");
    rl(s, "RING", 1, "RING");
    rl(s, "BLAD", 2, "-", "-");
    params(s, 11, imax(0, length - 1) * 16, arms, lspeed & 0xFFFF, phase, (speed * 8) & 0xFFFF,
           p->check & 4 ? 16 : 0, speed & 0xFFFF, prerun & 0xFFFF, ((winkel ? winkel : 2000) * 2) & 0xFFFF,
           stretch & 0xFFFF, flags);
    return 1;
}

/* "Swin": Lomax's swing (object 46, Swin_Swing_Init 0x425780): a pendulum hanging from x, y - length.
   Params: length, speed, swing strength, arms, pre-run. Each tick the angular speed changes by the
   strength, which reverses once |speed| passes the speed param; the phase (1024 a turn, 16 units a
   step) adds the speed. Flink's pendulum (Swingplatform with 0 arms, Trunk: 0x2B5E8 / 0x263C6) is
   much the same: its speed changes by 0x4000/65536 of 2048 a turn a frame, reversing at the speed
   param, so in Lomax units: speed * 8, strength 2. Flink's placement is the pivot, Lomax's the
   platform. FLINK.DLL's swing.c replaces the update and draw with Flink's (README "Swings"), from
   three more params:
    - the bounce angle, Flink's 'winkel' * 2 (2048 a turn; -1 for none, as 0 is in Flink, which
      makes it 4000, an angle never reached): within |speed| of it the speed reverses (0x2B65A);
    - the second chain's x offset: the Trunk (draw 0x26510) draws every link twice, at the log's
      point and its hitbox's right - 10 px to the right of it (the record's +0xE byte, read at
      0x264EA: its hitbox right), so the log hangs on two parallel chains; 0 = one chain;
    - the first link's distance from the pivot: the Trunk's chain starts at the pivot (0x26510: 0, 16,
      .. r), the Swingplatform's 16 px out (0x2B752: 16, 32, .. r), but without its knob checkbox (4)
      its pivot draws a RING of its own (init 0x2B472, draw 0x2B6B2), so the chain starts at 0 too;
      with it the pivot draws KNOB, not converted (the chain starts at 16).
   Records: 'TREE' the platform, 'RING' a link (ring_record), 'BLAD' (two frames, blank) Lomax's leaves. */
static int plat_pendulum(PlatSpec *s, int length, int speed, int prerun, int small, int winkel, int trunk, int knob)
{
    int r = imax(0, length - 1) * 16, sp = s16v(speed);
    spec(s, "Swin", 8, 0, 0);
    rl(s, "TREE", 1, small ? "SMAL" : "BIGG");
    rl(s, "RING", 1, "RING");
    rl(s, "BLAD", 2, "-", "-");
    params(s, 8, r, (sp < 0 ? -sp : sp) * 8, 2, 0, prerun, winkel ? (winkel * 2) & 0xFFFF : 0xFFFF, 0,
           knob && !trunk ? 16 : 0);
    s->dy = r;
    s->variant = trunk ? "Trunk" : "Flink";
    return 1;
}

static int plat_swing(const Place *p, PlatSpec *s)
{
    if (prm(p, 2, 0)) return plat_wheel(p, s);
    return plat_pendulum(s, prm(p, 0, 0), prm(p, 1, 0), prm(p, 4, 0), p->check & 2, prm(p, 3, 0), 0, p->check & 4);
}

static int plat_trunk(const Place *p, PlatSpec *s)
{
    return plat_pendulum(s, prm(p, 0, 0), prm(p, 1, 0), prm(p, 3, 0), p->check & 2, prm(p, 2, 0), 1, 0);
}

/* Flink's Bouncy platform (0x2BE16, think 0x2BE6A; TREE's leaf on a stalk; params x-speed, checkbox 1
   'look right'): the Jumppad's throw, at its 'JUMP' animation's frame 7 after landing, and with its
   x-speed (1/100 px a frame) to the left, to the right with 'look right' (see FLINK.DLL's jumppad.c).
   Lomax's bounce pad (object 68) throws at a fixed speed straight up, so it is a static 'Mega Bouncy'
   (Lomax's MovingPlatform, object 64) whose update FLINK.DLL replaces, with all of JUMP's frames,
   mirrored for 'look right' (Flink's +3 bit 3), and the Jumppad's unwidened standing surface. Params
   5-7: the signed x speed, JUMP's frame count and speed (Flink's +4 and +0x32; set in convert_platforms). */
static int plat_bouncy(const Place *p, PlatSpec *s)
{
    int v = s16v(prm(p, 0, 0));
    if (v < 0) v = -v;
    spec(s, "Mega", 8, 5, 1);
    rl(s, "MAIN", 1, "JUMP");
    params(s, 8, 0, 0, 0, 0, 0, (p->check & 1 ? v : -v) & 0xFFFF, 0, 0);
    s->mirror = p->check & 1;
    s->variant = "Bouncy";
    return 1;
}

static int plat_one(PlatSpec *s, const char *tag, const char *variant)
{
    spec(s, "Mega", 5, 5, 1);
    rl(s, "MAIN", 1, tag);
    params(s, 5, 0, 0, 0, 0, 0);
    s->variant = variant;
    return 1;
}
/* Flink's Jumppad (0x2BF8E, no params; its 'JPAD' record differs per world: FOREST's and
   VILLAGE's look different). Landed on, it squashes and wobbles for 48 frames and throws Flink up 20
   frames after he landed: harder when Jump is held at that moment (see FLINK.DLL's jumppad_update).
   Lomax's bounce pad throws at a fixed speed, so the Jumppad is a static 'Mega Jumppad' (Lomax's
   MovingPlatform, object 64) whose update FLINK.DLL replaces. Its record keeps all of JPAD's frames
   (0 squashed, 1 at rest, 2 stretched; FLINK.DLL picks the frame) and Flink's standing surface: the
   player's centre within the hitbox's sides, so it isn't widened (JUMPPAD_EDGE: Lomax's test is strict). */
static int plat_jumppad(const Place *p, PlatSpec *s) { (void)p; return plat_one(s, "JPAD", "Jumppad"); }
/* Flink's Seesaw (0x2BBE2, no params; FOREST and VILLAGE), the seesaw: a plank whose 'SESA' frames
   tilt from the left end down (0) to the right end down (3). A thrown object falling on its raised
   end tips it and throws Flink, standing on the other end, up (see FLINK.DLL's seesaw.c). A static
   'Mega Seesaw' (Lomax's MovingPlatform, object 64) whose update FLINK.DLL replaces, with all of
   SESA's frames and Flink's hitbox (its standing surface: JUMPPAD_EDGE, as the Jumppad's). */
static int plat_seesaw(const Place *p, PlatSpec *s) { (void)p; return plat_one(s, "SESA", "Seesaw"); }
static int plat_leaves(const Place *p, PlatSpec *s) { (void)p; return plat_one(s, "FALL", "Leaves"); }

/* Flink's Exploding platform (init 0x2397A, think 0x239C0, digit hook 0x23A12; MOUNTAIN map 7 = 19-2
   (13), map 13 = 23-1 (2)): at rest until Flink stands on it, then it goes sideways at speed * 0x28F
   a frame for good, counting down time * 60 - 1 frames with the seconds left drawn on it (ZAHL), and
   breaks up (BOOM). A static 'Mega Explode' (FLINK.DLL explode.c) with MAIN (EINS), BOOM and ZAHL;
   7 params: Lomax's 5 (all 0), speed (signed), time (s). */
static int plat_exploding(const Place *p, PlatSpec *s)
{
    spec(s, "Mega", 7, 5, 1);
    rl(s, "MAIN", 1, "EINS"); rl(s, "BOOM", 1, "BOOM"); rl(s, "ZAHL", 1, "ZAHL");
    params(s, 7, 0, 0, 0, 0, 0, prm(p, 0, 0) & 0xFFFF, prm(p, 1, 0) & 0xFFFF);
    s->variant = "Explode";
    return 1;
}

/* Flink's Wolky platform (init 0x230E4, think 0x23120; META): a cloud rising at speed * 0x28F / 65536
   px a frame, swaying, sinking 20 frames when landed on, despawned at y -30 (its Generator brings it
   back). A static 'Mega Wolky' (FLINK.DLL wolky.c) with MAIN (WOLK); 6 params: Lomax's 5 (all 0), the
   speed (signed, as Flink's muls). */
static int plat_wolky(const Place *p, PlatSpec *s)
{
    spec(s, "Mega", 6, 5, 1);
    rl(s, "MAIN", 1, "WOLK");
    params(s, 6, 0, 0, 0, 0, 0, prm(p, 0, 100) & 0xFFFF);
    s->variant = "Wolky";
    return 1;
}

/* Flink 0x2B818: a spawner drawn as nothing, sending leaves (FLY) leftward from x + area + 420,
   bobbing up and down, on a timer. FLINK.DLL's floating.c runs it: a 'Floating' Mega at the
   placement whose MAIN is FLY (its leaves'), params 5-8 Flink's area, timer, speed, pre-run. */
static int plat_floating(const Place *p, PlatSpec *s)
{
    static const int D[4] = { 0, 100, 100, 0 };
    spec(s, "Mega", 9, 5, 1);
    rl(s, "MAIN", 1, "FLY ");
    params(s, 9, 0, 0, 0, 0, 0, prml(p, 0, D) & 0xFFFF, prml(p, 1, D) & 0xFFFF, prml(p, 2, D) & 0xFFFF,
           prml(p, 3, D) & 0xFFFF);
    s->variant = "Floating";
    return 1;
}

/* Flink's Leaveplatform (init 0x2B9F0): with 'lonely leave' a leaf at rest (STAY) that, stood on,
   falls swaying from side to side (FALL's frames 0-2, picked by the sway) until it is under the map;
   without it, a spawner drawn as nothing dropping such a leaf from 80 px above it every 64 frames.
   FLINK.DLL's leaves.c moves them: a static Mega 'Leaf' (mirrored for 'look left', checkbox 1, with
   param 2 = 1: its drift and sway go the other way) and a 'Leaves' spawner whose MAIN is FALL. */
static int plat_leaf(const Place *p, PlatSpec *s)
{
    int flip = (p->check & 2) != 0;
    spec(s, "Mega", 5, 5, 1);
    rl(s, "MAIN", 1, "STAY"); rl(s, "FALL", 1, "FALL");
    params(s, 5, 0, 0, flip, 0, 0);
    s->mirror = flip;
    s->variant = "Leaf";
    return 1;
}

static int plat_leaveplatform(const Place *p, PlatSpec *s)
{
    return p->check & 1 ? plat_leaf(p, s) : plat_leaves(p, s);
}

/* "Wate": Lomax's water (object 81, Water_Init 0x4317F0). The surface is the placement's y - 20;
   it draws 'WAV1' (front, frames tiled in 64 px strips across the screen), 'WAV2' (back) and 'WAV3'
   (surface strip). The player sinks and the level is lost once his feet are 32 px under it, unless he
   jumps out at once (Water_Update 0x431A20). Flink's Water (0x2E832) puts the surface at its y plus a
   tide, amplitude (4th param) * 2 * sin(phase) / 65536 = amplitude / 2 px up and down, the phase
   stepping 5 of 1024 a frame (3 when amplitude * 2 > 128); Flink swims in it; as lava (MOUNTAIN) it
   hurts. Here: the surface at Flink's mean level, with the tide's amplitude (px) and step as the
   placement's two params, which FLINK.DLL applies every tick (Lomax's Water_Init ignores params; without
   the DLL the water stays at the mean level). Flink's 32 px 'WAVE' sprites, doubled to 64 px, are
   'WAV1', 4 px above the surface (WAVE_TOP): Flink's Water draw (0x2EA14) puts them there itself,
   not at the WAVE record's offset (-6, -7, -8 in FOREST, VILLAGE, MOUNTAIN): Sprite_DrawScreen's y is
   the surface - g_CameraY (0x874A) + 0x7C in raw sprite coordinates, where 128 is the screen's top.
   Water_Draw (0x431D80) takes 12 frames of 'WAV1' and 'WAV2' (one per 4 ticks), so Flink's 3
   are repeated 4 times. Flink draws one row of waves, so 'WAV2' is blank (12 blank frames: with fewer
   the missing ones are read as garbage).
   Tint: Flink's water (0x2E832) sends its params 0-2 to main-CPU command 0x10, which adds them (as
   r, g, b levels of the Mega Drive's 0-7) to every palette colour, clamped, and an H-interrupt at the
   surface line switches to that palette: everything below the surface is tinted (water -1, -1, 0 or
   so: darker and bluer; lava 3, -2, -3: red). Lomax's water body is 'WAV3', one frame drawn in 64 px
   strips at the surface + 6 (Water_Draw ignores its offset) as a translucent sprite, its alpha the
   actor's +0xE8 (0x8A from Water_Init). 'WAV3' here is a WATER_TINT_H px strip of colour WATER_TINT
   (palette 0), and FLINK.DLL sets the alpha from the 3rd param: water_tint picks the colour and alpha
   whose blend comes nearest to Flink's tint over the map's colours; the alphas are set apart
   (WATER_ALPHA, LAVA_ALPHA; Flink's checkbox 1 is lava), the colour staying the fit's. The strip's
   sprite has a blank 2nd frame, which FLINK.DLL draws instead when the game has no real
   translucency (a checkerboard or opaque strip would hide the water's depths). FLINK.DLL also draws
   the strip the 4th param's px higher (from the waves' grey rows down), and puts the waves and the
   strip in front of the water's plane (the ground) but behind Flink's priority plane.
   Lava: Flink's checkbox 1 'lava' is the 5th param (1: FLINK.DLL lava.c hurts or kills Lomax in it,
   Flink's Water think 0x2E976, and he never swims there).
   Splashes (Water_SpawnSplash 0x4320E0: 'SPL2', 'SPLA'; sink splash 'SPL3', droplets 'SPL4',
   bubbles 'BUBL'; a missing one crashes Actor_Draw) come from the type named exactly "Water", so the
   template's own Water type is renamed and this one takes the name.

   the strip's top: the waves' top (y + their dy) + WAVE_FOAM, as a lift over Water_Draw's y + 6 */
#define WATER_ALPHA 0x60
#define LAVA_ALPHA 0x80
#define WAVE_FOAM 6
static Ctx *g_ctx;
static int plat_water(const Place *p, PlatSpec *s)
{
    int amp = s16v(prm(p, 3, 0));
    if (amp < 0) amp = -amp;
    spec(s, "Water", 5, 1, 0x4000);
    rl(s, "WAV1", 4, "WAVE*2", "WAVE*2", "WAVE*2", "WAVE*2");
    rl(s, "WAV2", 12, "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-");
    rl(s, "WAV3", 1, "FILL");
    rl(s, "SPLA", 1, "SPLA"); rl(s, "SPL2", 1, "SPLA"); rl(s, "SPL3", 1, "SPLA");
    rl(s, "SPL4", 1, "SPLA#-1"); rl(s, "BUBL", 1, "SPLA#-1");
    params(s, 5, amp / 2, amp * 2 > 128 ? 3 : 5, p->check & 1 ? LAVA_ALPHA : WATER_ALPHA,
           6 - (g_ctx->wave_dy + WAVE_FOAM), p->check & 1);
    s->dy = 20;
    return 1;
}

/* Flink's Plant (init 0x2E3B0; FOREST), the Plant: stood on, the Quick Grow spell makes it grow
   param 0 ('height') px up, its stalk (ROOT) drawn from the ground under it; left alone it sinks back.
   A static Mega 'Plant' (FLINK.DLL plant.c: Ability and 10 defeated enemies make it grow) with MAIN =
   LEAF and ROOT; params: the stalk's foot below the placement (the X speed: set in convert_platforms,
   after the placement's final y is known) and the height (the Y speed), amplitudes 0. */
static int plat_plant(const Place *p, PlatSpec *s)
{
    spec(s, "Mega", 5, 5, 1);
    rl(s, "MAIN", 1, "LEAF"); rl(s, "ROOT", 1, "ROOT");
    params(s, 5, 0, 0, 0, prm(p, 0, 0) & 0x7FFF, 0);
    s->variant = "Plant";
    return 1;
}

/* Flink's Throw platform (0x27952), the Throw platform: carried and thrown (FLINK.DLL grab.c and throwplat.c); in
   the water it floats at the surface + 5 and drifts the way it was thrown at |param| * 0x28F / 65536
   px a frame until a wall stops it. A static Mega 'Throw' at Flink's placement (where it lies on the
   ground, ready to be picked up), with |param| as the Mega's x speed (an amplitude of 0: no motion). */
static int plat_throwplat(const Place *p, PlatSpec *s)
{
    int v = s16v(prm(p, 0, 0));
    plat_static(s, "STAY", 0);
    params(s, 5, 0, 0, v < 0 ? -v : v, 0, 0);
    s->variant = "Throw";
    return 1;
}

/* Flink's Wall platform (init 0x2897E, think 0x289BC; VILLAGE 8 and 11 = 13-1): a brick that slides
   out of the wall and back on timers, standable only while (nearly) out. See FLINK.DLL's wall.c. A
   static 'Mega Wall' with WALL's frames 0-4 (the record goes 0..4..1; FLINK.DLL picks the frame) and
   8 params: Lomax's 5 (all 0: no motion) and Flink's three, frames in, frames out, pre-run. */
static int plat_wall(const Place *p, PlatSpec *s)
{
    spec(s, "Mega", 8, 5, 1);
    rl(s, "MAIN", 5, "WALL#0", "WALL#1", "WALL#2", "WALL#3", "WALL#4");
    params(s, 8, 0, 0, 0, 0, 0, prm(p, 0, 0), prm(p, 1, 0), prm(p, 2, 0));
    s->variant = "Wall";
    return 1;
}

/* Flink's Rock (init 0x2917C, think 0x291EC; 11-1, 11-2, MOUNTAIN 8 and 16): a block of stones Flink
   pushes (Actor_CheckPushedByPlayer 0x1AA56), that falls off ledges and stands on the ground; one,
   two or three stones high: EINS, ZWEI with checkbox 2, DREI with checkbox 4. Checkbox 8 makes it
   breakable (param 0: the thrown objects' hits it takes; a slide breaks it too) and 16 break when it
   lands: not ported, so those (MOUNTAIN 11's two) are left out, as before, rather than block the
   way. A static 'Mega Rock' (FLINK.DLL rock.c) whose hitbox is Flink's, not widened (Lomax pushes
   it from its hitbox's side, as Flink).
   Checkbox 8 ('razorback-check', 21-1's two rocks: FLINK.DLL rock.c rock_hit) makes it breakable: param
   0 is the hits it takes (from the Razorbacks that run into it and thrown objects), and the next one breaks
   it, its TOD1 / TOD2 / TOD3 frame (as BOOM) flying apart. Params: Lomax's 5, breakable, hits.
   Checkbox 16 ('touch down', breaks when it lands) isn't in the converted maps, and isn't converted. */
static int plat_rock(const Place *p, PlatSpec *s)
{
    const char *tag = p->check & 4 ? "DREI" : p->check & 2 ? "ZWEI" : "EINS";
    int brk = p->check & 8 ? 1 : 0;
    if (p->check & 0x10) return 0;
    spec(s, "Mega", 7, 5, 1);
    rl(s, "MAIN", 1, tag);
    if (brk) rl(s, "BOOM", 1, !strcmp(tag, "EINS") ? "TOD1" : !strcmp(tag, "ZWEI") ? "TOD2" : "TOD3");
    params(s, 7, 0, 0, 0, 0, 0, brk, brk ? prm(p, 0, 0) & 0xFFFF : 0);
    s->variant = "Rock";
    return 1;
}

/* Flink's Wagon (init 0x23A4C, think 0x23AA0; MOUNTAIN maps 5 = 18-2 (two) and 10 = 20-1 (two, one
   'deadly', one a Generator's)): the Wagon on the rails. It follows the ground and rolls down
   slopes, starts at 'start-speed' when Flink lands in it at rest, goes at most at 'max-speed' (both
   * 0x28F a frame) and throws him out 'distance' px right of where it spawned. A static 'Mega Wagon'
   (FLINK.DLL wagon.c) with EINS (level), ZWEI and DREI (tilted) as MAIN, ZWEI and DREI, their hide
   selectors kept (while he rides it the Wagon's back wall, selector 1, is drawn behind Lomax and its
   front, selector 2, in front of him: Flink's 0x23DE4 / 0x23E0A), Flink's hitbox (its top is the
   standing surface) widened by 1 px a side for Lomax's strict test (Flink's 0x1A29A includes the
   sides); 9 params: Lomax's 5 (all 0) and Flink's start-speed, max-speed, distance, deadly. */
static int plat_wagon(const Place *p, PlatSpec *s)
{
    spec(s, "Mega", 9, 5, 1);
    rl(s, "MAIN", 1, "EINS"); rl(s, "ZWEI", 1, "ZWEI"); rl(s, "DREI", 1, "DREI");
    params(s, 9, 0, 0, 0, 0, 0, prm(p, 0, 0) & 0xFFFF, prm(p, 1, 0) & 0xFFFF, prm(p, 2, 0) & 0xFFFF, p->check & 1);
    s->variant = "Wagon";
    return 1;
}

static int plat_normal(const Place *p, PlatSpec *s)
{
    plat_static(s, "NORM", 1);
    params(s, 5, 0, 0, 0, 0, 0);
    s->mirror = p->check & 1;                              /* 'look left' */
    s->variant = "Dip";
    return 1;
}

static int plat_sink(const Place *p, PlatSpec *s)
{
    plat_static(s, p->check & 1 ? "SMAL" : "BIGG", 6);    /* 0.5 px a tick, rises again */
    params(s, 5, 0, 0, 0, 50, 0);
    return 1;
}

static int plat_ball(const Place *p, PlatSpec *s)
{
    if (p->check & 8) return 0;                            /* the deadly ones: 'Spik Ball' (convert) */
    return plat_swing(p, s);
}

static int plat_steer(const Place *p, PlatSpec *s)
{
    (void)p;
    plat_static(s, "CONT", 1);
    params(s, 5, 0, 0, 0, 0, 0);
    s->variant = "Steer";
    return 1;
}

static const struct { const char *name; int (*fn)(const Place *, PlatSpec *); } PLATFORMS[] = {
    { "Normalplatform", plat_normal }, { "Mega platform", plat_mega }, { "Meg2 platform", plat_mega },
    { "Hand", plat_hand }, { "Sinkplatform", plat_sink }, { "Tra2 platform", plat_trap },
    { "Tra2 Platform", plat_trap }, { "Trap platform", plat_trap }, { "Leaveplatform", plat_leaveplatform },
    { "Swingplatform", plat_swing }, { "Swing platform", plat_swing }, { "Trunk", plat_trunk },
    { "Bouncy platform", plat_bouncy }, { "Jumppad", plat_jumppad }, { "Seesaw", plat_seesaw },
    { "Ball (swinging)", plat_ball }, { "Exploding platform", plat_exploding }, { "Wolky platform", plat_wolky },
    { "Floating platform", plat_floating }, { "Wall platform", plat_wall }, { "Rock", plat_rock },
    { "Wagon", plat_wagon }, { "Steer platform", plat_steer }, { "Water", plat_water },
    { "Throw platform", plat_throwplat }, { "Plant", plat_plant },
};

/* a tag's record name: without '&N', '#n' and '*2' */
static void base_tag(const char *t, char *out)
{
    int i;
    for (i = 0; i < 4 && t[i]; i++) out[i] = t[i];
    out[i] = 0;
}

/* Flink's stalk foot: the ground under x, y (within PLANT_GROUND px) + 2 */
/* Flink's stalk foot (plant_foot): the ground under x, y within PLANT_GROUND px, + 2 */
#define PLANT_GROUND 100
static int plant_foot(const Ctx *c, int x, int y)
{
    int g;
    return (ground_at(c, x, y, &g) && g - y <= PLANT_GROUND ? g : y + PLANT_GROUND) + 2;
}

static int contains_lower(const char *s, const char *what)
{
    char b[64];
    int i;
    for (i = 0; s[i] && i < 63; i++) b[i] = (char)(s[i] >= 'A' && s[i] <= 'Z' ? s[i] + 32 : s[i]);
    b[i] = 0;
    return strstr(b, what) != NULL;
}

static void skip(PlatOut *out, const char *name)
{
    int i;
    for (i = 0; i < out->skippedName.n; i++)
        if (!strcmp(out->skippedName.v[i], name)) { out->skippedCount.v[i]++; return; }
    vpush(out->skippedName, name);
    vpush(out->skippedCount, 1);
}

/* -> (new NMY type records, their anim banks per type, GNMY entries, skipped type counts).
   ctx: the play map's collision words and size, and the water surface (none without water).

   Flink's Sinkplatform (think 0x2F1D4) never stays above the surface - 2 (y = surface
   - 2 when it would be) and, left alone, rises 0.25 px a frame back to it: it floats at
   the surface - 2, not at its placement (2-1's lilypads were 7 px too low).
   Lomax steps onto a platform only if its top is level with or below his feet
   (Platform_MoveAndCarryPlayer); Flink's sit up to a few px above the floor next to
   them, and walking onto one dropped Lomax past its edge.
   Flink's chains (0x2B752) draw the sprite in 0x86BA, which every Swingplatform and swinging
   ball init (0x2B484) sets to its own RING: the last one spawned wins. 16-1's two wheels at
   x 1049 and 1174 share their pivots with deadly balls (VILLAGE's ball ring is brown, the
   platforms' grey), and Flink shows them with the ball's; such a
   chain takes the ball's RING. Elsewhere the global isn't followed: each keeps its own. */
void convert_platforms(FWorld *fw, Places *pl, Sprites *sp, int first_type, Ctx *ctx, PlatOut *out)
{
    HMap index = { 0 };
    int i, k, ntypes = 0;
    g_ctx = ctx;
    for (i = 0; i < pl->n; i++) {
        const Place *p = &pl->v[i], *ball = NULL;
        PlatSpec s;
        int (*fn)(const Place *, PlatSpec *) = NULL, ok = 0, dx, dy, mirror, idx, j, ringSlot;
        Recs *recs = world_recs(fw, p->slot), *ringRecs;
        const char *variant;
        char key[512];
        size_t kl;
        for (k = 0; k < (int)(sizeof(PLATFORMS) / sizeof(PLATFORMS[0])); k++)
            if (!strcmp(PLATFORMS[k].name, p->name)) { fn = PLATFORMS[k].fn; break; }
        if (fn) ok = fn(p, &s);
        if (ok)
            for (k = 0; k < s.nrl && ok; k++)
                for (j = 0; j < s.rl[k].ntags; j++) {
                    char b[5];
                    const char *t = s.rl[k].tags[j];
                    if (!strcmp(t, "-") || !strcmp(t, "FILL")) continue;
                    base_tag(t, b);
                    if (!recs_has(recs, b)) { ok = 0; break; }
                }
        if (!ok) {
            if (contains_lower(p->name, "plat") || !strcmp(p->name, "Trunk") ||
                (!strcmp(p->name, "Ball (swinging)") && !(p->check & 8)) || !strcmp(p->name, "Water"))
                skip(out, p->name);
            continue;
        }
        dx = s.dx; dy = s.dy; mirror = s.mirror != 0; variant = s.variant;
        if (!strcmp(s.beh, "Mega")) {
            char b[5];
            const Rec *r0;
            int left, top, right;
            base_tag(s.rl[0].tags[0], b);
            r0 = rec_get(recs, b);
            left = r0->hb[0]; top = r0->hb[1] + p->y + dy; right = r0->hb[2];
            if (!strcmp(variant, "Jumppad") || !strcmp(variant, "Bouncy") || !strcmp(variant, "Seesaw") ||
                !strcmp(variant, "Leaves") || !strcmp(variant, "Floating") || !strcmp(variant, "Wolky")) {
            } else if (strcmp(variant, "Fall") && strcmp(variant, "Throw") && strcmp(variant, "Leaf") &&
                       strcmp(variant, "Hand") && strcmp(variant, "Wall") && strcmp(variant, "Rock") &&
                       strcmp(variant, "Wagon") && strcmp(variant, "Explode") && ctx->hasWater &&
                       abs(top - ctx->mean) <= 16) {
                variant = "Float";                         /* on the water: rides the tide (FLINK.DLL) */
                /* Flink's Sinkplatform never stays above the surface - 2: it floats there */
                if (!strcmp(p->name, "Sinkplatform")) dy = ctx->mean - 2 - p->y;
            } else if (!s.params[0] && !s.params[1] && strcmp(variant, "Rock") && strcmp(variant, "Wagon")) {
                /* Lomax steps onto a platform only if its top is level with or below his feet: one a
                   few px above the floor beside it is lowered to it */
                int xs[2], drop = 0, g, q;
                xs[0] = p->x + dx + left - 8; xs[1] = p->x + dx + right + 8;
                for (q = 0; q < 2; q++)
                    if (ground_at(ctx, xs[q], top - 6, &g) && g - top > 0 && g - top <= 6) drop = imax(drop, g - top);
                dy += drop;
            }
        }
        if (!strcmp(variant, "Bouncy")) {                  /* JUMP's frame count and speed */
            const Rec *jr = rec_get(recs, "JUMP");
            int st[4096];
            s.params[6] = flink_frames(jr, st);
            s.params[7] = jr->speed;
        }
        if (!strcmp(variant, "Plant"))                     /* the stalk's foot, below the final y */
            s.params[2] = imax(0, plant_foot(ctx, p->x, p->y) - (p->y + dy));
        if (!strcmp(s.beh, "Swin") && !strcmp(variant, "Trunk"))   /* the second chain: the log's hitbox right - 10 */
            s.params[6] = rec_get(recs, s.rl[0].tags[0])->hb[2] - 10;
        /* Flink's chains draw the RING of the last Swingplatform or swinging ball spawned: a chain
           sharing its pivot with a ball takes the ball's RING */
        for (k = 0; k < pl->n; k++) {
            const Place *q = &pl->v[k];
            if (!strcmp(q->name, "Ball (swinging)") && q != p && q->x == p->x && q->y == p->y && q->slot < fw->recs.n) { ball = q; break; }
        }
        ringRecs = recs;
        ringSlot = -1;
        if (ball && (!strcmp(s.beh, "Swin") || !strcmp(s.beh, "Rota")) && recs_has(&fw->recs.v[ball->slot], "RING")) {
            ringRecs = &fw->recs.v[ball->slot];
            if (ringRecs != recs) ringSlot = ball->slot;
        }
        kl = (size_t)snprintf(key, sizeof(key), "%s|%s|", p->name, s.beh);
        for (k = 0; k < s.nrl; k++) {
            kl += (size_t)snprintf(key + kl, sizeof(key) - kl, "%s:", s.rl[k].fourcc);
            for (j = 0; j < s.rl[k].ntags; j++) kl += (size_t)snprintf(key + kl, sizeof(key) - kl, "%s,", s.rl[k].tags[j]);
            kl += (size_t)snprintf(key + kl, sizeof(key) - kl, ";");
        }
        kl += (size_t)snprintf(key + kl, sizeof(key) - kl, "|%d|%s|%d", mirror, variant, ringSlot);
        if (kl >= sizeof(key)) die("platform key too long");
        if (!hmap_get(&index, key, (int)kl, &idx)) {
            LRec rr[8];
            Buf t = { 0 };
            char name[64];
            for (k = 0; k < s.nrl; k++) {
                const RL *r = &s.rl[k];
                if (r->ntags == 1 && !strcmp(r->tags[0], "FILL")) {    /* the water's tint strip */
                    int zero[4] = { 0, 0, 0, 0 };
                    Ent e = { sprites_bank(sp, sk(SK_FILL, 64, WATER_TINT_H, WATER_TINT)), 1, 0, 0, 0x80 };
                    rr[k] = pack_record(r->fourcc, 0x40, zero, &e, 1);
                    continue;
                }
                if ((!strcmp(s.beh, "Swin") || !strcmp(s.beh, "Rota")) && !strcmp(r->fourcc, "RING")) {
                    rr[k] = ring_record(fw, rec_get(ringRecs, "RING"), sp);   /* drawn link by link by swing.c, wheel.c */
                    continue;
                }
                {
                    RecSel sel[12];
                    int dbl = 0, rpt = 0, edge;
                    for (j = 0; j < r->ntags; j++) {
                        const char *t = r->tags[j], *amp = strchr(t, '&'), *hash = strchr(t, '#');
                        char b[5];
                        if (strstr(t, "*2")) dbl = 1;
                        if (amp) rpt = imax(rpt, atoi(amp + 1));
                        if (!strcmp(t, "-")) { sel[j].rec = NULL; sel[j].hasOnly = 0; sel[j].only = 0; continue; }
                        base_tag(t, b);
                        sel[j].rec = rec_get(recs, b);
                        sel[j].hasOnly = hash != NULL;
                        sel[j].only = hash ? atoi(hash + 1) : 0;
                    }
                    edge = !strcmp(variant, "Jumppad") || !strcmp(variant, "Bouncy") || !strcmp(variant, "Seesaw") ? JUMPPAD_EDGE :
                           !strcmp(variant, "Rock") ? 0 : !strcmp(variant, "Wagon") ? WAGON_EDGE : PLATFORM_EDGE;
                    rr[k] = lomax_record(r->fourcc, sel, r->ntags, sp, mirror, dbl, rpt, edge, !strcmp(variant, "Wagon"), 0, 0);
                }
            }
            idx = first_type + ntypes;
            hmap_put(&index, key, (int)kl, idx);
            if (!strcmp(s.beh, "Water")) snprintf(name, sizeof(name), "Water");
            else snprintf(name, sizeof(name), "%s %s %d", s.beh, variant, ntypes);
            lomax_type(&t, name, s.np, s.nc, rr, s.nrl);
            vpush(out->types, t);
            ntypes++;
            for (k = 0; k < s.nrl; k++) { bank_union(&out->banks, &rr[k].banks); free_lrec(&rr[k]); }
        }
        {
            Obj o = obj(idx, p->x + dx, p->y + dy, s.flags, s.params, s.nparams, p);
            vpush(out->objs, o);
        }
    }
    hmap_free(&index);
}
