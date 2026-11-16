/*
 * wagon.c - Flink's Wagon on the rails (init 0x23A4C, think 0x23AA0, draws 0x23DE4 /
 * 0x23E0A; MOUNTAIN maps 5 = 18-2 (two) and 10 = 20-1 (two, one 'deadly', one a Generator's);
 * README "Wagons").
 *
 * Flink's:
 * - Init: draw list 1 ((a5) = 0x7D6C: in front of Flink), window 0, record EINS; +0x48 the start
 *   speed and +0x4C the top speed (params 0, 1 * 0x28F), +0x54 the end x (its x + param 2), +0x52
 *   'deadly' (checkbox 1), +0x50 Flink in it, +0x58 rolling.
 * - Each frame (0x23AA0):
 *   - it faces the way it goes (vx < 0: mirrored), and so does Flink while he is in it;
 *   - at x >= the end x: Flink in it gets vy 2.0 up, no jump boost (0x8306) and the Wagon's vx; a
 *     'deadly' one he stands on hurts him (Player_TakeHit 0x1A7DC); then with +6 bit 4 (set by
 *     Actors_DrawAll 0x334FE while some of it was drawn on the screen) it breaks up (0x1B1F8, below:
 *     actor_break_up), else it is despawned (0x32D8E: its placement spawns it again);
 *   - its animation step (+0x32) is |vx| / 1000, at most 64 (Actor_AdvanceAnim 0x337EA);
 *   - not rising: on the ground (Actor_SnapToGround 0x1AE96: the ground from y - 16 down 3 cells,
 *     within 0x1AECA's table at vx's whole px: 3 at 0, 4, 6, 7, then 10; 4, 6, 7, 9, then 10 going
 *     left) it stands there (vy 0) and by the slope class: level, EINS, vx goes toward 0 by 0x40
 *     (0x1D448); falling to the left, DREI, vx - 0x200 while it is >= 0, else - 0x800 down to - the top
 *     speed; falling to the right, ZWEI, the same mirrored. No ground: vy + 0x800 a frame (no cap)
 *     and Flink is not in it (+0x50 0) until he stands in it again below;
 *   - (0x23C8C) Flink dying: he is out (+0x50 0) with the Wagon's vx. Else, not in the air and not
 *     standing on it: the plain draw again (below), and if he was in it he is out with its vx;
 *   - with the plain draw it moves by vx, vy; with the riding draw (0x23DE4) it is a platform:
 *     Platform_Stand (0x1A29A) moves it and carries Flink, the surface its hitbox top (1 px lower on
 *     frame 1); standing on it when he wasn't (0x82FE changed) with the Wagon at rest, the Wagon moves
 *     to his x and goes at the start speed, his vx 0. Standing on it, he is in it (+0x50);
 *   - in it: Flink's x is the Wagon's (in the air too: a jump in it comes down in it); Left and Right
 *     are taken out of the pad (bclr 0x870A): Left - 0x400 while vx > 2.0, Right + 0x400 up to the
 *     top speed;
 *   - Flink's feet 32 px or more above its y: the riding draw while he is within 24 px across, else
 *     the plain one (so it stays as it is while he is lower, in it or beside it).
 * - The riding draw (0x23DE4, then 0x23E0A from list 2, behind Flink): its frame drawn twice, in
 *   front of Flink without the pieces with hide selector 1 (state bit 8: the back wall) and behind
 *   him without those with selector 2 (bit 9: the wheels and the front), so he stands in the Wagon.
 * - The rolling sound (0x38, PCM effect 41, endless): on the first frame on the ground moving (+0x58
 *   set) it starts if the Wagon is on the screen (+6 bit 4), and Pcm_StopEffect (0x3FE4A, d6 0x29)
 *   stops it when it leaves the ground. Here sfx_loop keeps it going (sfx.c), and it also stops when
 *   the Wagon breaks up or is despawned.
 * Here: the converter's 'Mega Wagon' (a static MovingPlatform, object 64, whose spawn platforms.c
 * wraps): records MAIN (EINS), ZWEI, DREI with their hide slots (Lomax +0x70 bit n hides slot n =
 * Flink's bit 7 + n); params Lomax's 5, then start, top speed, distance, deadly. wagon_update is
 * Flink's think (before Player_Update, as Flink's thinks run before his); Platform_MoveAndCarryPlayer
 * is 0x1A29A; "standing on it" before the move is Lomax's last carry of it (Flink's 0x82FE, which his
 * Player_Update clears in the air). Left and Right are taken out around Player_Update (wagon_before /
 * wagon_after in player_tick). Plane_FindGroundY's slope class 0 / mirrored is Flink's class 1 /
 * negative.
 */
#include "enemy.h"

#define WAGON_UNIT      0x28F            /* a param's speed unit */
#define FLAT_DECAY      0x40             /* 0x23BCC */
#define UPHILL          0x200            /* 0x23C04: going against the slope */
#define DOWNHILL        0x800            /* 0x23C1E */
#define WAGON_GRAVITY   0x800            /* 0x23C64 */
#define PEDAL           0x400            /* 0x23D80: Left / Right */
#define BRAKE_MIN       0x20000          /* 0x23D74: Left brakes only above 2.0 */
#define THROW_VY        0x20000          /* 0x23AF0: 2.0 up at the end */
#define ARM_HEIGHT      32               /* 0x23DAC: his feet 32 px above its y */
#define ARM_SPAN        24               /* 0x23DD2 */
#define ANIM_MAX        64               /* 0x23B54 */
#define GROUND_CELLS    3                /* 0x1AE9E */
#define HIDE_BACK       (1u << 1)        /* selector 1: the back wall (Flink's state bit 8) */
#define HIDE_FRONT      (1u << 2)        /* selector 2: the wheels and the front (bit 9) */
#define VIEW_H          240
#define SEEN_MARGIN     32               /* its sprite's half-width: some of it on screen */
#define SHATTER_MAX     17               /* pieces that fit in a Lomax actor (Actor_ShatterIntoPieces' layout) */
#define PIECE_GRAVITY   0x4000           /* 0x1B3A2 */
#define IN_LEFT         0x8000
#define IN_RIGHT        0x2000

/* 0x1AECA: the ground's reach by vx's whole px, -8 .. 7 */
static const BYTE STICK[16] = { 10, 10, 10, 10, 9, 7, 6, 4, 3, 4, 6, 7, 10, 10, 10, 10 };

typedef struct {
    BYTE *type;
    int vx, vy;                          /* +0x1C, +0x20 (16.16, y down) */
    int start, top, end, deadly;         /* +0x48, +0x4C, +0x54, +0x52 */
    int in;                              /* +0x50: Lomax is in it */
    int riding;                          /* the riding draw (0x23DE4), which also makes it a platform */
    int stood;                           /* Lomax stood on it at its last move (Flink's 0x82FE) */
    int rolling, sound;                  /* +0x58: rolling on the ground; its sound started (on screen) */
} Wagon;
static Wagon g_wagon[MAX_ACTORS];

/* the Wagon Lomax is in this tick (-1 none), and the buttons taken out around Player_Update */
static int g_in = -1;
static struct { int masked; WORD held, pressed; } M;

void wagons_reset(void) { memset(g_wagon, 0, sizeof(g_wagon)); g_in = -1; M.masked = 0; }

static Wagon *wagon_of(const BYTE *a)
{
    int i = slot_of(a);
    return i >= 0 && g_wagon[i].type && g_wagon[i].type == A_TYPE(a) ? &g_wagon[i] : NULL;
}

static void set_player_x(int x) { *G_PLAYERX = x; ((volatile short *)G_PLAYERXFIXED)[1] = (short)x; }

/* he is out of it, with its speed (0x23CBC) */
static void get_out(Wagon *w)
{
    w->in = 0;
    *G_PLAYERVX = w->vx;
}

/* 0x1A29A's surface: the hitbox top, 1 px lower on frame 1 (0x23CF2) */
static int __cdecl wagon_surface(BYTE *a, int relx)
{
    (void)relx;
    return A_FRAME(a) ? 1 : 0;
}

/* the riding draw (0x23DE4 / 0x23E0A) or the plain one (Actor_Draw) */
static void __cdecl wagon_draw(BYTE *a)
{
    Wagon *w = wagon_of(a);
    WORD hide = A_HIDE(a);
    short layer = A_LAYER(a);
    if (w && w->riding) {
        A_HIDE(a) = (WORD)HIDE_BACK;             /* list 1: in front of him */
        draw_actor(a);
        A_LAYER(a) = (short)FLINK_LAYER(2);      /* list 2: behind him */
        A_HIDE(a) = (WORD)HIDE_FRONT;
        draw_actor(a);
    } else {
        A_HIDE(a) = 0;
        draw_actor(a);
    }
    A_HIDE(a) = hide;
    A_LAYER(a) = layer;
}

/* 0x1D448: toward 0 by d */
static int toward_zero(int v, int d)
{
    if (v > 0) return v > d ? v - d : 0;
    if (v < 0) return v < -d ? v + d : 0;
    return 0;
}

/* Flink's +6 bit 4: some of it drawn on the screen (Actors_DrawAll 0x334FE) */
static int seen(const BYTE *a)
{
    const BYTE *pl = PLANE(A_PLANE(a));
    int x = A_X(a) - P_CAMX(pl), y = A_Y(a) - P_CAMY(pl), vw = P_W(pl) * 16 - P_MAXX(pl);
    if (P_MAXX(pl) <= 0 || vw < 320) vw = 320;   /* the view's width, as enemy.c's on_screen */
    return x >= -SEEN_MARGIN && x <= vw + SEEN_MARGIN && y >= 0 && y <= VIEW_H + SEEN_MARGIN;
}

/* 0x1B388: each piece moves by its speed and falls (y up: y - vy, vy - 0x4000) */
static void __cdecl pieces_update(BYTE *a)
{
    int n = *(int *)(a + 0x80), k;
    for (k = 0; k < n; k++) {
        BYTE *l = ((BYTE **)(a + 0x84))[k >> 3];
        int *p = l ? (int *)(l + 8) + (k & 7) * 4 : NULL;
        if (!p) continue;
        p[0] += p[2];
        p[1] += p[3];
        p[3] += PIECE_GRAVITY;
    }
}

/* 0x1B1F8 (Actor_BreakUp): its current frame's pieces fly off, each from where it is drawn, at
   vx (rand & 31) - 15 over 8 px and 1.0 + (rand & 31) / 8 px up (+3 bit 7 set: no + 4.0), with sound
   0x2A. Flink doesn't set its placement's pending bit (0x6400): it isn't spawned again before the
   restart from the saved bits (a lost life); here its placement is let go of (A_DEF NULL), so that
   Shatter_DrawPieces' Actor_Despawn leaves it spawned, as Actor_Kill, until a lost life. The pieces'
   layout is Actor_ShatterIntoPieces' (game.h), which breaks up BOOM instead of the current frame.
   Shared with explode.c (flink.h), whose caller frees its own state first */
void actor_break_up(BYTE *a) { actor_break_up_sound(a, 1); }

void actor_break_up_sound(BYTE *a, int sound) { actor_break_up_vy(a, sound, 0); }

/* up: more up speed for every piece (Flink's 0x1B344: + 4.0 when +3 bit 7 is clear, as for the Meta
   Boss, which goes straight to 0x1B202) */
void actor_break_up_vy(BYTE *a, int sound, int up)
{
    BYTE *p = *(BYTE **)(a + 8) + 0x20 + *(short *)(a + 0x6E) * 8;   /* (as 0x430690) */
    WORD hi = *(WORD *)(A_TYPE(a) + 0x1C) & 0x100;
    DWORD *spr = (DWORD *)(a + 0x94);
    int n = 0, *q = NULL;
    for (;; p += 8) {
        if (p[0] && p[1]) {
            if (n >= SHATTER_MAX) break;
            if (!(n & 7)) {
                BYTE *l = Actor_AllocLink(a);
                if (!l) break;
                ((BYTE **)(a + 0x84))[n >> 3] = l;
                q = (int *)(l + 8);
            }
            *spr++ = (DWORD)(G_ANIMBANKS[p[0] | hi] + p[1] * 0x38 + 0x10);
            *spr++ = *(WORD *)(p + 6);
            *q++ = (*(short *)(p + 2) + A_X(a)) << 16;
            *q++ = (*(short *)(p + 4) + A_Y(a)) << 16;
            *q++ = ((rand() & 31) - 15) << 13;
            *q++ = -(0x10000 + ((rand() & 31) << 13)) - up;
            n++;
        }
        if (*(WORD *)(p + 6) & 0x80) break;
    }
    *(int *)(a + 0x80) = n;
    A_FLAGS(a) &= 0xFFF6FFDFu;                   /* (as 0x430690) */
    A_DEF(a) = NULL;
    A_UPDATE(a) = (void *)pieces_update;
    A_DRAW(a) = (void *)Shatter_DrawPieces;
    if (sound) flink_sound(a, 0x2A);
    flog("breaks up into %d pieces at (%d,%d)", n, A_X(a), A_Y(a));
}

/* 0x23AA0 */
static void __cdecl wagon_update(BYTE *a)
{
    Wagon *w = wagon_of(a);
    int i = slot_of(a), g, k, air, prev, rising;
    if (!w) { MovingPlatform_Update(a); return; }

    if (w->vx < 0) { A_FLAGS(a) |= AF_LEFT; if (w->in) *G_PLAYERFLAGS |= AF_LEFT; }
    else if (w->vx > 0) { A_FLAGS(a) &= ~AF_LEFT; if (w->in) *G_PLAYERFLAGS &= ~AF_LEFT; }

    if (A_X(a) >= w->end) {                      /* the end of the ride (0x23ADC) */
        int on = w->stood;                       /* (0x82FE: he stood on it at its last move) */
        if (w->in) {
            *G_PLAYERVY = -THROW_VY;
            *G_PLAYERSTATE |= PS_AIR;
            *G_JUMPHOLDFORCE = 0;
        }
        if (w->deadly && on) Player_TakeHit(A_X(a), 0, 0);
        if (w->in) *G_PLAYERVX = w->vx;
        if (*G_PRIDINGPLATFORM == a) *G_PRIDINGPLATFORM = NULL;
        flog("wagon at its end x %d (vx %d/65536): %s", w->end, w->vx,
             w->in ? (w->deadly && on ? "Lomax is thrown out, hurt" : "Lomax is thrown out") : "empty");
        if (g_in == i) g_in = -1;
        w->type = NULL;
        if (seen(a)) actor_break_up(a);             /* (0x23B30: +6 bit 4, drawn last frame) */
        else Actor_Despawn(a);                   /* (0x32D8E: its placement spawns it again) */
        return;
    }

    k = abs(w->vx) / 1000;                       /* the animation (0x23B46) */
    *(short *)(a + 0x68) = (short)(k > ANIM_MAX ? ANIM_MAX : k);
    Actor_AdvanceAnim(a);

    if (w->vy >= 0 && w->vx && !w->vy && !w->rolling) {   /* the rolling sound (0x23B76) */
        w->rolling = 1;
        w->sound = seen(a);
    }
    g = w->vy >= 0 ? (int)Plane_FindGroundY(PLANE(A_PLANE(a)), A_X(a), A_Y(a), GROUND_CELLS) : NO_GROUND;
    k = w->vx >> 16;
    if (g != NO_GROUND && abs(g - A_Y(a)) <= STICK[(k < -8 ? -8 : k > 7 ? 7 : k) + 8]) {   /* 0x1AE96 */
        place_y(a, g);
        w->vy = 0;
        if (!*G_GROUNDSLOPECLASS) {              /* level (0x23BC8) */
            w->vx = toward_zero(w->vx, FLAT_DECAY);
            set_anim(a, "MAIN");
        } else if (*G_GROUNDMIRRORED) {          /* falling to the left (0x23BF0) */
            set_anim(a, "DREI");
            if (w->vx >= 0) w->vx -= UPHILL;
            else if (w->vx > -w->top) w->vx -= DOWNHILL;
        } else {                                 /* falling to the right (0x23C2C) */
            set_anim(a, "ZWEI");
            if (w->vx < 0) w->vx += UPHILL;
            else if (w->vx < w->top) w->vx += DOWNHILL;
        }
    } else {                                     /* in the air (0x23C64) */
        w->vy += WAGON_GRAVITY;
        w->in = 0;
        w->rolling = w->sound = 0;               /* (0x23C70: Pcm_StopEffect) */
    }
    if (w->sound) sfx_loop(a, 0x38);

    air = (*G_PLAYERSTATE & PS_AIR) != 0;        /* (0x23C8C) */
    if (*G_PLAYERDEATHTIMER) { if (w->in) get_out(w); }
    else if (!air && !w->stood) {
        w->riding = 0;
        if (w->in) get_out(w);
    }

    /* 0x1A29A doesn't stand a rising player on it (0x1A2DC: vy > 0 up), and Flink's Player_Update
       clears 0x82FE in the air. Lomax's carry moves a player it still rides with (g_pRidingPlatform)
       before its landing test, which held his feet on the Wagon for good after a jump: once he rises
       (after his jump's wind-up, g_JumpTakeoffTimer, which stands him on the Wagon), he isn't carried
       and no longer rides it */
    rising = (*G_PLAYERSTATE & PS_AIR) && *G_PLAYERVY < 0 && !*G_JUMPTAKEOFFTIMER;
    if (rising && *G_PRIDINGPLATFORM == a) *G_PRIDINGPLATFORM = NULL;
    if (!w->riding || rising) {                  /* (0x23CC8) */
        A_FX(a) += w->vx;
        A_FY(a) += w->vy;
        set_pos(a);
        w->stood = 0;
    } else {                                     /* (0x23CE8: Platform_Stand) */
        prev = w->stood;
        A_VX(a) = w->vx == 1 ? 0 : w->vx;
        A_VY(a) = w->vy;
        w->stood = Platform_MoveAndCarryPlayer(a, wagon_surface);
        A_VX(a) = A_VY(a) = 0;
        if (w->stood) {
            if (!prev && !w->vx) {               /* landed in it at rest: it goes (0x23D34) */
                A_X(a) = *G_PLAYERX;
                ((short *)&A_FX(a))[1] = (short)A_X(a);
                w->vx = w->start;
                *G_PLAYERVX = 0;
                flog("Lomax lands in the wagon at (%d,%d): it goes at %d/65536 to x %d", A_X(a), A_Y(a), w->vx, w->end);
            }
            w->in = 1;
        }
    }

    if (w->in) {                                 /* (0x23D58) */
        WORD held = *G_INPUTHELD;
        set_player_x(A_X(a));
        if (held & IN_LEFT && w->vx > BRAKE_MIN) w->vx -= PEDAL;
        if (held & IN_RIGHT && w->vx < w->top) w->vx += PEDAL;
        g_in = i;
    } else if (g_in == i) g_in = -1;

    if (A_Y(a) - ARM_HEIGHT >= *G_PLAYERY)       /* (0x23DA8) */
        w->riding = abs(A_X(a) - *G_PLAYERX) <= ARM_SPAN && *G_PLAYERPLANE == A_PLANE(a);

    if (below_map(a)) { if (g_in == i) g_in = -1; w->type = NULL; Actor_Despawn(a); }
}

/* before Player_Update (player_tick): in a Wagon, Left and Right are its (0x23D68: bclr 0x870A) */
void wagon_before(void)
{
    BYTE *a = actor_at(g_in);
    Wagon *w = a ? wagon_of(a) : NULL;
    WORD mask = IN_LEFT | IN_RIGHT;
    if (!g_active || !w || !w->in || !actor_active(g_in)) { g_in = -1; return; }    M.held = *G_INPUTHELD & mask; M.pressed = *G_INPUTPRESSED & mask;
    *G_INPUTHELD &= (WORD)~mask; *G_INPUTPRESSED &= (WORD)~mask;
    M.masked = 1;
}

void wagon_after(void)
{
    if (M.masked) { *G_INPUTHELD |= M.held; *G_INPUTPRESSED |= M.pressed; M.masked = 0; }
}

/* 0x23A4C (from mega_spawn); params: Lomax's 5, start speed, top speed, distance, deadly */
void wagon_spawn(BYTE *a, BYTE *def, int i)
{
    Wagon *w = &g_wagon[i];
    memset(w, 0, sizeof(*w));
    w->type = A_TYPE(a);
    w->start = def_param(a, def, 5, 0) * WAGON_UNIT;
    w->top = def_param(a, def, 6, 0) * WAGON_UNIT;
    w->end = A_X(a) + def_param(a, def, 7, 0);
    w->deadly = def_param(a, def, 8, 0) != 0;
    A_FX(a) = A_X(a) << 16;
    A_FY(a) = A_Y(a) << 16;
    A_VX(a) = A_VY(a) = 0;
    A_LAYER(a) = (short)FLINK_LAYER(1);          /* (a5) = 0x7D6C: list 1 */
    A_UPDATE(a) = (void *)wagon_update;
    A_DRAW(a) = (void *)wagon_draw;
    set_anim(a, "MAIN");
    flog("wagon at (%d,%d): start %d/65536, top %d/65536, end x %d%s", A_X(a), A_Y(a), w->start, w->top,
         w->end, w->deadly ? ", deadly" : "");
}
