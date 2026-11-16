/*
 * sfx.c - Flink's sound effects (README "Sound effects").
 *
 * Flink plays its sounds by ID (Sound_PlayEffect 0x358AA, IDs 0x17-0x53). SoundIdTable (0x358FC)
 * turns an ID into one of the 47 effects of the sub CPU's PCM driver (scripts of 18 samples,
 * ..\..\Flink\re\pcm_sound.md), or into an FM sound of the main CPU (negative), which has no file
 * here. FLINK.EXE (extractor\sfx.c) renders each effect into MODS\FLINK\SFX\Enn.WAV, 22050 Hz 16-bit mono,
 * the format Lomax's driver takes (SndDrv_LoadWav 0x4382D0).
 *
 * Loading: the first time a Flink level loads (sfx_set, from flink_settings) with the driver open
 * (g_bSoundAvailable), every effect an ID names is loaded through SndDrv_LoadWav, as
 * Sound_InitAndLoadSfx (0x403B80) loads Lomax's SFX\*.WAV. The driver holds 128 samples (the mixer's
 * g_MixerSamples and DSHw's g_DSHwSamples alike); Lomax's take 67, Flink's 42. They stay loaded until
 * the game closes. A missing or unloadable file leaves its effect out: what plays it then plays
 * nothing.
 *
 * Playing (flink_sound): Sound_PlayEffect (0x40D5F0) plays only g_SoundDefs entries, with its
 * position, plane depth, fade and volume setting. So the effect's sample is put into a scratch
 * entry (SCRATCH, an alias that only shares BOUNCE1's sample), played, and the entry restored. Every
 * FLINK.DLL sound that stands for a Flink sound with a PCM effect goes through flink_sound with
 * Flink's ID. The FM sounds (0x18, 0x29, 0x48, ...) stay Lomax's.
 *
 * Lomax's own sounds (sfx_set): in Flink's levels the 1UP's g_SoundDefs entry (3, Item_UpdateOneUp
 * 0x42CA45) gets Flink's 0x1E, and gets Lomax's back when Flink's mode ends (the continue screen,
 * Lomax's levels). Lomax's jump, landing and hurt sounds stay his.
 *
 * Loops (sfx_loop): E41 (Flink's 0x38, the Wagon's and the pushed Rock's rolling) never ends in
 * Flink, which stops it with Pcm_StopEffect (0x3FE4A). Here it is a looping def (Sound_PlayEffect
 * returns its voice); its owner asks for it every tick while it should sound, and sfx_tick (the end
 * of player_tick) stops a loop not asked for since, or whose owner is gone. world_load stops all.
 */
#include "flink.h"

#define ID_FIRST           0x17
#define SCRATCH            15            /* g_SoundDefs[15]: a nameless alias (of BOUNCE1) */
#define SFX_VOLUME         200           /* Lomax's usual (g_SoundDefs' 200); the effects carry Flink's own levels */
#define SFX_PITCH          0x100
#define EFFECTS            47
#define WAV_MAX            (1 << 20)
#define MAX_LOOPS          8

/* SoundIdTable (0x358FC), IDs 0x17-0x53: the PCM effect, or < 0 an FM sound */
static const signed char SOUND_IDS[] = {
     0, -24, 20,  1,  6, 16, 18, 15, 17, 19,  0, 11,  8,  0, 10,  2, -39, -40, -41,  4,
    24,  25, 12,  5, 26, 43, 27, 28, 29,  3, 13, 30, 31, 41, 33, 34,  27,  35,  36, 11,
    37,  38, 39, 40, 42,  0,  0,  0,  0, -72, 21, 44, 45,  0,  0,  0,  0,   9, -63, -64,
    46,
};

/* the effects that never end in Flink: looping voices here */
#define LOOPING(e)         ((e) == 41 || (e) == 36)

/* Lomax's entries that his own code plays for Flink's events: the entry, its file's name, Flink's ID */
static const struct { int id; const char *name; int flink; } SWAPS[] = {
    { 0x03, "1UP",   0x1E },             /* the 1UP */
};

static int g_sample[EFFECTS];            /* the driver's sample + 1, 0: none */
static int g_loaded;                     /* loading was tried */
static int g_swapped;
static SoundDef g_saved[COUNT(SWAPS)];   /* Lomax's entries while Flink's are in */

static struct { BYTE *a, *type; int voice, asked; } g_loop[MAX_LOOPS];

/* the entries SWAPS changes are Lomax's: their names (a check of the retail table, flink_init) */
int sfx_retail(void)
{
    size_t i;
    for (i = 0; i < COUNT(SWAPS); i++) {
        const char *n = G_SOUNDDEFS[SWAPS[i].id].name;
        if (!n || strcmp(n, SWAPS[i].name)) return 0;
    }
    return !G_SOUNDDEFS[SCRATCH].name;
}

static int effect_of(int id)
{
    return id >= ID_FIRST && id < ID_FIRST + (int)sizeof(SOUND_IDS) ? SOUND_IDS[id - ID_FIRST] : -1;
}

static int sfx_has(int id)
{
    int e = effect_of(id);
    return e >= 0 && g_sample[e] > 0;
}

/* Enn.WAV -> the driver's sample, < 0: none */
static int load_effect(int e)
{
    char path[MAX_PATH];
    HANDLE f;
    DWORD size, got = 0;
    BYTE *buf;
    int s = -1;
    snprintf(path, sizeof(path), "%s\\SFX\\E%02d.WAV", g_dir, e);
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return -1;
    size = GetFileSize(f, NULL);
    if (size > 0x2C && size <= WAV_MAX && (buf = malloc(size))) {
        if (ReadFile(f, buf, size, &got, NULL) && got == size &&
            !memcmp(buf, "RIFF", 4) && !memcmp(buf + 8, "WAVEfmt ", 8) && *(WORD *)(buf + 0x16) == 1 &&
            *(DWORD *)(buf + 0x18) == 22050 && *(WORD *)(buf + 0x22) == 16 && !memcmp(buf + 0x24, "data", 4) &&
            *(DWORD *)(buf + 0x28) <= size - 0x2C)
            s = SndDrv_LoadWav(buf, NULL, 0);
        free(buf);
    }
    CloseHandle(f);
    if (s < 0) flog("sound effect %s: not loaded (%d)", path, s);
    return s;
}

static void load_all(void)
{
    size_t i;
    int n = 0;
    g_loaded = 1;
    for (i = 0; i < sizeof(SOUND_IDS); i++) {
        int e = SOUND_IDS[i], s;
        if (e < 0 || g_sample[e]) continue;
        s = load_effect(e);
        g_sample[e] = s >= 0 ? s + 1 : -1;
        if (s >= 0) n++;
    }
    for (i = 0; i < EFFECTS; i++) if (g_sample[i] < 0) g_sample[i] = 0;
    flog("Flink's sound effects: %d loaded from %s\\SFX", n, g_dir);
}

/* the def an effect plays with */
static void effect_def(SoundDef *d, int e)
{
    d->voice = g_sample[e] - 1;
    d->volume = SFX_VOLUME;
    d->pitch = SFX_PITCH;
    d->loop = LOOPING(e);
}

/* the effects, once the driver is open (Flink's intro plays before any Flink level) */
void sfx_load(void) { if (!g_loaded && *G_BSOUNDAVAILABLE) load_all(); }

/* the game's settings for Flink's maps or Lomax's (flink_settings) */
void sfx_set(int flink)
{
    size_t i;
    if (flink && !g_loaded && *G_BSOUNDAVAILABLE) load_all();
    if (flink && !g_swapped) {
        for (i = 0; i < COUNT(SWAPS); i++) {
            int e = effect_of(SWAPS[i].flink);
            g_saved[i] = G_SOUNDDEFS[SWAPS[i].id];
            if (e >= 0 && g_sample[e] > 0) effect_def(&G_SOUNDDEFS[SWAPS[i].id], e);
        }
        g_swapped = 1;
    } else if (!flink && g_swapped) {
        for (i = 0; i < COUNT(SWAPS); i++) G_SOUNDDEFS[SWAPS[i].id] = g_saved[i];
        g_swapped = 0;
    }
}

/* Flink's sound `id` from src (NULL: Lomax, as Sound_PlayEffect), nothing without its file;
   Sound_PlayEffect's result (a looping effect's voice, < 0: none) */
int flink_sound(BYTE *src, int id)
{
    int e = effect_of(id), r;
    SoundDef saved;
    if (e < 0 || g_sample[e] <= 0) return -1;
    saved = G_SOUNDDEFS[SCRATCH];
    effect_def(&G_SOUNDDEFS[SCRATCH], e);
    r = Sound_PlayLoop(src, SCRATCH);
    G_SOUNDDEFS[SCRATCH] = saved;
    return r;
}

static void loop_stop(int k)
{
    if (g_loop[k].voice >= 0) Sound_StopVoice(g_loop[k].voice);
    memset(&g_loop[k], 0, sizeof(g_loop[k]));
}

/* Flink's looping sound `id` from actor a, kept going while asked for every tick */
void sfx_loop(BYTE *a, int id)
{
    int k, free = -1, v;
    if (!sfx_has(id)) return;
    for (k = 0; k < MAX_LOOPS; k++) {
        if (g_loop[k].a == a && g_loop[k].type == A_TYPE(a)) { g_loop[k].asked = 1; return; }
        if (!g_loop[k].a && free < 0) free = k;
    }
    if (free < 0) return;
    v = flink_sound(a, id);                  /* (too quiet to start: tried again next tick) */
    if (v < 0) return;
    g_loop[free].a = a;
    g_loop[free].type = A_TYPE(a);
    g_loop[free].voice = v;
    g_loop[free].asked = 1;
}

/* the end of player_tick: a loop not asked for since the last tick, or whose actor is gone, stops */
void sfx_tick(void)
{
    int k;
    for (k = 0; k < MAX_LOOPS; k++) {
        BYTE *a = g_loop[k].a;
        int i;
        if (!a) continue;
        i = slot_of(a);
        if (!g_loop[k].asked || i < 0 || !actor_active(i) || A_LAYER(a) == KILLED_LAYER || A_TYPE(a) != g_loop[k].type)
            loop_stop(k);
        else g_loop[k].asked = 0;
    }
}

/* world_load: every loop stops */
void sfx_reset(void)
{
    int k;
    for (k = 0; k < MAX_LOOPS; k++) if (g_loop[k].a) loop_stop(k);
}
