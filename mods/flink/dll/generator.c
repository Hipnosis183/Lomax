/*
 * generator.c - Flink's Generators: hidden objects that appear when Lomax gets somewhere (README
 * "Hidden objects").
 *
 * Flink's Generator (0x2DD54) spawns a chain of linked GNMY entries, usually ones never spawned by
 * proximity, when Flink's box touches x +- 16, y - height .. y + 16. Lomax's placements have the
 * same links (flags 0x2000 never spawned by proximity, 0x8000 linked: placement +4 is the next one)
 * and Lomax's trigger, 'Gene' (object 86), spawns such a chain: Trigger_Spawn (0x42D0D0) keeps the
 * link in +0x80, the height in +0x84 (param 0, default 64) and "re-armable" in +0x88; Trigger_Update
 * (0x42D160) fires when Lomax's hitbox holds its x and reaches y - height .. y, spawns every placement
 * of the chain not already spawned (placement +0x15 bit 7; Object_TrySpawn sets it, Actor_Kill keeps
 * it) and, for a chain of two or more, makes them one group: an Actor_Alloc'd actor whose update,
 * TriggerGroup_UpdateDespawn (0x42D2B0), despawns them all (and itself) once all of them are off
 * screen (+0x80 their highest cull phase, which picks the margin, +0x84 the count, +0x88 the
 * actors, each set to cull phase 3, never culled by itself). Then it is removed (Actor_Kill).
 * 'Gene Flink' (the converter's, params height and mode) gets gene_update instead:
 * - Flink's box: x +- 16, y - height .. y + 16, against Lomax's hitbox.
 * - Every spawned object of the chain goes into a group (even a single one), unless mode has
 *   GENE_NO_GROUP (Flink's 'no scr check': they despawn one by one). The group drops members whose
 *   slot no longer holds them (killed, or the slot reused) before TriggerGroup_UpdateDespawn runs,
 *   so it never despawns another actor, nor lets a killed one spawn again.
 * - Fired, it stays (GENE_REARM), or with GENE_ONCE (Flink's level flag, param 1) it is removed
 *   (Actor_Kill). The group has no placement, so it leaves the Gene's alone. Flink's Generator
 *   despawns after firing, but Flink's Spawn_NearCamera (0x32B6C) spawns every pending entry inside
 *   the camera's window each frame, on screen too, so it is back at once and fires again as soon as
 *   its objects are not out; its init (0x2DD54) waits 16 frames first. Lomax spawns an object only as
 *   it scrolls in from off screen (Objects_SpawnNearCamera 0x412450), so a Gene that despawned on
 *   screen would stay away: at width 320 4-2's Trunks (x 1404, 1598, the Generator at 1207) would be
 *   despawned by their group with Lomax back at the liana (x 1066), the Generator in view, and never
 *   come back. Hidden (flag 4), the Gene isn't culled by Actors_DrawAndCull, so it despawns itself
 *   once it is GENE_NEAR px off the view, past Lomax's spawn band (80 px), and comes back by
 *   proximity as it scrolls in again.
 * Flink also despawns an object spawned on screen at its first draw, unless the Generator has its
 * 'spec. generator' box; here they always appear (the view is wider than Flink's 320 px, and an
 * object that never appears would be worse than one that pops in).
 */
#include "flink.h"

#define GENE_ONCE          1
#define GENE_NO_GROUP      2
#define GENE_REARM         16            /* ticks before a fired Gene can fire again (Flink's init wait) */
#define GENE_NEAR          160           /* px off the view at which a Gene despawns itself: past where
                                            its placement (spawn phase 0) spawns, 80 px (110 on a
                                            rotating plane; table 0x464540), so it never flickers */
#define GENE_WAIT(a)       (*(int *)((BYTE *)(a) + 0x8C))
#define FLINK_VIEW_W       320
#define VIEW_H             240
#define GROUP_MAX          16            /* actors at +0x88, their placements at +0xC8 (the actor is 0x120 bytes) */
#define GROUP_ACTORS(g)    ((BYTE **)((g) + 0x88))
#define GROUP_DEFS(g)      ((BYTE **)((g) + 0x88 + GROUP_MAX * 4))

/* The group's window is at least Lomax's cull phase 1 (x margin 160 px, table 0x464540). Flink's
   watcher keeps the group while its x span is inside the largest of the members' activation windows,
   and the Swingplatform (init 0x2B414) and the Drop Ball (0x27B38) have the mid one (8: camera - 128
   .. + 448). Their Lomax objects have phase 0 (80 px): at width 320 16-1's last two wheels (x 1049,
   1174, the Generator at 827) and 10-3's Generator-spawned Drop Ball (x 440, the Generator at 142)
   were outside it when the Generator fired, so the group despawned them at once. 160 px around
   Lomax's view covers Flink's mid window at every width (Flink's camera is Lomax's x - 0xA4). */
#define GROUP_PHASE        1

/* Flink's watcher (0x2DFB0) despawns the group only when its x span leaves the window: y is not
   tested (the members have window 0x18, {0, 0xFFFF, 0, 0xFFFF}, which never culls them).
   TriggerGroup_UpdateDespawn tests x and y, so the members' y is put at the camera's for the call
   (it reads only +0x3C / +0x40 and g_pCurPlane's scroll) and given back after: its x test, with
   Lomax+'s widened window, stays. Without this 15-1's third liana (a Generator's at x 293, fired
   on the floor at y 420) was despawned at once, 300 px above the view. */
static void __cdecl group_update(BYTE *g)
{
    BYTE **m = GROUP_ACTORS(g), **pl = GROUP_DEFS(g), *cur = *G_PCURPLANE;
    int n = *(int *)(g + 0x84), i, k = 0, y[GROUP_MAX];
    for (i = 0; i < n && i < GROUP_MAX; i++)
        if (A_DEF(m[i]) == pl[i] && A_LAYER(m[i]) != KILLED_LAYER) { m[k] = m[i]; pl[k++] = pl[i]; }
    *(int *)(g + 0x84) = k;
    if (!k || !cur) { if (!k) Actor_Despawn(g); return; }
    for (i = 0; i < k; i++) { y[i] = A_Y(m[i]); A_Y(m[i]) = P_CAMY(cur); }
    TriggerGroup_UpdateDespawn(g);
    for (i = 0; i < k; i++) A_Y(m[i]) = y[i];
}

static BYTE *group_new(BYTE *a)
{
    BYTE *g = Actor_Alloc(a, NULL);
    if (g) {
        A_UPDATE(g) = (void *)group_update;
        A_FLAGS(g) |= AF_HIDDEN;
        A_CULL(g) = CULL_NEVER;
    }
    return g;
}

/* the live actor of placement p: Object_TrySpawn's, or the copy that its spawn made of it
   (Rope_Spawn 0x429F60 calls Actor_MoveToHighSlot 0x412B50, which kills the original), which
   would otherwise be left out of the group and culled by its own phase (15-1's third liana) */
static BYTE *spawned_actor(BYTE *o, BYTE *p)
{
    int i;
    if (A_DEF(o) == p && A_LAYER(o) != KILLED_LAYER) return o;
    for (i = MAX_ACTORS - 1; i >= 0; i--) {
        BYTE *t = actor_at(i);
        if (A_TYPE(t) && A_DEF(t) == p && A_LAYER(t) != KILLED_LAYER) return t;
    }
    return o;
}

/* within GENE_NEAR px of the view (its width as enemy_on_screen takes it: the map's width less the
   camera's last x, Lomax+'s widened view included) */
static int gene_near(const BYTE *a)
{
    const BYTE *pl = PLANE(A_PLANE(a));
    int w = P_W(pl) * 16 - P_MAXX(pl), x = A_X(a) - P_CAMX(pl), y = A_Y(a) - P_CAMY(pl);
    if (P_MAXX(pl) <= 0 || w < FLINK_VIEW_W) w = FLINK_VIEW_W;
    return x >= -GENE_NEAR && x <= w + GENE_NEAR && y >= -GENE_NEAR && y <= VIEW_H + GENE_NEAR;
}

static void __cdecl gene_update(BYTE *a)
{
    const BYTE *anim = *G_PLAYERANIM;
    int x = A_X(a), y = A_Y(a), px = *G_PLAYERX, py = *G_PLAYERY;
    int mode = *(int *)(a + 0x88), n = 0, spawned = 0, phase = GROUP_PHASE;
    BYTE *p, *o, *g = NULL;
    if (!gene_near(a)) { Actor_Despawn(a); return; }
    if (GENE_WAIT(a) > 0) { GENE_WAIT(a)--; return; }
    if (A_PLANE(a) != *G_PLAYERPLANE || !anim ||
        x - 16 > px + HB_X2(anim) || x + 16 < px + HB_X1(anim) ||
        y - *(int *)(a + 0x84) > py + HB_Y2(anim) || y + 16 < py + HB_Y1(anim))
        return;
    for (p = *(BYTE **)(a + 0x80); p; p = D_NEXT(p)) {
        if (D_STATE(p) & DS_SPAWNED) continue;                          /* spawned (or killed) */
        if (!g && !(mode & GENE_NO_GROUP)) g = group_new(a);
        if (!(o = Object_TrySpawn(p))) continue;
        o = spawned_actor(o, p);
        spawned++;
        if (g && n < GROUP_MAX && A_CULL(o) != CULL_NEVER) {
            if (A_CULL(o) > phase) phase = A_CULL(o);
            A_CULL(o) = CULL_NEVER;
            GROUP_ACTORS(g)[n] = o;
            GROUP_DEFS(g)[n++] = p;
        }
    }
    if (g) {
        *(int *)(g + 0x80) = phase;
        *(int *)(g + 0x84) = n;
        if (!n) Actor_Despawn(g);
    }
    if (!spawned) return;
    flog("generator at (%d,%d) fired at (%d,%d): %d spawned", x, y, px, py, spawned);
    if (mode & GENE_ONCE) Actor_Kill(a);
    else GENE_WAIT(a) = GENE_REARM;
}

void __cdecl gene_spawn(BYTE *a, BYTE *def)
{
    Trigger_Spawn(a, def);
    if (!g_active || !def || !is_type(a, "Gene Flink") || !*(BYTE **)(a + 0x80)) return;
    A_UPDATE(a) = (void *)gene_update;
    *(int *)(a + 0x84) = def_param(a, def, 0, 16);
    *(int *)(a + 0x88) = def_param(a, def, 1, 0);
    GENE_WAIT(a) = 0;
}
