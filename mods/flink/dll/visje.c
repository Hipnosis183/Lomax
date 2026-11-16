/*
 * visje.c - the Visje, Flink's 'Visje' (init 0x280B8, think 0x2814C), FOREST and VILLAGE (README
 * "Enemies"). The converter makes it a 'Muta Visje n' type with the world's records SWIM, JUMP and DEAD
 * (VILLAGE's have no DEAD: they are all deadly); params: the checkboxes (bit 0 'move left', bit 1
 * 'deadly'), 'area', 'jump-timer' and 'speed'. Flink's behaviour:
 * - Swimming. With an area, it swims back and forth around its placement: x = x0 + sin(phase) * area
 *   / 2, the phase stepping 4 (of 1024 a turn) a frame, starting to the right; it turns where the
 *   sine peaks. With no area it swims straight on at speed / 100 px a frame (left with 'move left').
 *   Meanwhile it bobs: y += sin(bob) / 65536 px a frame, the bob stepping 10 a frame (about 4 px up and
 *   down). SWIM.
 * - Jumping. A countdown starts at 30; at 0 it jumps (6.0 px a frame up) and starts again from
 *   'jump-timer'. Gravity 0.1875 px a frame; falling below the water's surface (g_WaterSurfaceY 0x8718)
 *   faster than 1.0 px it is slowed to 1.0. Crossing the surface either way splashes (0x2E770). Back at
 *   its placement's y it swims again, the bob from 0. JUMP while in the air.
 * - Touching it hurts. Unless 'deadly' (Actor +6 bit 3 cleared, no Actor_CheckStompedByPlayer), a stomp
 *   kills it (Enemy_Die 0x1B12E) as the other enemies; a deadly one can't be stomped or killed.
 * Here: Lomax's sine table (G_SINTABLE, within 1% of Flink's); the surface is g_WaterLevelY, set by
 * Water_Update from the water actor every tick (the tide included); the splash is Lomax's
 * (Water_SpawnSplash, sound 0x2C). Lomax's attacks kill it too, as Flink's slide and spells; a deadly
 * one is not hittable, so a stomp hurts Lomax and his attacks pass through it.
 */
#include "enemy.h"

#define VISJE_FIRST_JUMP    30
#define VISJE_JUMP          (-0x60020)    /* 0xFFF9FFE0 */
#define VISJE_GRAVITY       0x3000
#define VISJE_WATER_VY      0x10000
#define VISJE_SPEED_UNIT    0x28F         /* speed param 1 = 0.01 px a frame */
#define CHK_LEFT           1
#define CHK_DEADLY         2

static void __cdecl visje_update(BYTE *a)
{
    int i = enemy_slot(a), oldy, newy, vy, w = *G_WATERLEVELY;
    if (i < 0) return;
    if (g_enemy[i].dead) { enemy_dead_update(a, i); return; }
    if (!g_enemy[i].deadly && Actor_CheckHitByPlayer(a)) { enemy_die(a, i, 1); return; }
    if (!g_enemy[i].amp) A_FX(a) += g_enemy[i].speed;             /* straight on (0x28176) */
    else {                                                        /* back and forth (0x28184) */
        int s;
        g_enemy[i].xph = (g_enemy[i].xph + 4) & 0x3FF;
        s = G_SINTABLE[g_enemy[i].xph];
        if ((g_enemy[i].xph & 0x1FF) == 0x100) A_FLAGS(a) ^= AF_LEFT;   /* at a peak: turns */
        A_FX(a) = (g_enemy[i].cx + (s * g_enemy[i].amp >> 16)) << 16;
    }
    if (--g_enemy[i].t == 0) {                                    /* (a word: 0 never jumps again) */
        A_VY(a) = VISJE_JUMP;
        g_enemy[i].t = g_enemy[i].every;
    }
    vy = A_VY(a);
    if (vy) {                                                     /* jumping (0x281DE) */
        set_anim(a, "JUMP");
        oldy = A_Y(a);
        A_FY(a) += vy;
        set_pos(a);
        newy = A_Y(a);
        vy += VISJE_GRAVITY;
        if (vy >= 0 && newy >= w && vy > VISJE_WATER_VY) {
            vy = VISJE_WATER_VY;                                   /* into the water: slowed */
            if (oldy < w) Water_SpawnSplash(a);
        }
        A_VY(a) = vy;
        if (vy < 0) {
            if (oldy >= w && newy < w) Water_SpawnSplash(a);      /* out of the water */
        } else if (newy >= g_enemy[i].home) {                     /* back home */
            place_y(a, g_enemy[i].home);
            A_VY(a) = 0;
            g_enemy[i].yph = 0;
        }
    } else {                                                      /* bobbing (0x2826C) */
        set_anim(a, "SWIM");
        g_enemy[i].yph = (g_enemy[i].yph + 10) & 0x3FF;
        A_FY(a) += G_SINTABLE[g_enemy[i].yph];
    }
    set_pos(a);
    Actor_AdvanceAnim(a);
}

/* (from enemy_spawn) params: checkboxes, area, jump-timer, speed */
void visje_spawn(BYTE *a, int i, const BYTE *def)
{
    int chk = def_param(a, def, 0, 0), area = def_param(a, def, 1, 0), speed = (short)def_param(a, def, 3, 0);
    g_enemy[i].visje = 1;
    g_enemy[i].deadly = (chk & CHK_DEADLY) != 0;
    g_enemy[i].t = VISJE_FIRST_JUMP;
    g_enemy[i].every = (WORD)def_param(a, def, 2, 0);
    if (!g_enemy[i].every) g_enemy[i].every = 0x10000;            /* (subq.w from 0: 65536 frames) */
    g_enemy[i].home = A_Y(a);
    g_enemy[i].speed = abs(speed) * VISJE_SPEED_UNIT;
    if (chk & CHK_LEFT) g_enemy[i].speed = -g_enemy[i].speed;
    if (area) {                                                   /* back and forth, starting right */
        g_enemy[i].amp = area * 2;
        g_enemy[i].cx = A_X(a);
        g_enemy[i].speed = 0;
        A_FLAGS(a) &= ~AF_LEFT;
    }
    if (g_enemy[i].deadly) A_FLAGS(a) &= ~AF_HITTABLE;           /* touching it only hurts */
    A_FLAGS(a) &= ~0x80000u;                 /* (not Water_Update's: it splashes and slows itself) */
    A_UPDATE(a) = (void *)visje_update;
    Actor_SetAnim(a, FOURCC("SWIM"));
}
