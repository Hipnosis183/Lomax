/*
 * spells.c - Flink's spells: the spell wheel, the chosen spell on the HUD and its cost, GHOST and
 * SHRINK (README "Spells").
 *
 * Flink (decompilation.md §10.10, §10.11): the chosen spell is g_SelectedSpell (0x6396, -1 none),
 * picked in the pause menu from the known spells (PauseMenu_SelectSpell 0x16F7E); the HUD shows its
 * icon, MAIN.WLD's 'Available Ingredients' INGR frame g_SelectedSpell + 0x1E (0x33FFA). Each spell
 * costs magic (g_MagicPower 0x82E6; SpellHandlerTable 0x32F0A): QUICK GROW 4 (Spell4_GrowPlant
 * 0x315C2: cmpi.w #4, g_MagicPower; bgt), GHOST 8 (Spell6_KillAreaWarp 0x3227E), SHRINK 8
 * (Spell7_Shrink 0x31F1E); a spell whose cost is not below the magic held only makes the HUD flash
 * (g_NoMagicFlashTimer 0x830E = 64 frames). The MAGIC KEY (spell 11) is not cast and costs no magic:
 * with it chosen a stomp opens a locked chest, using up a key (0x2E196).
 *
 * Here the spells are chosen on Lomax's power wheel, and paid with the spirits held (spirits.c,
 * Flink's magic: a collected spirit is g_MagicPower + 1 in Flink too). SPELLS lists the ones Lomax has,
 * in the wheel's order, at Flink's costs, paid with that many or more held; the MAGIC KEY costs 10
 * (a chest's magic key gives 10 spirits, chests.c; Lomax has no keys). A spell takes effect where
 * the object it is for asks for it, with Ability: the Plant (plant.c), a locked chest (chests.c,
 * chest_unlock), GHOST's and SHRINK's areas (spell_area_cast), all through spell_pay; SPIRIT BOMB, DUST
 * DEVIL, LIGHTNING, PLATFORM, SHIELD and DEMON anywhere (spellfx.c, spell_cast); DEMON costs 8, or
 * 6 while its demon can grow (cost_of, spellfx_demon_cost). HELMET (SPELL_HELMET, 5) is not Flink's:
 * cast anywhere (spell_cast calls spell_helmet_cast), it gives Lomax what a picked-up helmet gives him
 * (Item_UpdateCoinOrHelmet 0x42C6A0: g_bHasHelmet if he has none on his head and none thrown, else
 * g_bHasSpareHelmet) with that pickup's sound (0x14); with both it does nothing and costs nothing.
 * Its icon is a frame of Lomax's MAIN.WLD 'Items' HELM (HELMET_ICON_FRAME), drawn through that type
 * and centred on the point as the SPEL icons are (icon_set). The converter's
 * 'Spik Spells' type (never placed, found by name) has the icons (SPEL, frame n = spell n's, centred)
 * and Lomax's small digits tinted red (KLEI).
 *
 * - The wheel. Player_Update calls PowerWheel_Open (0x430BA0) at 0x40FCD6 when Select is pressed;
 *   in Flink's maps powers.c calls spell_wheel_open instead, which opens it as PowerWheel_Open does
 *   (an actor at Lomax, his state | 0x80: frozen, sound 0x17, the CD paused on a level of world 5)
 *   with the 'Spik Spells' type and wheel_update / wheel_draw in place of PowerWheel_Update (0x430C90)
 *   and PowerWheel_Draw (0x430FA0), which turn Lomax's six icons. Theirs, for any number of icons:
 *   the radius (+0x80) grows by 2 a tick to 60 while the wheel turns 6 degrees a tick (+0x84), so it
 *   comes up turning half a circle; holding Left chooses the next spell and Right the one before
 *   (sound 0x41), the wheel turning by the gap between two icons in 15 ticks (+0x90 counts them);
 *   Select closes it (sound 0x18, the radius shrinking by 2 a tick as it turns on, then Lomax's state
 *   loses 0x80 and the CD resumes). The chosen spell is the icon at the top (Lomax's power p is his
 *   icon (p + 3) % 6, which the half-turn brings there). The icon at the top is drawn whole, the others
 *   translucent (alpha 0xA0), as Lomax's; Flink's icons have no black square behind them.
 * - The HUD. Right of the magic glass and its count (spirits_hud_draw calls spells_hud_draw), the
 *   chosen spell's icon and its cost in Lomax's small digits ('KLEI', as the coins' and the defeated
 *   enemies' counts), each piece through a stand-in of its own (spirits.c's hud_piece). A cost that
 *   can't be paid (spell_pay) makes the digits flicker red for FLASH_TICKS
 *   (Flink's HUD flashes 64 frames) and plays Lomax's GRAB1 (sound 14; Flink plays none).
 * - GHOST and SHRINK are cast only in their areas, Flink's Kill area (0x22C18, think 0x22C54) and Shrink
 *   area (0x22CCE, think 0x22D28), whose thinks set g_InKillArea (0x825F) / g_InShrinkArea (0x825D)
 *   each frame while the player's y is the area's and his x within 30 px of it. The converter lists
 *   them in LEVELS.INI's [Areas] (FILE=NAME,x,y): 14-1's tree (Shrink area) and 20-1's altar (Kill
 *   area). Flink's SHRINK takes Flink into TREE (world 4) and GHOST into META (world 5); here each is a
 *   shortcut (levels.c, level_shortcut): SHRINK to level 25, GHOST to level 29, once a game
 *   (g_shortcuts; with its shortcut taken the area takes no spell, and the press is a pick-up).
 *   - SHRINK: Spell7_Shrink scales Flink down over 100 steps while he can't be hurt, the music fading
 *     at step 42 and the level left below step 10. Here Lomax stands (the input locked, as at an Exit:
 *     g_bLevelEnding) and shrinks from 1000 to 100 per mille over SHRINK_TICKS, drawn through
 *     player_draw (groundtype.c, spell_scale; g_PlaneTransitionScale not 0 keeps him from being hurt
 *     and his animation as it is, as in a door), the music fading 42 ticks before the end and the
 *     screen fading out 10 before it.
 *   - GHOST: Spell6_KillAreaWarp's puff appears at cast frame -30 and the level is left on its 3rd
 *     frame. Here Lomax leaves as into a Finish trapdoor (Trapdoor_Update 0x42AD80: player state |
 *     0x43, input locked, g_bLevelEnding, vy -4.0, then 0x7000 more upward a tick, his animation
 *     Player_Update's for that state; 0x40 keeps the camera's y where it is), but with no trapdoor
 *     to clip him: he jumps up off the top of the screen, which fades out from GHOST_FADE ticks on.
 *     His hurt timer is held at -1 as the trapdoor holds it.
 * Both then leave for the world map, which leaves for the shortcut's level (level_shortcut).
 */
#include "flink.h"

#define SPELL_ICONS        12            /* the converter's SPELL_ICONS: SPEL's frames, Flink's spells 0..11 */

/* the spell, its cost, name, area (the Flink type name in [Areas]), shortcut and its level */
static const struct { int spell, cost; const char *name, *area; int shortcut, to; } SPELLS[] = {
    { SPELL_HELMET, 5, "HELMET", NULL, 0, 0 },    /* (not Flink's: spell_helmet_cast) */
    { 0, 4, "SPIRIT BOMB", NULL, 0, 0 },          /* (cast anywhere: spellfx.c) */
    { 1, 8, "DUST DEVIL", NULL, 0, 0 },           /* (spellfx.c) */
    { 2, 10, "LIGHTNING", NULL, 0, 0 },           /* (spellfx.c) */
    { SPELL_QUICK_GROW, 4, "QUICK GROW", NULL, 0, 0 },
    { SPELL_GHOST, 8, "GHOST", "Kill area", SHORTCUT_GHOST, SHORTCUT_GHOST_TO },
    { SPELL_SHRINK, 8, "SHRINK", "Shrink area", SHORTCUT_SHRINK, SHORTCUT_SHRINK_TO },
    { 8, 8, "PLATFORM", NULL, 0, 0 },             /* (spellfx.c) */
    { 9, 10, "SHIELD", NULL, 0, 0 },              /* (spellfx.c) */
    { SPELL_DEMON, 8, "DEMON", NULL, 0, 0 },      /* (spellfx.c; 6 to grow its demon: cost_of) */
    { SPELL_MAGIC_KEY, 10, "MAGIC KEY", NULL, 0, 0 },
};
#define NSPELLS            ((int)COUNT(SPELLS))

/* HELMET's icon: this frame (from 1) of the 'Items' type's HELM record (Lomax's MAIN.WLD, bank 35) */
#define HELMET_ICON_TAG    "HELM"
#define HELMET_ICON_FRAME  1
#define SND_HELMET         0x14          /* FANFARE: Item_UpdateCoinOrHelmet's, a helmet picked up */

/* the wheel: PowerWheel_Open / PowerWheel_Update's fields */
#define W_RADIUS(a)        (*(int *)((a) + 0x80))
#define W_TURN(a)          (*(int *)((a) + 0x84))  /* degrees: the open / close turn (Lomax's angle) */
#define W_CX(a)            (*(int *)((a) + 0x88))
#define W_CY(a)            (*(int *)((a) + 0x8C))
#define W_SPIN(a)          (*(int *)((a) + 0x90))  /* ticks of the turn to the chosen icon (+: Left), WHEEL_CLOSING */
#define WHEEL_RADIUS       60
#define WHEEL_STEP_TICKS   15
#define WHEEL_CLOSING      100
#define WHEEL_DIM          0xA0          /* the alpha of the icons not at the top */
#define SND_WHEEL_OPEN     0x17
#define SND_WHEEL_CLOSE    0x18
#define SND_WHEEL_TURN     0x41
#define INPUT_SELECT       0x100         /* g_InputPressed */
#define INPUT_LEFT         0x8000        /* g_InputHeld */
#define INPUT_RIGHT        0x2000
#define PS_WHEEL           0x80u         /* g_PlayerState while the wheel is open */
#define G_SINTABLE         ((const short *)0x004639C0u)   /* 1024 a turn, amplitude 0x6186 */
#define G_PCOSTABLE        ((const short * const *)0x00464400u)
#define SIN_ONE            0x6186

/* the HUD, from the ammo's ones (spirits.c): the icon's centre, then the cost's first digit */
#define ICON_X             40
#define ICON_Y             (-9)
#define COST_X             (ICON_X - 10)
#define COST_Y             0             /* (KLEI at the ammo's own y, as Hud_Draw draws it) */
#define DIGIT_STEP         5             /* (Hud_Draw's coins: 0xB3, 0xB8) */
#define FLASH_TICKS        120           /* about 2 seconds */
#define FLASH_PHASE        8             /* red and white in turn, this many ticks each */
#define SND_NO_SPIRITS     14            /* GRAB1 (Lomax's sound table at 0x461E20; Flink only flashes the HUD) */

/* GHOST's and SHRINK's areas and effects */
#define MAX_AREAS          8
#define AREA_REACH         30            /* Flink's |x - area x| < 30 (0x22CBE) */
#define SHRINK_TICKS       100           /* Flink's 100 steps */
#define SHRINK_MIN         200           /* per mille, at the end */
#define SHRINK_MUSIC       (SHRINK_TICKS - 42)   /* the music fades (Flink's step 42) */
#define SHRINK_LEAVE       (SHRINK_TICKS - 10)   /* the screen fades out (below step 10) */
#define GHOST_FADE         20            /* the screen fades out as he leaves it */
#define GHOST_VY           (-0x40000)    /* Trapdoor_Update's jump into an exit trapdoor */
#define GHOST_PULL         0x7000        /*   and its pull up, a tick */
#define PS_TRAPDOOR        0x43u         /*   and the player state it sets */

static int g_sel;                        /* the chosen spell, an index in SPELLS */
static int g_flash;                      /* ticks of the cost's red flicker */
static struct { int spell, sec, sub, x, y; } g_area[MAX_AREAS];
static int g_nareas;
static int g_effect = -1;                /* the SPELLS index of GHOST or SHRINK under way, -1: none */
static int g_effectT;                    /* its ticks */

static BYTE *spells_type(void) { return ObjType_FindByName("Spik Spells"); }

int spell_chosen(int spell) { return SPELLS[g_sel].spell == spell; }

/* spell k's cost now: DEMON's is 6 while its demon can grow (spellfx.c) */
static int cost_of(int k) { return SPELLS[k].spell == SPELL_DEMON ? spellfx_demon_cost() : SPELLS[k].cost; }

/* Lomax may cast: Flink's spell button works on the ground and not while hurt (Player_HandleButtons
   0x32E0E); here Ability, on the ground, the hurt timer run out, and in a boss fight (boss_fight,
   forestboss.c) only HELMET */
int spell_can_cast(void)
{
    return !(*G_PLAYERSTATE & PS_AIR) && *G_PLAYERHURTTIMER <= 0 && (!boss_fight() || spell_chosen(SPELL_HELMET));
}

int spell_affordable(int spell)
{
    int cost = cost_of(g_sel);
    if (!spell_chosen(spell)) return 0;
    if (g_spirits < cost) {
        if (!g_flash) flog("%s: %d spirits, can't pay %d", SPELLS[g_sel].name, g_spirits, cost);
        g_flash = FLASH_TICKS;
        Sound_PlayEffect(NULL, SND_NO_SPIRITS);
        return 0;
    }
    return 1;
}

int spell_pay(int spell)
{
    int cost = cost_of(g_sel);
    if (!spell_chosen(spell)) return 0;
    if (g_spirits < cost) {
        if (!g_flash) flog("%s: %d spirits, can't pay %d", SPELLS[g_sel].name, g_spirits, cost);
        g_flash = FLASH_TICKS;
        Sound_PlayEffect(NULL, SND_NO_SPIRITS);
        return 0;
    }
    g_spirits -= cost;
    flog("%s: paid %d spirits, %d left", SPELLS[g_sel].name, cost, g_spirits);
    return 1;
}

/* every tick (player_tick in grab.c; the HUD may be drawn more often, at Lomax+'s frame rates) */
void spells_tick(void)
{
    if (g_flash > 0) g_flash--;
    spellfx_tick();
    if (g_effect < 0 || *G_BPAUSED) return;
    *G_BLEVELENDING = *G_BINPUTLOCKED = 1;
    *G_PLAYERVX = 0;
    g_effectT++;
    if (SPELLS[g_effect].spell == SPELL_SHRINK) {
        int t = g_effectT < SHRINK_TICKS ? g_effectT : SHRINK_TICKS;
        *G_PLANETRANSITIONSCALE = 1000 - (1000 - SHRINK_MIN) * t / SHRINK_TICKS;
        if (g_effectT == SHRINK_MUSIC) *G_BFADEOUTMUSIC = 1;
        if (g_effectT == SHRINK_LEAVE) level_shortcut(SPELLS[g_effect].shortcut, SPELLS[g_effect].to);
    } else {
        *G_PLAYERVY -= GHOST_PULL;
        *G_PLAYERHURTTIMER = -1;
        *(DWORD *)(G_PLAYERACTOR + 0x78) = 0;    /* (no blink for that timer, as boss_quiet_after) */
        if (g_effectT == GHOST_FADE) level_shortcut(SPELLS[g_effect].shortcut, SPELLS[g_effect].to);
    }
}

/* a map loads: no effect (Level_Start clears g_PlaneTransitionScale and the input lock) */
void spells_reset(void) { g_effect = -1; g_effectT = 0; spellfx_reset(); }

int spell_scale(void) { return g_effect >= 0 && SPELLS[g_effect].spell == SPELL_SHRINK ? *G_PLANETRANSITIONSCALE : 0; }

/* Ability pressed (grab.c's grab_before): GHOST or SHRINK chosen, Lomax may cast, in its area on this
   map (Flink's think: his y the area's, x within 30 px), its shortcut not taken yet. Then 1: it is
   paid and starts, or the cost flashes */
int spell_area_cast(void)
{
    int i, k = g_sel, px = *G_PLAYERX, py = *G_PLAYERY;
    if (!g_active || g_effect >= 0 || !SPELLS[k].shortcut || (g_shortcuts & SPELLS[k].shortcut) || !spell_can_cast())
        return 0;
    for (i = 0; i < g_nareas; i++)
        if (g_area[i].spell == SPELLS[k].spell && g_area[i].sec == *G_LOADEDSECTION && g_area[i].sub == *G_CURSUBLEVEL &&
            abs(px - g_area[i].x) < AREA_REACH && py == g_area[i].y) break;
    if (i == g_nareas) return 0;
    if (!spell_pay(SPELLS[k].spell)) return 1;
    g_effect = k;
    g_effectT = 0;
    *G_BLEVELENDING = *G_BINPUTLOCKED = 1;
    *G_PLAYERVX = 0;
    *G_BAIRSPIN = 0;
    *G_PLAYERSPINTIMER = 0;
    if (SPELLS[k].spell == SPELL_SHRINK) {
        *G_PPLANETRANSITIONFROM = *G_PCURPLANE;     /* (not read on player_draw's path) */
        *G_PLANETRANSITIONSCALE = 1000;
        set_anim(G_PLAYERACTOR, "STAY");
    } else {                                     /* GHOST: as into a Finish trapdoor */
        *G_PLAYERSTATE |= PS_TRAPDOOR;
        *G_PLAYERVY = GHOST_VY;
        *G_PLAYERHURTTIMER = -1;
    }
    flog("%s cast at (%d,%d): level %d next", SPELLS[k].name, px, py, SPELLS[k].to);
    return 1;
}

/* LEVELS.INI's [Areas]: FILE=NAME,x,y */
void spells_load(const char *ini)
{
    static char buf[1024];
    char file[MAX_PATH], name[32], *p, *eq;
    int n = (int)GetPrivateProfileSectionA("Areas", buf, sizeof(buf), ini), x, y, k, sec, sub = 0, found;
    g_nareas = 0;
    for (p = buf; n && *p; p += strlen(p) + 1) {
        if (!(eq = strchr(p, '=')) || eq - p >= (int)sizeof(file) || sscanf(eq + 1, "%31[^,],%d,%d", name, &x, &y) != 3) {
            flog("[Areas] %s: not understood", p);
            continue;
        }
        memcpy(file, p, eq - p);
        file[eq - p] = 0;
        for (k = 0; k < NSPELLS && !(SPELLS[k].area && !_stricmp(SPELLS[k].area, name)); k++) ;
        for (found = 0, sec = 0; sec < g_nlevels && !found; sec++)
            for (sub = 0; sub < g_nsubs[sec] && !found; sub++)
                if (!_stricmp(map_name(sec, sub), file)) found = 1;
        if (k == NSPELLS || !found || g_nareas == MAX_AREAS) { flog("[Areas] %s: not used", p); continue; }
        g_area[g_nareas].spell = SPELLS[k].spell;
        g_area[g_nareas].sec = sec - 1;          /* (both loops counted on past the match) */
        g_area[g_nareas].sub = sub - 1;
        g_area[g_nareas].x = x;
        g_area[g_nareas].y = y;
        flog("%s: %s at %d-%d (%d,%d)", SPELLS[k].name, name, sec, sub, x, y);
        g_nareas++;
    }
}

/* HELMET with Ability, Lomax able to cast (spell_cast): as a helmet picked up (Item_UpdateCoinOrHelmet:
   the helmet unless he wears one or has thrown it, else the spare), paid, with the pickup's sound;
   with both, nothing (and nothing paid) */
int spell_helmet_cast(void)
{
    int worn = *G_BHASHELMET || *G_BHELMETINFLIGHT;
    if (!spell_chosen(SPELL_HELMET)) return 0;
    if (worn && *G_BHASSPAREHELMET) { flog("HELMET: Lomax has both helmets"); return 1; }
    if (!spell_pay(SPELL_HELMET)) return 1;
    if (worn) *G_BHASSPAREHELMET = 1;
    else *G_BHASHELMET = 1;
    Sound_PlayEffect(NULL, SND_HELMET);
    flog("HELMET: %s", worn ? "the spare helmet" : "a helmet");
    return 1;
}

/* ---- The icons ------------------------------------------------------------------------------------ */

/* actor a drawn as SPELLS[k]'s icon: its type, record and entry (+0x6E, which Actor_Draw reads); dx,
   dy the offset from the icon's centre to draw a at. The Flink spells': 'Spik Spells' SPEL, frame n
   spell n's, centred by the converter. HELMET's: frame HELMET_ICON_FRAME of 'Items' HELM, the box of
   its pieces centred here. 0 if the type, the record or the frame is missing */
static int icon_set(BYTE *a, int k, int *dx, int *dy)
{
    BYTE *type;
    const BYTE *e, *bank, *f;
    int n, first, x1 = 0x7FFF, y1 = 0x7FFF, x2 = -0x7FFF, y2 = -0x7FFF;
    *dx = *dy = 0;
    if (SPELLS[k].spell != SPELL_HELMET) {
        if (!(type = spells_type())) return 0;
        A_TYPE(a) = type;
        Actor_SetAnim(a, FOURCC("SPEL"));
        *(short *)(a + 0x6E) = (short)SPELLS[k].spell;   /* (SPEL: one entry a frame) */
        return 1;
    }
    if (!(type = ObjType_FindByName(S_ITEMS))) return 0;
    A_TYPE(a) = type;
    if (!Actor_SetAnim(a, FOURCC(HELMET_ICON_TAG))) return 0;
    if (HELMET_ICON_FRAME < 1 || HELMET_ICON_FRAME > *(short *)(a + 0x64)) return 0;   /* (Actor_SetAnim: frames) */
    e = A_RECORD(a) + 0x20;
    for (first = 0, n = 1; n < HELMET_ICON_FRAME && first < *(short *)(a + 0x66); first++)   /* (entries) */
        if (*(const WORD *)(e + first * 8 + 6) & 0x80) n++;
    if (first >= *(short *)(a + 0x66)) return 0;
    for (e += first * 8; ; e += 8) {             /* (Actor_Draw: a piece at x + dx, y + dy, w x h) */
        if (e[0] && e[1] && (bank = G_ANIMBANKS[e[0] | (T_BANKS(type) & 0x100)])) {
            int px = *(const short *)(e + 2), py = *(const short *)(e + 4);
            f = bank + e[1] * 0x38 + 0x10;
            if (px < x1) x1 = px;
            if (py < y1) y1 = py;
            if (px + *(const short *)(f + 0xC) > x2) x2 = px + *(const short *)(f + 0xC);
            if (py + *(const short *)(f + 0xE) > y2) y2 = py + *(const short *)(f + 0xE);
        }
        if (*(const WORD *)(e + 6) & 0x80) break;
    }
    *(short *)(a + 0x6E) = (short)first;
    if (x1 <= x2) { *dx = -(x1 + x2) / 2; *dy = -(y1 + y2) / 2; }
    return 1;
}

/* ---- The wheel ------------------------------------------------------------------------------------ */

/* spell k's place on the wheel, degrees from the top (clockwise), as PowerWheel_Update's: the turn,
   the spell's place from the chosen one, and the turn to it still to come */
static int wheel_angle(BYTE *a, int k)
{
    int t = (k - g_sel) * WHEEL_STEP_TICKS, d;   /* (in 15ths of the gap: 360 / NSPELLS need not be whole) */
    if (W_SPIN(a) != WHEEL_CLOSING) t += W_SPIN(a);
    d = (W_TURN(a) + t * 360 / (NSPELLS * WHEEL_STEP_TICKS)) % 360;
    return d < 0 ? d + 360 : d;
}

/* places the actor at spell k's icon; 1 if it is the one at the top (PowerWheel_Update's test) */
static int wheel_place(BYTE *a, int k)
{
    int i = wheel_angle(a, k) * 0x6F1C / 10000;
    A_X(a) = W_CX(a) + G_SINTABLE[i] * W_RADIUS(a) / SIN_ONE;
    A_Y(a) = W_CY(a) + (*G_PCOSTABLE)[i] * W_RADIUS(a) / -SIN_ONE;
    return W_RADIUS(a) == WHEEL_RADIUS && (i < 10 || i > 0x3F2);
}

static void __cdecl wheel_update(BYTE *a)
{
    if (!*G_BINPUTLOCKED && *G_INPUTPRESSED & INPUT_SELECT && W_SPIN(a) != WHEEL_CLOSING) {
        W_TURN(a) += W_SPIN(a) * 360 / (NSPELLS * WHEEL_STEP_TICKS);   /* (a turn under way stops where it is) */
        W_SPIN(a) = WHEEL_CLOSING;
        W_RADIUS(a) -= 2;
        Sound_PlayEffect(NULL, SND_WHEEL_CLOSE);
    }
    if (*G_BPAUSED) return;
    if (W_RADIUS(a) < WHEEL_RADIUS) {            /* opening or closing: turning 6 degrees a tick */
        if (W_SPIN(a) != WHEEL_CLOSING) W_RADIUS(a) += 2;
        else if ((W_RADIUS(a) -= 2) <= 0) {
            BYTE *p = *G_PRIDINGPLATFORM;
            int ps = p ? slot_of(p) : -1, ws = slot_of(a);
            if (level_wld_index(*G_LOADEDSECTION) == 5) CD_Resume();
            Actor_Despawn(a);
            *G_PLAYERSTATE &= ~PS_WHEEL;
            /* the platform Lomax stands on, in a slot before the wheel's, was left out of this tick's
               actor updates (still frozen); without its carry (Platform_MoveAndCarryPlayer) this
               tick's Player_Update drops him onto the map's ground under it: it is updated now */
            if (ps >= 0 && ps < ws && actor_active(ps) && A_UPDATE(p)) ((ActorFn)A_UPDATE(p))(p);
            flog("spell wheel closed: %s", SPELLS[g_sel].name);
            return;
        }
        W_TURN(a) = (W_TURN(a) + 6) % 360;
        return;
    }
    if (!W_SPIN(a) && !*G_BINPUTLOCKED && *G_INPUTHELD & (INPUT_LEFT | INPUT_RIGHT)) {
        if (*G_INPUTHELD & INPUT_LEFT) { g_sel = (g_sel + 1) % NSPELLS; W_SPIN(a) = WHEEL_STEP_TICKS; }
        else { g_sel = (g_sel + NSPELLS - 1) % NSPELLS; W_SPIN(a) = -WHEEL_STEP_TICKS; }
        Sound_PlayEffect(NULL, SND_WHEEL_TURN);
    }
    if (W_SPIN(a) > 0) W_SPIN(a)--;              /* (from the press's own tick, as Lomax's) */
    else if (W_SPIN(a) < 0) W_SPIN(a)++;
}

static void __cdecl wheel_draw(BYTE *a)
{
    BYTE *type = A_TYPE(a);
    int k, dx, dy;
    for (k = 0; k < NSPELLS; k++) {
        int top = wheel_place(a, k);
        if (!icon_set(a, k, &dx, &dy)) continue;
        A_X(a) += dx;
        A_Y(a) += dy;
        Actor_Draw(a, 1000, 0, top ? 0 : WHEEL_DIM);
    }
    A_TYPE(a) = type;
    Actor_SetAnim(a, FOURCC("SPEL"));
}

/* Select in Flink's maps (powers.c): PowerWheel_Open with Flink's spells */
void spell_wheel_open(void)
{
    BYTE *type = spells_type(), *a;
    if (g_effect >= 0 || !type || !(a = Actor_Alloc(NULL, "Spik Spells"))) return;   /* (in a boss fight too: HELMET) */
    A_X(a) = *G_PLAYERX;
    A_Y(a) = *G_PLAYERY;
    if (*(short *)(*G_PCURPLANE + 0x42) == 0x2EE) A_Y(a) -= 16;
    A_LAYER(a) = -1;
    W_RADIUS(a) = 0;
    W_TURN(a) = 180;                             /* the half-turn while it opens brings it to 0 */
    W_CX(a) = A_X(a);
    W_CY(a) = A_Y(a) - 20;
    W_SPIN(a) = 0;
    *G_PLAYERSTATE |= PS_WHEEL;
    Sound_PlayEffect(NULL, SND_WHEEL_OPEN);
    if (level_wld_index(*G_LOADEDSECTION) == 5) CD_Pause();
    A_FLAGS(a) |= 0x4000000;                     /* updated while Lomax is frozen */
    Actor_SetAnim(a, FOURCC("SPEL"));
    A_DRAW(a) = (void *)wheel_draw;
    A_UPDATE(a) = (void *)wheel_update;
    flog("spell wheel open: %s", SPELLS[g_sel].name);
}

/* ---- The HUD -------------------------------------------------------------------------------------- */

/* (spirits.c, from Hud_Draw's ammo-ones call: the HUD actor a stands at the ammo's ones, x, y) */
void spells_hud_draw(BYTE *a, int x, int y)
{
    BYTE *type = A_TYPE(a), *spells = spells_type();
    int cost = cost_of(g_sel), red, dx, dy;
    if (!spells) return;
    red = g_flash && (g_flash / FLASH_PHASE & 1) == 0;
    if (red) A_TYPE(a) = spells;                 /* the red KLEI */
    Actor_SetAnim(a, FOURCC("KLEI"));
    if (cost >= 10) hud_piece(a, HUD_COST_TENS, cost / 10 % 10, x + COST_X, y + COST_Y);
    hud_piece(a, cost >= 10 ? HUD_COST_ONES2 : HUD_COST_ONES, cost % 10, x + COST_X + (cost >= 10 ? DIGIT_STEP : 0), y + COST_Y);
    /* HELMET's icon (another type, centred by its own offset) has a stand-in of its own: through
       HUD_SPELL its anchor would move by that offset when the choice changes, a slide for a tick */
    if (icon_set(a, g_sel, &dx, &dy))
        hud_piece(a, SPELLS[g_sel].spell == SPELL_HELMET ? HUD_SPELL_HELMET : HUD_SPELL, *(short *)(a + 0x6E), x + ICON_X + dx, y + ICON_Y + dy);
    A_TYPE(a) = type;
}
