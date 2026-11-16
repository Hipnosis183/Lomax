/*
 * deco.c - Flink's objects on Lomax's decoration, 'Spik' (object 51, Deco_Spawn 0x427060, whose spawn
 * is wrapped): 'Spik Deadly', Flink's spikes, and 'Spik Tree', Flink's Trees (README "Trees").
 * 'Spik Stone', Flink's stones, is grab.c's (stone_spawn); 'Spik Ground', Flink's Ground type, groundtype.c's;
 * 'Spik Snake', Flink's Snake, snake.c's; 'Spik Ball', Flink's deadly swinging balls, balls.c's;
 * 'Spik Stahl', Flink's Stahlkugeln, stahlkugel.c's; 'Spik Spike', Flink's Spikes, spike.c's;
 * 'Spik Drop', Flink's Drop Balls, drop.c's; 'Spik Slide', Flink's Rope slides, ropeslide.c's;
 * 'Spik Butterfly', Flink's Butterflies, butterfly.c's; 'Spik Lava', Flink's Lava column, lava.c's; 'Spik Prot',
 * 'Spik Great', 'Spik Burst' and 'Spik Fountain', Flink's other lava kinds, protuberance.c's,
 * greatballs.c's, burst.c's and fountain.c's; 'Spik Rolling', Flink's Rolling stone, rollingstone.c's.
 */
#include "flink.h"

/* Flink's Tree columns, per actor slot (see tree_draw): their type, x offset, speed num / den */
static struct { BYTE *type; int xoff, num, den; } g_tree[MAX_ACTORS];

void deco_reset(void) { memset(g_tree, 0, sizeof(g_tree)); }

/* Flink's Deadly ground: hurts Lomax while he is within +-param px (+0x80) of it and up to 8 px above
   / 24 below its line (Player_TakeHit pushes him away from the x it is given: the nearer end). */
static void __cdecl deadly_update(BYTE *a)
{
    int x = A_X(a), y = A_Y(a), hw = *(int *)(a + 0x80), px = *G_PLAYERX, py = *G_PLAYERY;
    if (A_PLANE(a) == *G_PLAYERPLANE && abs(px - x) < hw + 8 && py >= y - 8 && py <= y + 24)
    {
        flog("deadly ground at (%d,%d) +-%d hurts Lomax at (%d,%d)", x, y, hw, px, py);
        Player_TakeHit(px < x ? px + 1 : px - 1, 0, 0);
    }
}

/* Flink's Tree (0x30B22, draw hook 0x30BC2): a column of its sprite down the screen at screen x
   ((0x25F - P) & 0x1FF) - 0x80, P = (camera x * num / den + x offset) & 0x1FF, one column every 512 px,
   and y -(camera y * num / den & 31) + 32 k (see the converter's 'Spik Tree'). Drawn in screen space
   (flag 0x40), after the cameras are set for the frame (ParallaxPlane_ApplyCamera runs first).
   The record is one sprite, drawn here once per piece: TREE_COLS columns 512 px apart and TREE_ROWS
   rows 32 px apart, each piece moving with the camera without wrapping and taken round to the other
   side of its range (x over 1024 px from -256, y over 32 * TREE_ROWS px from -64) only while it is
   off the screen (up to 512 x 240). The picture is Flink's. Why: a host that draws frames between
   ticks moving every sprite part of the way (Lomax+'s FPS Override, by each Actor_Draw call's anchor)
   takes a wrap of the whole column (31 px up or down, 511 px across) for a slide the wrong way,
   and a wrap too big to interpolate (Lomax+'s limit is 48 px a tick) leaves the column standing still
   for a tick while the screen moves: either way the Trees would flicker as the camera moves. */
#define TREE_COLS 2
#define TREE_ROWS 11
static int tree_mod(int v, int m) { v %= m; return v < 0 ? v + m : v; }

static void __cdecl tree_draw(BYTE *a)
{
    int i = slot_of(a), u, v, c, j;
    const BYTE *cur = *G_PCURPLANE;
    if (i < 0 || g_tree[i].type != A_TYPE(a) || !cur) return;
    u = P_CAMX(cur) * g_tree[i].num / g_tree[i].den + g_tree[i].xoff;     /* P before the & 0x1FF */
    v = P_CAMY(cur) * g_tree[i].num / g_tree[i].den;
    for (c = 0; c < TREE_COLS; c++)
        for (j = 0; j < TREE_ROWS; j++) {
            A_X(a) = tree_mod(0x1DF - u + 512 * c + 256, 512 * TREE_COLS) - 256;
            A_Y(a) = tree_mod(32 * j - v + 64, 32 * TREE_ROWS) - 64;
            draw_actor(a);
        }
}

static void __cdecl tree_update(BYTE *a) { (void)a; }

/* the converter repeats the Tree's placement every screen (Lomax spawns objects only as they scroll
   in): the first to spawn stays for good (never despawned), the others go. Params: x offset, speed
   num / den. */
static void tree_spawn(BYTE *a, const BYTE *def)
{
    BYTE *type = A_TYPE(a);
    int i = slot_of(a), k;
    for (k = 0; k < MAX_ACTORS; k++)
        if (k != i && g_tree[k].type == type) { Actor_Kill(a); return; }
    if (i < 0 || T_NPARAMS(type) < 3) return;
    g_tree[i].type = type;
    g_tree[i].xoff = D_PARAM(def, 0);
    g_tree[i].num = D_PARAM(def, 1);
    g_tree[i].den = D_PARAM(def, 2) ? D_PARAM(def, 2) : 1;
    A_FLAGS(a) = (A_FLAGS(a) & ~(0x200000u | AF_HURTS)) | AF_SCREEN;   /* harmless, screen space */
    A_CULL(a) = CULL_NEVER;
    A_UPDATE(a) = (void *)tree_update;
    A_DRAW(a) = (void *)tree_draw;
    flog("tree column: x offset %d, speed %d/%d, plane %d", g_tree[i].xoff, g_tree[i].num, g_tree[i].den, A_PLANE(a));
}

void __cdecl spik_spawn(BYTE *a, BYTE *def)
{
    Deco_Spawn(a, def);
    if (g_title && is_type(a, "Spik Logo")) { logo_spawn(a); return; }   /* Flink's title (title.c) */
    if (g_scene >= 0 && is_type(a, "Spik Scene")) { scene_spawn(a); return; }   /* intro, ending (scenes.c) */
    if (g_active && is_type(a, "Spik Deadly")) {
        *(int *)(a + 0x80) = D_PARAM(def, 0);
        A_UPDATE(a) = (void *)deadly_update;
    } else if (g_active && def && is_type(a, "Spik Tree"))
        tree_spawn(a, def);
    else if (g_active && is_type(a, "Spik Stone"))
        stone_spawn(a, def);
    else if (g_active && is_type(a, "Spik Ground"))
        groundtype_spawn(a, def);
    else if (g_active && is_type(a, "Spik Snake"))
        snake_spawn(a, def);
    else if (g_active && is_type(a, "Spik Ball"))
        ball_spawn(a, def);
    else if (g_active && is_type(a, "Spik Stahl"))
        stahl_spawn(a, def);
    else if (g_active && is_type(a, "Spik Spike"))
        spike_spawn(a, def);
    else if (g_active && is_type(a, "Spik Drop"))
        drop_spawn(a, def);
    else if (g_active && is_type(a, "Spik Apple"))
        apple_spawn(a, def);
    else if (g_active && is_type(a, "Spik Appl"))      /* (after "Spik Apple", which it prefixes) */
        bigapple_spawn(a, def, 0);
    else if (g_active && is_type(a, "Spik App2"))
        bigapple_spawn(a, def, 1);
    else if (g_active && is_type(a, "Spik Butterfly"))
        butterfly_spawn(a, def);
    else if (g_active && is_type(a, "Spik Slide"))
        ropeslide_spawn(a, def);
    else if (g_active && is_type(a, "Spik Lava"))
        lava_spawn(a, def);
    else if (g_active && is_type(a, "Spik Prot"))
        prot_spawn(a, def);
    else if (g_active && is_type(a, "Spik Great"))
        great_spawn(a, def);
    else if (g_active && is_type(a, "Spik Burst"))
        burst_spawn(a, def);
    else if (g_active && is_type(a, "Spik Fountain"))
        fountain_spawn(a, def);
    else if (g_active && is_type(a, "Spik Rolling"))
        rollingstone_spawn(a, def);
}
