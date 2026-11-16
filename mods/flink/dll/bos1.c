/*
 * bos1.c - Flink's final boss, MOUNTAIN's 'Bos1' (init 0x20E86) in 24-2 (MOUNTAIN map 17; README
 * "Enemies" (The final boss)), and what is only his: his fireballs (children, hook 0x21E96, thinks
 * 0x21F24 / 0x21F4A), the dragon he becomes (think 0x20F96, draw 0x215CC, its sprites Bos2's, built
 * by Bos2's init 0x20DCC), its falling fire ('Boom', 0x216EA) and the Bos3 dudes he drops ('Bos3 dude',
 * 0x21F90). The converter makes him a 'Muta Bos1 n' type: STAY, STA2, FLY, OUCH, ZAP, WARN, HEAD,
 * TAIL, Bos2's HEAD and TAIL as DHED and DTAL, Boom's HEAD, TAIL and BOOM as BHED, BTAL and BOOM;
 * params 0 and the music at his defeat (a Lomax Music param). His placement links to a Finish in the
 * middle of the map (placement +4). The Bos3 dude is a 'Muta Bos3 n' enemy (MOVE, STUN, DEAD),
 * placed only to load the type.
 *
 * Flink's boss, three phases on one actor:
 * - Init: hits 0x8288 3; draw list 3, window 0x18, +3 bit 7 (sprite priority: in front of the
 *   Priority map's tiles), +6 bit 0; home y +0x58 its y, +0x5C 100; HEAD (the fireball he comes as),
 *   its three sparks (0x2EE0C), y 400; 120 trail entries for the dragon (in 8 slots, 0x32DB4) unset.
 * - The fireball form (0x21D4A): toward Flink's x + +0x5C (kept 50 px inside the map) and home - 28
 *   (Actor_MoveToward 0x1B008: 2.0, 0x800); once vy >= 0 within 2 px of that y: sound 0x51, the
 *   flash, the first fireball's wait +0x5E 30 + rand & 31, fireballs +0x60 5 + rand & 3, facing
 *   Flink (+3 bit 3: mirrored, the sprites face left), and he forms (0x21CC4): y home, ZAP's frame
 *   +0x5A / 4 for +0x5A = 15 .. 0 (the burst shrinking into his outline), then 30 frames (+0x5A):
 * - STA2 (0x2182A): touching him hurts (0x1A462). Its frames' point 1 calls the hook 0x21E96 at
 *   each draw: +0x5E - 1, at 0 60 again and +0x60 - 1: while not below 0 a fireball (0x32CD8 at the
 *   point; draw list 1, window 8, +3 bit 7): WARN 31 frames (0x21F24), then HEAD with its sparks and
 *   sound 0x3C (0x21F4A), toward x +-20000 the way he faces and its y + -20, 10, -8 or 20 (rand & 3)
 *   at 4.0, 0x4000 a frame; touching it hurts. +0x60 below 0, 30 frames on:
 * - STAY (0x21B56) 100 frames (+0x5A): Actor_CheckStompedByPlayer (0x1A352) on his box: a spirit
 *   at y - 30, hits - 1, OUCH 60 frames, the shake 0x23, the flash, sound 0x2B; not stomped,
 *   touching him hurts. Then, hits left: sound 0x52, +0x5C 100 facing right, -100 left, and he goes
 *   (0x21D02): ZAP's frame 3 - +0x5A / 4 for +0x5A = 15 .. 0, then HEAD and its sparks, vy -4.0, y
 *   home - 28: the fireball form again, to the other side of Flink.
 * - No hits left (0x21B68): hits 5, the hook table 0x220E2 (point 1 nothing, point 2 0x21F68), +6
 *   bit 3 (thrown objects reach him), flying away from Flink (+0x62 +-20000, +3 bit 3 his way),
 *   +0x64 home - 80, vy -2.0, +0x5E 100. Each frame (0x2186A): hit (+3 bit 6) by a dying enemy (the
 *   hitter's think Enemy_Die's fall 0x1B1AA: a thrown Bos3 dude, which dies of the hit): sound 0x2B,
 *   the shake 0x23, the flash, hits - 1; left, OUCH until the shake is over (0x219AE), not moving.
 *   Otherwise every 32 frames (0x8710) +0x64 home - 100 + (rand & 31) - 16; going left, at the
 *   camera's x + 50 or less he turns right (+0x62 20000), going right at + 270 or more left; toward
 *   (+0x62, +0x64) at 3.0, 0x1000; FLY. FLY's point 2 (0x21F68): +0x5E - 1, below 0 100 again and a
 *   Bos3 dude there (0x32BD4). Touching him does nothing.
 * - The last hit (0x219CC): OUCH, facing left, draw list 3: he falls (vy + 0x600 a frame) shrinking
 *   from 97% by 0.5 a frame, to 50% at the least (+0x5A, 16.16; the sprite rebuilt at that scale,
 *   its top-left at x - 21, y - 61, without the priority bit: behind the tiles, 0x21B14); START is
 *   ignored meanwhile. At y 400:
 * - The dragon (0x20F44): draw list 1, the camera's x stays (0x8723), its box x + 8 .. 40, y + 8 ..
 *   40 (+0x28 = its own +0x56), hits 5. Each frame (0x20F96) a hit by a dying enemy with vy 0 (and
 *   Flink not dying) is sound 0x2B, the shake, the flash, hits - 1. Then its move (+0x4E; 0 picks
 *   -2, -3, -2, -3, -1, -2, -3, -2 by rand & 7, a negative one starts):
 *     3 (0x21096): +6 bit 3, draw list 3, y 250, vy -1.75, x the camera's + 160, +0x46 0x100,
 *       +0x4A 0, +0x50 (13 + rand & 7) * 60 frames; vy + 0x200 a frame up to 0, then it hovers for
 *       +0x50, then vy + 0x200 a frame down to 1.75, +6 bit 3 cleared and the next. Hovering, every
 *       128 frames a Bos3 dude at x + 20, y + 50 (draw list 2). x + sin(+0x46 + 3) * 10, y +
 *       sin(+0x48 + 9) * 5 + vy (the table 0x1491E, 1024 a turn, 0x4000 = 1.0); its depth
 *       |((+0x4A >> 3) & 7) - 4| + min(|vy| / 0x1000, 5), 3 .. 7;
 *     1 (0x2129C): the same with no +6 bit 3, +0x50 (8 + rand & 3) * 60, every 64 frames hovering
 *       (Flink not dying) a Boom at x + 10, y + 40 falling to his home y; x + sin(+0x46 + 5) * 8, y
 *       + sin(+0x48 + 8) * 4 + vy; depth |((+0x4A >> 3) & 7) - 4| + 5, at most 7;
 *     2 (0x211F4): draw list 1, y 300, vy -5.0, x Flink's, then x - 70 and vx 0.5 with Flink left
 *       of the camera's + 160, else x + 40 and vx -0.5; +0x50 -30; vy + 0x1000 a frame, at 9.0 the
 *       next; depth 7 - (+0x50 / 13, 0 .. 7) as +0x50 counts up: it leaps up from behind the tiles
 *       and falls back, coming near.
 *   Each frame the depth, x and y go into the next of the 120 trail entries (+0x4C). Its draw: 8
 *   segments 8 entries apart, the head (Bos2's HEAD) at the newest, at x - 8, then 7 of TAIL, each
 *   its frame at the depth's scale (0x86 - 8 depth per cent: 134 .. 78), centred in the 48 x 48 box
 *   whose top-left is the entry's point, without the priority bit (behind the tiles); in move 2 the
 *   far ones (depth over 4) in draw list 3, the near ones in list 1, with the bit while vy > 0, each
 *   then testing x .. x + 24, y .. y + 24 against Flink (0x1A46E: Player_TakeHit).
 * - Defeated (0x2101E): Flink can't be hurt (hurt timer -2), vx 2.0, vy 1.0, the music at his
 *   defeat (index 21, 0x29A7C); 0x21414: every 32 frames of +0x50 (319 down) sound 0x36, the shake,
 *   the flash and a spirit at x + 20, y + 30; it bounces between the camera's x + 50 .. 270, y + 50
 *   .. 70, x + sin(+0x46 + 13) * 8, y + sin(+0x48 + 17) * 4, depth |((0x8710 >> 3) & 7) - 4| + 2.
 *   Below 0: Flink's pad cleared each frame; once he stands, START pressed with 0x81A6 -17: the
 *   pause screen shows message 16 of HelpTextTable ("WOW!! YOU REALLY SHOWED WAINWRIGHT ...",
 *   0x1819C), which shuts at START, A, B or C (0x188E8; sound 0x40 at 0x1574A) and clears 0x81A6;
 *   then the level ends (Level_CompleteMapPoint 0x2EBC8): the game's end.
 * - Boom (0x216EA): sound 0x2F, draw list 2, +3 bit 7, HEAD, its sparks (0x2EE0C); toward Flink's x
 *   and the ground y at 3.0, 0x2000 (0x2176E); there BOOM (draw list 1, played once, sound 0x35),
 *   its box hurting until it ends (0x21804).
 * - The sparks (0x21E62; Boom's 0x2176E): each one's phase + 0x30 a frame; at a high byte of 3 or
 *   more (Boom's: 3) back to its low byte and the spark at x - 8, y - 8 (Boom's y - 16); drawn
 *   (0x2EF6C) as TAIL frame (the high byte), its sprite's top-left there, from the phases 0x400,
 *   0x2AB, 0x156 and unplaced (y -1, not drawn); without the priority bit.
 * - The Bos3 dude (0x21F90, think 0x21FF4): draw list 3, +6 bits 3 and 0 (heavy), held at +0, +4,
 *   sound 0x1A, facing Flink (its sprites face right), MOVE. Hit (+3 bit 6): dies (Enemy_Die).
 *   Stunned (+0x38): the stun (0x1D074), and stomped dies. Else stomped: stunned 300, vy 3.0 up,
 *   sound 0x48; otherwise touching it hurts, MOVE frame 1, its fall (0x1B0E2: 0x2000, at most 4.0)
 *   and 1.25 px a frame its way while rising or in the air; on the ground (0x1AE96) frame 0, 7 frames
 *   (+0x46; sound 0x29 on screen at the landing) and a hop at 2.5. No priority bit (here drawn at Lomax's layer, in front of the tiles).
 *
 * Here: the same, one actor through the phases; Lomax's stomp is Flink's test (flink_stomp_box), with
 * his bounce. A thrown object's touch (grab.c's strike asks bos1_struck) is the hit flag, counted the
 * next tick. Not Flink's: wherever a stomp or a thrown object hits him, all of them do, and Lomax's
 * spin, roll and helmet too (player_attack_box): standing to be stomped, any thrown object, not only
 * a Bos3 dude (Flink's think counts a hitter that dies of it), and his spin, roll or helmet; flying
 * and as the dragon (only while hovering with dudes at vy 0, as Flink's thrown hit), any thrown object,
 * his stomp, spin, roll or helmet, B1_GUARD ticks apart (lomax_hit). Only HELMET from his first fireball
 * on (boss_fighting, forestboss.c). Flink's
 * camera is the middle 320 px of Lomax's view (cam_x; Lomax+'s may be wider), its y the view's top
 * + 8 (Flink's screen is 224 px high, Lomax's 240). 24-2 is a Priority map: what Flink draws without
 * the priority bit (the sparks, the falling sorcerer, the dragon but its near segments falling in move
 * 2) is drawn behind the play map's tiles by stand-in actors on the background plane
 * (as metaboss.c's body), each its own struct for Lomax+'s FPS Override. The scaled sprites are
 * Actor_Draw at that scale, moved so that the point Flink scales about stays. The level's end is the
 * banner's: g_LastRealSection + 1, to the map, the fade (levels.c then ends the game after 24); Lomax's
 * input is locked from Flink's pad clearing on. The shake is Flink's counter (razorback.c's
 * flink_quake), the fanfare played once (music_play_once). Flink's game stops under its message;
 * here his explosion goes on behind it until the level's end; the message is Lomax's hint box
 * (hints.c's hint_box) when Flink opens it, shut by a new press of Jump, Shoot, Ability or Helm
 * (message_done), and the level ends then. Not ported: the flashes, the START lock while he falls. A defeated boss stays gone for the level visit (gone.c): his Finish is there from
 * the start. Sounds: Flink's 0x51, 0x52, 0x2B, 0x3C, 0x1A, 0x36, 0x2F, 0x35, 0x48 and 0x29 (sfx.c;
 * 0x51, 0x52, 0x48 and 0x29 are FM sounds, which have no file).
 */
#include <math.h>
#include "enemy.h"

#define B1_HITS1           3             /* 0x20F1A: 0x8288, the sorcerer's stomps */
#define B1_HITS2           5             /* 0x21B78: the flying sorcerer's hits */
#define B1_HITS3           5             /* 0x20F8C: the dragon's */
#define B1_START_Y         0x190         /* 0x20F34 */
#define B1_SIDE            100           /* +0x5C */
#define B1_EDGE            50            /* 0x21D54: Flink's x + +0x5C kept 50 px inside the map */
#define B1_HOVER_DY        28            /* 0x21D42: the fireball form at home - 28 */
#define B1_FORM_MAX        0x20000       /* 0x21D7A */
#define B1_FORM_ACC        0x800
#define B1_ARRIVE          2
#define B1_FORM_T          16            /* ZAP's frames, +0x5A */
#define B1_RISE_VY         (-0x40000)    /* 0x21D2A */
#define B1_STA2_AFTER      30            /* 0x21CFA */
#define B1_STAY_T          100           /* 0x2184E */
#define B1_GUARD           60            /* ticks after Lomax's stomp or attack in the second and third phases */
#define B1_OUCH_T          60            /* 0x21C64 */
#define B1_FIRE_WAIT       30            /* +0x5E: 30 + rand & 31 */
#define B1_FIRE_EVERY      60
#define B1_FIRES           5             /* +0x60: 5 + rand & 3 */
#define B1_SPIRIT_DY       30            /* 0x21C4C */
#define B1_QUAKE           0x23
#define B1_FIRE_X          8             /* STA2's point 1 (the converter's BOS1_FIRE_AT) */
#define B1_FIRE_Y          (-39)
#define FIRE_WARN          30            /* 0x21EE4 */
#define FIRE_AWAY          20000         /* 0x21EEA */
#define FIRE_MAX           0x40000       /* 0x21F50 */
#define FIRE_ACC           0x4000
#define B2_TY              80            /* 0x21BAE: home - 80 */
#define B2_VY              (-0x20000)
#define B2_DROP_FIRST      100           /* 0x21BC2 */
#define B2_DROP_EVERY      100           /* 0x21F70 */
#define B2_DROP_X          (-2)          /* FLY's point 2 (the converter's BOS1_DROP_AT) */
#define B2_DROP_Y          (-32)
#define B2_HIGH            100           /* 0x21934: home - 100 + (rand & 31) - 16 */
#define B2_LEFT            50            /* 0x2194E: the camera's x + 50 .. 270 */
#define B2_RIGHT           0x10E
#define B2_AWAY            20000
#define B2_MAX             0x30000       /* 0x21992 */
#define B2_ACC             0x1000
#define FALL_SCALE         0x61          /* 0x218DA: 97% */
#define FALL_SCALE_MIN     0x32
#define FALL_SHRINK        0x8000
#define FALL_ACC           0x600
#define FALL_END           0x190         /* 0x219CC */
#define SHRINK_X           (-21)         /* OUCH's top-left (the converter's BOS1_SHRINK_AT) */
#define SHRINK_Y           (-61)
#define TRAIL              120           /* 0x213D6 */
#define SEGS               8
#define SEG_GAP            8
#define DRAGON_BOX1        8             /* 0x20F6E: its box x + 8 .. 40, y + 8 .. 40 */
#define DRAGON_BOX2        0x28
#define DRAGON_HURT        24            /* 0x2169C: x .. x + 24, y .. y + 24 */
#define DRAGON_NEAR        4             /* 0x2165C */
#define DRAGON_HEAD_DX     (-8)          /* 0x216C2 */
#define DRAGON_SCALE0      0x86          /* 0x20E48: per cent at depth 0, - 8 a depth */
#define DRAGON_BOX         48            /* Bos2's HEAD and TAIL frames (the converter's BOS1_PIECES) */
#define DHED_X             (-23)
#define DHED_Y             (-26)
#define DTAL_X             (-16)
#define DTAL_Y             (-18)
#define CAM_W              320           /* Flink's screen */
#define CAM_DY             8             /* Flink's 224 px in Lomax's 240 */
#define M3_Y               0xFA          /* 0x210D2 */
#define M_VY               (-0x1C000)
#define M_DVY              0x200
#define M_MID              0xA0          /* the camera's x + 160 */
#define M3_WAIT            13            /* (13 + rand & 7) * 60 */
#define M1_WAIT            8             /* (8 + rand & 3) * 60 */
#define M2_Y               0x12C         /* 0x21224 */
#define M2_VY              (-0x50000)
#define M2_DVY             0x1000
#define M2_END             0x90000
#define M2_VX              0x8000
#define M2_DXL             (-70)         /* 0x21240 */
#define M2_DXR             40
#define M2_START           (-30)
#define M2_STEP            13
#define DUDE_DX            20            /* 0x2113A */
#define DUDE_DY            50
#define BOOM_DX            10            /* 0x21340 */
#define BOOM_DY            40
#define BOOM_MAX           0x30000       /* 0x217A8 */
#define BOOM_ACC           0x2000
#define DIE_T              0x13F         /* 0x21026 */
#define DIE_VX             0x20000
#define DIE_VY             0x10000
#define DIE_SPIRIT_DX      20            /* 0x21478 */
#define DIE_SPIRIT_DY      30
#define DIE_LEFT           0x32          /* 0x214E8: x + 50 .. 270, y + 50 .. 70 */
#define DIE_RIGHT          0x10E
#define DIE_TOP            0x32
#define DIE_BOTTOM         0x46
#define B1_MESSAGE         16            /* 0x214B4: 0x81A6 = -17, HelpTextTable's message 16 (the converter's BOS1_MESSAGE) */
#define MSG_BUTTONS        0xF0          /* Jump, Shoot, Ability, Helm (Flink's START, A, B, C) */
#define SPARKS             3
#define SPARK_STEP         0x30
#define SPARK_DX           8             /* the spark at x - 8, y - 8 (Boom's y - 16) */
#define SPARK_DY           8
#define BOOM_SPARK_DY      16
#define TAIL_X             7             /* Bos1's TAIL at (-7, -10), Boom's at (-7, -16) */
#define TAIL_Y             10
#define BTAL_Y             16
#define DUDE_GRAVITY       0x2000        /* 0x22054 */
#define DUDE_FRICTION      0x2AA
#define DUDE_STEP          0x14000       /* 0x220C6 */
#define DUDE_WAIT          7             /* 0x2208E */
#define DUDE_HOP           (-0x28000)    /* 0x220B2 */
#define BG_PLANE           2             /* the converter's background plane */
#define NEAR_W             128           /* Flink's mid window (8): the fireballs' */
#define REC_SPEED(r)       (*(const short *)((r) + 0xC))   /* a loaded record's speed (8.8 frames a tick) */

enum { S_NONE, S_RISE, S_FORM, S_STA2, S_STAY, S_UNFORM, S_FLY, S_HURT, S_FALL, S_DRAGON, S_DYING };
enum { K_NONE, K_FIRE, K_BOOM };

typedef struct { int phase, x, y; } Spark;

static const int MODE_PICK[8] = { -2, -3, -2, -3, -1, -2, -3, -2 };   /* 0x21086 */
static const int FIRE_DY[4] = { -20, 10, -8, 20 };                    /* 0x21F1C */

static struct {
    BYTE *a, *type, *finish;
    int win, state, hits;
    int frame;                           /* Flink's frame counter (0x8710), a tick each */
    int vx, vy;                          /* +0x1C, +0x20 */
    int home;                            /* +0x58 */
    int t;                               /* +0x5A (falling: the scale, 16.16 per cent) */
    int side;                            /* +0x5C */
    int fire_t, fires;                   /* +0x5E, +0x60 */
    int tx, ty;                          /* +0x62, +0x64 */
    int hittable;                        /* +6 bit 3 */
    int hit;                             /* +3 bit 6 ... */
    BYTE *hitter;                        /*   and the hitter (+0x34) */
    Spark sp[SPARKS];                    /* +0x46 (the fireball form) */
    int mode, cnt, ph1, ph2, ph3, ti, quit;   /* the dragon: +0x4E, +0x50, +0x46, +0x48, +0x4A, +0x4C, +0x52 */
    struct { int f, x, y; } trail[TRAIL];
    int guard;                           /* ticks no stomp or attack of Lomax's counts (B1_GUARD) */
    BYTE *box;                           /* defeated: the message's hint box (B.quit 1 while it is open) */
    WORD held;                           /*   the buttons held at the last tick */
} B;

/* the children (by slot): fireballs (state 0 WARN, 1 HEAD) and Booms (0 falling, 1 BOOM) */
static struct { BYTE *type; int kind, state, t, tx, ty, vx, vy, ground, anim; Spark sp[SPARKS]; } g_kid[MAX_ACTORS];

/* the stand-ins (README "Enemies" (The final boss)): the boss's sparks and shrinking self, the
   dragon's segments, each child's sparks, each Bos3 dude */
static BYTE g_si_spark[MAX_ACTORS][SPARKS][ACTOR_STRIDE];
static BYTE g_si_seg[SEGS][ACTOR_STRIDE], g_si_self[ACTOR_STRIDE];
static short g_sine[1024];               /* Flink's table 0x1491E: round(0x4000 * sin) */

static int b1_rand(void) { static unsigned r = 0x2B7E1516u; r = r * 1103515245u + 12345u; return (int)(r >> 16) & 0x7FFF; }

void bos1_reset(void)
{
    int k;
    memset(&B, 0, sizeof(B));
    memset(g_kid, 0, sizeof(g_kid));
    if (!g_sine[256])
        for (k = 0; k < 1024; k++) g_sine[k] = (short)floor(16384.0 * sin(k * 3.14159265358979 / 512) + 0.5);
}

static int is_boss(const BYTE *a) { return a && a == B.a && B.type && A_TYPE(a) == B.type; }

/* Flink's word writes of a 16.16 position's whole px (the fraction stays) */
static void set_wx(BYTE *a, int x) { A_FX(a) = (x << 16) | (A_FX(a) & 0xFFFF); set_pos(a); }
static void set_wy(BYTE *a, int y) { A_FY(a) = (y << 16) | (A_FY(a) & 0xFFFF); set_pos(a); }

/* Flink's camera (0x8746, 0x874A): its 320 x 224 screen in the middle of Lomax's view */
/* Lomax's view's width (Lomax+'s may be wider), at least Flink's */
static int cam_w(const BYTE *a)
{
    const BYTE *pl = PLANE(A_PLANE(a));
    int w = P_W(pl) * 16 - P_MAXX(pl);
    return P_MAXX(pl) <= 0 || w < CAM_W ? CAM_W : w;
}

static int cam_x(const BYTE *a) { return P_CAMX(PLANE(A_PLANE(a))) + (cam_w(a) - CAM_W) / 2; }

static int cam_y(const BYTE *a) { return P_CAMY(PLANE(A_PLANE(a))) + CAM_DY; }

/* Actor_MoveToward (0x1B008) on one axis, as metaboss.c's */
static void move_toward(int *v, int *pos, int target, int max, int acc)
{
    int p = (short)(*pos >> 16);
    if (target == p && abs(*v) <= acc) *v = 0;
    else if (target >= p) { if (*v < max) *v += *v < 0 ? 2 * acc : acc; }
    else if (-*v < max) *v -= *v > 0 ? 2 * acc : acc;
    *pos += *v;
}

/* 0x1A46E: the box (inclusive) against Lomax's hitbox: Player_TakeHit, unless he is hurt */
static int hurt_box(const BYTE *a, int x1, int y1, int x2, int y2, const char *what)
{
    const BYTE *pa = *G_PLAYERANIM;
    int px = *G_PLAYERX, py = *G_PLAYERY;
    if (!pa || A_PLANE(a) != *G_PLAYERPLANE || *G_PLAYERDEATHTIMER || *G_PLAYERHURTTIMER) return 0;
    if (x2 < px + HB_X1(pa) || x1 > px + HB_X2(pa) || y2 < py + HB_Y1(pa) || y1 > py + HB_Y2(pa)) return 0;
    flog("final boss's %s hurts Lomax at (%d,%d)", what, px, py);
    Player_TakeHit(A_X(a), 0, 0);
    return 1;
}

/* 0x1A462: its record's hitbox */
static void hurt_touch(const BYTE *a, const char *what)
{
    int x1, y1, x2, y2;
    if (actor_box(a, &x1, &y1, &x2, &y2)) hurt_box(a, x1, y1, x2, y2, what);
}

/* Actor_CheckStompedByPlayer (0x1A352) on its hitbox */
static int stomped(BYTE *a)
{
    int x1, y1, x2, y2;
    return actor_box(a, &x1, &y1, &x2, &y2) && flink_stomp_box(A_PLANE(a), x1, y1, x2, y2);
}

/* the box thrown objects, stomps and Lomax's attacks hit: the dragon's (DRAGON_BOX*, 0x1A5A0's) or
   his record's hitbox */
static int boss_box(const BYTE *a, int *x1, int *y1, int *x2, int *y2)
{
    if (B.state == S_DRAGON || B.state == S_DYING) {
        *x1 = A_X(a) + DRAGON_BOX1; *x2 = A_X(a) + DRAGON_BOX2;
        *y1 = A_Y(a) + DRAGON_BOX1; *y2 = A_Y(a) + DRAGON_BOX2;
        return 1;
    }
    return actor_box(a, x1, y1, x2, y2);
}

/* not Flink's: Lomax's stomp (with his bounce) or his spin, roll or helmet on the box, unless guarded
   (B1_GUARD after each); 1 = a hit */
static int lomax_hit(BYTE *a)
{
    int x1, y1, x2, y2;
    if (B.guard || *G_PLAYERDEATHTIMER || !boss_box(a, &x1, &y1, &x2, &y2)) return 0;
    if (flink_stomp_box(A_PLANE(a), x1, y1, x2, y2)) {
        Player_BounceOffEnemy();
        stomp_bounce();
    } else if (!player_attack_box(A_PLANE(a), x1, y1, x2, y2)) return 0;
    B.guard = B1_GUARD;
    flog("final boss hit by Lomax");
    return 1;
}

static int near_view(const BYTE *a, int margin)
{
    const BYTE *pl = PLANE(A_PLANE(a));
    int w = P_W(pl) * 16 - P_MAXX(pl), x = A_X(a) - P_CAMX(pl), y = A_Y(a) - P_CAMY(pl);
    if (P_MAXX(pl) <= 0 || w < CAM_W) w = CAM_W;
    return x >= -margin && x < w + margin && y >= -margin && y < 240 + margin;
}

/* a level type whose name starts with prefix (Flink's 0x32BD4 finds it by its 4-letter code) */
static BYTE *type_named(const char *prefix)
{
    int n = *G_NUMOBJTYPES, k;
    for (k = 0; k < n && k < 0x100; k++) {
        BYTE *t = G_OBJTYPES[k];
        if (t && !strncmp(T_NAME(t), prefix, strlen(prefix))) return t;
    }
    return NULL;
}

/* ---- Drawing ------------------------------------------------------------------------------------ */

/* record `tag`'s frame (NULL: src's own frame) of src's type, drawn through stand-in s with its
   record's origin at x, y, scaled (per mille) around it; behind: on the background plane at that
   place on the screen, under the play map's tiles (Flink: no sprite priority on a Priority map) */
static void piece(const BYTE *src, BYTE *s, const char *tag, int frame, int x, int y, int scale, int behind, int mirror)
{
    const BYTE *pl = PLANE(A_PLANE(src)), *bg = PLANE(BG_PLANE);
    memcpy(s, src, ACTOR_STRIDE);
    A_FLAGS(s) &= ~(AF_HIDDEN | AF_LEFT);
    if (mirror) A_FLAGS(s) |= AF_LEFT;
    A_HIDE(s) = 0;
    *(int *)(s + 0x78) = 0;                  /* (Actor_Draw's blink counter) */
    if (tag) {
        Actor_SetAnim(s, FOURCC(tag));
        hold_frame(s, frame);
        Actor_AdvanceAnim(s);
    }
    if (behind) {
        A_PLANE(s) = BG_PLANE;
        x += P_CAMX(bg) - P_CAMX(pl);
        y += P_CAMY(bg) - P_CAMY(pl);
    }
    A_X(s) = x; A_Y(s) = y;
    A_FX(s) = x << 16; A_FY(s) = y << 16;
    Actor_Draw(s, scale, 0, 0);
}

/* the sparks (0x2EF6C): TAIL's frame (the phase's high byte), its sprite's top-left at the spark */
static void sparks_draw(const BYTE *a, BYTE (*si)[ACTOR_STRIDE], const Spark *sp, const char *tag, int dy)
{
    int k;
    for (k = 0; k < SPARKS; k++)
        if (sp[k].y >= 0 && (sp[k].phase >> 8 & 0xFF) < 3)
            piece(a, si[k], tag, sp[k].phase >> 8 & 0xFF, sp[k].x + TAIL_X, sp[k].y + dy, 1000, 1, 0);
}

/* 0x2EE0C */
static void sparks_init(Spark *sp)
{
    int k;
    for (k = 0; k < SPARKS; k++) {
        sp[k].phase = 0x400 - 0x155 * k;
        sp[k].x = sp[k].y = -1;
    }
}

/* 0x21E62 (any: a high byte of 3 or more) and Boom's 0x2176E (3 only): the next phase, a spark
   back at the actor */
static void sparks_step(Spark *sp, const BYTE *a, int dy, int any)
{
    int k, hi;
    for (k = 0; k < SPARKS; k++) {
        sp[k].phase = (sp[k].phase + SPARK_STEP) & 0xFFFF;
        hi = (signed char)(sp[k].phase >> 8);
        if (any ? hi < 3 : hi != 3) continue;
        sp[k].phase &= 0xFF;
        sp[k].x = A_X(a) - SPARK_DX;
        sp[k].y = A_Y(a) - dy;
    }
}

/* ---- The Bos3 dudes ------------------------------------------------------------------------------ */

static void __cdecl dude_update(BYTE *a)
{
    int i = enemy_slot(a), frame = 1;
    Enemy *e;
    if (i < 0 || !g_enemy[i].bos3) return;
    e = &g_enemy[i];
    if (e->dead) { enemy_dead_update(a, i); return; }
    switch (enemy_attacked(a, i)) {
    case 2: enemy_die(a, i, 1); return;
    case 1: dummy_stomped(a, i, 1); if (e->dead) return;
    }
    if (e->timer > 0) {                      /* stunned (0x1D074) */
        fall_and_land(a, 0x1800, 1);
        if (--e->timer > 0) stun_frame(a, i);
        else { A_VY(a) = -0x30000; set_anim(a, "MOVE"); }   /* 3.0 up */
        Actor_AdvanceAnim(a);
        return;
    }
    e->hop = 0;                              /* (dummy_stomped's, or landed from a throw) */
    set_anim(a, "MOVE");
    if (fall_and_land_g(a, DUDE_GRAVITY, DUDE_FRICTION, 1)) {   /* on the ground (0x1AE96) */
        frame = 0;
        if (!e->t) {
            e->t = DUDE_WAIT;
            if (enemy_on_screen(a)) flink_sound(a, 0x29);
        }
        if (!--e->t) A_VY(a) = DUDE_HOP;
    } else {                                 /* rising or in the air: its way (0x220BC) */
        A_FX(a) += A_FLAGS(a) & AF_LEFT ? -DUDE_STEP : DUDE_STEP;
        set_pos(a);
    }
    hold_frame(a, frame);
    Actor_AdvanceAnim(a);
}

#define DUDE_LAYER 6                 /* Lomax's layer (bucket + 6) */

/* drawn as Lomax is: on the play plane at his layer, in front of the map's tiles (Flink's has no
   priority bit, which would put it behind 24-2's rocks and floor) */
static void __cdecl dude_draw(BYTE *a)
{
    draw_actor(a);
}

static void dude_init(BYTE *d, int k, int layer)
{
    memset(&g_enemy[k], 0, sizeof(g_enemy[k]));
    g_enemy[k].type = A_TYPE(d);
    g_enemy[k].bos3 = 1;
    A_UPDATE(d) = (void *)dude_update;
    A_DRAW(d) = (void *)dude_draw;
    A_FLAGS(d) = AF_HURTS | AF_HITTABLE;
    if (A_X(d) >= *G_PLAYERX) A_FLAGS(d) |= AF_LEFT;   /* (its sprites face right) */
    A_LAYER(d) = (short)layer;
    A_CULL(d) = 1;                           /* (gone off the screen: Flink's window 0) */
    A_VX(d) = A_VY(d) = 0;
    A_RECORD(d) = NULL;                      /* (Actor_Alloc leaves the slot's last one) */
    Actor_SetAnim(d, FOURCC("MOVE"));
}

/* (from enemy_spawn) a placed one (24-2 places one never to spawn) */
void bos3_spawn(BYTE *a, int i) { dude_init(a, i, DUDE_LAYER); }

/* 0x32BD4 'Bos3': a Bos3 dude at x, y */
static void dude_new(BYTE *a, int x, int y, int layer)
{
    BYTE *t = type_named("Muta Bos3"), *d = t ? Actor_Alloc(a, T_NAME(t)) : NULL;
    int k = d ? slot_of(d) : -1;
    if (k < 0) return;
    A_X(d) = x; A_Y(d) = y;
    A_FX(d) = x << 16; A_FY(d) = y << 16;
    dude_init(d, k, layer);
    if (!A_RECORD(d)) { g_enemy[k].type = NULL; Actor_Despawn(d); return; }
    flink_sound(a, 0x1A);
    flog("final boss drops a bos3 dude at (%d,%d)", x, y);
}

/* ---- The fireballs and Booms -------------------------------------------------------------------- */

static int kid_of(const BYTE *d, int kind)
{
    int k = slot_of(d);
    return k >= 0 && g_kid[k].kind == kind && g_kid[k].type == A_TYPE(d) && A_LAYER(d) != KILLED_LAYER ? k : -1;
}

static void kid_free(BYTE *d, int k)
{
    g_kid[k].kind = K_NONE;
    Actor_Despawn(d);
}

static BYTE *kid_new(BYTE *a, int kind, const char *tag, int layer, int x, int y)
{
    BYTE *d = Actor_Alloc(a, NULL);
    int k;
    if (!d || (k = slot_of(d)) < 0) return NULL;
    memset(&g_kid[k], 0, sizeof(g_kid[k]));
    g_enemy[k].type = NULL;                  /* (not an enemy) */
    g_kid[k].kind = kind;
    g_kid[k].type = A_TYPE(d);
    A_FLAGS(d) = 0;                          /* (touches nothing: Flink's tests) */
    A_CULL(d) = CULL_NEVER;
    A_LAYER(d) = (short)layer;
    A_VX(d) = A_VY(d) = 0;
    A_X(d) = x; A_Y(d) = y;
    A_FX(d) = x << 16; A_FY(d) = y << 16;
    A_RECORD(d) = NULL;
    Actor_SetAnim(d, FOURCC(tag));
    if (!A_RECORD(d)) { g_kid[k].kind = K_NONE; Actor_Despawn(d); return NULL; }
    sparks_init(g_kid[k].sp);
    return d;
}

/* 0x21F24 (WARN) and 0x21F4A (HEAD) */
static void __cdecl fire_update(BYTE *d)
{
    int k = kid_of(d, K_FIRE);
    if (k < 0) return;
    if (!near_view(d, NEAR_W)) { kid_free(d, k); return; }   /* (out of Flink's mid window) */
    if (!g_kid[k].state) {
        if (--g_kid[k].t < 0) {
            g_kid[k].state = 1;
            set_anim(d, "HEAD");
            sparks_init(g_kid[k].sp);
            flink_sound(d, 0x3C);
        }
        Actor_AdvanceAnim(d);
        return;
    }
    move_toward(&g_kid[k].vx, &A_FX(d), g_kid[k].tx, FIRE_MAX, FIRE_ACC);
    move_toward(&g_kid[k].vy, &A_FY(d), g_kid[k].ty, FIRE_MAX, FIRE_ACC);
    set_pos(d);
    hurt_touch(d, "fireball");
    sparks_step(g_kid[k].sp, d, SPARK_DY, 1);
    Actor_AdvanceAnim(d);
}

/* 0x220EA: the sparks, then the fireball */
static void __cdecl fire_draw(BYTE *d)
{
    int k = kid_of(d, K_FIRE);
    if (k < 0) return;
    if (g_kid[k].state) sparks_draw(d, g_si_spark[k], g_kid[k].sp, "TAIL", TAIL_Y);
    draw_actor(d);
}

/* the hook 0x21E96 (STA2's point 1, at each draw) */
static void fire_hook(BYTE *a)
{
    BYTE *d;
    int k, x = A_X(a) + (A_FLAGS(a) & AF_LEFT ? -B1_FIRE_X : B1_FIRE_X), y = A_Y(a) + B1_FIRE_Y;
    if (--B.fire_t) return;
    B.fire_t = B1_FIRE_EVERY;
    if (--B.fires < 0) return;
    if (!(d = kid_new(a, K_FIRE, "WARN", FLINK_LAYER(1), x, y))) return;
    k = slot_of(d);
    g_kid[k].t = FIRE_WARN;
    g_kid[k].tx = A_FLAGS(a) & AF_LEFT ? FIRE_AWAY : -FIRE_AWAY;
    g_kid[k].ty = y + FIRE_DY[b1_rand() & 3];
    A_UPDATE(d) = (void *)fire_update;
    A_DRAW(d) = (void *)fire_draw;
    flog("final boss casts a fireball at (%d,%d), %d more", x, y, B.fires);
}

/* 0x2176E (falling) and 0x21804 (BOOM) */
static void __cdecl boom_update(BYTE *d)
{
    int k = kid_of(d, K_BOOM), n;
    if (k < 0) return;
    if (g_kid[k].state) {
        n = A_RECORD(d) ? A_NFRAMES(d) : 0;
        g_kid[k].anim += A_RECORD(d) ? REC_SPEED(A_RECORD(d)) : 0x100;
        if (g_kid[k].anim >> 8 >= n) { kid_free(d, k); return; }   /* played once */
        hold_frame(d, g_kid[k].anim >> 8);
        Actor_AdvanceAnim(d);
        hurt_touch(d, "boom");
        return;
    }
    sparks_step(g_kid[k].sp, d, BOOM_SPARK_DY, 0);
    move_toward(&g_kid[k].vx, &A_FX(d), *G_PLAYERX, BOOM_MAX, BOOM_ACC);
    move_toward(&g_kid[k].vy, &A_FY(d), g_kid[k].ground, BOOM_MAX, BOOM_ACC);
    set_pos(d);
    if (A_Y(d) < g_kid[k].ground) { Actor_AdvanceAnim(d); return; }
    g_kid[k].state = 1;
    g_kid[k].anim = 0;
    place_y(d, g_kid[k].ground);
    A_LAYER(d) = (short)FLINK_LAYER(1);      /* (a5) = 0x7D6C */
    Actor_SetAnim(d, FOURCC("BOOM"));
    hold_frame(d, 0);
    Actor_AdvanceAnim(d);
    flink_sound(d, 0x35);
    flog("boom bursts at (%d,%d)", A_X(d), A_Y(d));
}

/* 0x21818: the sparks, then Boom */
static void __cdecl boom_draw(BYTE *d)
{
    int k = kid_of(d, K_BOOM);
    if (k < 0) return;
    if (!g_kid[k].state) sparks_draw(d, g_si_spark[k], g_kid[k].sp, "BTAL", BTAL_Y);
    draw_actor(d);
}

/* 0x32BD4 'Boom' (0x216EA), falling to y ground */
static void boom_new(BYTE *a, int x, int y, int ground)
{
    BYTE *d = kid_new(a, K_BOOM, "BHED", FLINK_LAYER(2), x, y);
    if (!d) return;
    g_kid[slot_of(d)].ground = ground;
    A_UPDATE(d) = (void *)boom_update;
    A_DRAW(d) = (void *)boom_draw;
    flink_sound(d, 0x2F);
    flog("the dragon drops a boom at (%d,%d)", x, y);
}

/* ---- The sorcerer --------------------------------------------------------------------------------- */

/* 0x21E3C: the fireball form (HEAD), its sparks */
static void fireball_form(BYTE *a)
{
    set_anim(a, "HEAD");
    sparks_init(B.sp);
}

static void face_lomax(BYTE *a)
{
    if (A_X(a) <= *G_PLAYERX) A_FLAGS(a) |= AF_LEFT;   /* (mirrored: his sprites face left) */
    else A_FLAGS(a) &= ~AF_LEFT;
}

/* 0x21D4A: to Flink's other side, then he forms */
static void rise(BYTE *a)
{
    int w = P_W(PLANE(A_PLANE(a))) * 16, tx = *G_PLAYERX + B.side, ty = B.home - B1_HOVER_DY;
    if (tx <= B1_EDGE) tx = B1_EDGE;
    if (tx >= w - B1_EDGE) tx = w - B1_EDGE;
    move_toward(&B.vx, &A_FX(a), tx, B1_FORM_MAX, B1_FORM_ACC);
    move_toward(&B.vy, &A_FY(a), ty, B1_FORM_MAX, B1_FORM_ACC);
    set_pos(a);
    if (B.vy >= 0 && abs(ty - A_Y(a)) <= B1_ARRIVE) {
        flink_sound(a, 0x51);
        B.state = S_FORM;
        B.t = B1_FORM_T;
        B.fire_t = B1_FIRE_WAIT + (b1_rand() & 0x1F);
        B.fires = B1_FIRES + (b1_rand() & 3);
        face_lomax(a);
        flog("final boss forms at (%d,%d), %d fireballs", A_X(a), B.home, B.fires);
    }
    sparks_step(B.sp, a, SPARK_DY, 1);
    Actor_AdvanceAnim(a);
}

/* 0x21CC4: ZAP backwards, the burst shrinking into him */
static void form(BYTE *a)
{
    place_y(a, B.home);
    set_anim(a, "ZAP ");
    if (--B.t < 0) { B.state = S_STA2; B.t = B1_STA2_AFTER; return; }
    hold_frame(a, B.t >> 2);
    Actor_AdvanceAnim(a);
}

/* 0x21D02: ZAP forwards, then the fireball form again (0x21D4A at once) */
static void unform(BYTE *a)
{
    set_anim(a, "ZAP ");
    if (--B.t >= 0) {
        hold_frame(a, 3 - (B.t >> 2));
        Actor_AdvanceAnim(a);
        return;
    }
    fireball_form(a);
    B.vy = B1_RISE_VY;
    B.vx = 0;
    B.state = S_RISE;
    set_wy(a, B.home - B1_HOVER_DY);
    rise(a);
}

/* 0x2182A: casting fireballs */
static void sta2(BYTE *a)
{
    set_anim(a, "STA2");
    if (B.fires < 0 && --B.t < 0) {
        B.state = S_STAY;
        B.t = B1_STAY_T;
        set_anim(a, "STAY");
    }
    hurt_touch(a, "touch");
    Actor_AdvanceAnim(a);
}

/* 0x21B68: the second phase, flying */
static void fly_start(BYTE *a)
{
    int to = B2_AWAY;
    B.state = S_FLY;
    B.hits = B1_HITS2;
    B.hittable = 1;
    A_FLAGS(a) |= AF_LEFT;
    if (*G_PLAYERX - A_X(a) >= 0) { A_FLAGS(a) &= ~AF_LEFT; to = -to; }   /* away from Flink */
    B.tx = to;
    B.ty = B.home - B2_TY;
    B.vy = B2_VY;
    B.vx = 0;
    B.fire_t = B2_DROP_FIRST;
    flog("final boss flies (phase 2)");
}

static int hit_taken(void);

/* 0x21B56: standing to be stomped (here hit by a thrown object, Lomax's spin, roll or helmet too) */
static void stay(BYTE *a)
{
    const BYTE *r = A_RECORD(a);
    int x1, y1, x2, y2;
    if (--B.t < 0) {
        if (B.hits <= 0) { fly_start(a); return; }
        flink_sound(a, 0x52);
        B.state = S_UNFORM;
        B.t = B1_FORM_T;
        B.side = A_FLAGS(a) & AF_LEFT ? B1_SIDE : -B1_SIDE;
        return;
    }
    if (r && *(const DWORD *)(r + 4) == FOURCC("OUCH")) { B.hit = 0; return; }
    if (stomped(a)) {
        Player_BounceOffEnemy();             /* (Flink: g_PlayerBounceReq) */
        stomp_bounce();
    } else if (!hit_taken() && !(boss_box(a, &x1, &y1, &x2, &y2) && player_attack_box(A_PLANE(a), x1, y1, x2, y2))) {
        hurt_touch(a, "touch");
        return;
    }
    B.hit = 0;
    spirit_release(a, A_X(a), A_Y(a) - B1_SPIRIT_DY);
    B.hits--;
    B.t = B1_OUCH_T;
    flink_quake_set(B1_QUAKE);
    flink_sound(a, 0x2B);
    set_anim(a, "OUCH");
    Actor_AdvanceAnim(a);
    flog("final boss hit, %d left", B.hits);
}

/* the hit flag (+3 bit 6) taken: 1 for any thrown object (Flink's think 0x1B1AA counts only a hitter
   that dies of it: a thrown Bos3 dude) */
static int hit_taken(void)
{
    if (!B.hit) return 0;
    B.hit = 0;
    B.hitter = NULL;
    return 1;
}

static void dragon_start(BYTE *a);
static void fall(BYTE *a);

/* 0x2186A: flying across, dropping Bos3 dudes */
static void fly(BYTE *a)
{
    int fc = cam_x(a);
    if (hit_taken() | lomax_hit(a)) {
        flink_sound(a, 0x2B);
        flink_quake_set(B1_QUAKE);
        if (--B.hits > 0) {
            B.state = S_HURT;
            set_anim(a, "OUCH");
            Actor_AdvanceAnim(a);
            flog("final boss hit, %d left", B.hits);
            return;
        }
        B.state = S_FALL;                    /* 0x218D6 */
        B.t = FALL_SCALE << 16;
        A_FLAGS(a) &= ~AF_LEFT;
        B.vy = 0;
        A_LAYER(a) = (short)FLINK_LAYER(3);
        set_anim(a, "OUCH");
        Actor_AdvanceAnim(a);
        flog("final boss falls at (%d,%d)", A_X(a), A_Y(a));
        fall(a);                             /* (at once: bra 0x219CC) */
        return;
    }
    if (!(B.frame & 0x1F)) B.ty = B.home - B2_HIGH + (b1_rand() & 0x1F) - 16;
    if (!(A_FLAGS(a) & AF_LEFT)) {
        if (A_X(a) <= fc + B2_LEFT) { A_FLAGS(a) |= AF_LEFT; B.tx = B2_AWAY; }
    } else if (A_X(a) >= fc + B2_RIGHT) { A_FLAGS(a) &= ~AF_LEFT; B.tx = -B2_AWAY; }
    move_toward(&B.vx, &A_FX(a), B.tx, B2_MAX, B2_ACC);
    move_toward(&B.vy, &A_FY(a), B.ty, B2_MAX, B2_ACC);
    set_pos(a);
    set_anim(a, "FLY ");
    Actor_AdvanceAnim(a);
    if (--B.fire_t < 0) {                    /* FLY's point 2 (0x21F68) */
        B.fire_t = B2_DROP_EVERY;
        dude_new(a, A_X(a) + (A_FLAGS(a) & AF_LEFT ? -B2_DROP_X : B2_DROP_X), A_Y(a) + B2_DROP_Y, DUDE_LAYER);
    }
}

/* 0x219AE: OUCH while the shake runs */
static void hurt(BYTE *a)
{
    set_anim(a, "OUCH");
    Actor_AdvanceAnim(a);
    if (!flink_quake()) B.state = S_FLY;
}

/* 0x219CC: falling and shrinking, then the dragon */
static void fall(BYTE *a)
{
    if (A_Y(a) >= FALL_END) { dragon_start(a); return; }
    B.vy += FALL_ACC;
    A_FY(a) += B.vy;
    set_pos(a);
    B.t -= FALL_SHRINK;
    if (B.t >> 16 < FALL_SCALE_MIN) B.t = (FALL_SCALE_MIN << 16) | (B.t & 0xFFFF);
}

/* ---- The dragon ----------------------------------------------------------------------------------- */

static void dragon_start(BYTE *a)
{
    int k;
    B.state = S_DRAGON;
    A_LAYER(a) = (short)FLINK_LAYER(1);      /* (a5) = 0x7D6C */
    scroll_stop_x();                         /* 0x8723 */
    B.mode = 0;
    B.ti = 0;
    B.hits = B1_HITS3;
    B.hit = 0;
    B.hitter = NULL;
    for (k = 0; k < TRAIL; k++) B.trail[k].f = -1;
    flog("final boss becomes the dragon (phase 3)");
}

/* 0x213D2: the next trail entry */
static void record(BYTE *a, int f)
{
    if (++B.ti >= TRAIL) B.ti = 0;
    B.trail[B.ti].f = f;
    B.trail[B.ti].x = A_X(a);
    B.trail[B.ti].y = A_Y(a);
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* moves 3 and 1 (0x21096, 0x2129C): up from below, hovering, down again; 0 when the next is picked */
static int hover(BYTE *a, int m)
{
    if (B.mode < 0) {
        if (m == 3) B.hittable = 1;
        A_LAYER(a) = (short)FLINK_LAYER(3);  /* (a5) = 0x7D70 */
        B.mode = m;
        set_wy(a, M3_Y);
        B.vy = M_VY;
        set_wx(a, cam_x(a) + M_MID);
        B.ph1 = 0x100;
        B.ph3 = 0;
        B.cnt = ((m == 3 ? M3_WAIT + (b1_rand() & 7) : M1_WAIT + (b1_rand() & 3))) * 60;
        flog("the dragon rises to hover (move %d, %d frames)", m, B.cnt);
    } else if (--B.cnt >= 0) {
        if (B.vy) B.vy += M_DVY;
    } else {
        B.vy += M_DVY;
        if (B.vy == -M_VY) {
            if (m == 3) B.hittable = 0;
            return 0;
        }
    }
    if (m == 3) {
        if (!B.vy && !(B.frame & 0x7F)) dude_new(a, A_X(a) + DUDE_DX, A_Y(a) + DUDE_DY, DUDE_LAYER);
        B.ph1 = (B.ph1 + 3) & 0x3FF;
        A_FX(a) += g_sine[B.ph1] * 10;
        B.ph2 = (B.ph2 + 9) & 0x3FF;
        A_FY(a) += g_sine[B.ph2] * 5;
    } else {
        if (!B.vy && !(B.frame & 0x3F) && !*G_PLAYERDEATHTIMER) boom_new(a, A_X(a) + BOOM_DX, A_Y(a) + BOOM_DY, B.home);
        B.ph1 = (B.ph1 + 5) & 0x3FF;
        A_FX(a) += g_sine[B.ph1] * 8;
        B.ph2 = (B.ph2 + 8) & 0x3FF;
        A_FY(a) += g_sine[B.ph2] * 4;
    }
    A_FY(a) += B.vy;
    set_pos(a);
    B.ph3++;
    if (m == 3) record(a, clampi(abs(((B.ph3 >> 3) & 7) - 4) + (abs(B.vy) / 0x1000 > 5 ? 5 : abs(B.vy) / 0x1000), 3, 7));
    else record(a, clampi(abs(((B.ph3 >> 3) & 7) - 4) + 5, 0, 7));
    return 1;
}

/* move 2 (0x211F4): a leap up from behind the tiles at Flink, falling back near */
static int leap(BYTE *a)
{
    int d;
    if (B.mode < 0) {
        int vx = M2_VX, dx = M2_DXL;
        A_LAYER(a) = (short)FLINK_LAYER(1);  /* (a5) = 0x7D6C */
        B.mode = 2;
        B.cnt = M2_START;
        set_wy(a, M2_Y);
        B.vy = M2_VY;
        set_wx(a, *G_PLAYERX);
        if (!(cam_x(a) + M_MID > A_X(a))) { vx = -vx; dx = M2_DXR; }
        B.vx = vx;
        set_wx(a, A_X(a) + dx);
        flog("the dragon leaps at x %d", A_X(a));
    } else {
        B.vy += M2_DVY;
        if (B.vy == M2_END) return 0;
    }
    A_FY(a) += B.vy;
    A_FX(a) += B.vx;
    set_pos(a);
    d = ++B.cnt / M2_STEP;
    record(a, 7 - clampi(d, 0, 7));
    return 1;
}

static void dragon_die(BYTE *a);
static void dying(BYTE *a);

/* 0x20F96 */
static void dragon(BYTE *a)
{
    int n;
    if ((hit_taken() | (B.hittable && !B.vy && lomax_hit(a))) && !B.vy && !*G_PLAYERDEATHTIMER) {
        flink_sound(a, 0x2B);
        flink_quake_set(B1_QUAKE);
        if (--B.hits <= 0) { dragon_die(a); return; }
        flog("the dragon hit, %d left", B.hits);
    }
    for (n = 0; n < 4; n++) {                /* (a new move starts in the same frame) */
        int m = abs(B.mode);
        if ((m == 1 || m == 3) && hover(a, m)) break;
        if (m == 2 && leap(a)) break;
        B.mode = MODE_PICK[b1_rand() & 7];
    }
    /* 0x2168C: in move 2 a near segment falling is drawn in front and hurts */
    if (B.mode == 2 && B.vy > 0)
        for (n = 0; n < SEGS; n++) {
            int e = (B.ti - n * SEG_GAP + TRAIL) % TRAIL;
            if (B.trail[e].f >= 0 && B.trail[e].f <= DRAGON_NEAR) {
                hurt_box(a, A_X(a), A_Y(a), A_X(a) + DRAGON_HURT, A_Y(a) + DRAGON_HURT, "dragon");
                break;
            }
        }
}

/* 0x2101E: defeated */
static void dragon_die(BYTE *a)
{
    B.state = S_DYING;
    B.cnt = DIE_T;
    B.quit = 0;
    B.vx = DIE_VX;
    B.vy = DIE_VY;
    B.hittable = 0;
    boss_untouchable();
    forget_placement(A_DEF(a));
    enemy_defeated(a);
    music_play_once(B.win);                  /* (a fanfare: Lomax would loop it) */
    flog("final boss defeated");
    dying(a);                                /* (at once: bra 0x21414) */
}

/* the level's end, as the Finish's banner does it (Flink: Level_CompleteMapPoint 0x2EBC8) */
static void level_end(void)
{
    *G_LASTREALSECTION = *G_LOADEDSECTION + 1;
    *G_BGOTOMAP = 1;
    *G_BSECTIONCHANGE = 1;
    if (!*G_FADESTEP) *G_FADESTEP = -0x40;
    *G_BFADEOUTMUSIC = 1;
    flog("level %d-%d ends: the final boss is defeated", *G_LOADEDSECTION + 1, *G_CURSUBLEVEL + 1);
}

/* Flink's message at the end (0x214AC: START pressed with 0x81A6 = -17, message 16 of HelpTextTable;
   its window shuts at START, A, B or C, 0x188E8, with sound 0x40, 0x1574A), in Lomax's hint box: 1
   once it is shut or could not open */
static int message_done(BYTE *a)
{
    WORD held = *G_INPUTHELD, press = held & (WORD)~B.held;
    B.held = held;
    if (!B.box) return 1;
    if (*(int *)(B.box + 0x88) || !(press & MSG_BUTTONS)) return 0;
    *(int *)(B.box + 0x80) = 1;              /* (hint_text_draw despawns it) */
    B.box = NULL;
    flink_sound(a, 0x40);
    flog("the final message shut");
    return 1;
}

/* 0x21414: dying, then the game's end */
static void dying(BYTE *a)
{
    int fc = cam_x(a), fy = cam_y(a);
    boss_untouchable();                      /* (hurt timer -2) */
    if (!(--B.cnt & 0x1F)) {
        flink_sound(a, 0x36);
        flink_quake_set(B1_QUAKE);
        spirit_release(a, A_X(a) + DIE_SPIRIT_DX, A_Y(a) + DIE_SPIRIT_DY);
    }
    if (B.cnt < 0) {                         /* Flink's pad cleared */
        *G_BLEVELENDING = *G_BINPUTLOCKED = 1;
        *G_PLAYERVX = 0;
        if (!B.quit && !(*G_PLAYERSTATE & PS_AIR)) {
            B.quit = 1;
            B.held = *G_INPUTHELD;
            B.box = hint_box(B1_MESSAGE, cam_w(a));
        }
        if (B.quit == 1 && message_done(a)) { B.quit = 2; level_end(); }
    }
    if (A_X(a) < fc + DIE_LEFT && B.vx < 0) B.vx = -B.vx;
    if (A_X(a) > fc + DIE_RIGHT && B.vx >= 0) B.vx = -B.vx;
    if (A_Y(a) < fy + DIE_TOP && B.vy < 0) B.vy = -B.vy;
    if (A_Y(a) > fy + DIE_BOTTOM && B.vy >= 0) B.vy = -B.vy;
    A_FX(a) += B.vx;
    A_FY(a) += B.vy;
    B.ph1 = (B.ph1 + 13) & 0x3FF;
    A_FX(a) += g_sine[B.ph1] * 8;
    B.ph2 = (B.ph2 + 17) & 0x3FF;
    A_FY(a) += g_sine[B.ph2] * 4;
    set_pos(a);
    record(a, abs(((B.frame >> 3) & 7) - 4) + 2);
}

/* 0x215CC / 0x215EE / 0x21628: the 8 segments, the newest (the head) first, each at its depth's
   scale centred in its 48 x 48 box; behind the tiles but the near ones falling in move 2 */
static void dragon_draw(BYTE *a)
{
    int n;
    for (n = 0; n < SEGS; n++) {
        int e = (B.ti - n * SEG_GAP + TRAIL) % TRAIL, f = B.trail[e].f, sc, ox, oy, cx, cy, front;
        if (f < 0) continue;
        sc = (DRAGON_SCALE0 - 8 * f) * 10;
        if (!n) { ox = B.trail[e].x + DRAGON_HEAD_DX - DHED_X; oy = B.trail[e].y - DHED_Y; }
        else { ox = B.trail[e].x - DTAL_X; oy = B.trail[e].y - DTAL_Y; }
        cx = (n ? DTAL_X : DHED_X) + DRAGON_BOX / 2;     /* the box's middle, from the origin */
        cy = (n ? DTAL_Y : DHED_Y) + DRAGON_BOX / 2;
        ox += cx * (1000 - sc) / 1000;
        oy += cy * (1000 - sc) / 1000;
        front = B.mode == 2 && f <= DRAGON_NEAR && B.vy > 0;    /* (in the boss's draw list 1) */
        piece(a, g_si_seg[n], n ? "DTAL" : "DHED", 0, ox, oy, sc, !front, 0);
    }
}

/* ---- The boss --------------------------------------------------------------------------------------- */

static void __cdecl bos1_update(BYTE *a)
{
    const BYTE *r;
    if (!is_boss(a)) return;
    B.frame++;
    boss_fighting();                         /* (only HELMET: spells.c) */
    if (B.guard) B.guard--;
    switch (B.state) {
    case S_RISE:   rise(a); break;
    case S_FORM:   form(a); break;
    case S_STA2:   sta2(a); break;
    case S_STAY:   stay(a); break;
    case S_UNFORM: unform(a); break;
    case S_FLY:    fly(a); break;
    case S_HURT:   hurt(a); break;
    case S_FALL:   fall(a); break;
    case S_DRAGON: dragon(a); break;
    case S_DYING:  dying(a); break;
    }
    /* point 1 of STA2, STAY and OUCH in the first phase (the hook table 0x220DE, at each draw) */
    r = A_RECORD(a);
    if ((B.state == S_STA2 || B.state == S_STAY) && r &&
        (*(const DWORD *)(r + 4) == FOURCC("STA2") || *(const DWORD *)(r + 4) == FOURCC("STAY") || *(const DWORD *)(r + 4) == FOURCC("OUCH")))
        fire_hook(a);
}

static void __cdecl bos1_draw(BYTE *a)
{
    int sc;
    if (!is_boss(a) || A_FLAGS(a) & AF_HIDDEN) return;
    switch (B.state) {
    case S_RISE:                             /* 0x220EA: the sparks, then the fireball form */
        sparks_draw(a, g_si_spark[slot_of(a)], B.sp, "TAIL", TAIL_Y);
        draw_actor(a);
        break;
    case S_FALL:                             /* 0x21B14: shrunk toward OUCH's top-left, behind the tiles */
        sc = (B.t >> 16) * 10;
        piece(a, g_si_self, NULL, 0, A_X(a) + SHRINK_X * (1000 - sc) / 1000, A_Y(a) + SHRINK_Y * (1000 - sc) / 1000, sc, 1, 0);
        break;
    case S_DRAGON:
    case S_DYING:
        dragon_draw(a);
        break;
    default:
        draw_actor(a);
    }
}

/* grab.c's strike: thrown object o's box touched t; 1 if t is the boss and thrown objects reach him
   (+6 bit 3): his hit flag (Flink's Actor_FindTouchingActor 0x1A5A0) */
int bos1_struck(BYTE *t, BYTE *o, int x1, int y1, int x2, int y2)
{
    const BYTE *r;
    int tx1, ty1, tx2, ty2;
    if (!is_boss(t)) return 0;
    r = A_RECORD(t);
    if (!B.hittable && !(B.state == S_STAY && !(r && *(const DWORD *)(r + 4) == FOURCC("OUCH")))) return 0;   /* (standing to be stomped: here a thrown object too) */
    if (!boss_box(t, &tx1, &ty1, &tx2, &ty2)) return 0;
    if (x2 < tx1 || x1 > tx2 || y2 < ty1 || y1 > ty2) return 0;
    if (!B.hit) {
        B.hit = 1;
        B.hitter = o;
    }
    return 1;
}

/* (from enemy_spawn) param 1: the music at his defeat (0x20E86) */
void bos1_spawn(BYTE *a, const BYTE *def)
{
    int k;
    bos1_reset();
    B.a = a;
    B.type = A_TYPE(a);
    B.finish = def && D_FLAGS(def) & 0x8000 ? D_NEXT(def) : NULL;
    B.win = def_param(a, def, 1, 0);
    B.hits = B1_HITS1;
    B.home = A_Y(a);
    B.side = B1_SIDE;
    B.state = S_RISE;
    for (k = 0; k < TRAIL; k++) B.trail[k].f = -1;
    A_UPDATE(a) = (void *)bos1_update;
    A_DRAW(a) = (void *)bos1_draw;
    A_FLAGS(a) &= ~(AF_CONTACT | AF_LEFT | AF_HIDDEN);   /* (nothing touches him: Flink's tests) */
    A_CULL(a) = CULL_NEVER;
    A_LAYER(a) = (short)FLINK_LAYER(3);      /* (a5) = 0x7D70 */
    A_VX(a) = A_VY(a) = 0;
    A_FX(a) = A_X(a) << 16;
    place_y(a, B1_START_Y);
    fireball_form(a);
    flog("final boss at x %d, home y %d, finish %s", A_X(a), B.home, B.finish ? "linked" : "none");
}

/* (from enemy_spawn) defeated already in this visit: only his Finish */
void bos1_gone(BYTE *a, const BYTE *def)
{
    if (def && D_FLAGS(def) & 0x8000) boss_finish_later(a, D_NEXT(def), 1);
}
