/*
 * metaboss.c - Flink's Meta Boss (actor 'Boss', init 0x2948C -> 0x2A540 for META, thinks 0x2A5E8,
 * 0x2A616 and 0x2A656, draw 0x2ACB8), META's boss in 31-2 (META map 6; README "Enemies" (The Meta
 * Boss)): a cloud monster that rises out of the ground, shoots lightning from its eyes, claps its
 * hands together in the middle and pounds the ground, a cloud rising at each pound, on which Lomax
 * rides up to its head. The converter makes it a 'Muta MetaBoss n' type (mboss_records): EINS, ZWEI,
 * DREI and VIER with their arms (selector 1 pieces), BEIN, BZWE, BDRE and BVIE with their body and
 * head (the others), ZAP1, ZAP2, ZAP3 and WOLK; params 0, the fight's music and the music at its
 * defeat (Lomax Music params). Its placement links to the arena's Finish (placement +4).
 *
 * Flink's boss:
 * - Init (0x2A540): hits 0x8286 (8 at each level start, 0x14776; 1 if 0); a child (0x32CD8), frozen,
 *   the lightning (+0x5A), whose +0x4C is the boss's y + 0x2E: where it strikes; draw list 3, window
 *   0x18, hidden (+3 bit 1); y + 10, home y +0x54; +0x5C 100, +0x5E 0x100; EINS. Its draw hook 0x2ACB8
 *   gives the pieces with selector 1 the sprite priority bit and the others none; 31-2's Priority
 *   actor (0x255E8) gives all the map's tiles the bit, so the body and head are drawn behind the play
 *   map's tiles and the arms in front. Its attach points with hook 1 (0x2AB5A) are its eyes.
 * - Waiting (0x2A5E8): once Flink's x + 140 is within 16 px of its x, the boss music (index 8) and:
 * - Rising (0x2A616), shown: +0x5E - 4 a frame (64 frames), y = home + (0x4000 - sin(0x100 - +0x5E))
 *   * 0x230 >> 16 (140 px under it at first, the table 0x1491E: 1024 a turn), and it moves (below).
 * - Each frame then (0x2A656), unless hurt (+0x52):
 *   - DREI running (+0x4C 22 down to -1): frame 11 - |t - 11|; at 11 (its hands on the ground) sound
 *     0x35, the shake 0x23 (0x874E) and a cloud (0x230F0, the Wolky platform's init: draw list 2, vy
 *     -0.25, rising 0x200 a frame faster: +0x4A) at x + +0x58 - 64, home + 32; +0x58 + 30 + rand & 15,
 *     & 127. At its end ZWEI and the wait +0x4E 20, or with no pound left (+0x50) 100 + rand & 63.
 *   - VIER running (+0x4C 76 down to -1): frame 23 - min(|t - 23|, 23) (its hands wide 30 frames,
 *     then clapping together and opening again); at 23 sound 0x3E and VIER's second box (x - 30 .. x
 *     + 59, y - 3 .. y + 47) against Flink (0x1A46E: Player_TakeHit). At its end ZWEI, the wait 60 +
 *     rand & 63.
 *   - Waiting (+0x4E): - 1; with pounds left nothing more, else the lightning and the move (below).
 *   - Pounds left: - 1 and DREI again.
 *   - Otherwise, the lightning idle (its child frozen, +0x4A 0): Flink more than 60 px above its y:
 *     the wait 30, the lightning and the move; Flink within 12 px of its face (x + 16): vx 0, and one
 *     time in 2 DREI with 2 more pounds (+0x58 = rand & 127), otherwise VIER (+0x5C 60 + rand & 31).
 *     Else the lightning and the move.
 *   - The lightning (0x2A75A): only with Flink no more than 60 px above its y: +0x5C - 1, and at 0
 *     with the lightning idle it charges (+0x4A 60) and +0x5C is 80 + rand & 63.
 *   - The move (0x2AADC): toward Flink's x (kept 90 .. 590) with its face (x + 16): the speed
 *     (16.16) goes toward (that - face) / 64 px by 0x1000 a frame (0x3000 turning), and x + vx.
 * - Hurt (+0x52, 0x2A8E2): no attack or move. Defeated, Flink can't be hurt (hurt timer -2), its home
 *   y + 1 every other frame (it sinks 96 px), every 32 frames a spirit (0x2EDAA at y - 100 - rand
 *   & 31), a flash and sound 0x1A. Then - 1: at 0 back at its home y, or defeated, its end; odd, y =
 *   home + (rand & 7) - 4 (a shake), and not defeated three colours of the palette brighter (0x8B92).
 * - Stomped (0x2AA3C; unless +0x56 runs, which counts down): Actor_CheckStompedByPlayer (0x1A352) on
 *   its hitbox, the top of its head (y - 100 .. - 89): sound 0x2B, a spirit at y - 100; unless Flink
 *   is dying: ZWEI, the shake 0x23, the wait 100, no pound left, hurt 60, +0x56 120, hits - 1; at 0
 *   +0x56 1000, EINS, hurt 193, Flink's hurt timer -2 and the music at its defeat (index 21, 0x29A7C).
 * - Its end (0x2A9E8): no eyes (+8 = 0x30C4A), its crystal (Item_Spawn 0x1A at y - 100), ZWEI, +2
 *   bit 1 (the break-up leaves the selector 2 pieces out), sound 0x53 and the break-up (0x1B202, +3
 *   bit 5: no sound 0x2A; +3 bit 7 clear: up at 5.0 + (rand & 31) / 8): only its arms fly off.
 * - The lightning: charging, the boss's hook draws ZAP1 at both eyes (stepping it once a frame) and
 *   counts +0x4A down; at 0 it is shot from between the eyes (x + 13, y - 79): ZAP2, sound 0x3C, draw
 *   list 2, think 0x2AC0A: toward (Flink's x, +0x4C) by Actor_MoveToward (0x1B008: 1.0 a frame, at
 *   most 4.0); there sound 0x2D, a flash, ZAP3 once (+3 bit 0), draw list 1, think 0x2AC9C: its
 *   hitbox against Flink (0x1A462) every frame until ZAP3 has played, then frozen again.
 * - Its clouds are the Wolky platform's think (0x23120, wolky.c), vy - 0x200 each frame: gone at y
 *   -30 or above, swaying, carrying Flink, sinking 20 frames when landed on.
 *
 * Here: the same; Lomax's stomp is Flink's test (flink_stomp_box), with his bounce; a thrown object
 * touching its head and body (MB_BODY_*: mboss_struck, from grab.c's strike) and Lomax's spin, roll or
 * helmet reaching them (player_attack_box) are hits too, which Flink's think doesn't ask for. No
 * spells but HELMET from its rise on (boss_fighting, forestboss.c). The body and head are drawn behind the play map's
 * tiles by a stand-in actor (a copy of the boss's struct, its BEIN / BZWE / BDRE / BVIE frame) on
 * the background plane, at the boss's place on the screen: Actors on a plane go into its buckets, the
 * background plane's under the play plane's tiles; through Actor_Draw, Lomax+'s FPS Override moves
 * it with the boss between ticks. While it dies Lomax can't be hurt (boss_untouchable, forestboss.c);
 * the shake is Flink's counter (razorback.c's flink_quake); the fanfare is played once
 * (music_play_once). Not ported: the flashes, the palette's hit flash and the crystal: 100 ticks
 * after the break-up the arena's Finish appears with a poof (boss_finish_later), in the middle of the
 * map. A defeated boss stays gone for the level visit (gone.c): the arena then has the Finish from
 * the start. Sounds: Flink's 0x35, 0x3E, 0x3C, 0x2D, 0x2B, 0x1A and 0x53 (sfx.c).
 */
#include <math.h>
#include "enemy.h"

#define MB_HITS            8             /* 0x14776: 0x8286 */
#define MB_DY              10            /* 0x2A5A4: its y */
#define MB_STRIKE_DY       0x2E          /* 0x2A56E: the lightning's ground, from the placement's y */
#define MB_NEAR            0x8C          /* 0x2A5EE: Flink's x + 140 ... */
#define MB_NEAR_W          16            /*   within 16 px of its x */
#define MB_RISE            0x100         /* +0x5E */
#define MB_RISE_STEP       4
#define MB_RISE_SCALE      0x230         /* 0x2A644: 140 px at most */
#define MB_FACE_DX         16            /* 0x2A66A */
#define MB_REACH           12            /* 0x2A6F2 */
#define MB_HIGH            60            /* 0x2A6D2 */
#define MB_HIGH_WAIT       30
#define MB_POUND_T         0x16          /* DREI: +0x4C from 22 ... */
#define MB_POUND_AT        11            /*   the hands on the ground */
#define MB_POUNDS          2
#define MB_POUND_WAIT      0x14
#define MB_CLAP_T          0x4C          /* VIER: +0x4C from 76 ... */
#define MB_CLAP_AT         0x17          /*   the hands together */
#define CLAP_X1            (-30)         /* VIER's second box (the converter's MBOSS_CLAP_BOX) */
#define CLAP_Y1            (-3)
#define CLAP_X2            59
#define CLAP_Y2            47
#define MB_CLOUD_DX        0x40          /* 0x2A7E2 */
#define MB_CLOUD_DY        0x20          /* 0x2A7EA */
#define MB_CLOUD_VY        0x4000        /* 0x2A804 */
#define MB_CLOUD_ACC       0x200         /* 0x2A80C */
#define MB_X_MIN           0x5A          /* 0x2AAEE: Flink's x kept 90 .. 590 */
#define MB_X_MAX           0x24E
#define MB_ACC             0x1000
#define MB_TURN            0x2000
#define MB_SPIRIT_DY       100           /* 0x2A928, 0x2AA5A */
#define MB_QUAKE           0x23
#define MB_HURT            60            /* 0x2AAA2 */
#define MB_GUARD           120
#define MB_HIT_WAIT        100
#define MB_DYING           0xC1
#define MB_DEAD_GUARD      1000
#define MB_ZAP_FIRST       100           /* 0x2A5C0: +0x5C */
#define MB_BREAK_UP        0x40000       /* 0x1B344: + 4.0 up */
#define MB_FINISH_WAIT     100           /* ticks from the break-up to the Finish */
#define MB_BODY_X1         (-24)         /* the box thrown objects and Lomax's attacks hit: its head and */
#define MB_BODY_X2         56            /*   body above the floor, from its x, y (BZWE's drawn pixels) */
#define MB_BODY_Y1         (-100)        /*   (its hitbox, the head's top, is the stomp's) */
#define MB_BODY_Y2         (-20)
#define EYE1_DX            3             /* the eyes (the converter's MBOSS_EYES) */
#define EYE2_DX            24
#define EYE_DY             (-79)
#define ZAP_CHARGE         0x3C          /* 0x2A792 */
#define ZAP_MAX            0x40000       /* 0x2AC14 */
#define ZAP_ACC            0x10000
#define CLOUD_TOP          (-30)         /* 0x23120 */
#define CLOUD_NEAR         128           /* Flink's mid window (8) */
#define BG_PLANE           2             /* the converter's background plane */
#define REC_SPEED(r)       (*(const short *)((r) + 0xC))   /* a loaded record's speed (8.8 frames a tick) */

enum { MB_WAIT, MB_RISING, MB_FIGHT, MB_BROKEN };
enum { AN_EINS, AN_ZWEI, AN_DREI, AN_VIER };
enum { Z_IDLE, Z_CHARGE, Z_BALL, Z_STRIKE };
enum { K_NONE, K_ZAP, K_CLOUD };

static const char *const FRONT[] = { "EINS", "ZWEI", "DREI", "VIER" };
static const char *const BACK[] = { "BEIN", "BZWE", "BDRE", "BVIE" };

static struct {
    BYTE *a, *type, *finish, *zapa;   /* (zapa: the lightning, a child) */
    int music, win;
    int hits;                            /* 0x8286 */
    int state, anim, frame;
    int rise;                            /* +0x5E */
    int home;                            /* +0x54 */
    int vx;                              /* +0x1C */
    int ground;                          /* the lightning's +0x4C */
    int t;                               /* +0x4C: DREI's or VIER's count */
    int wait;                            /* +0x4E */
    int pounds;                          /* +0x50 */
    int hurt;                            /* +0x52 */
    int guard;                           /* +0x56 */
    int cloudx;                          /* +0x58 */
    int zap_wait;                        /* +0x5C */
    int zs, charge, zt;                  /* the lightning's state (Z_*), +0x4A, ZAP3's frame (8.8) */
    int struck;                          /* a thrown object touched its body (mboss_struck) */
} M;

/* the children (by slot): the lightning and the clouds (phase, dip count, vy as wolky.c's) */
static struct { BYTE *type; int kind, phase, dip, vy; } g_kid[MAX_ACTORS];
static BYTE g_back[ACTOR_STRIDE];        /* the stand-in drawing the body behind the tiles */
static short g_sine[1024];               /* Flink's table 0x1491E: round(0x4000 * sin) */

static int mb_rand(void) { static unsigned r = 0x51F3A2C7u; r = r * 1103515245u + 12345u; return (int)(r >> 16) & 0x7FFF; }

void metaboss_reset(void)
{
    int k;
    memset(&M, 0, sizeof(M));
    memset(g_kid, 0, sizeof(g_kid));
    if (!g_sine[256])
        for (k = 0; k < 1024; k++) g_sine[k] = (short)floor(16384.0 * sin(k * 3.14159265358979 / 512) + 0.5);
}

static int is_boss(const BYTE *a) { return a && a == M.a && M.type && A_TYPE(a) == M.type; }

static void show(BYTE *a, int anim, int frame)
{
    M.anim = anim;
    M.frame = frame;
    set_anim(a, FRONT[anim]);
    hold_frame(a, frame);
    Actor_AdvanceAnim(a);
}

/* Actor_MoveToward (0x1B008) on one axis, as blitz.c's */
static void move_toward(int *v, int *pos, int target, int max, int acc)
{
    int p = (short)(*pos >> 16);
    if (target == p && abs(*v) <= acc) *v = 0;
    else if (target >= p) { if (*v < max) *v += *v < 0 ? 2 * acc : acc; }
    else if (-*v < max) *v -= *v > 0 ? 2 * acc : acc;
    *pos += *v;
}

/* 0x1A46E: the box (inclusive) against Lomax's hitbox: Player_TakeHit, unless he is hurt */
static int hurt_lomax(const BYTE *a, int x1, int y1, int x2, int y2, const char *what)
{
    const BYTE *pa = *G_PLAYERANIM;
    int px = *G_PLAYERX, py = *G_PLAYERY;
    if (!pa || A_PLANE(a) != *G_PLAYERPLANE || *G_PLAYERDEATHTIMER || *G_PLAYERHURTTIMER) return 0;
    if (x2 < px + HB_X1(pa) || x1 > px + HB_X2(pa) || y2 < py + HB_Y1(pa) || y1 > py + HB_Y2(pa)) return 0;
    flog("meta boss's %s hurts Lomax at (%d,%d)", what, px, py);
    Player_TakeHit(A_X(a), 0, 0);
    return 1;
}

/* ---- The children ------------------------------------------------------------------------------- */

static int kid_of(const BYTE *d, int kind)
{
    int k = slot_of(d);
    return k >= 0 && g_kid[k].kind == kind && g_kid[k].type == A_TYPE(d) && A_LAYER(d) != KILLED_LAYER ? k : -1;
}

static void kid_free(BYTE *d, int k)
{
    g_kid[k].kind = K_NONE;
    if (*G_PRIDINGPLATFORM == d) *G_PRIDINGPLATFORM = NULL;
    Actor_Despawn(d);
}

static void __cdecl plain_draw(BYTE *d) { draw_actor(d); }

static BYTE *kid_new(int kind, const char *tag, int layer)
{
    BYTE *d = Actor_Alloc(M.a, NULL);
    int k;
    if (!d || (k = slot_of(d)) < 0) return NULL;
    memset(&g_kid[k], 0, sizeof(g_kid[k]));
    g_enemy[k].type = NULL;                  /* (not an enemy) */
    g_kid[k].kind = kind;
    g_kid[k].type = A_TYPE(d);
    A_FLAGS(d) = 0;
    A_CULL(d) = CULL_NEVER;
    A_LAYER(d) = (short)layer;
    A_VX(d) = A_VY(d) = 0;
    A_DRAW(d) = (void *)plain_draw;
    A_RECORD(d) = NULL;                      /* (Actor_Alloc leaves the slot's last one) */
    Actor_SetAnim(d, FOURCC(tag));
    if (!A_RECORD(d)) { g_kid[k].kind = K_NONE; Actor_Despawn(d); return NULL; }
    return d;
}

/* the clouds: the Wolky platform's think (0x23120), rising ever faster */
static int near_view(const BYTE *a, int margin)
{
    const BYTE *pl = PLANE(A_PLANE(a));
    int w = P_W(pl) * 16 - P_MAXX(pl), x = A_X(a) - P_CAMX(pl), y = A_Y(a) - P_CAMY(pl);
    if (P_MAXX(pl) <= 0 || w < 320) w = 320;
    return x >= -margin && x < w + margin && y >= -margin && y < 240 + margin;
}

static void __cdecl cloud_update(BYTE *c)
{
    int k = kid_of(c, K_CLOUD);
    if (k < 0) return;
    if (A_Y(c) <= CLOUD_TOP || !near_view(c, CLOUD_NEAR)) { kid_free(c, k); return; }
    g_kid[k].vy -= MB_CLOUD_ACC;
    set_anim(c, "WOLK");
    wolky_step(c, &g_kid[k].phase, &g_kid[k].dip, &g_kid[k].vy);
}

static void cloud_spawn(int x, int y)
{
    BYTE *c = kid_new(K_CLOUD, "WOLK", FLINK_LAYER(2));
    if (!c) return;
    g_kid[slot_of(c)].vy = -MB_CLOUD_VY;
    A_FX(c) = x << 16;
    A_FY(c) = y << 16;
    set_pos(c);
    A_UPDATE(c) = (void *)cloud_update;
    flog("meta boss pounds the ground: a cloud at (%d,%d)", x, y);
}

/* the lightning: shot (0x2AC0A) and striking (0x2AC9C); charging and idle it is the boss's */
static void __cdecl zap_update(BYTE *d)
{
    int x1, y1, x2, y2;
    if (kid_of(d, K_ZAP) < 0 || d != M.zapa) return;
    if (M.zs == Z_BALL) {
        move_toward(&A_VX(d), &A_FX(d), *G_PLAYERX, ZAP_MAX, ZAP_ACC);
        move_toward(&A_VY(d), &A_FY(d), M.ground, ZAP_MAX, ZAP_ACC);
        set_pos(d);
        if (A_Y(d) >= M.ground) {
            flink_sound(d, 0x2D);
            place_y(d, M.ground);
            A_VX(d) = A_VY(d) = 0;
            M.zs = Z_STRIKE;
            A_LAYER(d) = FLINK_LAYER(1);     /* (a5) = 0x7D6C */
            Actor_SetAnim(d, FOURCC("ZAP3"));
            M.zt = A_RECORD(d) ? REC_SPEED(A_RECORD(d)) : 0;   /* (stepped once: 0x2AC96) */
            hold_frame(d, M.zt >> 8);
            flog("meta boss's lightning strikes at (%d,%d)", A_X(d), A_Y(d));
        }
        Actor_AdvanceAnim(d);
        return;
    }
    if (M.zs != Z_STRIKE) return;
    if (actor_box(d, &x1, &y1, &x2, &y2)) hurt_lomax(d, x1, y1, x2, y2, "lightning");
    M.zt += A_RECORD(d) ? REC_SPEED(A_RECORD(d)) : 0x100;
    if (M.zt >> 8 >= A_NFRAMES(d)) {         /* played once: frozen again */
        M.zs = Z_IDLE;
        A_FLAGS(d) |= AF_HIDDEN;
        return;
    }
    hold_frame(d, M.zt >> 8);
    Actor_AdvanceAnim(d);
}

static void zap_draw_at(BYTE *d, int x, int y)
{
    A_X(d) = x; A_Y(d) = y;
    A_FX(d) = x << 16; A_FY(d) = y << 16;
    draw_actor(d);
}

/* shot or striking: its own draw (charging, the boss draws it at its eyes) */
static void __cdecl zap_draw(BYTE *d)
{
    if (kid_of(d, K_ZAP) >= 0 && d == M.zapa && (M.zs == Z_BALL || M.zs == Z_STRIKE)) draw_actor(d);
}

/* charging (the hook 0x2AB5A, once a frame): ZAP1 stepped, the count; at 0 it is shot from between
   the eyes */
static void zap_charge(BYTE *a)
{
    BYTE *d = M.zapa;
    if (M.zs != Z_CHARGE || !d || kid_of(d, K_ZAP) < 0) return;
    Actor_AdvanceAnim(d);
    if (--M.charge > 0) return;
    M.zs = Z_BALL;
    A_FX(d) = (A_X(a) + (EYE1_DX + EYE2_DX) / 2) << 16;
    A_FY(d) = (A_Y(a) + EYE_DY) << 16;
    set_pos(d);
    A_VX(d) = A_VY(d) = 0;
    A_LAYER(d) = FLINK_LAYER(2);             /* (a5) = 0x7D6E */
    A_FLAGS(d) &= ~AF_HIDDEN;
    Actor_SetAnim(d, FOURCC("ZAP2"));
    flink_sound(d, 0x3C);
    flog("meta boss shoots its lightning from (%d,%d)", A_X(d), A_Y(d));
}

/* ---- The fight ---------------------------------------------------------------------------------- */

/* 0x2AADC: its face (x + 16) toward Flink's x, kept 90 .. 590 */
static void mb_move(BYTE *a)
{
    int face = A_X(a) + MB_FACE_DX, v = M.vx, to = *G_PLAYERX;
    if (to < MB_X_MIN) to = MB_X_MIN;
    if (to > MB_X_MAX) to = MB_X_MAX;
    to = (int)(short)(to - face) * 0x400;    /* (the px << 16 >> 6) */
    if (v < to) {
        if (to < 0) v += MB_TURN;
        v += MB_ACC;
        if (v >= to) v = to;
    } else if (v > to) {
        if (to >= 0) v -= MB_TURN;
        v -= MB_ACC;
        if (v <= to) v = to;
    }
    M.vx = v;
    A_FX(a) += v;
    set_pos(a);
}

/* 0x2A75A: the lightning charges on a timer while Flink is low, and the move */
static void lightning_move(BYTE *a)
{
    if (A_Y(a) - *G_PLAYERY < MB_HIGH) {
        if (M.zap_wait) M.zap_wait--;
        else if (M.zs == Z_IDLE && M.zapa) {
            M.zs = Z_CHARGE;
            M.charge = ZAP_CHARGE;
            Actor_SetAnim(M.zapa, FOURCC("ZAP1"));
            M.zap_wait = 0x50 + (mb_rand() & 0x3F);
            flog("meta boss charges its lightning");
        }
    }
    mb_move(a);
}

/* DREI (0x2A7B2): the hands down at 11, a cloud rising there */
static void pound(BYTE *a)
{
    int d = abs(M.t - MB_POUND_AT);
    if (!d) {
        flink_sound(a, 0x35);
        flink_quake_set(MB_QUAKE);
        cloud_spawn(M.cloudx + A_X(a) - MB_CLOUD_DX, M.home + MB_CLOUD_DY);
        M.cloudx = (M.cloudx + 0x1E + (mb_rand() & 0xF)) & 0x7F;
    }
    show(a, AN_DREI, MB_POUND_AT - d);
    if (--M.t >= 0) return;
    show(a, AN_ZWEI, 0);
    M.wait = M.pounds ? MB_POUND_WAIT : 0x64 + (mb_rand() & 0x3F);
}

static void pound_start(BYTE *a)
{
    M.t = MB_POUND_T;
    pound(a);
}

/* VIER (0x2A870): the hands together at 23, hurting in its second box */
static void clap(BYTE *a)
{
    int d = abs(M.t - MB_CLAP_AT);
    if (!d) {
        flink_sound(a, 0x3E);
        hurt_lomax(a, A_X(a) + CLAP_X1, A_Y(a) + CLAP_Y1, A_X(a) + CLAP_X2, A_Y(a) + CLAP_Y2, "clap");
    }
    if (d > MB_CLAP_AT) d = MB_CLAP_AT;
    show(a, AN_VIER, MB_CLAP_AT - d);
    if (--M.t >= 0) return;
    show(a, AN_ZWEI, 0);
    M.wait = 0x3C + (mb_rand() & 0x3F);
}

/* 0x2A656 up to 0x2A8E2: the attacks and the move, unless hurt */
static void attack(BYTE *a)
{
    if (M.anim == AN_VIER) { clap(a); return; }
    if (M.anim == AN_DREI) { pound(a); return; }
    if (M.wait) {
        M.wait--;
        if (!M.pounds) lightning_move(a);
        return;
    }
    if (M.pounds) { M.pounds--; pound_start(a); return; }
    if (M.zs != Z_IDLE) { lightning_move(a); return; }
    if (A_Y(a) - *G_PLAYERY > MB_HIGH) {     /* Flink up high: it waits, going under him */
        M.wait = MB_HIGH_WAIT;
        lightning_move(a);
        return;
    }
    if (abs(A_X(a) + MB_FACE_DX - *G_PLAYERX) > MB_REACH) { lightning_move(a); return; }
    M.vx = 0;
    if (mb_rand() & 1) {
        M.cloudx = mb_rand() & 0x7F;
        M.pounds = MB_POUNDS;
        flog("meta boss pounds the ground at x %d", A_X(a));
        pound_start(a);
    } else {
        flog("meta boss claps at x %d", A_X(a));
        M.t = MB_CLAP_T;
        M.zap_wait = 0x3C + (mb_rand() & 0x1F);
        clap(a);
    }
}

/* 0x2A9E8: its arms break off and fly away, then the Finish */
static void mb_die(BYTE *a)
{
    flog("meta boss at (%d,%d) breaks up", A_X(a), A_Y(a));
    M.state = MB_BROKEN;
    forget_placement(A_DEF(a));
    show(a, AN_ZWEI, 0);                     /* (its arms: the front pieces) */
    enemy_defeated(a);
    boss_finish_later(a, M.finish, MB_FINISH_WAIT);
    flink_sound(a, 0x53);
    actor_break_up_vy(a, 0, MB_BREAK_UP);
}

/* 0x2A8E2: hurt or dying; 1 if it broke up */
static int hurt_tick(BYTE *a)
{
    if (!M.hurt) return 0;
    if (M.hits <= 0) {
        boss_untouchable();                  /* 0x2A8F4: hurt timer -2 */
        if (M.hurt & 1) M.home++;
        if (!(M.hurt & 0x1F)) {
            int dy = mb_rand() & 0x1F;
            spirit_release(a, A_X(a), A_Y(a) - dy - MB_SPIRIT_DY);
            flink_sound(a, 0x1A);
        }
    }
    if (--M.hurt == 0) {
        if (M.hits > 0) { place_y(a, M.home); return 0; }
        mb_die(a);
        return 1;
    }
    if (M.hurt & 1) place_y(a, M.home + (mb_rand() & 7) - 4);
    return 0;
}

/* Flink's Actor_CheckStompedByPlayer (0x1A352) on its hitbox, the top of its head */
static int stomped(BYTE *a)
{
    int x1, y1, x2, y2;
    return actor_box(a, &x1, &y1, &x2, &y2) && flink_stomp_box(A_PLANE(a), x1, y1, x2, y2);
}

/* its head and body (MB_BODY_*) */
static void body_box(const BYTE *a, int *x1, int *y1, int *x2, int *y2)
{
    *x1 = A_X(a) + MB_BODY_X1; *x2 = A_X(a) + MB_BODY_X2;
    *y1 = A_Y(a) + MB_BODY_Y1; *y2 = A_Y(a) + MB_BODY_Y2;
}

/* Lomax's spin, roll or helmet reaches its body */
static int attacked(BYTE *a)
{
    int x1, y1, x2, y2;
    body_box(a, &x1, &y1, &x2, &y2);
    return player_attack_box(A_PLANE(a), x1, y1, x2, y2);
}

/* 0x2AA3C: a stomp, and here also a thrown object (mboss_struck) or Lomax's spin, roll or helmet */
static void stomp_check(BYTE *a)
{
    int struck = M.struck;
    M.struck = 0;
    if (M.guard) { M.guard--; return; }
    if (stomped(a)) {
        Player_BounceOffEnemy();             /* (Flink: g_PlayerBounceReq) */
        stomp_bounce();
    } else if (!struck && !attacked(a)) return;
    flink_sound(a, 0x2B);
    spirit_release(a, A_X(a), A_Y(a) - MB_SPIRIT_DY);
    if (*G_PLAYERDEATHTIMER) return;
    show(a, AN_ZWEI, 0);
    flink_quake_set(MB_QUAKE);
    M.wait = MB_HIT_WAIT;
    M.pounds = 0;
    M.hurt = MB_HURT;
    M.guard = MB_GUARD;
    if (--M.hits > 0) { flog("meta boss hit, %d left", M.hits); return; }
    M.guard = MB_DEAD_GUARD;
    show(a, AN_EINS, 0);
    M.hurt = MB_DYING;
    boss_untouchable();
    music_play_once(M.win);                  /* (a fanfare: Lomax would loop it) */
    flog("meta boss defeated");
}

static void __cdecl mboss_update(BYTE *a)
{
    if (!is_boss(a)) return;
    if (M.state == MB_WAIT) {                /* 0x2A5E8 */
        if (A_PLANE(a) != *G_PLAYERPLANE || abs(*G_PLAYERX + MB_NEAR - A_X(a)) > MB_NEAR_W) return;
        M.state = MB_RISING;
        music_play(M.music);
        flog("meta boss rises at x %d", A_X(a));
        return;
    }
    if (M.state == MB_RISING) {              /* 0x2A616 */
        boss_fighting();
        A_FLAGS(a) &= ~AF_HIDDEN;
        M.rise -= MB_RISE_STEP;
        if (!M.rise) M.state = MB_FIGHT;
        place_y(a, M.home + (((0x4000 - g_sine[MB_RISE - M.rise]) * MB_RISE_SCALE) >> 16));
        mb_move(a);
        return;
    }
    if (M.state == MB_BROKEN) return;
    boss_fighting();                         /* (only HELMET: spells.c) */
    if (M.state != MB_FIGHT) return;
    if (!M.hurt) attack(a);
    if (hurt_tick(a)) return;
    stomp_check(a);
    zap_charge(a);                           /* (its draw's hook, 0x2AB5A) */
}

/* the stand-in on the background plane: the body and head behind the play map's tiles */
static void back_draw(BYTE *a)
{
    BYTE *s = g_back, *pl = PLANE(A_PLANE(a)), *bg = PLANE(BG_PLANE);
    memcpy(s, a, ACTOR_STRIDE);
    A_FLAGS(s) &= ~AF_HIDDEN;
    A_HIDE(s) = 0;
    *(int *)(s + 0x78) = 0;                  /* (Actor_Draw's blink counter) */
    Actor_SetAnim(s, FOURCC(BACK[M.anim]));
    hold_frame(s, M.frame);
    Actor_AdvanceAnim(s);
    A_PLANE(s) = BG_PLANE;
    A_X(s) = A_X(a) - P_CAMX(pl) + P_CAMX(bg);
    A_Y(s) = A_Y(a) - P_CAMY(pl) + P_CAMY(bg);
    A_FX(s) = A_X(s) << 16;
    A_FY(s) = A_Y(s) << 16;
    draw_actor(s);
}

/* 0x2ACB8: the lightning charging at its eyes (their attach points come first: in front), its
   arms, its body */
static void __cdecl mboss_draw(BYTE *a)
{
    BYTE *d = M.zapa;
    if (!is_boss(a) || M.state == MB_BROKEN || A_FLAGS(a) & AF_HIDDEN) return;
    if (M.zs == Z_CHARGE && d && kid_of(d, K_ZAP) >= 0) {
        zap_draw_at(d, A_X(a) + EYE1_DX, A_Y(a) + EYE_DY);
        zap_draw_at(d, A_X(a) + EYE2_DX, A_Y(a) + EYE_DY);
    }
    draw_actor(a);
    back_draw(a);
}

/* grab.c's strike: thrown object o's box touched t; 1 if t is the boss fighting and not guarded: a hit
   at its next think (Flink's think asks only for a stomp) */
int mboss_struck(BYTE *t, int x1, int y1, int x2, int y2)
{
    int tx1, ty1, tx2, ty2;
    if (!is_boss(t) || M.state != MB_FIGHT || M.guard || M.hurt) return 0;
    body_box(t, &tx1, &ty1, &tx2, &ty2);
    if (x2 < tx1 || x1 > tx2 || y2 < ty1 || y1 > ty2) return 0;
    M.struck = 1;
    return 1;
}

/* (from enemy_spawn) params 1 and 2: the music of the fight and of its defeat (0x2A540) */
void mboss_spawn(BYTE *a, const BYTE *def)
{
    BYTE *z;
    metaboss_reset();
    M.a = a;
    M.type = A_TYPE(a);
    M.finish = def && D_FLAGS(def) & 0x8000 ? D_NEXT(def) : NULL;
    M.music = def_param(a, def, 1, 0);
    M.win = def_param(a, def, 2, 0);
    M.hits = MB_HITS;
    M.ground = A_Y(a) + MB_STRIKE_DY;
    M.home = A_Y(a) + MB_DY;
    M.rise = MB_RISE;
    M.zap_wait = MB_ZAP_FIRST;
    M.state = MB_WAIT;
    A_UPDATE(a) = (void *)mboss_update;
    A_DRAW(a) = (void *)mboss_draw;
    A_FLAGS(a) = (A_FLAGS(a) & ~(AF_CONTACT | AF_LEFT)) | AF_HIDDEN;   /* (nothing touches it: Flink's tests) */
    A_CULL(a) = CULL_NEVER;
    A_LAYER(a) = (short)FLINK_LAYER(3);      /* (a5) = 0x7D70 */
    A_VX(a) = A_VY(a) = 0;
    A_FX(a) = A_X(a) << 16;
    place_y(a, M.home);
    show(a, AN_EINS, 0);
    if ((z = kid_new(K_ZAP, "ZAP1", FLINK_LAYER(3)))) {
        A_FLAGS(z) |= AF_HIDDEN;
        A_UPDATE(z) = (void *)zap_update;
        A_DRAW(z) = (void *)zap_draw;
        M.zapa = z;
    }
    flog("meta boss at (%d,%d): strikes at y %d, finish %s", A_X(a), A_Y(a), M.ground, M.finish ? "linked" : "none");
}

/* (from enemy_spawn) defeated already in this visit: only its Finish */
void mboss_gone(BYTE *a, const BYTE *def)
{
    if (def && D_FLAGS(def) & 0x8000) boss_finish_later(a, D_NEXT(def), 1);
}
