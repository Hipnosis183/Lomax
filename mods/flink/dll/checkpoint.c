/*
 * checkpoint.c - Flink's revive points (README "Levels").
 *
 * Lomax checkpoints (object 3, the converter's Revive), but unseen. Lomax's Checkpoint_Update
 * (0x4277A0) fires once (placement +0x14 bit 0) when, Lomax alive (g_PlayerDeathTimer 0) and on its
 * plane, the checkpoint's point lies in his hitbox (y up to 20 px below it): it saves the restart
 * point (g_RestartX/Y = the checkpoint's x/y, g_pRestartPlane = the current plane) and the camera's
 * orbit offset (0, or 1000 - the plane's orbit position on a rotating plane, also kept in
 * g_RestartOrbitOffset), shows 'ZWEI' and plays sound 0x21. Checkpoint_Draw (0x4278C0) draws the
 * post. In Flink's levels the actor draws nothing, makes no sound and fires when Lomax's hitbox
 * reaches its x at any height (as long as it is spawned: while it is about on screen), saving the
 * same restart point; then it is removed (its placement stays used).
 */
#include "flink.h"

static void __cdecl checkpoint_draw(BYTE *a) { (void)a; }

static void __cdecl checkpoint_update(BYTE *a)
{
    WORD *used = &D_STATE(A_DEF(a));
    const BYTE *anim = *G_PLAYERANIM;
    int x = A_X(a), px = (short)*G_PLAYERX, plane = A_PLANE(a);
    if (*used & DS_USED) { Actor_Kill(a); return; }
    if (*G_PLAYERDEATHTIMER || plane != *G_PLAYERPLANE || !anim || px + HB_X1(anim) > x || px + HB_X2(anim) < x) return;
    *used |= DS_USED;
    *G_RESTARTX = x;
    *G_RESTARTY = A_Y(a);
    *G_CAMERAORBITOFFSET = P_ROTATING(PLANE(plane)) ? (short)(1000 - P_ORBITPOS(PLANE(plane))) : 0;
    *G_PRESTARTPLANE = *G_PCURPLANE;
    *G_RESTARTORBITOFFSET = *G_CAMERAORBITOFFSET;
    flog("revive point at (%d,%d) reached at (%d,%d)", x, A_Y(a), px, *G_PLAYERY);
    Actor_Kill(a);
}

void __cdecl checkpoint_spawn(BYTE *a, BYTE *def)
{
    Checkpoint_Spawn(a, def);
    if (!g_active) return;
    A_UPDATE(a) = (void *)checkpoint_update;
    A_DRAW(a) = (void *)checkpoint_draw;
}
