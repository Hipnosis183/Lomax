/*
 * gone.c - opened chests and dead enemies stay gone (README "Gone for good").
 *
 * Flink forgets the GNMY entry of an opened chest or a dead enemy (it clears its bit in the saved
 * pending bits 0x6480: 0x1B13A, which Enemy_Die 0x1B12E and the chest call; items taken too,
 * Item_Spawn 0x2C21A), so it never spawns again: not when it scrolls back in, not after a lost life
 * (the level restarts from the saved bits) and not when the map is entered again through a door
 * (3-1's secret room 3-4 has 1UPs that could otherwise be taken again and again). Here the placement
 * remembers it (+0x14 PLACEMENT_GONE; bit 0 is Lomax's own, bit 15 "spawned"), which lasts until the
 * map is reloaded, and so does g_gone, which lasts until the level is entered afresh from the map
 * screen (gone_reset, from world_load) or a continue is taken (levels.c's init_planes:
 * the continue restarts the level without a reload, and Level_InitPlanes(0) 0x40EC40 zeroes
 * the placements' states on every restart, lost lives too, so g_gone is what keeps them gone). Such an object spawns hidden and its first update removes it
 * (gone_update: Actor_Kill, which leaves the placement marked spawned, +0x15 bit 7, so neither the
 * camera nor a Generator spawns it again in that map load).
 *
 * A new kind of object that can be gone for good calls forget_placement when it goes, and in its
 * spawn placement_gone and, if so, spawn_gone (as enemy_spawn and chest_spawn do).
 */
#include "flink.h"

#define PLACEMENT_GONE     2
#define MAX_GONE           256

/* placements gone for good in this level visit, by sub-level and placement x, y */
static struct { int sub; short x, y; } g_gone[MAX_GONE];
static int g_ngone;

void gone_reset(void) { g_ngone = 0; }

void forget_placement(BYTE *def)
{
    if (!def) return;
    D_STATE(def) |= PLACEMENT_GONE;
    if (g_ngone < MAX_GONE) {
        g_gone[g_ngone].sub = *G_CURSUBLEVEL;
        g_gone[g_ngone].x = D_X(def);
        g_gone[g_ngone++].y = D_Y(def);
    }
}

int placement_gone(const BYTE *def)
{
    int k;
    if (!def) return 0;
    if (D_STATE(def) & PLACEMENT_GONE) return 1;
    for (k = 0; k < g_ngone; k++)
        if (g_gone[k].sub == *G_CURSUBLEVEL && g_gone[k].x == D_X(def) && g_gone[k].y == D_Y(def)) return 1;
    return 0;
}

static void __cdecl gone_update(BYTE *a) { Actor_Kill(a); }

/* (after its own spawn function) hidden, removed at once */
void spawn_gone(BYTE *a)
{
    A_UPDATE(a) = (void *)gone_update;
    A_FLAGS(a) = (A_FLAGS(a) | AF_HIDDEN) & ~AF_CONTACT;   /* unseen, neither hurts nor is hit */
    flog("%s at (%d,%d) is gone for good", T_NAME(A_TYPE(a)), A_X(a), A_Y(a));
}
