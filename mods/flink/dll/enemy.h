/*
 * enemy.h - shared by Flink's enemies: enemy.c (spawn, walking, falling, dying, Lomax's stomp),
 * dummy.c (the Forest Dummy), city.c (the City dummy), mountain.c (the Mountain dummy), intree.c (the Intree dummy), metadummy.c (the Meta dummy), storm.c (the Storm Cloud), blitz.c (the Blitz dude), circ.c (the Circling suckers), worm.c (the Worm), firemonster.c (the Firemonster), holder.c (the Ingredient Holder) and
 * yeti.c (the Yeti), buzzard.c (the Buzzard), razorback.c (the Razorbacks' stampede), cyclindude.c (the Cyclin' Dude, his Chopper Bike and the Little Dude), thief.c (the Thief), visje.c (the Visje), statue.c (the Statue), wurm.c (the Raketen wurm), puge.c (the Puge)
 * forestboss.c (the Forest's boss), donkey.c (the Donkey monkey) and bos1.c (the final boss's Bos3 dudes); villageboss.c, metaboss.c
 * and bos1.c run their bosses on their own.
 *
 * A new kind of enemy: an ENEMIES entry in the converter (a 'Muta <name> n' type), its behaviour in a
 * file of its own like dummy.c, and its spawn and stomp in enemy_spawn and enemy_stomped.
 */
#ifndef ENEMY_H
#define ENEMY_H

#include "flink.h"

#define WORM_SEGS          4             /* the Worm's head and body segments (worm.c) */

/* per actor slot: their type, dead, the stun or death timer, hopping (falling after a stomp),
   stopping to turn, sliding down a 45 degree slope, and the stun animation's counters (8.8 frames:
   its own, the bubbles', the wake-up lines') */
typedef struct {
    BYTE *type;
    int dead, timer, hop, turning, slide, own, fx[2];
    /* enemy_attacked: Lomax's attack overlapped it last tick; ticks left in which attacks don't count */
    int hit, guard;
    /* the Firemonster (fire_update): one, breathing, its walk or breath counter, falling, its mouth */
    int fire, breath, t, air, mdx, mdy;
    /* the Ingredient Holder (holder.c): one, hit once (runs scared); it also uses timer (the
       blinking after the first hit), air, slide, turning and own */
    int holder, scared;
    /* the Thief (thief.c): one, its state, the speed it walks at, the countdown before it stops
       chasing, the coins it holds and the coin drawn in its hand; t is its gasp countdown, timer the
       frames it doesn't hurt after a steal, mdx / mdy its hand's offset */
    int thief, state, speed, stop, stack;
    BYTE *coin;
    /* the Visje (visje.c): one, deadly (can't be killed), its swim's half-width * 4 (0: straight on at
       speed), centre x and phase, its bob's phase, its placement's y, the jump countdown's restart;
       speed is its straight vx, t the jump countdown */
    int visje, deadly, amp, cx, xph, yph, home, every;
    /* the Statue (statue.c): one, and its last shot; t counts to the next shot, every is its period,
       speed the shots' vx, home its placement's y (it rises to it), mdx / mdy its mouth */
    int statue;
    BYTE *shot;
    /* the Raketen wurm (wurm.c, its own state there): one */
    int wurm;
    /* the Puge (puge.c, its own state there): one */
    int puge;
    /* the boss (forestboss.c, its own state there): one */
    int boss;
    /* the Donkey monkey (donkey.c, its own state there): one */
    int donk;
    /* the City dummy (city.c): one, charging, its lance tip (left, top, right, bottom); speed is the
       speed it walks at (0: stopping to stand), t its walk countdown, stop the charge's frames left
       with Lomax behind it */
    int city, charge, lance[4];
    /* the Mountain dummy (mountain.c): one, the frames left in which its shield ignores shots
       (+0x4C); speed is the speed it walks at (0: stopping to stand), t its walk countdown (+0x4E),
       stop its stand countdown (+0x50) */
    int moun, block;
    /* the Yeti (yeti.c, its own state there): one */
    int yeti;
    /* the Buzzard (buzzard.c, its own state there): one */
    int buzzard;
    /* the Meta dummy (metadummy.c, its own state there): one; timer is its stun, as the Forest Dummy's */
    int meta;
    /* the Storm Cloud (storm.c, its own state there): one */
    int storm;
    /* the Blitz dude (blitz.c, its own state there): one */
    int blitz;
    /* the Circling suckers (circ.c, their own state there): 1 the ring's placement actor, 2 a head */
    int circ;
    /* the Cyclin' Dude and the Little Dude (cyclindude.c, their own state there): one */
    int cycl, litt;
    /* the Razorback, a spawner or one of its Razorbacks (razorback.c, its own state there): one */
    int razo;
    /* the Bos3 dude, dropped by the final boss (bos1.c): one; t is its wait on the ground (+0x46) */
    int bos3;
    /* the Intree dummy (intree.c): one, gliding (the walker's bit 15), the ticks since its jump (JUMP
       played once); air, slide and turning are the walker's bits 8, 9 and 10, t the jump timer */
    int intr, glide, jumped;
    /* the Worm (worm.c): one, its segments' x, y (negative: going left) and z (raised); t is its
       64-frame timer. Dead: wx / wy are its flying pieces' places (16.16), bvx / bvy their speeds,
       pieces their count, left whether DEAD is mirrored, timer the ticks since */
    int worm, wx[WORM_SEGS], wy[WORM_SEGS], wz[WORM_SEGS], bvx[WORM_SEGS], bvy[WORM_SEGS], pieces, left;
    /* enemy_wait: spawned ahead of Flink's window, it waits; its update and flags meanwhile */
    void *wake;
    DWORD wflags;
} Enemy;
extern Enemy g_enemy[MAX_ACTORS];

/* enemy.c */
int  enemy_slot(BYTE *a);
int  wall_at(const BYTE *a, int x, int right);
int  flink_wall_ahead(const BYTE *a, int right);   /* Flink's 0x1AD4C: the cells its hitbox spans */
int  fall_and_land(BYTE *a, int friction, int land);
int  fall_and_land_g(BYTE *a, int gravity, int friction, int land);
void enemy_walk(BYTE *a, int i);
void enemy_die(BYTE *a, int i, int knock);
void enemy_dead_update(BYTE *a, int i);
int  enemy_attacked(BYTE *a, int i);   /* 1: counts as a stomp, 2: the roll, kills */
BYTE *thrown_helmet(void);             /* Lomax's thrown helmet, if one is out */
int  player_attack_box(int plane, int x1, int y1, int x2, int y2);   /* his spin, roll or helmet reaches the box */
int  flink_stomp_box(int plane, int x1, int y1, int x2, int y2);     /* Flink's stomp test on the box */
int  enemy_on_screen(const BYTE *a);   /* within Lomax's view, 32 px margin */
int  flink_cam_x(const BYTE *pl);      /* Flink's camera x: the player's x - 0xA4 within the map, or the Scroll Point's */
int  flink_window(const BYTE *a);      /* its point is within Flink's activation window 0 */
BYTE *enemy_defeated(BYTE *a);

/* dummy.c */
void __cdecl dummy_update(BYTE *a);
void dummy_stomped(BYTE *a, int i, int knock);
void stun_frame(BYTE *a, int i);       /* the stun animation and its effect, from the timer */

/* city.c */
void city_spawn(BYTE *a, int i, const BYTE *def);
void city_walk(BYTE *a, int i);
void city_walk_again(int i);
void city_lance(BYTE *a);
void flying_piece(BYTE *a, DWORD tag);

/* mountain.c */
void moun_spawn(BYTE *a, int i);
void moun_walk(BYTE *a, int i);
void moun_walk_again(int i);
void moun_tick(int i);
void moun_shield_off(BYTE *a);
int  moun_blocks(BYTE *a, int i);

/* intree.c */
void intree_spawn(BYTE *a, int i);
void intree_think(BYTE *a, int i);

/* worm.c */
void worm_spawn(BYTE *a, int i);
void worm_stomped(BYTE *a);
void worm_kill(BYTE *a, int i);

/* yeti.c */
void yeti_spawn(BYTE *a, int i);

/* razorback.c */
void razo_spawn(BYTE *a, int i, const BYTE *def);

/* buzzard.c */
void buzzard_spawn(BYTE *a, int i, const BYTE *def);
void buzzard_die(BYTE *a, int i);

/* metadummy.c */
void meta_spawn(BYTE *a, int i);
void meta_stomped(BYTE *a, int i, int knock);

/* storm.c */
void storm_spawn(BYTE *a, int i);
void storm_die(BYTE *a, int i);

/* blitz.c */
void blitz_spawn(BYTE *a, int i, const BYTE *def);
void blitz_die(BYTE *a, int i, int knock);

/* circ.c */
void circ_spawn(BYTE *a, int i, const BYTE *def);

/* cyclindude.c */
void cycl_spawn(BYTE *a, int i, const BYTE *def);
void cycl_stomped(BYTE *a, int i);
void cycl_struck(BYTE *a, int i, int small);
void litt_spawn(BYTE *a, int i);
void bike_spawn(BYTE *a, int i);

/* holder.c */
int  jump_walk(BYTE *a, int i, int target, int ledge_turn);
int  turn_at_wall(BYTE *a, int i);
void holder_spawn(BYTE *a, int i);
void holder_stomped(BYTE *a, int i, int knock);

/* thief.c */
void thief_spawn(BYTE *a, int i, const BYTE *def);
int  thief_touched(BYTE *a, int i);
void thief_die(BYTE *a, int i, int knock);

/* visje.c */
void visje_spawn(BYTE *a, int i, const BYTE *def);

/* statue.c */
void statue_spawn(BYTE *a, int i, const BYTE *def);
void statue_stomped(BYTE *a);
void statue_die(BYTE *a, int i);

/* wurm.c */
void wurm_spawn(BYTE *a, int i, const BYTE *def);
void wurm_stomped(BYTE *a);

/* puge.c */
void puge_spawn(BYTE *a, int i, const BYTE *def);

/* forestboss.c */
void boss_untouchable(void);           /* a defeated boss dying: Lomax can't be hurt this tick */
void boss_quiet_after(void);           /* after Player_Update: no blink for that timer */
void boss_spawn(BYTE *a, int i, const BYTE *def);
void boss_gone(BYTE *a, const BYTE *def);
void boss_stomped(BYTE *a, int i);
void boss_struck(BYTE *a, int i);

/* donkey.c */
void donk_spawn(BYTE *a, int i, const BYTE *def);
void donk_gone(BYTE *a, int i, const BYTE *def);

/* bos1.c: the final boss (24-2) and his Bos3 dudes */
void bos1_spawn(BYTE *a, const BYTE *def);
void bos1_gone(BYTE *a, const BYTE *def);
void bos3_spawn(BYTE *a, int i);

/* firemonster.c */
void fire_spawn(BYTE *a, int i, const BYTE *def);
void fire_die(BYTE *a, int i);

#endif
