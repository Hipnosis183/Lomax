/*
 * music.c - Flink's music between maps, on Lomax's title and on the map screen (README "Music").
 *
 * The Music trigger (object 37, Musi_MusicTrigger_Init 0x423A60) plays its param's CD track
 * (CD_PlayTrackWrapper) when the map loads and, with its "once" flag (placement +0x10 bit 0, which
 * the converter sets), sets g_bExitFadesMusic, so that every Exit faded the music out
 * (g_bFadeOutMusic: Game_UpdateFade fades the CD volume with the screen) and the next map started its
 * track again, even the same one. In Flink's levels an Exit fades it only when the map it leads to
 * plays another track (LEVELS.INI [Music]; a map without one keeps the music going: music_changes,
 * from exits.c), and after an Exit (g_bLevelExitReached still set: the Music trigger is placed before
 * the Start trapdoor, which clears it) a trigger whose track is already playing doesn't play it
 * again: the track goes on. After a lost life, or entering the level from the map, it plays as before.
 *
 * Title and map. CD_PlayTrackWrapper(n) plays disc track n + 1, and Flink's track t is
 * MODS\FLINK\MUSIC\TRACK1tt, so n = 100 + t - 1.
 * - Title: Logo_Update (0x41D1A0) plays it with "push 0x14" (6A 14 at 0x41D1CE: TRACK21) when the
 *   logo has flown in; the immediate becomes TITLE_TRACK (Flink's track 2, its title theme).
 * - Map: MAP.WLD's Music trigger has param 18 (TRACK19); music_spawn gives it MAP_TRACK (Flink's
 *   track 13) when the map section loads it.
 * - Flink's title screen (title.c) has no Lomax Logo but a Music trigger with Lomax's title track,
 *   param 20 (TRACK21); music_spawn gives it TITLE_TRACK, so it plays from the start, as in Flink
 *   (the Logo's init, 0x2C7E6: CdAudio_PlayTrack index 0, disc track 2).
 * Each is done only when its file is in MODS\FLINK\MUSIC, so without the files Lomax's track plays.
 *
 * Played once (the boss's fanfare: music_play_once). CD_PlayTrack (0x40DFF0) sends "play cdaudio from
 * n+1 to n+2 notify"; at its end MCI posts MM_MCINOTIFY and Lomax_WndProc (0x404949) calls
 * CD_RestartTrack (0x40E0C0), which plays g_CurCDTrack again: every track loops. music_play_once sends
 * the same commands without "notify", through the game's own mciSendStringA import (as CD_PlayTrack
 * loads it at 0x40E046), and sets g_CurCDTrack and g_RequestedCDTrack as CD_PlayTrack does. Game_Resume
 * (window reactivation, the pause menu) resumes with CD_Resume (0x40E150), "notify" again; its call
 * (0x404A88) goes through cd_resume, which resumes a track played once without it.
 */
#include "flink.h"

#define PUSH_TITLE_TRACK   0x0041D1CEu   /* Logo_Update: push 0x14 ; call CD_PlayTrackWrapper */
#define CALL_CD_RESUME     0x00404A88u   /* Game_Resume: call CD_Resume */
#define LOAD_MCI           0x0040E046u   /* CD_PlayTrack: mov esi, [mciSendStringA's import] (read, not patched) */
static const BYTE titlePushOrig[] = { 0x6A, 0x14 };
#define TITLE_TRACK        101           /* TRACK102 */
#define MAP_TRACK_LOMAX    18            /* MAP.WLD's Music param (TRACK19) */
#define MAP_TRACK          112           /* TRACK113 */
#define TITLE_TRACK_LOMAX  20            /* Flink's title screen's Music param (TRACK21, the converter's TITLE_MUSIC) */

static int g_music[MAX_LEVELS][MAX_SUBS];        /* each map's Music param, LEVELS.INI [Music] (0 none) */
static int g_track = -1;                         /* the Music param that FLINK.DLL last saw played */
static int g_once = -1;                          /* the track music_play_once played, or -1 */
static int g_mapTrack;                           /* MAP_TRACK when its file is there, else 0 */
static int g_titleTrack;                         /* TITLE_TRACK when its file is there, else 0 */

void music_load(const char *ini)
{
    int k, j;
    for (k = 0; k < g_nlevels; k++)
        for (j = 0; j < g_nsubs[k]; j++) g_music[k][j] = GetPrivateProfileIntA("Music", map_name(k, j), 0, ini);
}

/* the file of CD_PlayTrackWrapper's track n is in MUSIC next to the DLL */
static int have_track(int n)
{
    char pat[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    snprintf(pat, sizeof(pat), "%s\\MUSIC\\TRACK%d.*", g_dir, n + 1);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    FindClose(h);
    return 1;
}

int music_changes(int sec, int sub)
{
    int t = sec >= 0 ? g_music[sec][sub] : -1;
    return t < 0 || (t > 0 && t != g_track);
}

/* a track started by an actor, as a Music trigger plays it (the boss: Flink's CdAudio_PlayTrack) */
void music_play(int t)
{
    if (t <= 0) return;
    g_once = -1;
    CD_PlayTrackWrapper(t);
    g_track = t;
    flog("music %d", t);
}

/* The game's mciSendStringA, through its own import (a host that answers the game's CD commands
   answers these too); NULL if the call site doesn't load it as the retail game does. */
typedef DWORD (WINAPI *MciSendFn)(LPCSTR cmd, LPSTR ret, UINT len, HWND notify);
static MciSendFn mci(void)
{
    static const BYTE load[] = { 0x8B, 0x35, 0xA4, 0x8A, 0x5A, 0x00 };   /* CD_PlayTrack: mov esi, [IAT] */
    return is_bytes(LOAD_MCI, load, sizeof(load)) ? *(MciSendFn *)IAT_MCISENDSTRINGA : NULL;
}

/* Played once: CD_PlayTrack's commands without "notify", so no MM_MCINOTIFY comes at its end and
   Lomax_WndProc doesn't loop it (CD_RestartTrack); the game's CD state is set as CD_PlayTrack sets it
   (the boss's fanfare) */
void music_play_once(int t)
{
    char cmd[64];
    MciSendFn f = mci();
    if (t <= 0) return;
    *G_REQUESTEDCDTRACK = t;
    if (!f || !*G_CDAUDIOAVAILABLE || !*G_CFGUSECD) return;
    *G_CURCDTRACK = t;
    g_track = g_once = t;
    snprintf(cmd, sizeof(cmd), "play cdaudio from %d to %d", t + 1, t + 2);
    f("set cdaudio time format tmsf", NULL, 0, NULL);
    f(cmd, NULL, 0, NULL);
    if (!*G_LOGOHOLDSCDVOLUME) CDAudio_SetVolume(*G_CDVOLUME * 255 / 100);
    flog("music %d, once", t);
}

/* Game_Resume's CD_Resume: a track played once resumes without "notify" too */
static void __cdecl cd_resume(void)
{
    char cmd[64];
    MciSendFn f = mci();
    if (g_once < 0 || *G_CURCDTRACK != g_once || !f || !*G_CDAUDIOAVAILABLE) { CD_Resume(); return; }
    snprintf(cmd, sizeof(cmd), "play cdaudio from %.15s to %d", CD_PAUSEPOS, g_once + 2);
    f("set cdaudio time format tmsf", NULL, 0, NULL);
    f(cmd, NULL, 0, NULL);
}

void __cdecl music_spawn(BYTE *a, BYTE *def)
{
    int t = def ? D_PARAM(def, 0) : 0;
    if (g_mapTrack && *G_LOADEDSECTION == SECTION_MAP && t == MAP_TRACK_LOMAX) {
        D_PARAM(def, 0) = (short)g_mapTrack;     /* the placement is reloaded with the map */
        t = g_mapTrack;
    }
    if (g_titleTrack && g_title && t == TITLE_TRACK_LOMAX) {
        D_PARAM(def, 0) = (short)g_titleTrack;
        t = g_titleTrack;
    }
    if (g_active && t > 0 && t == g_track && *G_BLEVELEXITREACHED) {
        *G_BEXITFADESMUSIC = *(const short *)(*(BYTE **)(def + 8) + 0x1A) >= 1 && (D_FLAGS(def) & DF_ONCE);
        flog("music %d goes on", t);
        Actor_Kill(a);
        return;
    }
    Musi_MusicTrigger_Init(a, def);
    if (t > 0) { g_track = g_active ? t : -1; g_once = -1; }
}

int music_retail(void)
{
    return is_bytes(PUSH_TITLE_TRACK, titlePushOrig, sizeof(titlePushOrig)) && is_call(PUSH_TITLE_TRACK + 2, (DWORD)CD_PlayTrackWrapper) &&
           is_call(CALL_CD_RESUME, (DWORD)CD_Resume) && mci();
}

void music_install(void)
{
    patch_call(CALL_CD_RESUME, (const void *)cd_resume);
    BYTE imm = TITLE_TRACK;
    g_titleTrack = have_track(TITLE_TRACK) ? TITLE_TRACK : 0;
    if (g_titleTrack) patch(PUSH_TITLE_TRACK + 1, &imm, 1);
    g_mapTrack = have_track(MAP_TRACK) ? MAP_TRACK : 0;
    flog("title music: %s, map music: %s", have_track(TITLE_TRACK) ? "Flink's" : "Lomax's", g_mapTrack ? "Flink's" : "Lomax's");
}
