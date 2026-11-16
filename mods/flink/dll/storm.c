/*
 * storm.c - META's dark cloud, Flink's 'Storm Cloud' (init 0x231B2, think 0x23266, draw 0x2346E; 29-1
 * on): a 'Muta Storm n' type with Flink's records WOLK (the cloud), WIND (its puffs, one sprite a
 * frame, drawn at their top-left) and BOOM (Flink's DEAD frame, which breaks up) (README "Enemies").
 * No params (Flink's init reads none). Init: draw list 2 (0x7D6E), activation window 8, stompable (+6
 * bit 3), moving (+2 bit 8, which also hides WOLK's piece with selector 1, its blowing mouth: Flink's
 * Actor_DrawFrame 0x328C8 skips selector n while bit 7 + n is set), its ground y (+0x46) its placement's, vx 0.5 (+0x1C, subtracted: it
 * goes left), 60 frames (+0x4A) before it first blows, 9 puffs (+0x4E: Actor_AllocExtra 0x32DB4; with
 * none left it isn't spawned), WIND's four frames (+0x50..+0x5C), WOLK. A map's first Storm Cloud
 * (spawn flag bit 6) only builds the shrunk frames below (0x23576) and is gone: the converter leaves
 * it out.
 * Flink's think, every frame:
 * - Hit (+3 bit 6: a thrown object, Flink's slide or a spell) or stomped (Actor_CheckStompedByPlayer
 *   0x1A352): DEAD, its puffs gone (0x32DFE), and Actor_ForgetForever (0x1B1E8): gone for good, its
 *   frame broken up into its sprites, sound 0x2A when on screen. Otherwise touching it hurts (0x1A462).
 * - Moving (+2 bit 8): + 1 a frame on the timer's way down; once it is below 0 and the breath (below)
 *   is at its full size, it blows: sound 0x3A when on screen (after Pcm_StopEffect of its effect,
 *   34), 90 frames (0x5A), the wind's origin 22 px in front of it (x - 0x16, mirrored x + 0x0A) and
 *   16 px up. Otherwise x - vx and Actor_SnapToGround (0x1AE96: Map_GetGroundHeight from its ground
 *   y - 16, 4 cells, within 3 or 4 px by vx's whole px, the table 0x1AECA): on it, that is its ground
 *   y; with none (a ledge, a step) it turns, x - 2 * the new vx.
 * - Blowing: Flink in front of it (his x - the origin's has vx's sign, the way it faces) is pushed
 *   that way by (180 - (|dx| + 2 * |Flink's y - 20 - the origin's y|)) * 0x444 a frame, straight on his
 *   x, while that is positive: 0.75 px a frame at the origin, nothing 180 px off. Every 4th frame a
 *   puff starts in the first free one of its 9: 28..35 frames to live, at the origin, vx 2.5..3.4 px a
 *   frame the way it faces (((rand & 7) + 20) << 5, 8.8), vy -(rand & 63) / 256. After the 90 frames
 *   it moves again for 80..207 frames ((rand & 0x7F) + 0x50).
 * - Then (both) it bobs: + 1 a frame (+0x48), y = its ground y - 10 + |((that >> 3) & 7) - 4|: 0..4 px
 *   over 64 frames. Mirrored (+3 bit 3) while vx < 0, going right.
 * Flink's draw: each live puff - 1 frame, x + vx, y + vy, vy - 4 (rising faster), its sprite at the
 * origin + its whole px (8.8's high byte), WIND's frame (slot index & 3, from slot 8 down), before the
 * cloud (in front). Moving, its breath counter + 1 a frame (+0x4C): at |((it >> 3) & 7) - 4| below 4 the
 * cloud is drawn shrunk, 85, 90, 95 or 100% about its frame's middle (0x235BE built them from WOLK's
 * frame without the mouth, 0x1DAD4 at 0x55 + 5 n percent, 0x1DE66 centred in the full frame's box);
 * at 4 it is WOLK itself. So it breathes, 64 frames a breath, and blows only when full.
 * Here: Lomax's stomp, spin, roll and thrown helmet kill it at once (as Flink's slide and spells),
 * thrown objects through enemy_struck. The mouth is Lomax's slot 1 (A_HIDE bit 1, the converter keeps
 * the selector), hidden while it moves. The push is Flink's scaled as Lomax's walk is (move_speed of
 * 1.5 times it, as groundtype.c's), added before Player_Update (storm_before, from player_tick), and
 * not into a wall in front of Lomax. The shrunk frames are WOLK drawn by Actor_Draw at that scale,
 * moved so that the middle of Flink's box (x -22..34, y -41..2) stays. The puffs update with the
 * think and are drawn as sprites (Sprite_QueueWorld) one bucket over the cloud. Sounds: Flink's 0x3A
 * (sfx.c; its 2.5 s are over before it blows again) and 0x2A.
 */
#include "enemy.h"

#define PUFFS           9
#define FIRST_WAIT      0x3C
#define BLOW_TIME       0x5A
#define MOVE_BASE       0x50
#define START_VX        0x8000
#define WIND_DX         0x16            /* the origin: x - 0x16, mirrored + 0x20 */
#define WIND_DY         0x10
#define WIND_AIM        0x14            /* Flink's y - 20 */
#define WIND_REACH      0xB4
#define WIND_FORCE      0x444
#define BOB_UP          10
#define GROUND_CELLS    4               /* 0x1AE9E: d2 = 3, dbra */
#define BOX_X2          (-22 + 34)      /* twice the middle of WOLK's box */
#define BOX_Y2          (-41 + 2)
#define MOUTH           2               /* A_HIDE: slot 1 */

typedef struct { short life, x, y, vx, vy; int shown; } Puff;
typedef struct { int moving, t, bob, breath, home, vx, ox, oy; Puff p[PUFFS]; } Storm;
static Storm g_storm[MAX_ACTORS];
static int g_wind;                      /* this tick's push (16.16), for storm_before */

/* 0x1AECA: how far Actor_SnapToGround follows the ground, by vx's whole px */
static const BYTE STICK[16] = { 10, 10, 10, 10, 9, 7, 6, 4, 3, 4, 6, 7, 10, 10, 10, 10 };

static int storm_rand(void) { static unsigned r = 0x3A5C1D27u; r = r * 1103515245u + 12345u; return (int)(r >> 16) & 0x7FFF; }

/* Actor_ForgetForever (0x1B1E8): gone for good, its DEAD frame broken up */
void storm_die(BYTE *a, int i)
{
    BYTE *coin;
    forget_placement(A_DEF(a));
    g_enemy[i].type = NULL;
    if (enemy_on_screen(a)) flink_sound(a, 0x2A);
    coin = enemy_defeated(a);
    Actor_ShatterIntoPieces(a, 0xC00);
    flog("storm cloud at (%d,%d) killed%s", A_X(a), A_Y(a), coin ? ", drops a coin" : "");
}

/* 0x2328A: the wind on Flink */
static void storm_push(const BYTE *a, const Storm *s)
{
    int dy = abs(*G_PLAYERY - WIND_AIM - s->oy) * 2, dx = *G_PLAYERX - s->ox, f;
    if (A_PLANE(a) != *G_PLAYERPLANE || ((short)dx ^ (short)(s->vx >> 16)) >= 0) return;   /* behind it */
    f = WIND_REACH - (abs(dx) + dy);
    if (f < 0) return;
    f *= WIND_FORCE;
    g_wind += A_FLAGS(a) & AF_LEFT ? f : -f;
}

/* 0x23328: a puff in the first free slot */
static void storm_puff(const BYTE *a, Storm *s)
{
    Puff *p;
    int k, v;
    for (k = 0; k < PUFFS && s->p[k].life >= 0; k++) ;
    if (k == PUFFS) return;
    p = &s->p[k];
    p->life = (short)((storm_rand() & 7) + 0x1C);
    p->x = p->y = 0;
    v = ((storm_rand() & 7) + 0x14) << 5;
    p->vx = (short)(A_FLAGS(a) & AF_LEFT ? v : -v);
    p->vy = (short)-(storm_rand() & 0x3F);
}

/* 0x233E2: x - vx, along the ground; at its end it turns */
static void storm_step(BYTE *a, Storm *s)
{
    int g, k = s->vx >> 16;
    A_FX(a) -= s->vx;
    set_pos(a);
    g = (int)Plane_FindGroundY(PLANE(A_PLANE(a)), A_X(a), s->home - 16, GROUND_CELLS);
    if (g != NO_GROUND && abs(g - s->home) <= STICK[(k < -8 ? -8 : k > 7 ? 7 : k) + 8]) s->home = g;
    else {
        s->vx = -s->vx;
        A_FX(a) -= 2 * s->vx;
        set_pos(a);
    }
}

static void storm_think(BYTE *a, Storm *s)
{
    int d;
    if (!s->moving) goto blow;
move:
    if (--s->t < 0 && !(s->breath >> 3 & 7)) {             /* full: it blows (0x23376) */
        if (enemy_on_screen(a)) flink_sound(a, 0x3A);
        s->t = BLOW_TIME;
        s->moving = 0;
        s->ox = A_X(a) - WIND_DX + (A_FLAGS(a) & AF_LEFT ? 0x20 : 0);
        s->oy = A_Y(a) - WIND_DY;
        flog("storm cloud at (%d,%d) blows", A_X(a), A_Y(a));
        goto blow;
    }
    storm_step(a, s);
    goto bob;
blow:
    storm_push(a, s);
    if (--s->t < 0) {
        s->t = (storm_rand() & 0x7F) + MOVE_BASE;
        s->moving = 1;
        goto move;
    }
    if (!(s->t & 3)) storm_puff(a, s);
bob:                                                        /* 0x23412 */
    d = ((++s->bob >> 3) & 7) - 4;
    place_y(a, s->home + abs(d) - BOB_UP);
    if (s->vx < 0) A_FLAGS(a) |= AF_LEFT; else A_FLAGS(a) &= ~AF_LEFT;
    A_HIDE(a) = s->moving ? MOUTH : 0;
}

static void __cdecl storm_update(BYTE *a)
{
    int i = enemy_slot(a), k;
    Storm *s;
    if (i < 0 || !g_enemy[i].storm) return;
    s = &g_storm[i];
    if (Actor_CheckHitByPlayer(a)) { storm_die(a, i); return; }   /* (as Flink's slide and spells) */
    storm_think(a, s);
    for (k = 0; k < PUFFS; k++) {                           /* (Flink's draw) */
        Puff *p = &s->p[k];
        if ((p->shown = p->life >= 0)) {
            p->life--;
            p->x = (short)(p->x + p->vx);
            p->y = (short)(p->y + p->vy);
            p->vy = (short)(p->vy - 4);
        }
    }
    if (s->moving) s->breath++;
    Actor_AdvanceAnim(a);
}

/* 0x2346E: the puffs, then the cloud, shrunk while it breathes in */
static void __cdecl storm_draw(BYTE *a)
{
    int i = enemy_slot(a), k, d, sc, dx, dy;
    const BYTE *e = record_of(a, FOURCC("WIND")), *bank;
    Storm *s;
    if (i < 0 || !g_enemy[i].storm) return;
    s = &g_storm[i];
    for (k = 0; e && k < PUFFS; k++) {
        const BYTE *f = e + ((PUFFS - 1 - k) & 3) * 8;    /* (one piece a frame) */
        if (!s->p[k].shown || !(bank = G_ANIMBANKS[f[0] | (T_BANKS(A_TYPE(a)) & 0x100)])) continue;
        Sprite_QueueWorld(PLANE(A_PLANE(a)), bank + f[1] * 0x38 + 0x10, s->ox + (signed char)(s->p[k].x >> 8),
                          s->oy + (signed char)(s->p[k].y >> 8), (unsigned)(Actor_GetDrawDepth(a) + 1) << 24 | (f[6] & 0x18), 20000);
    }
    d = abs(((s->breath >> 3) & 7) - 4);
    if (!s->moving || d == 4) { draw_actor(a); return; }
    sc = 85 + 5 * d;
    dx = (100 - sc) * (A_FLAGS(a) & AF_LEFT ? -BOX_X2 : BOX_X2) / 200;
    dy = (100 - sc) * BOX_Y2 / 200;
    A_X(a) += dx; A_Y(a) += dy;
    Actor_Draw(a, sc * 10, 0, 0);
    A_X(a) -= dx; A_Y(a) -= dy;
}

/* before Player_Update: this tick's wind on Lomax, not into a wall in front of him */
void storm_before(void)
{
    int v = g_wind, x;
    g_wind = 0;
    if (!v || !g_active || *G_BPAUSED || *G_PLAYERSTATE & PS_FROZEN || *G_PLAYERDEATHTIMER || door_walk_scale()) return;
    v = move_speed(v * 3 / 2);
    x = (*G_PLAYERXFIXED + v) >> 16;
    if (wall_at(G_PLAYERACTOR, x + (v > 0 ? 8 : -8), v > 0)) return;
    *G_PLAYERXFIXED += v;
    *G_PLAYERX = *G_PLAYERXFIXED >> 16;
}

void storms_reset(void) { g_wind = 0; }

/* (from enemy_spawn) 0x231B2 */
void storm_spawn(BYTE *a, int i)
{
    Storm *s = &g_storm[i];
    int k;
    memset(s, 0, sizeof(*s));
    for (k = 0; k < PUFFS; k++) s->p[k].life = -1;
    g_enemy[i].storm = 1;
    s->moving = 1;
    s->t = FIRST_WAIT;
    s->vx = START_VX;
    s->home = A_Y(a);
    A_LAYER(a) = FLINK_LAYER(2);
    A_UPDATE(a) = (void *)storm_update;
    A_DRAW(a) = (void *)storm_draw;
    A_HIDE(a) = MOUTH;
    Actor_SetAnim(a, FOURCC("WOLK"));
}
