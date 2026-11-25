/*
 * flink.h - what FLINK.DLL's files share: the levels read from LEVELS.INI, logging and patching,
 * and each file's entry points. See main.c for how the files fit together.
 */
#ifndef FLINK_H
#define FLINK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"

#define MAX_LEVELS         31            /* (the password keeps 5 bits of the section) */
#define MAX_SUBS           5             /* aSubLevelPlaneMask[5] */
#define ROW_BONUS          31            /* the level table's rows after Flink's: bonus level, map, title */
#define SECTION_MAP        (ROW_BONUS + 1)
#define SECTION_TITLE      (ROW_BONUS + 2)
#define SECTION_SCENE      (ROW_BONUS + 3)   /* Flink's intro and ending (scenes.c) */
#define SCENE_PLANES       0xF               /* their 4 planes (scenes.c) */
#define COUNT(arr)         (sizeof(arr) / sizeof(*(arr)))

/* Flink's draw list n (0-3, the tails 0x7D6A + 2n at actor +0) -> Lomax's draw layer (+0x60).
   Flink's Actors_DrawAll (0x33356) draws list 0, list 1, then Flink (0x81E2, draw 0x328C4), then
   lists 2 and 3, the first drawn in front. Lomax's layers 3, 2 (buckets plane + 10, + 8), Lomax's 6
   (+ 6), then 1, 0 (+ 4, + 3): the same order. */
#define FLINK_LAYER(n)     (3 - (n))

/* util.c: logging and patching */
extern char g_dir[MAX_PATH];             /* the DLL's folder */
extern int g_log;                        /* FLINK.LOG on: Lomax+'s log switch in the registry */
void log_init(void);
void flog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void patch(DWORD addr, const void *bytes, size_t n);
void patch_dword(DWORD addr, DWORD value);
void patch_call(DWORD at, const void *target);
int  is_call(DWORD at, DWORD target);
int  is_bytes(DWORD at, const void *bytes, size_t n);

/* levels.c: the level table, LEVELS.INI and the loading of Flink's maps */
extern int g_active;                     /* a Flink map is loaded */
extern int g_nlevels, g_nsubs[MAX_LEVELS];
extern char g_files[MAX_LEVELS][MAX_SUBS][MAX_PATH];   /* the names World_LoadWLD gets */
const char *map_name(int sec, int sub);  /* the file name alone (Lnn.WLD) */
int  level_secret(int sec);              /* a secret level ([Secret]): reached only by its route */
int  level_rank(int sec);                /* the level's place in the play order (levels.c DETOURS) */
int  level_wld_index(int sec);           /* its row's bWldIndex (+0x0A; 5: PowerWheel_Open pauses the CD) */
/* the shortcuts (README "Spells", Shortcuts): SHRINK at 14-1's tree to 25, GHOST at 20-1's altar to 29 */
#define SHORTCUT_SHRINK    1
#define SHORTCUT_GHOST     2
#define SHORTCUT_SHRINK_LEVEL 14
#define SHORTCUT_SHRINK_TO 25
#define SHORTCUT_GHOST_LEVEL 20
#define SHORTCUT_GHOST_TO  29
extern int g_shortcuts;                  /* the SHORTCUT_* taken in this game */
void level_shortcut(int shortcut, int to);   /* taken: the screen fades out into level `to` */
int  level_map_from(void);               /* the level the world map pans from (the one just left), -1: none */
void level_map_from_clear(void);         /* (password.c: a password's map pans as a new game's) */
int  level_map_rank(int sec);            /* level_rank for the world map's files (a taken shortcut's level
                                            counts as the next) */
int  levels_retail(void);
#define LEVELS_FOLDER      "WORLDS"      /* the converted levels, next to the DLL */
int  levels_load(void);
void levels_install(void);

/* worldmap.c: Flink's world map in place of Lomax's map screen */
extern int g_map;                        /* Flink's world map is loaded */
void worldmap_load(const char *ini, const char *rel, const char *dir);
const char *worldmap_file(void);         /* the map file for g_LastRealSection; NULL: Lomax's map */
void worldmap_row(BYTE *row);            /* the map's level table row for it */
void worldmap_reset(void);
void worldmap_before(void);              /* around Player_Update: Lomax at the view's bottom right */
void worldmap_after(void);
void __cdecl mapctl_spawn(BYTE *a, BYTE *def);
void __cdecl hero_spawn(BYTE *a, BYTE *def);

/* title.c: Flink's title screen in place of Lomax's */
extern int g_title;                      /* Flink's title is loaded */
void title_load(const char *ini, const char *rel, const char *dir);
const char *title_file(int sec, int sub);   /* Flink's title for that section and sub-level; NULL: none */
void title_row(BYTE *row);               /* the title's level table row for it */
void title_reset(void);
void title_before(void);                 /* around Player_Update: Lomax hangs from the logo, the camera */
void title_after(void);
void logo_spawn(BYTE *a);                /* 'Spik Logo' (from spik_spawn) */
void __cdecl titlectl_spawn(BYTE *a, BYTE *def);

/* scenes.c: Flink's intro and ending, without Flink */
extern int g_scene;                      /* the scene loaded (0-5 the intro, 6-7 the ending), -1: none */
void scenes_load(const char *ini, const char *rel, const char *dir);
void scene_row(BYTE *row);               /* the scenes' level table row */
const char *scene_file(void);            /* world_load: the scene's file, NULL: none (may turn the title's
                                            load into the intro's) */
int  scene_ending(void);                 /* setup_next_level: the game finished, the ending next; 0: none */
void scene_spawn(BYTE *a);               /* 'Spik Scene' (from spik_spawn) */
void scenes_reset(void);
int  scene_hides_player(void);           /* a scene: Lomax neither drawn nor updated */

/* gone.c: opened chests and dead enemies stay gone for the level visit */
void forget_placement(BYTE *def);
int  placement_gone(const BYTE *def);
void spawn_gone(BYTE *a);
void gone_reset(void);

/* water.c: the tide and swimming */
#define SWIM_MODE          2             /* Flink's water: 0 Lomax drowns, 1 he floats and never drowns,
                                            2 he walks under water and never drowns (water.c, move.c) */
int  water_present(void);                /* the map's water runs: g_WaterLevelY is its surface */
void water_tick(void);                   /* one player tick (move_after) */
void float_track(BYTE *a, int mega);
int  water_retail(void);
void water_install(void);
void water_reset(void);

/* move.c: Lomax's speed, jump height and walking under water in Flink's maps (its defines) */
void move_init(void);
int  move_retail(void);
void move_set(int flink);                /* from world_load */
int  move_speed(int v);                  /* a speed limit scaled as Lomax's speed is now */
void move_before(void);                  /* around Player_Update (player_tick) */
void move_after(void);
int  move_jump_force(void);              /* a full jump's start force now (heavy, under water) */
int  move_under_water(void);
void move_jump_off(void);                /* a jump off the Chopper Bike (cyclindude.c) */

/* stomp.c: the high bounce off an enemy's head with Jump held, Flink's way */
void stomp_reset(void);                  /* from world_load */
void stomp_before(void);                 /* before Player_Update (player_tick) */
void contact_before(void);               /* contact.c, before Player_Update */
void stomp_bounce(void);                 /* Lomax bounced off an actor (contact_bounce) */

/* platforms.c: Mega and boat spawns, the dipping leaves */
void __cdecl mega_spawn(BYTE *a, BYTE *def);
void __cdecl boat_spawn(BYTE *a, BYTE *def);
void platforms_reset(void);
int  platforms_retail(void);
void chains_set(int flink);              /* chain links every 16 px (Flink's maps) or 8 (Lomax's) */

/* jumppad.c: Flink's Jumppads and Bouncy platforms */
void jumppad_spawn(BYTE *a, int slot);
void bouncy_spawn(BYTE *a, BYTE *def, int slot);
void jumppads_reset(void);

/* seesaw.c: Flink's seesaws ('Mega Seesaw') */
void seesaw_spawn(BYTE *a, int slot);
void seesaws_reset(void);
int  seesaw_under_lomax(void);           /* Lomax stands on one (Flink: 0x82FE's think is 0x2BC0A) */

/* deco.c: Spik decorations: deadly ground and the Trees */
void __cdecl spik_spawn(BYTE *a, BYTE *def);
void deco_reset(void);

/* enemy.c (and dummy.c, firemonster.c, holder.c, thief.c, visje.c, statue.c): Flink's enemies */
void __cdecl enemy_spawn(BYTE *a, BYTE *def);
int  enemy_bounce(BYTE *a);              /* Lomax bounced off it: 1 if it is a Flink enemy */
int  enemy_harmless(BYTE *a);            /* a stunned or dying Flink enemy */
int  enemy_touched(BYTE *a);             /* Lomax touched it: 1 if that did something else than hurt him (a Thief's steal) */
int  enemy_stompable(BYTE *a);           /* a Flink enemy Flink's stomp test applies to (contact.c) */
void enemies_reset(void);

/* grab.c: picking up, carrying and throwing with the Ability button (README "Grabbing and throwing").
   What can be picked up, as Flink's actor fields describe it: */
typedef struct {
    int heavy;      /* carried overhead: Lomax walks slower and jumps lower; thrown in an arc (Flink +6 bit 0) */
    int small;      /* thrown as the small spinning shot, SHSM instead of SPIN (+6 bit 1) */
    int *fuse;      /* ticks before it jumps out of Lomax's hands and hurts him (a Forest Dummy's stun),
                       counting down while carried or thrown; NULL: none (+6 bit 2 set), and then it
                       survives what it hits and stays a thrown object once it lands */
    int dx, dy;     /* where it is held, from Lomax's hand (+0x10, +0x12) */
    int respawns;   /* thrown as a shot, it keeps its placement (the small stone, think 0x2E76A) */
    int platform;   /* thrown or at rest, Lomax can stand on it (the Throw platform: Platform_MoveAndCarryPlayer) */
    int (__cdecl *think)(BYTE *a);     /* thrown or at rest: runs first every tick (Flink's extra hook
                                          +0x5C, +6 bit 6); nonzero = it moved itself, no fall this tick */
    void (__cdecl *picked)(BYTE *a);   /* Lomax picked it up */
    void (__cdecl *after)(BYTE *a);    /* thrown or at rest: runs last every tick, as Flink's +0x5C
                                          hook does (0x325CA; grab_set_after) */
} GrabKind;
int  grab_retail(void);
void grab_install(void);
void grab_reset(void);
int  grab_held(const BYTE *a);           /* carried, thrown or a shot: grab.c runs it */
void grab_rest(BYTE *a, const GrabKind *k, int src);   /* a thrown object at rest from its spawn on */
void stone_spawn(BYTE *a, BYTE *def);    /* 'Spik Stone' (from spik_spawn) */
int  grab_carrying(void);                /* Lomax carries something (Flink: 0x8304 >= 0) */
int  grab_carrying_heavy(void);          /* Lomax carries a heavy object (Flink: 0x8304 >= 0, +6 bit 0) */
BYTE *grab_falling_on(const BYTE *a, int x1, int y1, int x2, int y2);   /* a thrown object falling into the box */
int  grab_land_on(const BYTE *p, int reach);
void grab_set_after(BYTE *a, void (__cdecl *after)(BYTE *a));   /* its +0x5C hook (NULL: none) */
void grab_let_go(BYTE *a);               /* thrown or carried: its own update again, at its speed */
int  grab_is_carried(const BYTE *a);     /* Lomax carries it */
void grab_freeze(BYTE *a, int on);       /* on: out of play (Flink's think Hook_Nop): no fall, pick-up or hit */   /* heavy thrown objects on platform p's top (Flink 0x1A1B2): 1 = any */
enum { SRC_ENEMY, SRC_CHEST, SRC_STONE, SRC_THROWPLAT, SRC_APPLE };

/* leaves.c: Flink's Leaveplatform, the falling leaves ('Mega Leaf') and their spawner ('Mega Leaves') */
void leaf_spawn(BYTE *a, BYTE *def, int slot, int spawner);
void leaves_reset(void);

/* floating.c: Flink's Floating platform ('Mega Floating'), the spawner of the leaves blowing across */
void floating_spawn(BYTE *a, BYTE *def, int slot);
void floatings_reset(void);

/* steer.c: Flink's Steer platform ('Mega Steer'), ridden and steered down */
void steer_spawn(BYTE *a, int slot);
void steers_reset(void);
void steer_before(void);                /*   around Player_Update (player_tick): Flink's walk on it */
void steer_after(void);
int  steer_waiting(void);               /*   a Steer platform not stood on yet: the Raketen wurms wait (wurm.c) */

/* hand.c: Flink's Hand ('Mega Hand'), a platform closing into a fist on a timer */
void hand_spawn(BYTE *a, BYTE *def, int slot);
void hands_reset(void);
void wall_spawn(BYTE *a, BYTE *def, int i);   /* (from mega_spawn) */
/* rock.c: Flink's Rock, the block Lomax pushes ('Mega Rock') */
void rock_spawn(BYTE *a, BYTE *def, int slot); /* (from mega_spawn) */
int  rock_is(BYTE *a);                    /* rock.c: a Rock (razorback.c's touch test) */
void rock_hit(BYTE *r, BYTE *b);         /*   a Razorback b ran into it and died: counted, b turned back */
void rocks_reset(void);
void rock_before(void);                 /*   around Player_Update (player_tick): Flink's push, 0x1C31C */
void rock_after(void);
int  rock_bounce(BYTE *o, int x1, int y1, int x2, int y2);   /* a thrown object's box met a rock: turned back (grab.c) */
int  push_check(BYTE *a, int x1, int y1, int x2, int y2, const int *vx, int vy);   /* Flink's 0x1AA56 for any pushable
                                            object (its hitbox, its vx, read again after Player_Update, its vy's px): 1 = pushed */
/* bigapple.c: Flink's Apple (big) ('Spik Appl', rolled and pushed) and App2 ('Spik App2', rolled, carried, broken) */
void bigapple_spawn(BYTE *a, BYTE *def, int small);   /* (from spik_spawn) */
void bigapples_reset(void);
int  bigapple_grab_kind(BYTE *a, GrabKind *k);   /* an App2 at rest can be picked up (grab.c) */
int  bigapple_struck(BYTE *t, int x1, int y1, int x2, int y2);   /* a thrown object's box breaks an App2 (grab.c) */
int  donk_struck(BYTE *t, BYTE *o, int x1, int y1, int x2, int y2, int shot);   /* donkey.c: o's box touched the Donkey monkey (shot: Flink's Shot) */
void donkeys_reset(void);
void donk_after(void);                   /*   after Player_Update (player_tick): the arena's edges */
int  flink_quake(void);                  /* razorback.c: Flink's shake counter (0x874E) */
void flink_quake_set(int n);
/* wagon.c: Flink's Wagon ('Mega Wagon') */
void wagon_spawn(BYTE *a, BYTE *def, int slot);   /* (from mega_spawn) */
void wagons_reset(void);
void wagon_before(void);                /*   around Player_Update (player_tick): Left / Right are the Wagon's */
void wagon_after(void);
void actor_break_up(BYTE *a);               /* Flink's 0x1B1F8: its current frame's pieces fly off (wagon.c) */
void actor_break_up_sound(BYTE *a, int sound); /*   with Flink's sound 0x2A or none (0: Flink's +3 bit 5) */
void actor_break_up_vy(BYTE *a, int sound, int up);   /*   and every piece faster up by `up` (16.16) */
/* explode.c: Flink's Exploding platform ('Mega Explode'), going and breaking up once stood on */
void explode_spawn(BYTE *a, BYTE *def, int slot);   /* (from mega_spawn) */
void explodes_reset(void);
/* wolky.c: Flink's Wolky platform ('Mega Wolky'), a cloud rising off the top; its think, which blitz.c's cloud runs */
void wolky_spawn(BYTE *a, BYTE *def, int slot);   /* (from mega_spawn) */
void wolky_reset(void);
void wolky_step(BYTE *a, int *phase, int *dip, int *vy);
/* bridge.c: Flink's rope bridges, whose planks fall ('Mega Bridge') */
void bridge_spawn(BYTE *a, int slot);          /* (from mega_spawn) */
void bridges_reset(void);
void walls_reset(void);
void wurms_reset(void);
void puges_reset(void);                 /* puge.c */
void cyclindudes_reset(void);              /* cyclindude.c: the Cyclin' Dude, his bike, the ride */
void ride_before(void);                 /*   around Player_Update (player_tick) */
void ride_after(void);
int  ride_active(void);                 /*   Lomax rides a Chopper Bike */
int  grab_touch(BYTE *a);               /* grab.c: Lomax touches it and may take hold (the Chopper Bike) */
int  grab_can_touch(const BYTE *a);     /*   (its conditions but the touch: the Rope slides test their own) */
int  wurm_struck(BYTE *t, int x1, int y1, int x2, int y2);   /* wurm.c: a thrown object's box kills it */
int  hand_harmless(const BYTE *a);       /* a closed, not deadly hand: Lomax only bounces off it */

/* plant.c: Flink's Plant ('Mega Plant'), grown with Ability for spirits */
void plant_spawn(BYTE *a, BYTE *def, int slot);
void plants_reset(void);
int  plant_grow(void);                   /* Ability pressed: 1 if the plant Lomax stands on grows */

/* throwplat.c: Flink's Throw platform ('Mega Throw'), carried and thrown, floats and drifts */
void throwplat_spawn(BYTE *a, BYTE *def, int slot);
void throwplats_reset(void);

/* enemy.c: the Forest Dummy can be picked up while stunned; thrown objects kill Flink's enemies */
int  enemy_grab_kind(BYTE *a, GrabKind *k);
void enemy_grab_frame(BYTE *a);          /* carried or thrown: its stun animation */
void enemy_grab_resume(BYTE *a);         /* landed: its own update runs again */
void enemy_grab_shot(BYTE *a);           /* thrown as a shot: out of the game (gone for good, defeated) */
void enemy_grab_break(BYTE *a);          /* broke on what it hit: dies */
int  enemy_struck(BYTE *a, int small);   /* hit by a thrown object (small: +6 bit 1): 1 if it hit a Flink enemy */
int  moun_shield(BYTE *t, BYTE *o, int shot);   /* mountain.c: the shield took thrown object o (shot: Flink's Shot) */
int  yeti_deflects(BYTE *t, BYTE *o);    /* yeti.c: a Yeti turned thrown object o back */
int  yeti_spiked(BYTE *a);               /* yeti.c: a Yeti (landing on it hurts) */
int  razorback_spiked(BYTE *a);               /* razorback.c: a running Razorback (landing on it hurts) */
void razorbacks_reset(void);                  /* razorback.c: from world_load and every level start (init_planes) */
void razorback_before(void);                  /*   around Player_Update (player_tick): the stampede's shake */
void razorback_after(void);
void storms_reset(void);                      /* storm.c: from world_load */
void blitz_reset(void);                       /* blitz.c: from world_load */
void circ_reset(void);                        /* circ.c: from world_load */
void storm_before(void);                      /*   before Player_Update (player_tick): the Storm Clouds' wind */

/* chests.c: Flink's chests */
void __cdecl pots_spawn(BYTE *a, BYTE *def);
int  is_chest(BYTE *a);
int  chest_grab_kind(BYTE *a, GrabKind *k);   /* an open chest can be picked up (heavy) */
void chest_bounce(BYTE *a);              /* Lomax bounced off it: opens a closed chest */
int  chest_unlock(void);                 /* Ability pressed: 1 if a locked chest in reach took it */
void ground_clods(BYTE *a, int x, int y);   /* Fx_SpawnDebris: 5 ERDE clods out of the ground at x, y (the actor's type has ERDE) */
int  chests_retail(void);
void chests_reset(void);

/* hints.c: Flink's information notes in Lomax's hint box */
void hints_load(const char *ini);
int  hints_retail(void);
void hints_install(void);
BYTE *hint_box(int msg, int view_w);     /* opens Lomax's hint box with Flink's message msg, centred in the view (NULL: none); +0x80 = 1 closes it */

/* contact.c: Player_Update's contact loop tells the actor */
int  contact_retail(void);
void contact_install(void);

/* exits.c: Exits, doors and arrivals */
void __cdecl trapdoor_spawn(BYTE *a, BYTE *def);
void __cdecl exit_spawn(BYTE *a, BYTE *def);
void arrivals_load(const char *ini);
void exits_reset(void);
void trapdoor_skip_drop(void);           /* a Scroll Point scrolling from the start: Lomax standing */
void exit_walk_before(void);             /* exits.c: the walk off an Exit follows the ground (player_tick) */
int door_walk_scale(void);
int door_walk_lift(void);

/* sfx.c: Flink's sound effects (SFX\Enn.WAV, from FLINK.EXE), played by Flink's sound ID */
int  sfx_retail(void);
void sfx_set(int flink);                 /* flink_settings: loads them once; the 1UP's sound */
void sfx_load(void);                     /* loads them once (the scenes: before any Flink level) */
int  flink_sound(BYTE *src, int id);     /* Flink's sound `id` (nothing without its file) */
void sfx_loop(BYTE *a, int id);          /* a looping Flink sound, asked for each tick */
void sfx_tick(void);                     /* the end of player_tick: loops not asked for stop */
void sfx_reset(void);                    /* world_load: every loop stops */

/* music.c: music between maps, on the title and the map screen */
void __cdecl music_spawn(BYTE *a, BYTE *def);
void music_load(const char *ini);
int  music_changes(int sec, int sub);    /* going to that map (sec < 0: unknown) changes the track */
void music_play(int t);                  /* plays Music param t now (the boss's fight; 0: nothing) */
void music_play_once(int t);             /* plays it once, not looped (the boss's fanfare) */
int  music_retail(void);
void music_install(void);

/* checkpoint.c: Flink's revive points */
void __cdecl checkpoint_spawn(BYTE *a, BYTE *def);

/* monkey.c: Flink's monkeys (object 67, Lomax's rope, as their tail) */
void __cdecl rope_spawn(BYTE *a, BYTE *def);
void monkeys_reset(void);

/* generator.c: Flink's Generators (hidden objects) */
void __cdecl gene_spawn(BYTE *a, BYTE *def);

/* groundtype.c: Flink's Ground type, Lomax sinks into it ('Spik Ground', the Ground type actors) */
void groundtype_spawn(BYTE *a, BYTE *def);
/* snake.c: Flink's Snake ('Spik Snake') */
void snake_spawn(BYTE *a, BYTE *def);    /* (from spik_spawn) */
void snakes_reset(void);      /* (from spik_spawn) */
/* balls.c: Flink's deadly swinging balls ('Spik Ball') */
void ball_spawn(BYTE *a, BYTE *def);     /* (from spik_spawn) */
void balls_reset(void);
void stahl_spawn(BYTE *a, BYTE *def);    /* (from spik_spawn) */
void stahls_reset(void);
void spike_spawn(BYTE *a, BYTE *def);    /* (from spik_spawn) */
void spikes_reset(void);
/* drop.c: Flink's Drop Balls, spiked balls dropping on chains ('Spik Drop') */
void drop_spawn(BYTE *a, BYTE *def);     /* (from spik_spawn) */
void drops_reset(void);
/* butterfly.c: Flink's Butterflies, decoration flying off in pairs ('Spik Butterfly') */
void butterfly_spawn(BYTE *a, BYTE *def);  /* (from spik_spawn) */
void butterflies_reset(void);
/* lava.c: Flink's lava, the Lava column ('Spik Lava') and the lava water's touch */
void lava_spawn(BYTE *a, BYTE *def);     /* (from spik_spawn) */
void lava_reset(void);
void lava_player(const BYTE *water);     /* (water.c) the lava hurts or kills Lomax */
/* shared by the lava kinds (lava.c): Flink's droplets, hurt box, hurt and on-screen box */
#define LAVA_DROPS         4
typedef struct { int t, x, y, vx, vy; } LavaDrop;   /* timer (-1: none), 16.16 top-left and speeds, up positive */
int  lava_rand(void);
int  lava_surface(void);                 /* g_WaterSurfaceY, NO_GROUND before the water runs */
void lava_drop_add(LavaDrop *d, int n, int x, int y, int up);
void lava_drops_move(LavaDrop *d);
void lava_drops_draw(BYTE *a, const LavaDrop *d, const char *tag, int ax, int ay);
int  lava_touches(const BYTE *a, int x1, int y1, int x2, int y2);
void lava_hurt_by(int vx, int left);
int  lava_seen(const BYTE *a, int x, int y);
void lava_piece(BYTE *a, const char *tag, int frame, int x, int y, int shown);
void prot_spawn(BYTE *a, BYTE *def);     /* protuberance.c: Flink's Protuberance ('Spik Prot') */
void prot_reset(void);
void great_spawn(BYTE *a, BYTE *def);    /* greatballs.c: Flink's Great balls of fire ('Spik Great') */
void great_reset(void);
void burst_spawn(BYTE *a, BYTE *def);    /* burst.c: Flink's Burst of lava ('Spik Burst') */
void burst_reset(void);
void fountain_spawn(BYTE *a, BYTE *def); /* fountain.c: Flink's Fountain ('Spik Fountain') */
void fountain_reset(void);
void rollingstone_spawn(BYTE *a, BYTE *def);   /* rollingstone.c: Flink's Rolling stone ('Spik Rolling') */
void rollingstones_reset(void);
int  water_lava(void);                   /* water.c: the map's water is lava (Flink's 0x82EA) */
int  throwplat_floating_near(int x);     /* throwplat.c: a floating Throw platform the Fountain waits for */
int  grab_lift(const BYTE *p, int top, int x1, int x2, int reach, int fling);   /* grab.c: Flink's 0x1A1BA */

/* apple.c: Flink's Falling apples, bursting on the ground or on Lomax ('Spik Apple') */
void apple_spawn(BYTE *a, BYTE *def);    /* (from spik_spawn) */
void apples_reset(void);
int  apple_struck(BYTE *t, int x1, int y1, int x2, int y2);   /* a thrown object's box bursts it (grab.c) */
/* ropeslide.c: Flink's Rope slides ('Spik Slide') */
void ropeslide_spawn(BYTE *a, BYTE *def);      /* (from spik_spawn) */
void ropeslides_reset(void);
void ropeslide_before(void);                  /*   around Player_Update (player_tick) */
void ropeslide_after(void);
int  ropeslide_active(void);                  /*   Lomax hangs from a Rope slide's handle */
/* swing.c: Flink's pendulums, the logs (Trunk) and swinging platforms ('Swin Trunk', 'Swin Flink') */
void __cdecl swin_spawn(BYTE *a, BYTE *def);
void swings_reset(void);
/* wheel.c: Flink's wheels, the Swingplatform with arms ('Rota Flink') */
void __cdecl rota_spawn(BYTE *a, BYTE *def);
void wheels_reset(void);
void groundtype_before(void);                   /* player_tick, around Player_Update */
void groundtype_after(void);
int  groundtype_retail(void);
void groundtype_install(void);
void groundtype_reset(void);
int  groundtype_type(void);              /* Flink's g_GroundType (0: ordinary ground, 1: mud) */
void player_kill(void);                  /* Lomax dies, helmet or not */
/* dustcloud.c: Flink's landing dust instead of Lomax's grass bits and walk puffs ('Spik Dustcloud') */
int  dustcloud_retail(void);
void dustcloud_install(void);

/* scroll.c: Flink's Scroll Points, the scrolling screens and the camera's locks ('Forc Flink') */
void __cdecl force_spawn(BYTE *a, BYTE *def);
void scroll_before(void);                /* player_tick, around Player_Update */
void scroll_after(void);
void shake_after(void);
void trunks_after(void);
int boss_trunk_bounce(BYTE *o, int x1, int y1, int x2, int y2);                  /* forestboss.c: g_ScreenShake in maps one screen tall (player_tick) */
void secret_before(void);                /* levels.c: a death in a secret level leaves it (player_tick) */
void secret_after(void);
void scroll_reset(void);
int  scroll_owns_x(void);                /* a Scroll Point moves or holds the camera's x */
int  scroll_keeps_y(void);               /* a Scroll Point holds the camera's y */
int  scroll_x_moving(void);              /* the camera's x moves at a Scroll Point's speed (Flink's 0x8738 not 0) */
void scroll_wrap(void);                  /* the map wraps around across, the camera on past its edges */
void scroll_stop_x(void);                /* the camera's x stays from now on */

/* villageboss.c: Flink's Village Boss (16-2, 'Muta Village') */
void vboss_spawn(BYTE *a, int i, const BYTE *def);
void vboss_gone(BYTE *a, const BYTE *def);
int  vboss_struck(BYTE *o, int x1, int y1, int x2, int y2, int shot);   /* a thrown object's box hit a man (grab.c) */
void villageboss_reset(void);
void boss_finish_later(BYTE *a, BYTE *def, int ticks);   /* forestboss.c: the arena's Finish appears */
void boss_fighting(void);                /* forestboss.c: a boss's fight runs this tick */
int  boss_fight(void);                   /*   a boss fight runs: only HELMET */
void boss_fight_reset(void);

/* bos1.c: Flink's final boss (24-2, 'Muta Bos1'), his dragon and his Bos3 dudes ('Muta Bos3') */
int  bos1_struck(BYTE *t, BYTE *o, int x1, int y1, int x2, int y2);   /* thrown object o's box touched t (grab.c) */
void bos1_reset(void);

/* metaboss.c: Flink's Meta Boss (31-2, 'Muta MetaBoss') */
void mboss_spawn(BYTE *a, const BYTE *def);
void mboss_gone(BYTE *a, const BYTE *def);
void metaboss_reset(void);
int  mboss_struck(BYTE *t, int x1, int y1, int x2, int y2);   /* thrown object's box touched t (grab.c) */

/* spirits.c: Flink's spirits, set free by defeated enemies, and the magic glass on the HUD */
extern int g_spirits;                    /* 0..99, what Flink's spells cost (spells.c) */
BYTE *spirit_release(BYTE *src, int x, int y);   /* the spirit, or NULL (no src: on Lomax's plane) */
void spirit_trail(BYTE *d);              /* a spell's spirit's trail, from its think (spellfx.c) */
void spirit_home(BYTE *d, int life);     /* a spell's spirit becomes a defeated enemy's, homing in on Lomax */
void spirit_vanish(BYTE *d);             /* a spell's spirit plays TAIL and goes */
BYTE *spirit_sparkle(BYTE *on, int dy);  /* sparkles on an actor (Flink's 0x2ECA6) */
void spirits_hurt_after(void);          /* a hit knocking Lomax's helmet off costs 5 spirits (player_tick, after Player_Update) */
void spirits_hud_draw(BYTE *hud);        /* (powers.c, at Hud_Draw's ammo-ones call) */
/* the HUD pieces drawn in Flink's maps, each through a stand-in of its own (spirits.c's hud_piece);
   ONES2: a ones digit next to its tens */
enum { HUD_SPIRITS_TENS, HUD_SPIRITS_ONES, HUD_SPIRITS_ONES2, HUD_GLASS,
       HUD_COST_TENS, HUD_COST_ONES, HUD_COST_ONES2, HUD_SPELL, HUD_SPELL_HELMET, HUD_PIECES };
void hud_piece(const BYTE *hud, int piece, int frame, int x, int y);   /* the HUD actor's current anim and type */
int  spirits_retail(void);
void spirits_install(void);

/* spells.c: Flink's spells, chosen on the power wheel, shown on the HUD and paid with spirits */
#define SPELL_QUICK_GROW   4             /* Flink's spell numbers (g_SelectedSpell) */
#define SPELL_GHOST        6
#define SPELL_SHRINK       7
#define SPELL_DEMON        10
#define SPELL_MAGIC_KEY    11
#define SPELL_HELMET       12            /* not Flink's: Lomax's helmet (spells.c) */
int  spell_chosen(int spell);            /* it is the spell chosen on the wheel */
int  spell_can_cast(void);               /* on the ground, not hurt (Flink's spell button) */
int  spell_pay(int spell);               /* chosen and paid: 1; chosen but too dear: 0, the cost flashes */
int  spell_affordable(int spell);        /* chosen and its cost held: 1; else 0, the cost flashes */
void spell_wheel_open(void);             /* (powers.c: Select in Flink's maps) */
void spells_hud_draw(BYTE *hud, int x, int y);   /* (spirits.c: right of the magic glass) */
void spells_tick(void);                  /* (player_tick) */
int  spell_helmet_cast(void);            /* (spell_cast) HELMET chosen: 1, the press taken */
int  spell_area_cast(void);            /* Ability pressed: 1 if GHOST or SHRINK took it (in its area) */
int  spell_scale(void);                  /* Lomax's scale while he shrinks (player_draw), 0 otherwise */
void spells_load(const char *ini);       /* LEVELS.INI's [Areas] */
void spells_reset(void);

/* spellfx.c: SPIRIT BOMB, DUST DEVIL, LIGHTNING, PLATFORM, SHIELD and DEMON, cast with Ability anywhere */
extern int g_shieldCount;                /* SHIELD's spirits (g_OrbitSpiritCount) */
extern int g_demonHits;                  /* DEMON's demon's hits left, 0 none (g_DemonActive) */
int  spellfx_demon_cost(void);           /* DEMON's cost now: 8, or 6 to grow the demon (spells.c) */
int  spell_cast(void);                   /* Ability on the ground, nothing picked up: 1 if a spell took it */
void spellfx_tick(void);                 /* (spells_tick) */
void spellfx_reset(void);                /* (spells_reset: a map loads) */
void spellfx_new_game(void);             /* (levels.c: to the title) */
int  spell_strike(BYTE *src, BYTE *t, int x1, int y1, int x2, int y2);   /* grab.c: a spell's box hits t (NULL: the first one) */

/* password.c: the shortcuts in Lomax's password */
void password_sync(void);                /* the password shown follows g_shortcuts */
int  password_retail(void);
void password_install(void);

/* powers.c: no helmet powers in Flink's maps (Ability, the power wheel, the HUD's power) */
int  powers_retail(void);
void powers_install(void);
void powers_set(int flink);              /* Level_Start gives Ability a mask of 0 (Flink's maps) or the scheme's */

#endif
