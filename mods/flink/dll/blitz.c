/*
 * blitz.c - META's man on a cloud, Flink's 'Blitz dude' (init 0x22DA2, think 0x22E60; 29-1 on): a
 * 'Muta Blitz n' type with Flink's records STAY (the man), DEAD, ZAP (his lightning) and WOLK / WARN
 * (his cloud, with a platform's standing surface) (README "Enemies"). No params (Flink's init reads
 * none). Init: draw list 2 (0x7D6E), activation window 0x18 (never culled once it runs), stompable
 * (+6 bit 3), 60 frames (+0x48) before its first strike, and, from Rand_GetNext (0x19C8A), the place it
 * keeps over Flink (+0x4E: x - 32..31, +0x50: y - 16..15), its top speed (+0x52: (24..31) / 16 px a
 * frame) and its acceleration (+0x56: 0x600..0x7F0); STAY. Its cloud is a child (0x32CD8; 0x230F0, the
 * Wolky platform's init: think 0x23120, draw list 3, WOLK, vy -0.25 (+0x20 = -0x4000)), frozen and
 * hidden (+3 bits 2 and 7): the man's hook for STAY's attach point (flags 1, at 0, 8; table 0x230BE ->
 * 0x230C2) puts it there, draws it (behind him: the attach point is STAY's last piece) and steps its
 * animation.
 * Flink's think, every frame:
 * - Hit (+3 bit 6: a thrown object, Flink's slide or a spell) or stomped (Actor_CheckStompedByPlayer
 *   0x1A352): the cloud is let go (+3 bits 2 and 7 cleared, 0x230AA) and he dies (Enemy_Die 0x1B12E).
 *   Otherwise touching him hurts (0x1A462).
 * - Following (+0x48 >= 0): the cloud WOLK; while Flink is in the air (g_PlayerFlags 0x828E bit 0) or
 *   on a platform (g_PlatformActor 0x82FE) the count is held at 5 or more, otherwise it counts down,
 *   and below 0 Flink's x, y then are the target (+0x4A, +0x4C) and the cloud warns (WARN). Either
 *   way he moves toward (Flink's x + his x offset, Flink's y - 120 + his y offset) at his own speeds
 *   (Actor_MoveToward 0x1B008: per axis, by the acceleration a frame, twice that while moving away, up
 *   to the top speed).
 * - Striking (+0x48 < 0): toward (the target's x, its y - 120) at 0x2000 a frame, up to 1.5; the count
 *   goes on down. From -60 to -999, once |vx| + |vy| is under 0.25 (0x22F00) and his x is within 8 px
 *   of the target's, the lightning (a child at the target, 0x23004: draw list 3, ZAP played once,
 *   +3 bit 0, at half a frame a frame, +0x32 = 0x80) strikes and the count goes to -1000 (with no
 *   free slot: 100, following again). At -1060 he follows again for 80..143 frames ((rand & 63) + 80).
 * The lightning's think: at frame 6 (+0x30 = 0x600) on the screen, the screen flashes (main command
 * 0x28) and, with no shake running (0x874E), it shakes 0x23 frames with sound 0x2D. From frame 9 to 14
 * its box (ZAP's hitbox, 50 x 25 px on the ground, 0x1A170) hits the first actor it touches
 * (0x1A5A0) and hurts Flink (0x1A46E). After its 35th frame it is freed (0x32D9E).
 * The cloud let go is the Wolky platform's think (0x23120): gone at y -30 or above (0x32D8E); WOLK; its
 * phase + 8 a frame, vx = sin(phase) (Flink's table 0x1491E, 1024 a turn, 0x4000 = 0.25 px: it sways
 * 5 px each way every 128 frames); a platform (0x1A28A: moved by vx, vy, carrying Flink); landed on, it
 * goes down at 0.25 for 20 frames, otherwise up at 0.25.
 * Here: Lomax's stomp, spin, roll and thrown helmet kill him at once (as Flink's slide and spells),
 * thrown objects and spells through enemy_struck. The cloud and the lightning are Actor_Alloc children
 * of his type: the cloud on FLINK_LAYER(3) (behind him) drawn at his point + (0, 8) and hidden with him
 * while he waits for Flink's window (enemy_wait), never culled, gone with him should he be despawned;
 * let go, gone at y -30 or 128 px off the view (Flink's mid window, 8). The man is never culled once his
 * think runs. Flink's airborne bit is Lomax's PS_AIR. The flash has no Lomax counterpart; the shake is
 * flink_quake (razorback.c), the sound Flink's 0x2D (sfx.c).
 */
#include <math.h>
#include "enemy.h"

#define FIRST_WAIT      0x3C
#define HOLD            5               /* the count while Flink is in the air or on a platform */
#define ABOVE           0x78            /* he keeps 120 px over Flink's feet */
#define STRIKE_MAX      0x18000
#define STRIKE_ACC      0x2000
#define STRIKE_FROM     (-60)           /* 0xFFC4: he may strike from here ... */
#define STRIKE_DONE     (-1000)         /* 0xFC18: ... to here, where a strike puts the count */
#define STRIKE_END      (-1060)         /* 0xFBDC: following again */
#define STRIKE_RETRY    100             /* no free slot for the lightning */
#define SLOW            0x4000          /* |vx| + |vy| under this: he is over the target */
#define AIM_DX          8
#define CLOUD_DY        8               /* STAY's attach point (the converter checks it) */
#define CLOUD_VY        0x4000
#define CLOUD_TOP       (-30)           /* 0xFFE2 */
#define CLOUD_NEAR      128
#define ZAP_SPEED       0x80
#define ZAP_FLASH       0x600
#define ZAP_HURT_FROM   9
#define ZAP_HURT_TO     14
#define ZAP_SHAKE       0x23

enum { BZ_NONE, BZ_CLOUD, BZ_ZAP };

/* the man (by his slot): his count, offsets, speeds, target, cloud. A child (by its slot): its kind,
   type, owner (the cloud while held), phase, dip count, vy; the lightning's 8.8 frame (t) */
typedef struct {
    int t, ox, oy, max, acc, tx, ty;
    BYTE *cloud;
    int kind;
    BYTE *type, *owner;
    int phase, dip, vy;
} Blitz;
static Blitz g_blitz[MAX_ACTORS];
static short g_sine[1024];              /* Flink's table 0x1491E: round(0x4000 * sin) */

static int blitz_rand(void) { static unsigned r = 0x6B2E9D41u; r = r * 1103515245u + 12345u; return (int)(r >> 16) & 0x7FFF; }

void blitz_reset(void) { memset(g_blitz, 0, sizeof(g_blitz)); }

/* Actor_MoveToward (0x1B008) on one axis, as metadummy.c's */
static void move_toward(int *v, int *pos, int target, int max, int acc)
{
    int p = (short)(*pos >> 16);
    if (target == p && abs(*v) <= acc) *v = 0;
    else if (target >= p) { if (*v < max) *v += *v < 0 ? 2 * acc : acc; }
    else if (-*v < max) *v -= *v > 0 ? 2 * acc : acc;
    *pos += *v;
}

/* a child of the kind, -1 if a is not one (any more) */
static int child_of(const BYTE *a, int kind)
{
    int k = slot_of(a);
    return k >= 0 && g_blitz[k].kind == kind && g_blitz[k].type == A_TYPE(a) && A_LAYER(a) != KILLED_LAYER ? k : -1;
}

/* the man holding the cloud k, NULL if he is gone */
static BYTE *owner_of(int k)
{
    BYTE *o = g_blitz[k].owner;
    int i = o ? enemy_slot(o) : -1;
    return i >= 0 && g_enemy[i].blitz && !g_enemy[i].dead && actor_active(i) && g_blitz[i].cloud == actor_at(k) ? o : NULL;
}

static void child_free(BYTE *a, int k)
{
    g_blitz[k].kind = BZ_NONE;
    if (*G_PRIDINGPLATFORM == a) *G_PRIDINGPLATFORM = NULL;
    Actor_Despawn(a);
}

static BYTE *child_new(BYTE *a, int kind, const char *tag, int layer)
{
    BYTE *d = Actor_Alloc(a, NULL);
    int k;
    if (!d || (k = slot_of(d)) < 0) return NULL;
    memset(&g_blitz[k], 0, sizeof(g_blitz[k]));
    g_enemy[k].type = NULL;                  /* (not an enemy) */
    g_blitz[k].kind = kind;
    g_blitz[k].type = A_TYPE(d);
    A_FLAGS(d) = 0;
    A_CULL(d) = CULL_NEVER;
    A_LAYER(d) = layer;
    A_VX(d) = A_VY(d) = 0;
    A_DRAW(d) = A_DRAW(a);
    A_RECORD(d) = NULL;                      /* (Actor_Alloc leaves the slot's last one) */
    Actor_SetAnim(d, FOURCC(tag));
    if (!A_RECORD(d)) { g_blitz[k].kind = BZ_NONE; Actor_Despawn(d); return NULL; }
    return d;
}

/* ---- the cloud ------------------------------------------------------------------------------------ */

static int near_view(const BYTE *a, int margin)
{
    const BYTE *pl = PLANE(A_PLANE(a));
    int w = P_W(pl) * 16 - P_MAXX(pl), x = A_X(a) - P_CAMX(pl), y = A_Y(a) - P_CAMY(pl);
    if (P_MAXX(pl) <= 0 || w < 320) w = 320;
    return x >= -margin && x < w + margin && y >= -margin && y < 240 + margin;
}

/* held: only its animation (the man's hook); let go: the Wolky platform's think (0x23120) */
static void __cdecl cloud_update(BYTE *a)
{
    int k = child_of(a, BZ_CLOUD);
    Blitz *c;
    if (k < 0) return;
    c = &g_blitz[k];
    if (c->owner) {
        if (!owner_of(k)) { child_free(a, k); return; }
        Actor_AdvanceAnim(a);
        return;
    }
    if (A_Y(a) <= CLOUD_TOP || !near_view(a, CLOUD_NEAR)) { child_free(a, k); return; }
    set_anim(a, "WOLK");
    wolky_step(a, &c->phase, &c->dip, &c->vy);       /* the Wolky platform's (wolky.c) */
}

/* held: at the man's attach point, hidden with him (0x230C2) */
static void __cdecl cloud_draw(BYTE *a)
{
    int k = child_of(a, BZ_CLOUD);
    BYTE *o;
    if (k < 0) return;
    if (g_blitz[k].owner) {
        if (!(o = owner_of(k)) || A_FLAGS(o) & AF_HIDDEN) return;
        A_FX(a) = A_FX(o);
        A_FY(a) = A_FY(o) + (CLOUD_DY << 16);
        set_pos(a);
    }
    draw_actor(a);
}

static void cloud_set(BYTE *a, const char *tag)
{
    int k = slot_of(a);
    BYTE *c = g_blitz[k].cloud;
    int j = c ? child_of(c, BZ_CLOUD) : -1;
    if (j >= 0 && g_blitz[j].owner == a) set_anim(c, tag);
}

/* 0x230AA: the cloud let go, where it is drawn */
static void cloud_release(BYTE *a, int i)
{
    BYTE *c = g_blitz[i].cloud;
    int j = c ? child_of(c, BZ_CLOUD) : -1;
    g_blitz[i].cloud = NULL;
    if (j < 0 || g_blitz[j].owner != a) return;
    g_blitz[j].owner = NULL;
    A_FX(c) = A_FX(a);
    A_FY(c) = A_FY(a) + (CLOUD_DY << 16);
    set_pos(c);
    flog("blitz dude's cloud let go at (%d,%d)", A_X(c), A_Y(c));
}

/* ---- the lightning -------------------------------------------------------------------------------- */

/* 0x1A46E: its box against Lomax's (inclusive) */
static void zap_hurt(const BYTE *a, int x1, int y1, int x2, int y2)
{
    const BYTE *pa = *G_PLAYERANIM;
    int px = *G_PLAYERX, py = *G_PLAYERY;
    if (!pa || A_PLANE(a) != *G_PLAYERPLANE || *G_PLAYERDEATHTIMER || *G_PLAYERHURTTIMER) return;
    if (x2 < px + HB_X1(pa) || x1 > px + HB_X2(pa) || y2 < py + HB_Y1(pa) || y1 > py + HB_Y2(pa)) return;
    flog("blitz dude's lightning hurts Lomax at (%d,%d)", px, py);
    Player_TakeHit(A_X(a), 0, 0);
}

/* 0x23004 */
static void __cdecl zap_update(BYTE *a)
{
    int k = child_of(a, BZ_ZAP), fr, x1, y1, x2, y2;
    Blitz *z;
    if (k < 0) return;
    z = &g_blitz[k];
    if (z->t == ZAP_FLASH && enemy_on_screen(a) && !flink_quake()) {
        flink_quake_set(ZAP_SHAKE);
        flink_sound(a, 0x2D);
    }
    fr = z->t >> 8;
    if (fr >= ZAP_HURT_FROM && fr <= ZAP_HURT_TO && actor_box(a, &x1, &y1, &x2, &y2)) {
        if (spell_strike(a, NULL, x1, y1, x2, y2)) flog("blitz dude's lightning hits an actor");
        zap_hurt(a, x1, y1, x2, y2);
    }
    z->t += ZAP_SPEED;
    if (z->t >> 8 >= A_NFRAMES(a)) { child_free(a, k); return; }
    hold_frame(a, z->t >> 8);
    Actor_AdvanceAnim(a);
}

/* 0x22FAC: 1 if it strikes */
static int zap_spawn(BYTE *a, const Blitz *b)
{
    BYTE *z = child_new(a, BZ_ZAP, "ZAP ", FLINK_LAYER(3));
    if (!z) return 0;
    A_FX(z) = b->tx << 16;
    A_FY(z) = b->ty << 16;
    set_pos(z);
    hold_frame(z, 0);
    Actor_AdvanceAnim(z);
    A_UPDATE(z) = (void *)zap_update;
    flog("blitz dude at (%d,%d) strikes at (%d,%d)", A_X(a), A_Y(a), b->tx, b->ty);
    return 1;
}

/* ---- the man -------------------------------------------------------------------------------------- */

/* from enemy_stomped / enemy_struck too: the cloud let go, Enemy_Die (0x230AA) */
void blitz_die(BYTE *a, int i, int knock)
{
    cloud_release(a, i);
    enemy_die(a, i, knock);
}

/* 0x22E7A on */
static void blitz_think(BYTE *a, Blitz *b)
{
    if (b->t >= 0) {                                    /* 0x22F18 */
        cloud_set(a, "WOLK");
        if (*G_PLAYERSTATE & PS_AIR || *G_PRIDINGPLATFORM) { if (b->t < HOLD) b->t = HOLD; }
        else if (--b->t < 0) {
            b->tx = *G_PLAYERX;
            b->ty = *G_PLAYERY;
            cloud_set(a, "WARN");
            flog("blitz dude at (%d,%d) aims at (%d,%d)", A_X(a), A_Y(a), b->tx, b->ty);
        }
        move_toward(&A_VX(a), &A_FX(a), *G_PLAYERX + b->ox, b->max, b->acc);
        move_toward(&A_VY(a), &A_FY(a), *G_PLAYERY - ABOVE + b->oy, b->max, b->acc);
        set_pos(a);
        return;
    }
    move_toward(&A_VX(a), &A_FX(a), b->tx, STRIKE_MAX, STRIKE_ACC);
    move_toward(&A_VY(a), &A_FY(a), b->ty - ABOVE, STRIKE_MAX, STRIKE_ACC);
    set_pos(a);
    if (--b->t == STRIKE_END) b->t = (blitz_rand() & 0x3F) + 0x50;
    else if (b->t <= STRIKE_FROM && b->t > STRIKE_DONE && abs(A_VX(a)) + abs(A_VY(a)) < SLOW && abs(A_X(a) - b->tx) <= AIM_DX)
        b->t = zap_spawn(a, b) ? STRIKE_DONE : STRIKE_RETRY;
}

static void __cdecl blitz_update(BYTE *a)
{
    int i = enemy_slot(a);
    if (i < 0 || !g_enemy[i].blitz) return;
    if (g_enemy[i].dead) { enemy_dead_update(a, i); return; }
    if (Actor_CheckHitByPlayer(a)) { blitz_die(a, i, 1); return; }   /* (as Flink's slide and spells) */
    A_CULL(a) = CULL_NEVER;                  /* (window 0x18) */
    blitz_think(a, &g_blitz[i]);
    Actor_AdvanceAnim(a);
}

/* (from enemy_spawn) 0x22DA2 */
void blitz_spawn(BYTE *a, int i, const BYTE *def)
{
    Blitz *b = &g_blitz[i];
    BYTE *c;
    int k;
    if (!g_sine[256])
        for (k = 0; k < 1024; k++) g_sine[k] = (short)floor(16384.0 * sin(k * 3.14159265358979 / 512) + 0.5);
    memset(b, 0, sizeof(*b));
    g_enemy[i].blitz = 1;
    b->t = FIRST_WAIT;
    b->ox = (blitz_rand() & 0x3F) - 0x20;
    b->oy = (blitz_rand() & 0x1F) - 0x10;
    b->max = ((blitz_rand() & 7) + 0x18) << 12;
    b->acc = ((blitz_rand() & 0x1F) + 0x60) << 4;
    A_LAYER(a) = FLINK_LAYER(2);
    A_UPDATE(a) = (void *)blitz_update;
    Actor_SetAnim(a, FOURCC("STAY"));
    if (placement_gone(def)) return;         /* (enemy_spawn takes it out) */
    if ((c = child_new(a, BZ_CLOUD, "WOLK", FLINK_LAYER(3)))) {
        k = slot_of(c);
        g_blitz[k].owner = a;
        g_blitz[k].vy = -CLOUD_VY;
        A_UPDATE(c) = (void *)cloud_update;
        A_DRAW(c) = (void *)cloud_draw;
        b->cloud = c;
    }
}
