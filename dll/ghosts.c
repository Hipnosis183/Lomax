/*
 * ghosts.c - widescreen: objects still waiting to spawn are shown, frozen, instead of popping in.
 */
#include "lomax.h"

/* Demos keep the original 320-px spawn windows, or the recording would desync, so the wider screen
   shows level objects before the game has spawned them. This runs during demos only.
   Spawning itself is left alone. Each such placement inside the view gets a "ghost": copies of the
   actors its spawn function would create, drawn with Actor_Draw and never updated. When the game
   really spawns the placement the ghost is gone and the real actor takes over at the same spot.

   A ghost's look comes from running the real spawn function once and undoing it. All game state
   lives in LOMAX.EXE's .data section (actors, the placements in the level and object heaps, random
   seeds), so that section is saved, the spawn runs, the new actors are copied and the section is
   put back. The ranges leave out the sound driver's variables, which its timer thread writes at any
   moment, so the driver must not be reached: its voice calls all return at once while G_SNDREADY is
   0 (only those calls read it, on the main thread; a loop started here would outlive the rollback
   and fault in the driver's timer after the next level load). Sound_PlayEffect stays silent while
   G_SOUNDGATE is 0, and the CRT's rand() seed is saved apart. Nothing of a trial spawn survives it,
   so the simulation (and demo sync) can't notice.

   A ghost is shown only after its placement has been outside the view (or since the level started),
   so an enemy that wandered off and despawned doesn't reappear at its home in plain sight. */

#define G_FIRSTPLANE      ((volatile short *)0x004699FCu)
#define G_LASTPLANE       ((volatile int *)0x0046A844u)
#define G_ACTORS          ((BYTE *)0x0046ABC0u)
#define ACTOR_SIZE        0x120
#define G_SOUNDGATE       ((volatile int *)0x0059F1DCu)   /* 0: no sounds, spawns allowed on screen */
#define G_SNDREADY        ((volatile BYTE *)0x00466B5Cu)  /* sound driver up: its voice calls work */
#define G_SPRITEOFSX      ((volatile int *)0x0059F168u)   /* Actor_GetCurSprite's side outputs */
#define G_SPRITEOFSY      ((volatile int *)0x0059F16Cu)
#define FN_OBJECT_SPAWN   0x004126A0u                     /* Object_TrySpawn */
#define FN_GENE_UPDATE    0x0042D160u                     /* a live Generator's pfnUpdate */
#define OBJ_GENERATOR     86
#define OBJ_TYPES         89

#define REC_NEXT(r)       (*(BYTE **)(r))                 /* next placement on the plane */
#define REC_CHAIN(r)      (*(BYTE **)((r) + 4))           /* Generator: next placement it spawns */
#define REC_TYPE(r)       (*(short *)(*(BYTE **)((r) + 8) + 0x16))
#define REC_X(r)          (*(short *)((r) + 0xC))
#define REC_Y(r)          (*(short *)((r) + 0xE))
#define REC_FLAGS(r)      (*(unsigned short *)((r) + 0x10))
#define REC_STATE(r)      (*(unsigned short *)((r) + 0x14))
#define REC_BYGENERATOR   0x2000                          /* the spawner skips it */
#define REC_SPAWNED       0x8000                          /* Actor_Kill leaves it set */

#define GHOST_HASH_BITS   13
#define GHOST_HASH        (1 << GHOST_HASH_BITS)
#define GHOST_MAX         (GHOST_HASH / 2)
#define GHOST_MAX_PARTS   2048
#define GHOST_MAX_SHOWN   256
#define GHOST_OUT_MARGIN  128      /* px past the view that count as outside it (the largest sprites) */
#define GHOST_NEAR_MARGIN 64       /* captured this far ahead, a few per tick */
#define GHOST_NEAR_BUDGET 4

typedef struct {
    BYTE *rec;
    BYTE *generator;               /* the Generator that spawns it, if any */
    int   part, nParts;            /* its actor copies in g_parts */
    unsigned short state;          /* placement state the copies were made from */
    BYTE  captured, armed;
    DWORD genAlive;                /* Generator: tick stamp of the last time its actor was seen */
} Ghost;

/* Objects that can be shown: enemies, hazards, pickups and props. Left out: level logic, screens,
   bosses and anything drawn in screen space. */
static const BYTE kGhostTypes[] = {
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
    43, 44, 45, 46, 47, 48, 50, 51, 52, 53, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66,
    67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 82, 83, 84, 85,
};

/* .data without the sound driver's variables */
static const struct { DWORD from, to; } kGameState[] = {
    { 0x00461000u, 0x00466A00u },
    { 0x00466B80u, 0x0059F3ECu },
};

/* the actor slots: allocated, and drawn this tick (Actors_UpdateAll builds it from the slots it ran) */
static const struct { volatile DWORD *mask, *drawn; int first; } kBanks[3] = {
    { (volatile DWORD *)0x0052C5A0u, (volatile DWORD *)0x0046ABB0u, 0 },
    { (volatile DWORD *)0x0059E768u, (volatile DWORD *)0x0059F324u, 33 },
    { (volatile DWORD *)0x0059F1F8u, (volatile DWORD *)0x00476DA8u, 66 },
};

int g_ghostSandbox;                /* a trial spawn is running: the DemoTrace hooks stay quiet */

static BYTE   g_ghostType[OBJ_TYPES];
static Ghost  g_ghosts[GHOST_HASH];
static int    g_nGhosts, g_nParts, g_nShown, g_linked;
static Ghost *g_shown[GHOST_MAX_SHOWN];
static BYTE  *g_parts;             /* GHOST_MAX_PARTS actor copies */
static BYTE  *g_saved;             /* the game state during a batch of trial spawns */
static DWORD  g_savedSeed, g_stamp;
static int    g_saving;

static Ghost *ghost_get(BYTE *rec)
{
    unsigned h = ((DWORD)rec * 2654435761u) >> (32 - GHOST_HASH_BITS);
    while (g_ghosts[h].rec && g_ghosts[h].rec != rec) h = (h + 1) & (GHOST_HASH - 1);
    if (!g_ghosts[h].rec) {
        if (g_nGhosts >= GHOST_MAX) return NULL;
        g_nGhosts++;
        g_ghosts[h].rec = rec;
        g_ghosts[h].armed = 1;     /* not seen before: the level just started */
    }
    return &g_ghosts[h];
}

static void state_copy(int restore)
{
    BYTE *p = g_saved;
    size_t i;
    for (i = 0; i < sizeof(kGameState) / sizeof(kGameState[0]); i++) {
        size_t n = kGameState[i].to - kGameState[i].from;
        if (restore) memcpy((void *)kGameState[i].from, p, n);
        else         memcpy(p, (const void *)kGameState[i].from, n);
        p += n;
    }
}

static DWORD *rand_seed(void)
{
    return (DWORD *)((BYTE *)((GetPtd_t)FN_GETPTD)() + 0x14);
}

/* Runs the placement's spawn function, keeps a copy of each actor it leaves to be drawn the default
   way, and undoes it. Actors with their own draw callback aren't copied: it may read other actors,
   which don't exist once the state is back. */
static void ghost_capture(Ghost *g)
{
    DWORD before[3];
    BYTE sndReady = *G_SNDREADY;
    int b, bit;

    if (!g_saving) {
        state_copy(0);
        g_savedSeed = *rand_seed();
        g_saving = 1;
    }
    for (b = 0; b < 3; b++) before[b] = *kBanks[b].mask;
    *G_SOUNDGATE = 0;

    g->part = g_nParts;
    g->nParts = 0;
    g->state = REC_STATE(g->rec) & ~REC_SPAWNED;
    g->captured = 1;

    g_ghostSandbox = 1;
    *G_SNDREADY = 0;
    if (((int *(__cdecl *)(BYTE *))FN_OBJECT_SPAWN)(g->rec))
        for (b = 0; b < 3; b++) {
            DWORD added = *kBanks[b].mask & ~before[b];
            for (bit = 0; added; bit++, added >>= 1) {
                const BYTE *a = G_ACTORS + (kBanks[b].first + bit) * ACTOR_SIZE;
                if (!(added & 1)) continue;
                if ((*(const DWORD *)(a + 0x58) & 6) || *(const void *const *)(a + 0x1C) || !*(const void *const *)(a + 8))
                    continue;              /* hidden or removed, own draw callback, no animation */
                if (g_nParts >= GHOST_MAX_PARTS) continue;
                memcpy(g_parts + g_nParts++ * ACTOR_SIZE, a, ACTOR_SIZE);
                g->nParts++;
            }
        }
    *G_SNDREADY = sndReady;
    g_ghostSandbox = 0;

    state_copy(1);
    *rand_seed() = g_savedSeed;
}

static void ghosts_link_generators(int first, int last)
{
    int i;
    for (i = first; i <= last; i++) {
        BYTE *rec;
        for (rec = *(BYTE **)(G_PLANES + i * PLANE_STRIDE); rec; rec = REC_NEXT(rec)) {
            BYTE *m;
            if (REC_TYPE(rec) != OBJ_GENERATOR) continue;
            for (m = REC_CHAIN(rec); m; m = REC_CHAIN(m)) {
                Ghost *g = ghost_get(m);
                if (g) g->generator = rec;
            }
        }
    }
    g_linked = 1;
}

/* A Generator can still fire if it is waiting to spawn or its actor is alive; one that fired for
   good was killed, which leaves its placement marked as spawned. */
static int generator_armed(BYTE *rec)
{
    Ghost *g;
    if (!(REC_STATE(rec) & REC_SPAWNED)) return 1;
    g = ghost_get(rec);
    return g && g->genAlive == g_stamp;
}

static void generators_mark_alive(void)
{
    int b, bit;
    g_stamp++;
    for (b = 0; b < 3; b++) {
        DWORD m = *kBanks[b].mask;
        for (bit = 0; m; bit++, m >>= 1) {
            const BYTE *a = G_ACTORS + (kBanks[b].first + bit) * ACTOR_SIZE;
            if ((m & 1) && *(const DWORD *)(a + 0x18) == FN_GENE_UPDATE && *(BYTE *const *)(a + 4)) {
                Ghost *g = ghost_get(*(BYTE *const *)(a + 4));
                if (g) g->genAlive = g_stamp;
            }
        }
    }
}

/* After Objects_SpawnNearCamera, with the real (wide) camera in the planes: which ghosts to show. */
void ghosts_update(int demo)
{
    int i, first, last, budget = GHOST_NEAR_BUDGET;
    if (!g_parts || g_modRunning[0] || !*G_PCURPLANE) return;
    if (!demo) { g_nShown = 0; return; }
    first = *G_FIRSTPLANE;
    last = *G_LASTPLANE;
    if (first < 0) first = 0;
    if (last >= MAX_PLANES) last = MAX_PLANES - 1;
    if (!g_linked) ghosts_link_generators(first, last);
    generators_mark_alive();

    g_nShown = 0;
    for (i = first; i <= last; i++) {
        BYTE *plane = (BYTE *)(G_PLANES + i * PLANE_STRIDE), *rec;
        int sx = *(int *)(plane + 4), sy = *(int *)(plane + 8), viewH = *(volatile int *)0x00464424u;
        if (*(int *)(plane + 0x3C)) continue;        /* rotating planes: positions aren't flat */
        for (rec = *(BYTE **)plane; rec; rec = REC_NEXT(rec)) {
            int type = REC_TYPE(rec), x, y, near_, inView;
            Ghost *g;
            if (type < 0 || type >= OBJ_TYPES || !g_ghostType[type]) continue;
            if (!(g = ghost_get(rec))) continue;
            if (REC_STATE(rec) & REC_SPAWNED) { g->armed = 0; continue; }

            x = REC_X(rec) - sx;
            y = REC_Y(rec) - sy;
            if (x < -GHOST_OUT_MARGIN || x > g_wsWidth + GHOST_OUT_MARGIN ||
                y < -GHOST_OUT_MARGIN || y > viewH + GHOST_OUT_MARGIN) {
                g->armed = 1;
                continue;
            }
            if (!g->armed) continue;
            if ((REC_FLAGS(rec) & REC_BYGENERATOR) && (!g->generator || !generator_armed(g->generator))) continue;

            inView = x >= -GHOST_NEAR_MARGIN / 2 && x <= g_wsWidth + GHOST_NEAR_MARGIN / 2;
            near_ = x >= -GHOST_NEAR_MARGIN && x <= g_wsWidth + GHOST_NEAR_MARGIN;
            if (!g->captured || g->state != (REC_STATE(rec) & ~REC_SPAWNED)) {
                if (!inView && !(near_ && budget > 0)) continue;
                if (!inView) budget--;
                ghost_capture(g);
            }
            if (g->nParts && g_nShown < GHOST_MAX_SHOWN) g_shown[g_nShown++] = g;
        }
    }
    g_saving = 0;
}

/* A Generator fires inside Actors_UpdateAll, which only puts the slots it ran in the draw mask: the
   actor it just spawned isn't drawn until the next tick. Its ghost stays up for that tick. */
static int ghost_awaits_actor(const BYTE *rec)
{
    int b, bit, found = 0;
    for (b = 0; b < 3; b++) {
        DWORD m = *kBanks[b].mask, d = *kBanks[b].drawn;
        for (bit = 0; m; bit++, m >>= 1, d >>= 1) {
            const BYTE *a = G_ACTORS + (kBanks[b].first + bit) * ACTOR_SIZE;
            if (!(m & 1) || *(const BYTE *const *)(a + 4) != rec) continue;
            if (d & 1) return 0;
            found = 1;
        }
    }
    return found;
}

/* After Actors_DrawAndCull. */
void ghosts_draw(void)
{
    int i, ofsX, ofsY;
    if (!g_nShown) return;
    ofsX = *G_SPRITEOFSX;
    ofsY = *G_SPRITEOFSY;
    for (i = 0; i < g_nShown; i++) {
        const Ghost *g = g_shown[i];
        int p;
        if ((REC_STATE(g->rec) & REC_SPAWNED) && !ghost_awaits_actor(g->rec)) continue;
        for (p = 0; p < g->nParts; p++)
            ((void (__cdecl *)(BYTE *, int, int, int))FN_ACTOR_DRAW)(g_parts + (g->part + p) * ACTOR_SIZE, 1000, 0, 0);
    }
    *G_SPRITEOFSX = ofsX;
    *G_SPRITEOFSY = ofsY;
}

/* A level is being set up: its placements are new. */
void ghosts_reset(void)
{
    memset(g_ghosts, 0, sizeof(g_ghosts));
    g_nGhosts = g_nParts = g_nShown = g_linked = 0;
}

void ghosts_init(void)
{
    size_t i, stateSize = 0;
    for (i = 0; i < sizeof(kGameState) / sizeof(kGameState[0]); i++)
        stateSize += kGameState[i].to - kGameState[i].from;
    for (i = 0; i < sizeof(kGhostTypes); i++) g_ghostType[kGhostTypes[i]] = 1;
    g_saved = VirtualAlloc(NULL, stateSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_parts = g_saved ? VirtualAlloc(NULL, GHOST_MAX_PARTS * ACTOR_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) : NULL;
    log_msg("widescreen: waiting objects %s", g_parts ? "shown frozen" : "not shown (out of memory)");
}
