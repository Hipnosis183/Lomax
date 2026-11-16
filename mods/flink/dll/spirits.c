/*
 * spirits.c - Flink's spirits and the magic glass (README "Spirits").
 *
 * A spirit (Flink's Spirit, 0x2EDAA, think 0x2EE2C, MAIN.WLD) is set free by every defeated enemy
 * (enemy_defeated in enemy.c; in Flink only the boss and the spells set them
 * free) and by the boss at each hit (forestboss.c). The converter puts its records in every map as the
 * 'Spik Spirit' type, never placed: spirit_release finds it by name (ObjType_FindByName).
 * As in Flink: it starts up at 2.0, homes in on Lomax's x, y - 15 (Actor_MoveToward 0x1B008: 0x800 a
 * frame up to 2.0), drops a trail of 3 TAIL puffs, lives 600 frames (blinking the last 60); touching
 * Lomax collects it (Flink's sound 0x1D, g_MagicPower + 1), and then, or when its time is up, it
 * plays TAIL once and goes.
 *
 * Collected spirits are counted in g_spirits, 0..99 (it never goes over); Flink
 * counts them in g_MagicPower (0x82E6, 0..31). They pay for Flink's spells here (spells.c: QUICK
 * GROW on the Plant, the MAGIC KEY on a locked chest). A new game resets them with
 * Lomax's counters: Game_ResetNewGame's mov [g_LemmingsFreed], eax (0x40EE0C, 5 bytes) is a call to
 * spirits_reset_stub, which does that store and clears g_spirits and the SHIELD's spirits (spellfx.c).
 *
 * A hit that knocks Lomax's helmet off costs 5 spirits (HURT_SPIRITS, down to 0; not Flink's); a hit
 * that kills him costs none. spirits_hurt_after, after Player_Update (player_tick, grab.c), sees
 * g_PlayerHurtTimer start again; spirits lost so make the glass flicker for HURT_FLICKER ticks.
 * Player_TakeHit (0x412E30) hits only while the timer is 0 and sets it
 * to 180 (helmet lost) or 1000 (killed); the knock-back's landing raises it from below 0, which is no
 * new hit, and boss_untouchable's -2 is none either. The spirits lost fly out of Lomax (spirits_lose),
 * fanned out as a chest's, already blinking as a spirit in its last SPIRIT_BLINK frames: they don't home
 * in and can't be collected, they slow down (LOST_DRAG) and play TAIL once their time is up.
 *
 * The spells SPIRIT BOMB and SHIELD (spellfx.c) send out spirits of their own: spirit_release with no
 * source, their own think calling spirit_trail; spirit_home makes one the spirit above, spirit_vanish
 * plays its TAIL and removes it (Flink's 0x2EEEC).
 *
 * The HUD (Hud_Draw 0x42A390). In Flink's maps the chosen power's ammo and icon are not drawn
 * (powers.c); in their place, at the top left, the magic glass shows the spirits held, filling up as
 * Flink's HUD gauge does (Hud_UpdateMagic 0x34348: from the bottom), and the count is drawn next to
 * it in Lomax's big digits ('GROS'), as his lives are, left-aligned against the glass (the tens only
 * when not 0). Hud_Draw's call of
 * Actor_Draw for the ammo's ones (0x42A6C1, made every tick) goes through powers.c's hook, which calls
 * spirits_hud_draw: it draws the glass ('Spik Glass' BOTT, frame = the fill level, GLASS_LEVELS) and
 * the digits, each through a copy of the HUD actor of its own (hud_piece, for Lomax+'s FPS Override),
 * then gives the HUD actor back its type, 'KLEI' and position. The glass flickers as Flink's gauge
 * does (its HUD draw, 0x333E2): with no spirits it is drawn only while bit 4 of the tick count is set
 * (Flink's frame count 0x8710: 16 ticks on, 16 off); otherwise, while g_glassFlicker (Flink's
 * g_NoMagicFlashTimer 0x830E, cleared at 0) counts down, only while bit 3 of it is set. The count's
 * digits never flicker. Lomax+ moves the
 * ammo's x for widescreen (the mov before the call); everything here keeps the same offset from it.
 */
#include "enemy.h"

#define SPIRITS_MAX        99
#define GLASS_LEVELS       29            /* the converter's GLASS_LEVELS: frames 0..29 */
#define CALL_RESET_LEMMINGS 0x0040EE0Cu  /* Game_ResetNewGame: mov [g_LemmingsFreed], eax */
static const BYTE resetOrig[5] = { 0xA3, 0xA8, 0xC5, 0x52, 0x00 };

/* the HUD: Hud_Draw puts the ammo's ones at x 0x23 (KLEI, y 0x29; 0x25 at 50 Hz) and the lives' big
   digits 2 px lower than the small ones, 7 px apart */
#define HUD_AMMO_X         0x23
#define GLASS_X            (0x1B - HUD_AMMO_X)   /* the glass's bottom centre, from the ammo's x, y */
#define GLASS_Y            4
#define DIGITS_X           (GLASS_X + 17)        /* the count's first digit, from the ammo's x: 2 px right of the glass */
#define DIGITS_Y           2
#define DIGIT_STEP         7

#define SPIRIT_LIFE        600
#define SPIRIT_BLINK       60
#define SPIRIT_MAX         0x20000
#define SPIRIT_ACCEL       0x800
#define SPIRIT_TRAIL       3
#define SPIRIT_TAIL_TICKS  24            /* TAIL's 4 frames at its speed 44 (Flink plays it once at 0x30) */
#define LOST_DRAG          24            /* a lost spirit's speed loses 1/24 a frame (spirits_lose) */

int g_spirits;
static int g_glassFlicker;               /* ticks left of the glass's flicker after a hit (Flink's 0x830E) */
#define HURT_FLICKER       180           /* 3 seconds */

#define SPARKLE_PUFFS      5             /* (+0x58) */

/* per slot: a spirit's life, homing, trail; a sparkle's actor (and its type), where it was, puffs
   left, their offsets (oy -1: none) */
typedef struct {
    int life, homing, lost, cnt[SPIRIT_TRAIL], tx[SPIRIT_TRAIL], ty[SPIRIT_TRAIL];
    BYTE *on, *ontype;
    int ax, ay, dy, left, ox[SPIRIT_TRAIL], oy[SPIRIT_TRAIL];
} Spirit;
static Spirit g_spirit[MAX_ACTORS];

void spirits_reset_stub(void);
__asm__(".text\n"
        ".globl _spirits_reset_stub\n"
        "_spirits_reset_stub:\n"
        "  movl %eax, 0x52C5A8\n"
        "  movl $0, _g_spirits\n"
        "  movl $0, _g_shieldCount\n"
        "  movl $0, _g_demonHits\n"
        "  ret\n");

/* Actor_MoveToward (0x1B008) on one axis: v toward the target, 2 * accel while going the other way */
static int move_toward(int pos, int target, int v)
{
    int av = v < 0 ? -v : v;
    if (target == pos && av <= SPIRIT_ACCEL) return 0;
    if (target >= pos) { if (v < SPIRIT_MAX) v += v < 0 ? 2 * SPIRIT_ACCEL : SPIRIT_ACCEL; }
    else if (-v < SPIRIT_MAX) v -= v > 0 ? 2 * SPIRIT_ACCEL : SPIRIT_ACCEL;
    return v;
}

/* one TAIL frame, its top-left at x, y */
static void tail_draw(BYTE *d, int frame, int x, int y)
{
    const BYTE *e = record_of(d, FOURCC("TAIL")), *bank;
    if (!e) return;
    e += frame * 8;                          /* (one piece a frame) */
    if (!(bank = G_ANIMBANKS[e[0] | (T_BANKS(A_TYPE(d)) & 0x100)])) return;
    Sprite_QueueWorld(PLANE(A_PLANE(d)), bank + e[1] * 0x38 + 0x10, x, y, (unsigned)Actor_GetDrawDepth(d) << 24, 20000);
}

static void __cdecl spirit_draw(BYTE *d)
{
    int k = slot_of(d), n;
    if (k < 0) return;
    if (!(A_FLAGS(d) & AF_HIDDEN)) draw_actor(d);
    for (n = 0; n < SPIRIT_TRAIL; n++)
        if (g_spirit[k].ty[n] != -1) tail_draw(d, g_spirit[k].cnt[n] >> 8, g_spirit[k].tx[n], g_spirit[k].ty[n]);
}

static int touches_lomax(BYTE *d)
{
    const BYTE *pa = *G_PLAYERANIM;
    int x1, y1, x2, y2;
    if (!pa || A_PLANE(d) != *G_PLAYERPLANE || !actor_box(d, &x1, &y1, &x2, &y2)) return 0;
    return x1 < *G_PLAYERX + HB_X2(pa) && *G_PLAYERX + HB_X1(pa) < x2 &&
           y1 < *G_PLAYERY + HB_Y2(pa) && *G_PLAYERY + HB_Y1(pa) < y2;
}

/* the trail: a puff dropped every 0x400 / 0x30 frames (0x2EE2C), none once it no longer homes in */
void spirit_trail(BYTE *d)
{
    Spirit *s;
    int k = slot_of(d), n;
    if (k < 0) return;
    s = &g_spirit[k];
    for (n = 0; n < SPIRIT_TRAIL; n++) {
        s->cnt[n] += 0x30;
        if (s->cnt[n] >> 8 < 4) continue;
        if (!s->homing) { s->ty[n] = -1; continue; }
        s->cnt[n] &= 0xFF;
        s->tx[n] = A_X(d) - 7;
        s->ty[n] = A_Y(d) - 12;
    }
}

static void __cdecl spirit_update(BYTE *d)
{
    Spirit *s;
    int k = slot_of(d);
    if (k < 0) return;
    s = &g_spirit[k];
    spirit_trail(d);
    if (!s->homing) {                        /* TAIL once, then gone */
        A_FLAGS(d) &= ~AF_HIDDEN;
        if (--s->life <= 0) { Actor_Despawn(d); return; }
        Actor_AdvanceAnim(d);
        return;
    }
    if (--s->life <= SPIRIT_BLINK) A_FLAGS(d) ^= AF_HIDDEN;
    if (s->lost) {                           /* lost by a hit (spirits_lose): drifting, slowing, blinking */
        A_VX(d) -= A_VX(d) / LOST_DRAG;
        A_VY(d) -= A_VY(d) / LOST_DRAG;
        A_FX(d) += A_VX(d);
        A_FY(d) += A_VY(d);
        set_pos(d);
        if (s->life <= 0) {
            s->homing = 0;
            s->life = SPIRIT_TAIL_TICKS;
            Actor_SetAnim(d, FOURCC("TAIL"));
        }
        Actor_AdvanceAnim(d);
        return;
    }
    A_VX(d) = move_toward(A_X(d), *G_PLAYERX, A_VX(d));
    A_VY(d) = move_toward(A_Y(d), *G_PLAYERY - 15, A_VY(d));
    A_FX(d) += A_VX(d);
    A_FY(d) += A_VY(d);
    set_pos(d);
    if (s->life <= 0 || touches_lomax(d)) {
        if (s->life > 0) {
            flink_sound(d, 0x1D);
            if (g_spirits < SPIRITS_MAX) g_spirits++;
            flog("spirit collected: %d", g_spirits);
        }
        s->homing = 0;
        s->life = SPIRIT_TAIL_TICKS;
        Actor_SetAnim(d, FOURCC("TAIL"));
    }
    Actor_AdvanceAnim(d);
}

/* a spirit flies out of src at x, y (0x2EDAA): none in a map converted before spirits. Returns it
   (NULL: none), going up at 2.0; a caller may change its speed (chests.c spreads a chest's out).
   No src: on Lomax's plane (the spells', spellfx.c). */
BYTE *spirit_release(BYTE *src, int x, int y)
{
    BYTE *type = ObjType_FindByName("Spik Spirit"), *d;
    int k, n;
    if (!type || !(d = src ? Actor_Alloc(src, NULL) : Actor_Alloc(NULL, "Spik Spirit")) || (k = slot_of(d)) < 0) return NULL;
    if (!src) A_PLANE(d) = *G_PLAYERPLANE;
    A_TYPE(d) = type;
    g_enemy[k].type = NULL;
    memset(&g_spirit[k], 0, sizeof(g_spirit[k]));
    A_UPDATE(d) = (void *)spirit_update;
    A_DRAW(d) = (void *)spirit_draw;
    A_CULL(d) = CULL_NEVER;
    A_FLAGS(d) = 0;
    A_LAYER(d) = 2;
    g_spirit[k].life = SPIRIT_LIFE;
    g_spirit[k].homing = 1;
    for (n = 0; n < SPIRIT_TRAIL; n++) { g_spirit[k].cnt[n] = 0x400 - n * 0x155; g_spirit[k].ty[n] = -1; }
    A_FX(d) = x << 16;
    A_FY(d) = y << 16;
    set_pos(d);
    A_VX(d) = 0;
    A_VY(d) = -0x20000;
    Actor_SetAnim(d, FOURCC("SPIR"));
    return d;
}

/* a spell's spirit (spellfx.c) becomes the one above, homing in on Lomax, life ticks left (0x2EE2C) */
void spirit_home(BYTE *d, int life)
{
    int k = slot_of(d);
    if (k < 0) return;
    g_spirit[k].homing = 1;
    g_spirit[k].life = life;
    A_UPDATE(d) = (void *)spirit_update;
}

/* a spell's spirit plays TAIL once and goes (0x2EEEC) */
void spirit_vanish(BYTE *d)
{
    int k = slot_of(d);
    if (k < 0) return;
    g_spirit[k].homing = 0;
    g_spirit[k].life = SPIRIT_TAIL_TICKS;
    A_FLAGS(d) &= ~AF_HIDDEN;
    Actor_SetAnim(d, FOURCC("TAIL"));
    A_UPDATE(d) = (void *)spirit_update;
}

/* the sparkle (0x2ECFC, its draw): three TAIL puffs at random over the actor, each starting again
   when its count (0x30 a frame) reaches 0x400, SPARKLE_PUFFS in all; gone once none is left */
static void __cdecl sparkle_update(BYTE *d)
{
    Spirit *s;
    int k = slot_of(d), n, j, any = 0;
    if (k < 0) return;
    s = &g_spirit[k];
    if (s->on && (j = slot_of(s->on)) >= 0 && actor_active(j) && A_TYPE(s->on) == s->ontype) {
        s->ax = A_X(s->on);
        s->ay = A_Y(s->on);
    } else s->on = NULL;
    for (n = 0; n < SPIRIT_TRAIL; n++) {
        s->cnt[n] += 0x30;
        if ((s->cnt[n] >> 8 & 0xFF) == 4) {
            if (--s->left < 0) s->oy[n] = -1;
            else {
                s->cnt[n] &= 0xFF;
                s->ox[n] = rand() & 7;
                s->oy[n] = rand() & 31;
                if (s->oy[n] >= 25) s->oy[n] -= 25;
            }
        }
        if (s->oy[n] < 0) { s->ty[n] = -1; continue; }
        any = 1;
        s->tx[n] = s->ax + s->ox[n] - 11;
        s->ty[n] = s->ay + s->oy[n] + s->dy - 0x26;
    }
    if (!any) Actor_Despawn(d);
}

/* sparkles on an actor (0x2ECA6: a Spirit, unseen, whose draw is the sparkle; +0x5E dy) */
BYTE *spirit_sparkle(BYTE *on, int dy)
{
    BYTE *d = spirit_release(NULL, A_X(on), A_Y(on));
    Spirit *s;
    int k, n;
    if (!d || (k = slot_of(d)) < 0) return NULL;
    s = &g_spirit[k];
    s->homing = 0;
    s->on = on;
    s->ontype = A_TYPE(on);
    s->ax = A_X(on);
    s->ay = A_Y(on);
    s->dy = dy;
    s->left = SPARKLE_PUFFS;
    for (n = 0; n < SPIRIT_TRAIL; n++) s->oy[n] = -1;
    A_FLAGS(d) |= AF_HIDDEN;
    A_VY(d) = 0;
    A_LAYER(d) = FLINK_LAYER(1);
    A_UPDATE(d) = (void *)sparkle_update;
    return d;
}

/* ---- The HUD --------------------------------------------------------------------------------- */

/* Each piece is drawn through a stand-in of its own, a copy of the HUD actor: Lomax+'s FPS Override
   matches a sprite with the last tick's by actor and n-th Actor_Draw call of that actor, and Hud_Draw
   draws a varying number of pieces before these (the tens of its counts, the spare helmet), as do the
   tens here. Drawn as the HUD actor's n-th call, a piece would be matched with a neighbour (well under
   Lomax+'s 48 px limit) whenever that count changes, and drawn sliding from it for a tick. A ones digit
   has a stand-in for each layout (with and without its tens), so a new layout is not a slide either. */
static BYTE g_hudPiece[HUD_PIECES][ACTOR_STRIDE];

void hud_piece(const BYTE *a, int piece, int frame, int x, int y)
{
    BYTE *s = g_hudPiece[piece];
    memcpy(s, a, ACTOR_STRIDE);
    *(short *)(s + 0x6E) = (short)frame;
    A_X(s) = x;
    A_Y(s) = y;
    Actor_Draw(s, 1000, 0, 0);
}

/* in Flink's maps, at Hud_Draw's ammo-ones call: the HUD actor a stands at the ammo's ones */
void spirits_hud_draw(BYTE *a)
{
    BYTE *type = A_TYPE(a), *glass = ObjType_FindByName("Spik Glass");
    int x = A_X(a), y = A_Y(a), n = g_spirits < 0 ? 0 : g_spirits > SPIRITS_MAX ? SPIRITS_MAX : g_spirits;
    /* the digits first: within a draw bucket the first sprite queued is in front
       (else the glass covers the count) */
    Actor_SetAnim(a, FOURCC("GROS"));
    if (n >= 10) hud_piece(a, HUD_SPIRITS_TENS, n / 10, x + DIGITS_X, y + DIGITS_Y);
    hud_piece(a, n >= 10 ? HUD_SPIRITS_ONES2 : HUD_SPIRITS_ONES, n % 10, x + DIGITS_X + (n >= 10 ? DIGIT_STEP : 0), y + DIGITS_Y);
    if (glass) {
        A_TYPE(a) = glass;
        int shown = n == 0 ? (*G_TICKCOUNTER & 0x10) != 0
                  : g_glassFlicker <= 0 || (g_glassFlicker & 8) != 0;
        if (shown && record_of(a, FOURCC("BOTT"))) {
            Actor_SetAnim(a, FOURCC("BOTT"));
            hud_piece(a, HUD_GLASS, (n * GLASS_LEVELS + SPIRITS_MAX - 1) / SPIRITS_MAX, x + GLASS_X, y + GLASS_Y);
        }
        A_TYPE(a) = type;
    }
    spells_hud_draw(a, x, y);                    /* the chosen spell and its cost (spells.c) */
    Actor_SetAnim(a, FOURCC("KLEI"));
    A_X(a) = x;
    A_Y(a) = y;
}

/* after Player_Update: a new hit that knocks the helmet off (the hurt timer up from 0..2 to at most
   HURT_HELMET: a hit made later in the frame has counted down a tick or two by the next check; a
   killing hit sets 1000) costs HURT_SPIRITS */
#define HURT_SPIRITS       5
#define HURT_HELMET        180

/* n spirits fly out of Lomax's middle, fanned out -1.5 .. 1.5 px and up at 2.0 or 3.0 px (as a
   chest's), blinking for SPIRIT_BLINK frames, then gone */
static void spirits_lose(int n)
{
    const BYTE *pa = *G_PLAYERANIM;
    int k, y = *G_PLAYERY + (pa ? (HB_Y1(pa) + HB_Y2(pa)) / 2 : -15);
    BYTE *d;
    for (k = 0; k < n; k++) {
        if (!(d = spirit_release(NULL, *G_PLAYERX, y))) return;
        g_spirit[slot_of(d)].lost = 1;
        g_spirit[slot_of(d)].life = SPIRIT_BLINK;
        A_VX(d) = n > 1 ? (2 * k - (n - 1)) * 0x18000 / (n - 1) : 0;
        A_VY(d) = k & 1 ? -0x30000 : -0x20000;
    }
}
void spirits_hurt_after(void)
{
    static int last;
    int h = *G_PLAYERHURTTIMER;
    if (g_glassFlicker > 0) g_glassFlicker--;
    if (g_spirits <= 0) g_glassFlicker = 0;      /* (Flink's clr.w $830E at 0: the empty glass's blink) */
    if (g_active && last >= 0 && last <= 2 && h > 2 && h <= HURT_HELMET) {
        if (g_spirits > 0) g_glassFlicker = HURT_FLICKER;
        int n = g_spirits < HURT_SPIRITS ? (g_spirits > 0 ? g_spirits : 0) : HURT_SPIRITS;
        g_spirits -= n;
        spirits_lose(n);
        flog("hurt: %d spirits lost, %d left", n, g_spirits);
    }
    last = h;
}

int spirits_retail(void) { return is_bytes(CALL_RESET_LEMMINGS, resetOrig, sizeof(resetOrig)); }

void spirits_install(void) { patch_call(CALL_RESET_LEMMINGS, (const void *)spirits_reset_stub); }
