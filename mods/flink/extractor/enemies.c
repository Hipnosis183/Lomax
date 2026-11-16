/* enemies.c - Flink's enemies as Lomax objects (ENEMIES, convert_enemies, stun_records
   and each kind's *_params / *_records; their comments there and README "Enemies" explain each kind).

   Each entry: (behaviour, variant, the Flink records copied, one Lomax record each with the same tag
   and Flink's own hitbox, params from the placement and the records, more records or none). The type
   is named '<behaviour> <variant> <n>'. A tag ending in '?' is copied only if the world has it.
   'Boss' is three kinds that differ by world (the Forest Boss, the Village Boss, the Meta Boss): the
   first whose params can be made. The Ingredient Holder's and the Thief's records are MAIN.WLD's. */
#include "convert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHATTER_MAX 17
#define BOSS_NODE_DX (-11)
#define BOSS_MUSIC 8
#define BOSS_WIN_MUSIC 21
#define DONK_MUSIC 9
#define BOS1_MESSAGE 16
#define STATUE_RISES 2
#define WURM_UPSIDE 2

typedef int (*ParamsFn)(const Place *p, const Recs *r, int *out);     /* -> count, or -1: not converted */
typedef int (*ExtraFn)(const Recs *r, Sprites *s, LRec *out);          /* -> count */

static Recs g_worldRecs;                                    /* WORLD_RECS: by name (worldRecName) */
static VEC(const char *) g_worldRecName;
static VEC(Recs *) g_worldRecPtr;

static const Rec *need(const Recs *rs, const char *tag, const char *what)
{
    const Rec *r = rec_get(rs, tag);
    if (!r) die("%s: no %s record", what, tag);
    return r;
}

static int is_sprite(const Piece *p) { return !(p->a & 0x80) && !(p->f & 0x80); }

static int mus(int k)
{
    int t = flink_music_track(k);
    return k < 23 && t > 0 ? MUSIC_BASE + t - 1 : 0;
}

static int has_all(const Recs *r, const char *const *t, int n)
{
    int i;
    for (i = 0; i < n; i++) if (!recs_has(r, t[i])) return 0;
    return 1;
}

/* ---- params ---- */
static int p_check1(const Place *p, const Recs *r, int *o) { (void)r; o[0] = p->check & 1; return 1; }
static int p_zero(const Place *p, const Recs *r, int *o) { (void)p; (void)r; o[0] = 0; return 1; }

/* Ingredient Holder (Flink 0x2F568, think 0x2F5CC): see holder_update in FLINK.DLL. A system actor
   whose records and sprites are MAIN.WLD's (its init 0x2F568 takes them from 0x8686, MAIN's NMY; the
   level world's own entry has none): MAIN_ENEMIES. Its WALK pieces carry hide selectors 1-4 (the
   calm head and arm, 1 and 4, and the scared ones, 2 and 3), which FLINK.DLL swaps on the first
   stomp. WALK's attach point (flags 0xE1, hook 1 of the table 0x2F6CA) draws the carried ingredient
   in its bag; Lomax has none, so it is left out (as all attach points) and the bag stays empty.
   Param 0 of its placement (the ingredient) is not kept.
   Thief (Flink 0x2F6CE, think 0x2F718): see thief_update in FLINK.DLL. MAIN.WLD's records too (its
   init takes them from 0x8682). WALK's pieces with selector 4 are the arm carrying the loot, those with
   selector 3 the empty arm; FLINK.DLL swaps them when it steals. WALK's attach point with flags 1
   (hook 1 of 0x2F944) draws the stolen ingredient in its hand; here FLINK.DLL holds a Lomax coin there
   (params 1 and 2: its offset in WALK's first frame). */
static int p_thief(const Place *p, const Recs *r, int *o)
{
    int dx, dy;
    attach_point(need(r, "WALK", "Thief"), 1, &dx, &dy);
    o[0] = p->check & 1; o[1] = dx & 0xFFFF; o[2] = dy & 0xFFFF;
    return 3;
}

static int p_visje(const Place *p, const Recs *r, int *o)
{
    (void)r;
    o[0] = p->check & 3; o[1] = prm(p, 0, 0); o[2] = prm(p, 1, 0); o[3] = prm(p, 2, 0);
    return 4;
}

/* Forest Dummy (Flink 0x2F948, the basic enemy of FOREST): see dummy_update in FLINK.DLL.

   Firemonster (Flink 0x2F226, FOREST): see fire_update in FLINK.DLL. Its flames are single sprites
   drawn at the mouth, the attach point with flags 6 in WALK's second frame (params 1 and 2: its
   offset; hook 0x2F376 in the type's table 0x2F54C while it breathes): FIRE (2 frames) and FADE (3,
   the rising tip) of its own, and DUST, MAIN.WLD's Dustcloud smoke puffs (8, the descriptors at
   0x84E4). Flink draws each as one hardware sprite whose top-left is the point it computes, so their
   records have one piece a frame at (0, 0). It shatters from BOOM, WALK's second frame (Lomax's
   Actor_ShatterIntoPieces breaks up the type's BOOM record), as Flink breaks up its current frame.

   the Firemonster's mouth: (dx, dy) of the attach point with flags 6 in its WALK frames */
/* the Firemonster's mouth: the attach point with flags 6 in its WALK frames */
static int p_fire(const Place *p, const Recs *r, int *o)
{
    const Rec *w = need(r, "WALK", "Firemonster");
    int i, dx = 0, dy = 0;
    for (i = 0; i < w->npc; i++)
        if (w->pc[i].f & 0x80 && (w->pc[i].fl & 7) == 6) { dx = w->pc[i].dx; dy = w->pc[i].dy; break; }
    o[0] = p->check & 1; o[1] = dx & 0xFFFF; o[2] = dy & 0xFFFF;
    return 3;
}

/* The Statue (Flink 0x2829E, think 0x28358, FOREST): a stone head that shoots fireballs; see statue.c in
   FLINK.DLL. STAY's attach point with hook 1 (its hook table 0x283F0 -> 0x283F4) is the mouth, where it
   spawns a copy of itself as the shot (SHOT) every 'period' (param 0) frames, flying at 'speed' (param
   1) / 100 px a frame, left with checkbox 'move left'. Flink breaks DEAD up when it dies (0x1B1E8), so
   DEAD becomes Lomax's BOOM (Actor_ShatterIntoPieces). A Generator-spawned one (spawn flag bit 5) rises
   out of the ground with Fx_SpawnDebris' clods (0x2E0BC), as the chests: ERDE, MAIN.WLD's Dustcloud.
   Params: checkboxes ('move left' bit 0) | 2 when it rises, period, speed, the mouth's offset.
 */
static int p_statue(const Place *p, const Recs *r, int *o)
{
    int dx, dy;
    attach_point(need(r, "STAY", "Statue"), 1, &dx, &dy);
    o[0] = (p->check & 1) | (p->sf & 0x20 ? STATUE_RISES : 0);
    o[1] = prm(p, 0, 0); o[2] = prm(p, 1, 0); o[3] = dx & 0xFFFF; o[4] = dy & 0xFFFF;
    return 5;
}

/* Flink's Raketen wurm, Raketen wurm (0x25AA6, FOREST 4-1, 4-2, 8-1, TREE 27-1): see wurm.c in FLINK.DLL. HEAD
   and TAIL as they are (FLINK.DLL draws their frames 0-2 at its own points, flipped as needed); its
   MUD (TREE's has no sprite: blank frames), MUDV the mud flipped upside down for one hanging from a
   ceiling ('head-over-heals'), and MAIN.WLD's clods ERDE. Params: 2 = hanging, 'length', 'height' (not
   0: the vertical Raketen wurm of 4-2 and 8-1, which rises from one hole into another).
 */
static int p_wurm(const Place *p, const Recs *r, int *o)
{
    (void)r;
    o[0] = p->check & 1 ? WURM_UPSIDE : 0; o[1] = prm(p, 0, 0); o[2] = prm(p, 1, 0);
    return 3;
}

/* Flink's Puge, Puge (0x24F40, think 0x24FD2, draw 0x25108; FOREST map 15 = 5-3, VILLAGE map 19
   = 9-2): see puge.c in FLINK.DLL. The tree and its face are background cells; PUGE is only the open
   mouth (one frame) drawn over the face, mirrored with 'look left'. Its attach point with hook 1 (the
   table 0x251B6 -> 0x251A4) is the mouth, where it spits two groups of 5 rocks: MAIN.WLD's clods ERDE.
   Params: 'look left', 'time' (frames shut), the mouth's offset. */
static int p_puge(const Place *p, const Recs *r, int *o)
{
    int dx, dy;
    attach_point(need(r, "PUGE", "Puge"), 1, &dx, &dy);
    o[0] = p->check & 1; o[1] = prm(p, 0, 0); o[2] = dx & 0xFFFF; o[3] = dy & 0xFFFF;
    return 4;
}

/* The City dummy (VILLAGE; Flink 0x2FBD6, FLINK.DLL dll\city.c). Params: 'move left', then its lance
   tip, ATCK's second box (left, top, right, bottom from its feet), which 0x1A41C tests against the
   player while it charges. Its lance flies off as LANS when it dies. */
static int p_city(const Place *p, const Recs *r, int *o)
{
    const Rec *a = need(r, "ATCK", "City dummy");
    int i;
    o[0] = p->check & 1;
    for (i = 0; i < 4; i++) o[1 + i] = a->box2[i] & 0xFFFF;
    return 5;
}

/* The Cyclin' Dude and his bike (VILLAGE; FLINK.DLL dll\cyclindude.c). The Cyclin' Dude (0x3045A, think
   0x304F0) spawns a Chopper Bike (0x30724) and holds it at HANG's attach point with hook 1 (its hook
   table 0x3071C -> 0x30692), and drops a Little Dude (0x3099C) from the one with hook 2 (0x306AC) every
   240 frames. Flink spawns those two by type name (0x32BD4), so their maps place one of each that is
   never spawned (spawn flag bit 5) only to load the type; here they become 'Muta Bike n' and 'Muta Litt
   n' types (never spawned, 0x2000), which FLINK.DLL finds by name. Params of the Cyclin' Dude: 'move left',
   then hook 1 and hook 2 (dx, dy from his feet). The Little Dude is small (+6 bit 1): thrown, it is
   Flink's small shot (SMAL as SHSM). */
static int p_cycl(const Place *p, const Recs *r, int *o)
{
    const Rec *h = need(r, "HANG", "Cyclin' Dude");
    int dx, dy;
    o[0] = p->check & 1;
    attach_point(h, 1, &dx, &dy); o[1] = dx & 0xFFFF; o[2] = dy & 0xFFFF;
    attach_point(h, 2, &dx, &dy); o[3] = dx & 0xFFFF; o[4] = dy & 0xFFFF;
    return 5;
}

static int p_worm(const Place *p, const Recs *r, int *o) { (void)r; o[0] = p->check & 1 ? 0 : 1; return 1; }
static int p_razo(const Place *p, const Recs *r, int *o) { (void)r; o[0] = p->check & 1; o[1] = prm(p, 0, 0) & 0xFFFF; return 2; }
static int p_storm(const Place *p, const Recs *r, int *o) { (void)r; if (p->sf & 0x40) return -1; o[0] = 0; return 1; }
static int p_buzz(const Place *p, const Recs *r, int *o) { (void)r; o[0] = p->check & 1; o[1] = prm(p, 0, 0); o[2] = prm(p, 1, 0); return 3; }

static int p_circ(const Place *p, const Recs *r, int *o)
{
    int i;
    (void)r;
    o[0] = 0;
    for (i = 0; i < 4; i++) o[1 + i] = prm(p, i, 0);
    return 5;
}

static const char *const BOSS_TAGS[5] = { "WALK", "JUMP", "OUCH", "NODE", "DEAD" };
static const char *const VBOSS_TAGS[12] = { "RUN ", "WALK", "STAY", "DEAD", "MEN1", "MEN2", "MEN3", "MEN4", "MEN5", "BAL1", "BAL2", "BAL3" };
static const char *const MBOSS_TAGS[8] = { "EINS", "ZWEI", "DREI", "VIER", "ZAP1", "ZAP2", "ZAP3", "WOLK" };

/* Flink's Forest Boss (actor 'Boss', init 0x2948C -> 0x294B0 for FOREST, think
   0x295D0 / 0x29602, FOREST map 17 = 8-2): see forestboss.c in FLINK.DLL. A furry monster on two stone
   stilts. The stilts' spiked tops are two child actors (NODE, 0x294C2) that its frames' attach points
   with hook 1 (0x29A46, via the table 0x29A78) draw 11 px left of the point, the left one mirrored
   (its +3 bit 3, 0x294EE); here NODE's pieces are put into WALK, JUMP and OUCH in the attach point's
   place (the same order, so the same overlap), and LGWK, LGJP and LGOU hold the two points of each
   frame (blank pieces at x - 11, left one first) for FLINK.DLL's stilt boxes. DEAD as it is (frame 0
   the body, which breaks up, 1-4 the clusters thrown off: 0x29906), BOOM = DEAD's frame 0 (Lomax's
   Actor_ShatterIntoPieces keeps 17 pieces at most). The spirits set free by each hit are the shared
   'Spik Spirit' type (spirit_type). Params: 0, the fight's music (Flink's music index 8, 0x295FA) and the music at its
   defeat (index 21, 0x29A94), as Lomax Music params. Its arena's Finish waits in the middle of the
   map until it is defeated (see BOSS_FINISH in convert). */
static int p_boss(const Place *p, const Recs *r, int *o)
{
    (void)p;
    if (!has_all(r, BOSS_TAGS, 5)) return -1;               /* (VILLAGE's and META's bosses are others) */
    o[0] = 0; o[1] = mus(BOSS_MUSIC); o[2] = mus(BOSS_WIN_MUSIC);
    return 3;
}
/* Flink's Village Boss (actor 'Boss', init 0x2948C -> 0x29A9E for VILLAGE, think
   0x29CCC, VILLAGE map 17 = 16-2): see villageboss.c in FLINK.DLL. A walking machine with three men in glass
   domes on top (MEN4 the two at the sides, MEN1 the middle one: MEN2 once hit, MEN3 / MEN5 falling
   dead), each on a chain that shoots up to the ceiling (BAL1 the links, BAL3 the middle's spiked head,
   BAL2 the sides' hands, drawn with Sprite_DrawScreen 0x3430E: the hardware sprite's top-left at the
   point; the left hand flipped upside down, the right one both ways: 0x29C92 / 0x2A4A6). The men are
   child actors that the frames' attach points put in place (the hook table 0x2A534: hook 1 the middle
   one at the point - 12 (0x2A504), hook 2 the one left of the Village Boss's x at the point - 6, the right
   one at - 8 (0x2A4C4); hook 3, its feet, does nothing). Here RUN, WALK and STAY keep the Village Boss's own
   pieces, and PRUN, PWAL, PSTA hold each frame's hook 1 and 2 points in the frame's order (blank
   pieces), for FLINK.DLL to place and draw the men. DEAD as it is (frame 0 the body, which breaks up,
   1 the cluster thrown off: 0x29906), BOOM = DEAD's frame 0. Params: 0, the music at its defeat (index
   21, 0x29A7C; the fight's is the map's own, index 9). Its arena's Finish appears once it is defeated
   (see BOSS_FINISH in convert). */
static int p_vboss(const Place *p, const Recs *r, int *o)
{
    (void)p;
    if (!has_all(r, VBOSS_TAGS, 12)) return -1;
    o[0] = 0; o[1] = mus(BOSS_WIN_MUSIC);
    return 2;
}
/* Flink's Meta Boss (actor 'Boss', init 0x2948C -> 0x2A540 for META, think 0x2A5E8 / 0x2A616 /
   0x2A656, draw 0x2ACB8, its lightning 0x2AB5A / 0x2AC0A / 0x2AC9C; META map 6 = 31-2): see metaboss.c
   in FLINK.DLL. A cloud monster rising out of the ground: EINS (defeated) and ZWEI (standing), DREI
   (its hands pound the ground, a cloud rising at each pound) and VIER (its hands clap together in
   the middle); ZAP1 (the lightning charging at its eyes), ZAP2 (the ball it shoots) and ZAP3 (the
   strike on the ground); WOLK (the clouds, platforms). Its draw (0x2ACB8) gives the pieces with
   selector 1 (the arms and hands) the sprite priority bit and the others (the body and head) none,
   and 31-2 is a Priority map (all its tiles over sprites without the bit): the body is drawn behind
   the play map's tiles, the arms in front. So each frame is written twice: EINS, ZWEI, DREI, VIER
   with the selector 1 pieces (in front), BEIN, BZWE, BDRE, BVIE with the others (behind the tiles,
   metaboss.c). Its two attach points with hook 1 (0x2AB5A) are the eyes, MBOSS_EYES in every frame
   (checked). Defeated it breaks up with +2 bit 1 set (0x2AA1E), which leaves the selector 2 pieces
   out (0x1B2A6): only the arms fly off, from ZWEI's front frame. Params: 0, the fight's music (index
   8, 0x2A60C) and the music at its defeat (index 21, 0x29A7C). Its arena's Finish appears once it is
   defeated (see BOSS_FINISH in convert). */
static int p_mboss(const Place *p, const Recs *r, int *o)
{
    (void)p;
    if (!has_all(r, MBOSS_TAGS, 8)) return -1;
    o[0] = 0; o[1] = mus(BOSS_MUSIC); o[2] = mus(BOSS_WIN_MUSIC);
    return 3;
}
/* Flink's Donkey monkey (actor 'Donk', init 0x220FC, think 0x22176; TREE map 7 = 28-1): see donkey.c
   in FLINK.DLL. A gorilla at the top of the tree that jumps across Flink's screen and in place, and
   its in-place landings shake the apples out of the canopy. Its records as they are (STAY keeps its
   pieces' hide selectors 1 and 2, the blinking eyes and the mouth, which FLINK.DLL hides as Flink's
   +2 bits 0 and 1 do; OUCH's attach point calls Hook_Nop and is left out). Params 0 (it faces the
   way Flink's think picks), the fight's music (index 9, 0x222D2) and the music at its defeat (21,
   0x29A94). Flink spawns its apples by name (0x32BD4) at Flink's camera x + 90 + 40 k, y 0x3C: a
   Falling apple whose params are code bytes (0x226BC: 'distance' 0x226C8 = 1000) and an App2 with
   'start speed' 0 (0x226BE + 0xC); the converter adds one placement of each at the monkey's x,
   DONK_APPLE_Y, for FLINK.DLL to spawn. The fight is on Flink's last screen of the map
   (DONK_SCREEN px), where the Scroll Point stops the camera; the Finish waits in its middle. */
static int p_donk(const Place *p, const Recs *r, int *o)
{
    (void)p; (void)r;
    o[0] = 0; o[1] = mus(DONK_MUSIC); o[2] = mus(BOSS_WIN_MUSIC);
    return 3;
}
/* Once he is defeated and Flink stands, Flink's think opens message BOS1_MESSAGE (0x214B4: 0x81A6 =
   -17); FLINK.DLL shows it in Lomax's hint box (hint_box), its text in LEVELS.INI's [Hints]. */
/* once Bos1 is defeated Flink's think opens message BOS1_MESSAGE: its text goes into [Hints] */
static int p_bos1(const Place *p, const Recs *r, int *o)
{
    (void)p; (void)r;
    hints_add(BOS1_MESSAGE);
    o[0] = 0; o[1] = mus(BOSS_WIN_MUSIC);
    return 2;
}

/* ---- extra records ---- */
static void frames_one_piece_at0(const Rec *rec, Sprites *s, int world, Ents *ents)
{
    int st[4096], n = flink_frames(rec, st), k;
    for (k = 0; k < n; k++) {
        Ents e = { 0 };
        int from = ents->n;
        lomax_pieces(rec->pc + st[k], st[k + 1] - st[k], s, world, &e);
        if (e.n) { Ent x = e.v[0]; x.dx = x.dy = 0; vpush(*ents, x); }
        end_frame(ents, from);
        free(e.v);
    }
}

static void all_frames(const Rec *rec, Sprites *s, int world, Ents *ents)
{
    int st[4096], n = flink_frames(rec, st), k;
    for (k = 0; k < n; k++) {
        int from = ents->n;
        lomax_pieces(rec->pc + st[k], st[k + 1] - st[k], s, world, ents);
        end_frame(ents, from);
    }
}

static LRec erde(Sprites *s)
{
    const Rec *r = need(g_main.dust, "ERDE", "Dustcloud");
    Ents e = { 0 };
    LRec out;
    all_frames(r, s, 1, &e);
    out = pack_record("ERDE", r->speed, r->hb, e.v, e.n);
    free(e.v);
    return out;
}

static int x_spin(const Recs *r, Sprites *s, LRec *o) { (void)r; shot_record("SPIN", s, &o[0]); return 1; }
static int x_smal(const Recs *r, Sprites *s, LRec *o) { (void)r; shot_record("SMAL", s, &o[0]); return 1; }
static int x_erde(const Recs *r, Sprites *s, LRec *o) { (void)r; o[0] = erde(s); return 1; }
static int x_buzz(const Recs *r, Sprites *s, LRec *o) { o[0] = lomax_record1("BOOM", need(r, "DEAD", "Buzzard"), s, 0, 0); return 1; }

/* (BOOM without the attack face, slot 1: it is hidden while walking, when it is mostly killed) */
static int x_fire(const Recs *r, Sprites *s, LRec *o)
{
    static const char *const T[3] = { "FIRE", "FADE", "DUST" };
    RecSel sel;
    int i;
    for (i = 0; i < 3; i++) {
        const Rec *rec = i < 2 ? need(r, T[i], "Firemonster") : need(g_main.dust, "DUST", "Dustcloud");
        Ents e = { 0 };
        frames_one_piece_at0(rec, s, i == 2, &e);
        o[i] = pack_record(T[i], rec->speed, rec->hb, e.v, e.n);
        free(e.v);
    }
    /* (BOOM without the attack face, slot 1) */
    sel.rec = need(r, "WALK", "Firemonster"); sel.only = 1; sel.hasOnly = 1;
    o[3] = lomax_record("BOOM", &sel, 1, s, 0, 0, 0, 0, 0, 2, 0);
    return 4;
}

static int x_statue(const Recs *r, Sprites *s, LRec *o)
{
    o[0] = lomax_record1("BOOM", need(r, "DEAD", "Statue"), s, 0, 0);
    o[1] = erde(s);
    return 2;
}

static int x_wurm(const Recs *r, Sprites *s, LRec *o)
{
    const Rec *mud = need(r, "MUD ", "Raketen wurm");
    int st[4096], nf = flink_frames(mud, st), v, k, i;
    for (v = 0; v < 2; v++) {
        Ents e = { 0 };
        for (k = 0; k < nf; k++) {
            int from = e.n;
            for (i = st[k]; i < st[k + 1]; i++) {
                const Piece *p = &mud->pc[i];
                Ent x;
                if (!is_sprite(p)) continue;
                x.bank = sprites_bank(s, SK_A(p->a));
                x.frame = p->f + 1; x.dx = p->dx; x.dy = p->dy; x.fl = p->fl;
                if (v) {
                    Frames fr = flink_anim_frames(s->w->anims.v[p->a]);
                    x.dy = 1 - fr.f[p->f].h - p->dy;
                    x.fl ^= 0x10;
                }
                x.fl &= 0x18;
                vpush(e, x);
            }
            if (e.n == from) { Ent b = { sprites_bank(s, sk(SK_BLANK, 0, 0, 0)), 1, 0, 0, 0 }; vpush(e, b); }
            end_frame(&e, from);
        }
        o[v] = pack_record(v ? "MUDV" : "MUD ", mud->speed, mud->hb, e.v, e.n);
        free(e.v);
    }
    o[2] = erde(s);
    return 3;
}

static int cmp_pt(const void *a, const void *b)
{
    const int *x = a, *y = b;
    return x[0] != y[0] ? (x[0] > y[0]) - (x[0] < y[0]) : (x[1] > y[1]) - (x[1] < y[1]);
}

/* the Forest Boss: NODE's pieces put into WALK, JUMP and OUCH at the stilts' points (mirrored left of
   its x), LGWK / LGJP / LGOU the two points of each frame, DEAD, and BOOM = DEAD's frame 0 */
static int x_boss(const Recs *r, Sprites *s, LRec *o)
{
    static const char *const T[3] = { "WALK", "JUMP", "OUCH" }, *const L[3] = { "LGWK", "LGJP", "LGOU" };
    const Rec *node = need(r, "NODE", "Boss");
    RecSel sel;
    int t, k, i, j, n = 0;
    LRec legs[3];
    for (t = 0; t < 3; t++) {
        const Rec *rec = need(r, T[t], "Boss");
        int st[4096], nf = flink_frames(rec, st);
        Ents ents = { 0 }, lents = { 0 };
        for (k = 0; k < nf; k++) {
            int from = ents.n, pts[16][2], np = 0, lfrom;
            for (i = st[k]; i < st[k + 1]; i++) {
                const Piece *p = &rec->pc[i];
                if (is_sprite(p)) {
                    Ent e = { sprites_bank(s, SK_A(p->a)), p->f + 1, p->dx, p->dy, p->fl & 0x18 };
                    vpush(ents, e);
                    continue;
                }
                if ((p->fl & 7) != 1) continue;
                if (np == 16) die("Boss %s: too many stilts", T[t]);
                pts[np][0] = p->dx + BOSS_NODE_DX; pts[np][1] = p->dy; np++;
                for (j = 0; j < node->npc; j++) {
                    const Piece *q = &node->pc[j];
                    int ndx = q->dx, nfl = q->fl;
                    Ent e;
                    if (!is_sprite(q)) continue;
                    if (p->dx < 0) { ndx = 1 - sprites_width(s, SK_A(q->a), q->f) - ndx; nfl ^= 0x08; }
                    e.bank = sprites_bank(s, SK_A(q->a)); e.frame = q->f + 1;
                    e.dx = pts[np - 1][0] + ndx; e.dy = p->dy + q->dy; e.fl = nfl & 0x18;
                    vpush(ents, e);
                }
            }
            if (np != 2) die("Boss %s: a frame with %d stilts; FLINK.DLL expects 2", T[t], np);
            end_frame(&ents, from);
            qsort(pts, (size_t)np, sizeof(pts[0]), cmp_pt);
            lfrom = lents.n;
            for (i = 0; i < np; i++) { Ent e = { sprites_bank(s, sk(SK_BLANK, 0, 0, 0)), 1, pts[i][0], pts[i][1], 0 }; vpush(lents, e); }
            end_frame(&lents, lfrom);
        }
        o[n++] = pack_record(T[t], rec->speed, rec->hb, ents.v, ents.n);
        legs[t] = pack_record(L[t], rec->speed, rec->hb, lents.v, lents.n);
        free(ents.v); free(lents.v);
    }
    for (t = 0; t < 3; t++) o[n++] = legs[t];
    o[n++] = lomax_record1("DEAD", need(r, "DEAD", "Boss"), s, 0, 0);
    sel.rec = need(r, "DEAD", "Boss"); sel.only = 0; sel.hasOnly = 1;
    o[n++] = lomax_record("BOOM", &sel, 1, s, 0, 0, 0, 0, 0, 0, 0);
    return n;
}

/* XRUN, XWAL, XSTA: the same pieces in the same order (as lomax_record writes RUN, WALK,
   STAY), each its own one-piece frame drawn around the middle of its drawn pixels (offset
   minus that middle), for FLINK.DLL to draw each piece from that middle (Lomax+'s FPS
   Override moves each Actor_Draw call by its point's motion: drawn whole, the legs, which move
   on their own, shook between ticks; from the sprite's corner, the limbs, whose rotation is a
   new frame with another box each tick, shook too: the middle of a rotated limb moves as the
   limb does, the corner of its box doesn't)
   the chain's pieces: one hardware sprite, its top-left at the actor's point (as ring_record)
   Lomax's Actor_ShatterIntoPieces keeps a piece's 8 bytes from actor +0x94: 17 fit in the actor, an
   18th overwrote the next actor's type (the game crashed). DEAD's body has 18: BOOM takes the first
   17, BOO2 the rest, thrown off as one more cluster (villageboss.c). */
/* the Village Boss: PRUN / PWAL / PSTA the men's points, XRUN / XWAL / XSTA its pieces each drawn
   from its middle, the chain's pieces, BOOM and BOO2 (DEAD's body in two: SHATTER_MAX at most) */
static int x_vboss(const Recs *r, Sprites *s, LRec *o)
{
    static const char *const T[3] = { "RUN ", "WALK", "STAY" }, *const P[3] = { "PRUN", "PWAL", "PSTA" },
                             *const X[3] = { "XRUN", "XWAL", "XSTA" };
    static const struct { const char *tag, *src; int fl; } C[4] = {
        { "BAL1", "BAL1", 0 }, { "BAL3", "BAL3", 0 }, { "BL2L", "BAL2", 0x10 }, { "BL2R", "BAL2", 0x18 } };
    const Rec *dead = need(r, "DEAD", "Boss");
    int t, k, i, n = 0, st[4096], nf;
    Ents body = { 0 }, part = { 0 };
    for (t = 0; t < 3; t++) {
        const Rec *rec = need(r, T[t], "Boss");
        Ents pe = { 0 }, xe = { 0 };
        nf = flink_frames(rec, st);
        for (k = 0; k < nf; k++) {
            int from = pe.n, np = 0;
            for (i = st[k]; i < st[k + 1]; i++) {
                const Piece *p = &rec->pc[i];
                if (!is_sprite(p) && ((p->fl & 7) == 1 || (p->fl & 7) == 2)) np++;
            }
            if (np != 3) die("Boss %s: a frame with %d men; FLINK.DLL expects 3", T[t], np);
            for (i = st[k]; i < st[k + 1]; i++) {
                const Piece *p = &rec->pc[i];
                if (!is_sprite(p) && ((p->fl & 7) == 1 || (p->fl & 7) == 2)) {
                    Ent e = { sprites_bank(s, sk(SK_BLANK, 0, 0, 0)), 1, p->dx, p->dy, 0 };
                    vpush(pe, e);
                }
            }
            end_frame(&pe, from);
        }
        o[n++] = pack_record(P[t], rec->speed, rec->hb, pe.v, pe.n);
        for (k = 0; k < nf; k++)
            for (i = st[k]; i < st[k + 1]; i++) {
                const Piece *p = &rec->pc[i];
                int cx, cy;
                Ent e;
                if (!is_sprite(p)) continue;
                sprites_centre(s, SK_A(p->a), p->f, p->fl & 0x18, &cx, &cy);
                e.bank = sprites_bank(s, SK_A(p->a)); e.frame = p->f + 1; e.dx = -cx; e.dy = -cy; e.fl = (p->fl & 0x18) | 0x80;
                vpush(xe, e);
            }
        o[n++] = pack_record(X[t], 0, rec->hb, xe.v, xe.n);
        free(pe.v); free(xe.v);
    }
    for (k = 0; k < 4; k++) {
        const Rec *src = need(r, C[k].src, "Boss");
        const Piece *p = NULL;
        int xo, yo;
        Ent e;
        for (i = 0; i < src->npc; i++) if (is_sprite(&src->pc[i])) { p = &src->pc[i]; break; }
        if (!p) die("Boss %s: no sprite", C[k].src);
        anim_piece_offset(s->w->anims.v[p->a], p->f, &xo, &yo);
        if (xo || yo) die("Boss %s: not one sprite at its frame's top-left", C[k].src);
        e.bank = sprites_bank(s, SK_A(p->a)); e.frame = p->f + 1; e.dx = 0; e.dy = 0; e.fl = 0x80 | C[k].fl;
        o[n++] = pack_record(C[k].tag, 0x40, src->hb, &e, 1);
    }
    nf = flink_frames(dead, st);
    if (nf) lomax_pieces(dead->pc + st[0], st[1] - st[0], s, 0, &body);
    for (i = 0; i < body.n && i < SHATTER_MAX; i++) vpush(part, body.v[i]);
    end_frame(&part, 0);
    o[n++] = pack_record("BOOM", dead->speed, dead->hb, part.v, part.n);
    part.n = 0;
    for (i = SHATTER_MAX; i < body.n; i++) vpush(part, body.v[i]);
    if (!part.n && body.n) vpush(part, body.v[body.n - 1]);
    end_frame(&part, 0);
    o[n++] = pack_record("BOO2", dead->speed, dead->hb, part.v, part.n);
    free(body.v); free(part.v);
    return n;
}

/* (a frame with no piece of one kind gets a blank one there: EINS is all behind the tiles) */
/* the Meta Boss: each of EINS / ZWEI / DREI / VIER twice, the selector 1 pieces (in front) and the
   others (BEIN / BZWE / BDRE / BVIE, behind the tiles); ZAP1-3; WOLK with a platform's surface */
static int x_mboss(const Recs *r, Sprites *s, LRec *o)
{
    static const char *const T[4] = { "EINS", "ZWEI", "DREI", "VIER" }, *const B[4] = { "BEIN", "BZWE", "BDRE", "BVIE" };
    static const char *const Z[3] = { "ZAP1", "ZAP2", "ZAP3" };
    static const int CLAP[4] = { -30, -3, 59, 47 }, EYES[2][2] = { { 3, -79 }, { 24, -79 } };
    const Rec *vier = need(r, "VIER", "Boss"), *zwei;
    int t, k, i, n = 0, st[4096], nf, sel, arms = 0;
    if (memcmp(vier->box2, CLAP, sizeof(CLAP))) die("Boss VIER: its second box isn't the one FLINK.DLL expects");
    for (t = 0; t < 4; t++) {
        const Rec *rec = need(r, T[t], "Boss");
        nf = flink_frames(rec, st);
        for (k = 0; k < nf; k++) {
            int eyes[8][2], ne = 0;
            for (i = st[k]; i < st[k + 1]; i++) {
                const Piece *p = &rec->pc[i];
                if (!is_sprite(p) && (p->fl & 7) == 1) {
                    if (ne == 8) die("Boss %s: too many eyes", T[t]);
                    eyes[ne][0] = p->dx; eyes[ne][1] = p->dy; ne++;
                }
                if (is_sprite(p) && (p->fl & 7) != 1 && (p->fl & 7) != 2)
                    die("Boss %s: a piece of another selector than 1 and 2, as FLINK.DLL expects", T[t]);
            }
            qsort(eyes, (size_t)ne, sizeof(eyes[0]), cmp_pt);
            if (ne != 2 || memcmp(eyes, EYES, sizeof(EYES))) die("Boss %s: its eyes aren't where FLINK.DLL expects", T[t]);
        }
        for (sel = 1; sel <= 2; sel++) {
            Ents e = { 0 };
            for (k = 0; k < nf; k++) {
                int from = e.n;
                for (i = st[k]; i < st[k + 1]; i++)
                    if ((rec->pc[i].fl & 7) == sel) lomax_pieces(&rec->pc[i], 1, s, 0, &e);
                if (e.n == from) { Ent b = { sprites_bank(s, sk(SK_BLANK, 0, 0, 0)), 1, 0, 0, 0 }; vpush(e, b); }
                end_frame(&e, from);
            }
            o[n++] = pack_record(sel == 1 ? T[t] : B[t], rec->speed, rec->hb, e.v, e.n);
            free(e.v);
        }
    }
    for (t = 0; t < 3; t++) o[n++] = lomax_record1(Z[t], need(r, Z[t], "Boss"), s, 0, 0);
    o[n++] = lomax_record1("WOLK", need(r, "WOLK", "Boss"), s, PLATFORM_EDGE, 0);
    zwei = need(r, "ZWEI", "Boss");
    nf = flink_frames(zwei, st);
    for (i = st[0]; nf && i < st[1]; i++) if (is_sprite(&zwei->pc[i]) && (zwei->pc[i].fl & 7) == 1) arms++;
    if (arms > SHATTER_MAX) die("Boss ZWEI: %d pieces break up, Lomax's Actor_ShatterIntoPieces keeps %d", arms, SHATTER_MAX);
    return n;
}

static Recs *world_rec(const char *name)
{
    int i;
    Recs *r = NULL;
    for (i = 0; i < g_worldRecName.n; i++) if (!strcmp(g_worldRecName.v[i], name)) r = g_worldRecPtr.v[i];
    return r;
}

/* Flink's final boss, Bos1 (MOUNTAIN map 17 = 24-2; init 0x20E86, FLINK.DLL dll\bos1.c): the sorcerer
   (STAY, STA2, OUCH; FLY in his second phase) who comes as a fireball (HEAD, TAIL its sparks) and
   forms out of it (ZAP), throws fireballs (WARN, then HEAD), and becomes a dragon. The map's other
   types are only his: Bos2 (init 0x20DCC) builds the dragon's sprites from its HEAD and TAIL, Boom
   (0x216EA) is the dragon's falling fire (HEAD, TAIL, BOOM), Bos3 dude (0x21F90) what he drops.
   Bos2's and Boom's records go into Bos1's type (DHED, DTAL; BHED, BTAL, BOOM), their placements are
   left out; the Bos3 dude, placed never to spawn, is an enemy type of its own. FLINK.DLL needs these
   points and sprite places, checked here (its constants of the same names):
 */
/* Bos1: checks the points and sprite places FLINK.DLL needs, then Bos2's HEAD / TAIL (DHED, DTAL)
   and Boom's HEAD / TAIL / BOOM (BHED, BTAL, BOOM) */
static int x_bos1(const Recs *r, Sprites *s, LRec *o)
{
    static const struct { const char *tag, *name; int at[2], size[2]; } PIECES[4] = {
        { "TAIL", "Bos1", { -7, -10 }, { 16, 16 } }, { "TAIL", "Boom", { -7, -16 }, { 16, 16 } },
        { "HEAD", "Bos2", { -23, -26 }, { 48, 48 } }, { "TAIL", "Bos2", { -16, -18 }, { 48, 48 } } };
    const Recs *bos2 = world_rec("Bos2"), *boom = world_rec("Boom");
    const Rec *rec;
    int st[4096], nf, k, i, q, n = 0;
    /* STA2's point 1 at (8, -39), FLY's point 2 at (-2, -32), OUCH's top-left at (-21, -61) */
    {
        static const struct { const char *tag; int hook, x, y; } PT[2] = { { "STA2", 1, 8, -39 }, { "FLY ", 2, -2, -32 } };
        for (q = 0; q < 2; q++) {
            int any = 0;
            rec = need(r, PT[q].tag, "Bos1");
            for (i = 0; i < rec->npc; i++) {
                const Piece *p = &rec->pc[i];
                if (is_sprite(p) || (p->fl & 7) != PT[q].hook) continue;
                if (p->dx != PT[q].x || p->dy != PT[q].y) die("Bos1 %s: its point %d isn't where FLINK.DLL expects", PT[q].tag, PT[q].hook);
                any = 1;
            }
            if (!any) die("Bos1 %s: no point %d", PT[q].tag, PT[q].hook);
        }
        rec = need(r, "OUCH", "Bos1");
        nf = flink_frames(rec, st);
        {
            int mx = 1 << 30, my = 1 << 30;
            for (i = st[0]; nf && i < st[1]; i++) if (is_sprite(&rec->pc[i])) { mx = imin(mx, rec->pc[i].dx); my = imin(my, rec->pc[i].dy); }
            if (mx != -21 || my != -61) die("Bos1 OUCH: its top-left isn't (-21, -61), as FLINK.DLL expects");
        }
    }
    if (!bos2 || !boom) die("Bos1: its map's world has no Bos2 or Boom");
    for (q = 0; q < 4; q++) {
        const Recs *src = !strcmp(PIECES[q].name, "Bos1") ? r : !strcmp(PIECES[q].name, "Boom") ? boom : bos2;
        rec = need(src, PIECES[q].tag, PIECES[q].name);
        nf = flink_frames(rec, st);
        for (k = 0; k < nf; k++) {
            const Piece *ps = NULL;
            int cnt = 0, xo, yo, w, h;
            for (i = st[k]; i < st[k + 1]; i++) if (is_sprite(&rec->pc[i])) { if (!ps) ps = &rec->pc[i]; cnt++; }
            if (cnt != 1 || ps->dx != PIECES[q].at[0] || ps->dy != PIECES[q].at[1])
                die("%s %s: not one sprite where FLINK.DLL expects", PIECES[q].name, PIECES[q].tag);
            w = sprites_width(s, SK_A(ps->a), ps->f);
            h = sprites_frames(s, SK_A(ps->a))->f[ps->f].h;
            anim_piece_offset(s->w->anims.v[ps->a], ps->f, &xo, &yo);
            if (w != PIECES[q].size[0] || h != PIECES[q].size[1] || xo || yo)
                die("%s %s: not one %dx%d sprite, as FLINK.DLL expects", PIECES[q].name, PIECES[q].tag, PIECES[q].size[0], PIECES[q].size[1]);
        }
    }
    o[n++] = lomax_record1("DHED", need(bos2, "HEAD", "Bos2"), s, 0, 0);
    o[n++] = lomax_record1("DTAL", need(bos2, "TAIL", "Bos2"), s, 0, 0);
    o[n++] = lomax_record1("BHED", need(boom, "HEAD", "Boom"), s, 0, 0);
    o[n++] = lomax_record1("BTAL", need(boom, "TAIL", "Boom"), s, 0, 0);
    o[n++] = lomax_record1("BOOM", need(boom, "BOOM", "Boom"), s, 0, 0);
    return n;
}

/* The Worm's draw (0x28C32) puts each frame's one hardware sprite (0x34132) at its own point, without
   the sprite's offset inside the ANIM frame; Lomax's sprite is the whole frame, so FLINK.DLL (worm.c,
   WORM_HEAD_DY / WORM_BODY_DY) adds that offset: 0 for HEAD's frames, 8 px down for BODY's. Checked
   here; no records of its own. */
/* the Worm: HEAD's sprites at (0, 0) in their frames, BODY's at (0, 8) (worm.c); no records */
static int x_worm(const Recs *r, Sprites *s, LRec *o)
{
    static const char *const T[2] = { "HEAD", "BODY" };
    static const int WANT[2] = { 0, 8 };
    int t, k, i, st[4096], nf;
    (void)o;
    for (t = 0; t < 2; t++) {
        const Rec *rec = need(r, T[t], "Worm");
        nf = flink_frames(rec, st);
        for (k = 0; k < nf; k++)
            for (i = st[k]; i < st[k + 1]; i++)
                if (is_sprite(&rec->pc[i])) {
                    int xo, yo;
                    anim_piece_offset(s->w->anims.v[rec->pc[i].a], rec->pc[i].f, &xo, &yo);
                    if (xo || yo != WANT[t]) die("Worm %s: its sprite isn't where FLINK.DLL expects in its frame", T[t]);
                    break;
                }
    }
    return 0;
}

/* The Storm Cloud's puffs (storm.c): its draw (0x2346E) puts WIND's frame's one hardware sprite (0x34132)
   with its top-left at a puff's point, so WIND is one sprite a frame at 0, 0. It dies breaking DEAD's
   frame up (0x1B1E8): BOOM. */
static int x_storm(const Recs *r, Sprites *s, LRec *o)
{
    const Rec *wind = need(r, "WIND", "Storm Cloud");
    Ents e = { 0 };
    frames_one_piece_at0(wind, s, 0, &e);
    o[0] = pack_record("WIND", wind->speed, wind->hb, e.v, e.n);
    free(e.v);
    o[1] = lomax_record1("BOOM", need(r, "DEAD", "Storm Cloud"), s, 0, 0);
    return 2;
}

/* The Blitz dude's cloud and lightning (blitz.c): its hook for STAY's attach point (flags 1, 0x230C2)
   draws the cloud there, BLITZ_CLOUD_AT (FLINK.DLL's CLOUD_DY); let go, the cloud is a platform, so
   WOLK and WARN get a platform's standing surface (their hitbox widened by PLATFORM_EDGE). ZAP is one
   of the enemy's tags (its hitbox is the lightning's, 0x1A170). */
static int x_blitz(const Recs *r, Sprites *s, LRec *o)
{
    int dx, dy;
    attach_point(need(r, "STAY", "Blitz dude"), 1, &dx, &dy);
    if (dx != 0 || dy != 8) die("Blitz dude: STAY's cloud is at (%d, %d); FLINK.DLL expects (0, 8)", dx, dy);
    o[0] = lomax_record1("WOLK", need(r, "WOLK", "Blitz dude"), s, PLATFORM_EDGE, 0);
    o[1] = lomax_record1("WARN", need(r, "WARN", "Blitz dude"), s, PLATFORM_EDGE, 0);
    return 2;
}

/* The Circling suckers' heads (circ.c): their draw (0x23944) puts FLY's frame's hardware sprite
   (0x34132, fetched by the init) with its top-left at CIRC_DRAW from the head's point, frame 0 or 1,
   unflipped: 1 px right of where FLY's own pieces put it. FLY is written with those offsets. */
/* the Circling suckers: FLY's two frames, each one sprite at CIRC_DRAW */
static int x_circ(const Recs *r, Sprites *s, LRec *o)
{
    static const int DRAW[2][2] = { { -12, -24 }, { -15, -23 } };
    const Rec *fly = need(r, "FLY ", "Circling suckers");
    int st[4096], nf = flink_frames(fly, st), k, i;
    Ents e = { 0 };
    for (k = 0; k < nf; k++) {
        Ents x = { 0 };
        lomax_pieces(fly->pc + st[k], st[k + 1] - st[k], s, 0, &x);
        if (x.n != 1) nf = -1;
        free(x.v);
        if (nf < 0) break;
    }
    if (nf != 2) die("Circling suckers: FLY isn't 2 frames of one sprite each, as FLINK.DLL expects");
    for (k = 0; k < 2; k++)
        for (i = st[k]; i < st[k + 1]; i++)
            if (is_sprite(&fly->pc[i])) {
                int xo, yo, from = e.n;
                Ent x;
                anim_piece_offset(s->w->anims.v[fly->pc[i].a], fly->pc[i].f, &xo, &yo);
                if (xo || yo) die("Circling suckers: FLY's sprite isn't at 0, 0 in its frame");
                x.bank = sprites_bank(s, SK_A(fly->pc[i].a)); x.frame = fly->pc[i].f + 1; x.dx = DRAW[k][0]; x.dy = DRAW[k][1]; x.fl = 0;
                vpush(e, x);
                end_frame(&e, from);
                break;
            }
    o[0] = pack_record("FLY ", fly->speed, fly->hb, e.v, e.n);
    free(e.v);
    return 1;
}

/* ---- the table ---- */
typedef struct {
    const char *name, *beh, *variant;
    const char *tags[12]; int ntags;
    ParamsFn params; ExtraFn extra;
    int main;                                                /* MAIN.WLD's records */
} Kind;

/* Flink's enemies as Lomax objects. The type is named '<behaviour> <variant> <n>'; FLINK.DLL gives
   the variant its Flink behaviour (Lomax's own behaviour of that object id doesn't know the records).
   Each entry: (behaviour, variant, the Flink records copied, one Lomax record each with the same tag
   and Flink's own hitbox, params from the placement and the records, more records or none).
   "Muta" is object 74 (ZombieRunner, Enemy_ZombieRunner_Spawn 0x42E1B0), whose spawn FLINK.DLL wraps
   for 'Muta Dummy', 'Muta Fire', 'Muta Holder', 'Muta Thief' and 'Muta Visje' types. Param 0 of all: 1 = starts facing left (Flink's checkbox
   'move left', GNMY +0x0B bit 0).
 */
static const Kind ENEMIES[] = {
    { "Boss", "Muta", "Boss", { 0 }, 0, p_boss, x_boss, 0 },
    { "Boss", "Muta", "Village", { "RUN ", "WALK", "STAY", "DEAD", "MEN1", "MEN2", "MEN3", "MEN4", "MEN5" }, 9, p_vboss, x_vboss, 0 },
    { "Boss", "Muta", "MetaBoss", { 0 }, 0, p_mboss, x_mboss, 0 },
    { "Forest Dummy", "Muta", "Dummy", { "WALK", "STUN", "SL45", "DEAD" }, 4, p_check1, x_spin, 0 },
    { "Ingredient Holder", "Muta", "Holder", { "WALK", "JUMP", "SL45", "DEAD" }, 4, p_check1, NULL, 1 },
    { "Thief", "Muta", "Thief", { "STAY", "GASP", "WALK", "JUMP", "SL45", "DEAD" }, 6, p_thief, NULL, 1 },
    { "Visje", "Muta", "Visje", { "SWIM", "JUMP", "DEAD?" }, 3, p_visje, NULL, 0 },
    { "Firemonster", "Muta", "Fire", { "WALK" }, 1, p_fire, x_fire, 0 },
    { "Statue", "Muta", "Statue", { "STAY", "SHOT", "DEAD" }, 3, p_statue, x_statue, 0 },
    { "Raketen wurm", "Muta", "Wurm", { "HEAD", "TAIL" }, 2, p_wurm, x_wurm, 0 },
    { "Puge", "Muta", "Puge", { "PUGE" }, 1, p_puge, x_erde, 0 },
    { "City dummy", "Muta", "City", { "STAY", "WALK", "ATCK", "STUN", "SL45", "DEAD", "LANS" }, 7, p_city, x_spin, 0 },
    { "Mountain dummy", "Muta", "Moun", { "STAY", "WALK", "STUN", "SL45?", "DEAD", "SHLD" }, 6, p_check1, x_spin, 0 },
    { "Yeti", "Muta", "Yeti", { "STAY", "WARN", "SPIN", "WAIT", "DEAD" }, 5, p_zero, x_erde, 0 },
    { "Buzzard", "Muta", "Buzz", { "STAY", "FLY " }, 2, p_buzz, x_buzz, 0 },
    { "Cyclin' Dude", "Muta", "Cycl", { "HANG", "DEAD", "WALK", "SL45" }, 4, p_cycl, NULL, 0 },
    { "Chopper Bike", "Muta", "Bike", { "CYCL", "DEAD" }, 2, p_zero, NULL, 0 },
    { "Little Dude", "Muta", "Litt", { "WALK", "STUN", "DEAD" }, 3, p_check1, x_smal, 0 },
    { "Intree dummy", "Muta", "Intr", { "WALK", "JUMP", "FLY ", "STUN", "SL45", "DEAD" }, 6, p_check1, x_smal, 0 },
    { "Worm", "Muta", "Worm", { "DEAD", "HEAD", "BODY" }, 3, p_worm, x_worm, 0 },
    { "Razorback", "Muta", "Razo", { "RUN ", "DEAD" }, 2, p_razo, NULL, 0 },
    { "Meta dummy", "Muta", "Meta", { "FLY ", "STUN", "DEAD" }, 3, p_zero, x_spin, 0 },
    { "Storm Cloud", "Muta", "Storm", { "WOLK" }, 1, p_storm, x_storm, 0 },
    { "Blitz dude", "Muta", "Blitz", { "STAY", "DEAD", "ZAP " }, 3, p_zero, x_blitz, 0 },
    { "Circling suckers", "Muta", "Circ", { "DEAD" }, 1, p_circ, x_circ, 0 },
    { "Donkey monkey", "Muta", "Donk", { "STAY", "JUMP", "LAND", "OUCH" }, 4, p_donk, NULL, 0 },
    { "Bos1", "Muta", "Bos1", { "STAY", "STA2", "FLY ", "OUCH", "ZAP ", "WARN", "HEAD", "TAIL" }, 8, p_bos1, x_bos1, 0 },
    { "Bos3 dude", "Muta", "Bos3", { "MOVE", "STUN", "DEAD" }, 3, p_zero, NULL, 0 },
};
#define NKINDS ((int)(sizeof(ENEMIES) / sizeof(ENEMIES[0])))

static const Recs *kind_recs(FWorld *fw, const Place *p, int *main)
{
    int i;
    *main = 0;
    for (i = 0; i < NKINDS; i++)
        if (!strcmp(ENEMIES[i].name, p->name) && ENEMIES[i].main) {
            *main = 1;
            return !strcmp(p->name, "Thief") ? g_main.thief : g_main.holder;
        }
    return world_recs(fw, p->slot);
}

/* the ENEMIES entry for a placement: a list holds the kinds of one Flink type that differ by world
   (the bosses), the first whose params can be made */
/* enemy_kind: the ENEMIES entry for a placement; for 'Boss' the first whose params can be made (the
   other kinds' params are made only once their records are checked: Bos1's add its message) */
static const Kind *enemy_kind(const Place *p, const Recs *recs)
{
    int i, out[16];
    for (i = 0; i < NKINDS; i++) {
        if (strcmp(ENEMIES[i].name, p->name)) continue;
        if (strcmp(p->name, "Boss") || ENEMIES[i].params(p, recs, out) >= 0) return &ENEMIES[i];
    }
    return NULL;
}

/* The stun effect. An enemy's STUN frames carry an attach point (a piece with frame bit 7 and flags
   7: the 7th hook in its type's table at actor+8, 0x1B4BC for the Forest Dummy), which draws one
   frame of MAIN.WLD's 'Shot' type there as a single sprite, its top-left 8 px left of and 16 px above
   the point: the bubbles (Shot STUN) while the stun timer is 120 or more, then the wake-up lines (Shot
   WAKE). They run on counters of their own (0x86F0 / 0x86FC, stepped by the records' speeds at
   0x332DC). Lomax draws one record at a time, so the enemy gets a STUN and a WAKE record with a frame
   for every (own STUN frame, effect frame) pair, frame = own * effect frames + effect, the effect
   drawn first (in front, as Flink's attach point comes first), and FLINK.DLL picks the frame. Its
   STUN_FX table holds these frame counts and speeds, checked against MAIN.WLD here.

   an enemy's Flink STUN record -> its Lomax STUN and WAKE records, as (bytes, banks) */
/* stun_records: an enemy's STUN record -> its Lomax STUN and WAKE records, a frame for every (own
   frame, effect frame) pair, the effect (MAIN.WLD's Shot STUN or WAKE) at the attach point first */
static int stun_records(const Rec *rec, Sprites *s, LRec *o)
{
    static const struct { const char *tag; int n, speed; } FX[2] = { { "STUN", 4, 35 }, { "WAKE", 2, 42 } };
    int st[4096], nf = flink_frames(rec, st), q, k, j, i;
    for (q = 0; q < 2; q++) {
        const Rec *fx = rec_get(g_main.shot, FX[q].tag);
        int fst[4096], nfx;
        Ents e = { 0 };
        if (!fx) die("MAIN.WLD Shot: no %s", FX[q].tag);
        nfx = flink_frames(fx, fst);
        if (nfx != FX[q].n || fx->speed != FX[q].speed)
            die("MAIN.WLD Shot %s: %d frames at speed %d; FLINK.DLL expects %d at %d", FX[q].tag, nfx, fx->speed, FX[q].n, FX[q].speed);
        for (k = 0; k < nf; k++) {
            const Piece *at = NULL;
            for (i = st[k]; i < st[k + 1]; i++) if (rec->pc[i].f & 0x80 && rec->pc[i].fl & 7) { at = &rec->pc[i]; break; }
            for (j = 0; j < nfx; j++) {
                int from = e.n;
                if (at) {
                    Piece p = fx->pc[fst[j]];
                    p.dx = at->dx - 8; p.dy = at->dy - 16;
                    lomax_pieces(&p, 1, s, 1, &e);
                }
                lomax_pieces(rec->pc + st[k], st[k + 1] - st[k], s, 0, &e);
                end_frame(&e, from);
            }
        }
        o[q] = pack_record(FX[q].tag, rec->speed, rec->hb, e.v, e.n);
        free(e.v);
    }
    return 2;
}

/* -> (new NMY type records, their anim banks per type, GNMY entries, {name: count placed}) */
void convert_enemies(FWorld *fw, Places *pl, Sprites *s, int first_type, EnemyOut *out)
{
    HMap index = { 0 };
    int i, k, ntypes = 0;
    g_worldRecName.n = 0;
    g_worldRecPtr.n = 0;
    memset(&g_worldRecs, 0, sizeof(g_worldRecs));
    for (i = 0; i < fw->nmy.n && i < fw->recs.n; i++)          /* WORLD_RECS */
        if (*fw->nmy.v[i]) { vpush(g_worldRecName, nmy_name(fw->nmy.v[i])); vpush(g_worldRecPtr, &fw->recs.v[i]); }
    for (i = 0; i < pl->n; i++) {
        const Place *p = &pl->v[i];
        int main, prmv[16], np, idx;
        const Recs *recs = kind_recs(fw, p, &main);
        const Recs *rr = recs ? recs : &g_worldRecs;
        const Kind *e = enemy_kind(p, rr);
        if (!e) continue;
        for (k = 0; k < e->ntags; k++) {
            const char *t = e->tags[k];
            if (t[strlen(t) - 1] != '?' && !recs_has(rr, t)) break;
        }
        if (k < e->ntags) continue;
        if (e->extra == x_fire && (!recs_has(rr, "FIRE") || !recs_has(rr, "FADE"))) continue;
        np = e->params(p, rr, prmv);
        if (np < 0) continue;                                 /* (a variant not converted) */
        if (!hmap_get(&index, &p->slot, sizeof(int), &idx)) {
            LRec rec[48];
            int n = 0;
            Buf t = { 0 };
            char name[64];
            for (k = 0; k < e->ntags; k++) {
                char tag[5];
                memcpy(tag, e->tags[k], 4);
                tag[4] = 0;
                if (!recs_has(rr, tag)) continue;
                if (!strcmp(tag, "STUN")) n += stun_records(rec_get(rr, tag), s, rec + n);
                else {
                    RecSel sel = { rec_get(rr, tag), 0, 0 };
                    rec[n++] = lomax_record(tag, &sel, 1, s, 0, 0, 0, 0, 1, 0, main);
                }
            }
            if (e->extra) n += e->extra(rr, s, rec + n);
            idx = first_type + ntypes;
            hmap_put(&index, &p->slot, sizeof(int), idx);
            snprintf(name, sizeof(name), "%s %s %d", e->beh, e->variant, ntypes);
            lomax_type(&t, name, np, 0, rec, n);
            vpush(out->types, t);
            ntypes++;
            for (k = 0; k < n; k++) { bank_union(&out->banks, &rec[k].banks); free_lrec(&rec[k]); }
        }
        {
            Obj o = obj(idx, p->x, p->y, 0, prmv, np, p);
            vpush(out->objs, o);
        }
        for (k = 0; k < out->placedName.n; k++)
            if (!strcmp(out->placedName.v[k], p->name)) { out->placedCount.v[k]++; break; }
        if (k == out->placedName.n) { vpush(out->placedName, p->name); vpush(out->placedCount, 1); }
    }
    hmap_free(&index);
}
