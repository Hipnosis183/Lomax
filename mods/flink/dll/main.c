/*
 * FLINK.DLL - the levels of The Misadventures of Flink (Mega-CD, 1994) in Lomax (Psygnosis, 1997).
 *
 * A mod for the retail LOMAX.EXE, independent of Lomax+: it uses no Lomax+ code or hooks, only
 * patches it makes itself when the DLL is loaded (DllMain). Anything that loads it into the game
 * before the game's code runs will do; the current host is Lomax+, which loads every DLL in the game's
 * MODS folder.
 *
 * The mod is the folder MODS\FLINK in the game's folder (Lomax+ loads MODS\<name>\<name>.DLL):
 * FLINK.DLL, the converted levels (LEVELS_FOLDER, WORLDS: LEVELS.INI and the world files it lists,
 * written by FLINK.EXE) and Flink's music (MUSIC, which Lomax+ plays after the game's own
 * MUSIC folder). It has no settings file: the water's behaviour is SWIM_MODE
 * (flink.h), Lomax's movement move.c's defines, and FLINK.LOG next to the DLL is written while the
 * DWORD HKCU\Software\Psygnosis\Lomax\Extra\Log is not 0 (log_init, util.c): Lomax+'s own log
 * switch, read from the registry only, so the mod still needs nothing from the loader (no value:
 * no log).
 *
 * The files (each one's header comment explains its patches):
 *   game.h         what the mod knows of LOMAX.EXE: addresses, typed functions, actor layout
 *   flink.h        what the files share
 *   main.c         DllMain: checks every patch site, then patches (this file)
 *   util.c         FLINK.LOG and the patch helpers
 *   levels.c       the level table, LEVELS.INI, loading Flink's maps, sound depths
 *   exits.c        Exits, doors and arrivals (objects 0 and 2)
 *   music.c        music between maps, title and map music (object 37), a track played once
 *   sfx.c          Flink's sound effects (SFX), played by Flink's sound ID
 *   checkpoint.c   Flink's revive points (object 3)
 *   gone.c         opened chests and dead enemies stay gone
 *   water.c        the tide and swimming (the Water object's update)
 *   move.c         Lomax's walk / run speed, jump height and walking under water (g_PlayerMoveTable)
 *   stomp.c        the high bounce off an enemy's head with Jump held, and no air spin into it
 *   platforms.c    floating and dipping platforms (objects 64 and 65)
 *   jumppad.c       Flink's Jumppads and Bouncy platforms ('Mega Jumppad', 'Mega Bouncy')
 *   deco.c         deadly ground and the Trees (object 51)
 *   enemy.c        Flink's enemies (object 74): spawn, walking, dying; enemy.h
 *   dummy.c        the Forest Dummy
 *   city.c         the City dummy (VILLAGE): stands, looks back, charges with its lance
 *   mountain.c     the Mountain dummy (MOUNTAIN): walks, stands, looks back; its shield turns shots back
 *   intree.c       the Intree dummy (TREE): walks, jumps off ledges and on a timer, glides down
 *   worm.c         the Worm (TREE): four segments inching along, each turning at a ledge
 *   yeti.c         the Yeti (MOUNTAIN's Yeti): sleeps, wakes, spins at Lomax, tires
 *   buzzard.c         the Buzzard (MOUNTAIN): perched, flies down at Lomax in an arc, glides away
 *   razorback.c         the Razorbacks' stampede (MOUNTAIN's Razorback, 21-1): a spawner, the Razorbacks, the shake
 *   cyclindude.c      the Cyclin' Dude (VILLAGE), his Chopper Bike that Lomax rides, the Little Dude
 *   blitz.c        the Blitz dude (META): follows Lomax on his cloud and strikes where he stood; his
 *                  cloud, let go, rises as a platform
 *   circ.c         the Circling suckers (META): a ring of heads turning round and closing in
 *   wolky.c        the Wolky platform (META): a cloud rising off the top, its Generator bringing it back
 *   firemonster.c  the Firemonster
 *   holder.c       the Ingredient Holder
 *   thief.c        the Thief
 *   visje.c         the Visje
 *   statue.c       the Statue (the stone head that shoots fireballs)
 *   wurm.c         the Raketen wurm: out of its mud toward Lomax, back when stomped
 *   puge.c         the Puge: its mouth opens and drops rocks (5-3, 9-2)
 *   forestboss.c         the Forest's boss (8-2): its fight and the Finish after it
 *   villageboss.c      the Village Boss (16-2): the walking machine with three men, its chains and stones
 *   donkey.c       the Donkey monkey (28-1): TREE's boss, jumping and shaking the apples down
 *   metaboss.c     the Meta Boss (31-2): META's cloud monster, its lightning, clap and pounds, the
 *                  clouds rising to its head
 *   spirits.c      Flink's spirits (from defeated enemies), their count and the HUD's magic glass
 *   spells.c       Flink's spells: the spell wheel (Select), the chosen spell and its cost on the HUD,
 *                  GHOST and SHRINK in their areas
 *   password.c     the shortcuts taken (GHOST, SHRINK) in Lomax's password
 *   contact.c      Lomax's stomp and touch tell the actor (Player_Update's contact loop)
 *   chests.c       Flink's chests (object 85)
 *   hints.c        Flink's information notes in Lomax's hint box
 *   generator.c    Flink's Generators, the hidden objects (object 86)
 *   monkey.c       Flink's monkeys, whose tail is Lomax's rope (object 67)
 *   powers.c       no helmet powers in Flink's maps: Ability, the power wheel, the HUD's power
 *   grab.c         picking up, carrying and throwing with Ability (Game_Frame's call of Player_Update)
 *   throwplat.c         Flink's Throw platform: carried, thrown, floating and drifting ('Mega Throw')
 *   dustcloud.c    Flink's landing dust, no grass bits or walk puffs ('Spik Dustcloud')
 *   groundtype.c          Flink's Ground type: Lomax sinks into it and dies ('Spik Ground'; the player's draw call)
 *   snake.c        Flink's Snake, out of the background at Lomax ('Spik Snake')
 *   balls.c        Flink's deadly swinging balls, spike wheels and pendulums ('Spik Ball')
 *   stahlkugel.c        Flink's Stahlkugeln on chains, swinging toward the screen or turning ('Spik Stahl')
 *   wall.c         Flink's Wall platforms, sliding in and out of the wall on timers ('Mega Wall')
 *   bridge.c       Flink's rope bridges: planks that fall after a step ('Mega Bridge')
 *   rock.c         Flink's Rock: the block of stones Lomax pushes, falling off ledges ('Mega Rock')
 *   spike.c        Flink's Spikes, shooting out of the ground on a timer ('Spik Spike')
 *   drop.c         Flink's Drop Balls, spiked balls dropping on chains on a timer ('Spik Drop')
 *   apple.c        Flink's Falling apples, bursting on the ground or on Lomax ('Spik Apple')
 *   butterfly.c    Flink's Butterflies, decoration flying off in pairs ('Spik Butterfly')
 *   lava.c         Flink's lava: the Lava column rising out of it ('Spik Lava') and the lava's touch
 *   protuberance.c Flink's Protuberance, a stream of fireballs in an arc ('Spik Prot')
 *   greatballs.c   Flink's Great balls of fire, thrown up out of the lava ('Spik Great')
 *   burst.c        Flink's Burst of lava, a column shooting up out of it ('Spik Burst')
 *   fountain.c     Flink's Fountain, a geyser that throws a Throw platform up ('Spik Fountain')
 *   rollingstone.c Flink's Rolling stone, a stone ball rolled by walking on it ('Spik Rolling')
 *   ropeslide.c      Flink's Rope slides, Rope slides Lomax hangs from down a cable ('Spik Slide')
 *   swing.c        Flink's pendulums: the logs (Trunk, two chains) and swinging platforms, with
 *                  their bounce (object 46, 'Swin Trunk', 'Swin Flink')
 *   wheel.c        Flink's wheels (Swingplatform with arms) as Flink's think: the speed, the
 *                  pre-run, META's stretch and Flink's chains (object 21, 'Rota Flink')
 *   leaves.c       Flink's Leaveplatform: leaves that fall swaying when stood on, and the spawner
 *                  dropping them ('Mega Leaf', 'Mega Leaves')
 *   floating.c     Flink's Floating platform: the current of leaves blowing across ('Mega Floating')
 *   steer.c        Flink's Steer platform, ridden and steered down ('Mega Steer')
 *   hand.c         Flink's Hand: a platform closing into a fist on a timer ('Mega Hand')
 *   title.c        Flink's title screen: its logo falls with Lomax hanging from it; no attract demos
 *   scroll.c       Flink's Scroll Points: the scrolling screens, through Lomax's ForceScrolling
 *                  (object 8), and the camera's locks ('Forc Flink')
 *
 * Every patch is made only if every patch site still holds the retail bytes (flink_init). The
 * object spawns below wrap Lomax's spawn functions: each calls the original, and changes the actor
 * only in Flink's levels (g_active) and for the converter's types.
 */
#include "flink.h"

/* g_ObjectSpawnFuncs entries: the object, its retail spawn function, the wrapper */
static const struct { int obj; SpawnFn retail, hook; } SPAWNS[] = {
    { OBJ_START,      Trapdoor_SpawnA,          trapdoor_spawn },
    { OBJ_EXIT,       LevelExit_Spawn,          exit_spawn },
    { OBJ_FORCE,      ForceScroll_Init,         force_spawn },
    { OBJ_CHECKPOINT, Checkpoint_Spawn,         checkpoint_spawn },
    { OBJ_MUSIC,      Musi_MusicTrigger_Init,   music_spawn },
    { OBJ_SPIK,       Deco_Spawn,               spik_spawn },
    { OBJ_MEGA,       MovingPlatform_Spawn,     mega_spawn },
    { OBJ_SWIN,       Swin_Swing_Init,          swin_spawn },
    { OBJ_ROTA,       Rota_RotatingPlatforms_Init, rota_spawn },
    { OBJ_ROPE,       Rope_Spawn,               rope_spawn },
    { OBJ_BOAT,       DriftingBoat_Init,        boat_spawn },
    { OBJ_MUTA,       Enemy_ZombieRunner_Spawn, enemy_spawn },
    { OBJ_POTS,       ClayPot_Spawn,            pots_spawn },
    { OBJ_GENE,       Trigger_Spawn,            gene_spawn },
    { OBJ_HERO,       Litt_MapHeroMarker_Init,  hero_spawn },
    { OBJ_MAPCTL,     Map_WorldMap_Init,        mapctl_spawn },
    { OBJ_TITLECTL,   TitleScreen_Init,         titlectl_spawn },
};

/* every patch site holds the retail bytes */
static int retail(void)
{
    size_t i;
    for (i = 0; i < COUNT(SPAWNS); i++)
        if (((SpawnFn *)SPAWN_TABLE)[SPAWNS[i].obj] != SPAWNS[i].retail) { flog("spawn function %d", SPAWNS[i].obj); return 0; }
    if (!levels_retail()) return 0;
    if (!water_retail()) { flog("water"); return 0; }
    if (!move_retail()) { flog("move"); return 0; }
    if (!chests_retail()) { flog("chests"); return 0; }
    if (!hints_retail()) { flog("hints"); return 0; }
    if (!music_retail()) { flog("music"); return 0; }
    if (!contact_retail()) { flog("contact"); return 0; }
    if (!platforms_retail()) { flog("platforms"); return 0; }
    if (!powers_retail()) { flog("powers"); return 0; }
    if (!grab_retail()) { flog("grab"); return 0; }
    if (!groundtype_retail()) { flog("mud"); return 0; }
    if (!spirits_retail()) { flog("spirits"); return 0; }
    if (!password_retail()) { flog("password"); return 0; }
    if (!sfx_retail()) { flog("sound table"); return 0; }
    if (!dustcloud_retail()) { flog("landing dust"); return 0; }
    return 1;
}

static void flink_init(HINSTANCE self)
{
    char path[MAX_PATH], *p;
    size_t i;

    GetModuleFileNameA(self, g_dir, MAX_PATH);
    if ((p = strrchr(g_dir, '\\'))) *p = 0;
    log_init();
    if (g_log) { snprintf(path, sizeof(path), "%s\\FLINK.LOG", g_dir); DeleteFileA(path); }
    move_init();

    if (!retail()) { flog("not the retail LOMAX.EXE (or already patched): nothing done"); return; }
    if (!levels_load()) return;

    levels_install();
    for (i = 0; i < COUNT(SPAWNS); i++) patch_dword(SPAWN_TABLE + SPAWNS[i].obj * 4, (DWORD)SPAWNS[i].hook);
    water_install();
    hints_install();
    contact_install();
    music_install();
    powers_install();
    grab_install();
    groundtype_install();
    spirits_install();
    password_install();
    dustcloud_install();
    flog("%d Flink levels from %s\\%s replace Lomax's", g_nlevels, g_dir, LEVELS_FOLDER);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        flink_init(inst);
    }
    return TRUE;
}
