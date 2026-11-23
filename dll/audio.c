/*
 * audio.c - the sound mixer crash, 16 mixed voices, DirectSound mixing on Vista+, the hard-right pan fix, the CD check, and the CD music played from files.
 */
#include "lomax.h"
#include <dsound.h>

static MciSendStringA_t oMciSendStringA;
static IntFn_t          oVerifyDisc;

/* ---- Sound mixer crash (always on) -----------------------------------------------------------
   Mixer_TimerService (0x434910) runs on the sound driver's multimedia-timer thread. It sets
   DAT_00466A14 to the number of voices that just finished (g_pfnMixerMixVoices), then walks the active
   voice list from its head (DAT_0059FE04 = next voice) removing that many. The main thread adds 1
   to the same counter whenever it queues a voice command (Mixer_PlaySampleOneShot / Tracked,
   Mixer_StopVoice, Mixer_FreeSample), without a lock. A sound started between the two steps makes
   the walk run past the list's end: reading voice 0 (+0) at 0x434D26, an access violation that
   closed the game at random. The walk now ends at the list's end (0x434D21: next voice 0 -> the code
   after the loop, 0x434DA5, which sets its own registers). The extra count is the main thread's: its
   commands are applied by the slot loop on the next pass, as before. */
#define MIX_WALK                 0x00434D21u   /* mov eax,[0x59FE04]; mov ebx,[eax] (7 bytes) */
#define MIX_WALK_NEXT            0x00434D28u
#define MIX_WALK_END             0x00434DA5u

static void mixer_fix(void)
{
    static const BYTE orig[7] = { 0xA1, 0x04, 0xFE, 0x59, 0x00, 0x8B, 0x18 };
    BYTE *stub, jmp[7];
    int p = 0;
    if (!code_matches(MIX_WALK, orig, sizeof(orig))) { log_msg("mixer: unexpected code at %08lX, not patched", (unsigned long)MIX_WALK); return; }
    if (!(stub = (BYTE *)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE))) return;
    memcpy(stub, orig, 5); p = 5;                       /* mov eax, [0x59FE04] */
    stub[p++] = 0x85; stub[p++] = 0xC0;                 /* test eax, eax */
    stub[p++] = 0x0F; stub[p++] = 0x84;                 /* jz MIX_WALK_END */
    *(DWORD *)(stub + p) = MIX_WALK_END - ((DWORD)stub + p + 4); p += 4;
    stub[p++] = 0x8B; stub[p++] = 0x18;                 /* mov ebx, [eax] */
    stub[p++] = 0xE9;                                   /* jmp MIX_WALK_NEXT */
    *(DWORD *)(stub + p) = MIX_WALK_NEXT - ((DWORD)stub + p + 4); p += 4;
    jmp[0] = 0xE9;
    *(DWORD *)(jmp + 1) = (DWORD)stub - (MIX_WALK + 5);
    jmp[5] = jmp[6] = 0x90;
    patch_code(MIX_WALK, jmp, sizeof(jmp));
    log_msg("mixer: finished-voice walk stops at the list's end (timer thread race)");
}

/* ---- 16 mixed voices (always on) --------------------------------------------------------------
   The software mixer has 16 voice slots (g_MixerVoiceSlots) but mixes only the first
   g_mixerMaxVoices (0x5A5570) voices of its active list: 8, or 4 with "Use 4 channels only".
   Mixer_LinkVoice puts new voices at the front, so with more than 8 sounds playing the oldest ones
   (usually the ambient loops and the helicopter loop) are neither mixed nor advanced: they go
   silent mid-sound and carry on later. Mixer_Init (0x4342A0) now starts the limit at 16 and
   Mixer_SetMaxVoices (0x4343C0, which refused anything above 8) always sets 16. Nothing else is
   sized by the limit: every per-voice table has 16 entries, one per slot. */
#define MIX_INIT_MAXVOICES       0x004342A0u   /* mov dword [0x5A5570], 8 (10 bytes) */
#define MIX_SETMAX_CLAMP         0x004343CCu   /* mov eax,[esp+4]; cmp eax,8; ja +5 (9 bytes) */

static void voices_fix(void)
{
    static const BYTE origInit[10] = { 0xC7, 0x05, 0x70, 0x55, 0x5A, 0x00, 0x08, 0x00, 0x00, 0x00 };
    static const BYTE origClamp[9] = { 0x8B, 0x44, 0x24, 0x04, 0x83, 0xF8, 0x08, 0x77, 0x05 };
    static const BYTE newClamp[9]  = { 0xB8, 0x10, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90, 0x90 };  /* mov eax, 16 */
    BYTE sixteen = 16;
    if (!code_matches(MIX_INIT_MAXVOICES, origInit, sizeof(origInit)) ||
        !code_matches(MIX_SETMAX_CLAMP, origClamp, sizeof(origClamp))) {
        log_msg("voices: unexpected code at %08lX/%08lX, not patched", (unsigned long)MIX_INIT_MAXVOICES, (unsigned long)MIX_SETMAX_CLAMP);
        return;
    }
    patch_code(MIX_INIT_MAXVOICES + 6, &sixteen, 1);
    patch_code(MIX_SETMAX_CLAMP, newClamp, sizeof(newClamp));
    log_msg("voices: the mixer mixes all 16 voice slots instead of 8");
}

/* ---- DirectSound mixing on Windows Vista and later (always on) -------------------------------
   The sound driver has two backends: its own software mixer (Mixer_*, one streaming DirectSound
   buffer) and DirectSound mixing (DSHw_*, one static DirectSound buffer per sample, mixed by
   DirectSound itself, 32 voices). SndDrv_Init (0x437F80, called as (hwnd, 2, 22050)) takes the
   second when GetCaps reports an emulated driver (DSCAPS_EMULDRIVER, test at 0x438054), which in
   1997 meant DirectSound running on top of the wave driver. Since Vista every DirectSound device is
   emulated that way by the Windows audio engine, but the flag is no longer set, so the game always
   picks its own mixer, with its 8 mixed voices and hard clipping. On Windows 6.0 and later the
   loader counts the device as emulated (the jz at 0x438057 is NOPed for the call). Afterwards it
   creates one buffer the way DSHw_LoadSample does; if that, or SndDrv_Init itself, fails, the
   driver is shut down and started again with the original test: the software mixer. GetVersion
   is used on purpose, so XP compatibility mode keeps XP's choice. On XP the original test stands. */
#define FN_SNDDRV_INIT           0x00437F80u   /* int __cdecl (HWND, UINT flags, UINT rate) */
#define FN_SNDDRV_SHUTDOWN       0x004387D0u
#define SND_EMUL_JZ              0x00438057u   /* jz +6 past "or ebx, 0x8C" (2 bytes) */
#define G_PDIRECTSOUND           ((LPDIRECTSOUND volatile *)0x00466B58u)
#define G_BSNDUSEHWBUFFERS       ((volatile BYTE *)0x00466B64u)
#define G_SNDCHANNELS            ((volatile WORD *)0x005A53E2u)   /* g_SndWaveFormat.nChannels */

typedef int (__cdecl *SndDrvInit_t)(HWND, UINT, UINT);
static SndDrvInit_t oSndDrvInit;

/* one buffer as DSHw_LoadSample creates them: mono 16-bit 22050 Hz, static, frequency, volume
   (and pan when the primary buffer is stereo) */
static int ds_probe(void)
{
    LPDIRECTSOUND ds = *G_PDIRECTSOUND;
    LPDIRECTSOUNDBUFFER b = NULL;
    WAVEFORMATEX wf;
    DSBUFFERDESC d;
    HRESULT hr;
    if (!ds) return 0;
    memset(&wf, 0, sizeof(wf));
    wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 1; wf.nSamplesPerSec = 22050;
    wf.wBitsPerSample = 16; wf.nBlockAlign = 2; wf.nAvgBytesPerSec = 44100;
    memset(&d, 0, sizeof(d));
    d.dwSize = 20;                                      /* the DirectX 3 size the game uses */
    d.dwFlags = *G_SNDCHANNELS == 2 ? 0xE2 : 0xA2;
    d.dwBufferBytes = 4410;
    d.lpwfxFormat = &wf;
    hr = IDirectSound_CreateSoundBuffer(ds, &d, &b, NULL);
    if (FAILED(hr) || !b) { log_msg("sound: DirectSound buffer test failed (%08lX)", (unsigned long)hr); return 0; }
    IDirectSoundBuffer_Release(b);
    return 1;
}

static int __cdecl hkSndDrvInit(HWND hwnd, UINT flags, UINT rate)
{
    static const BYTE jz[2] = { 0x74, 0x06 }, nops[2] = { 0x90, 0x90 };
    int r;
    patch_code(SND_EMUL_JZ, nops, sizeof(nops));
    r = oSndDrvInit(hwnd, flags, rate);
    patch_code(SND_EMUL_JZ, jz, sizeof(jz));
    if (r == 0 && *G_BSNDUSEHWBUFFERS && ds_probe()) {
        log_msg("sound: DirectSound mixing (Windows %lu.%lu)", (unsigned long)LOBYTE(GetVersion()), (unsigned long)HIBYTE(LOWORD(GetVersion())));
        return 0;
    }
    if (r == 0) ((VoidFn_t)FN_SNDDRV_SHUTDOWN)();
    r = oSndDrvInit(hwnd, flags, rate);
    log_msg("sound: %s", r == 0 ? (*G_BSNDUSEHWBUFFERS ? "DirectSound mixing (emulated driver)" : "software mixer (DirectSound mixing unavailable)") : "sound driver failed to start");
    return r;
}

/* DSHw_TimerService (0x435A10) frees a voice's buffer instance unless GetStatus returns exactly 1
   (playing) or 4 (looping). A playing loop is 5, so every looping sound (ambience, helicopter) is
   rewound and its buffer marked free after one tick while it keeps playing; a second start of the
   same sample could then take the buffer and end the loop. "Still playing" becomes bit 0 set:
   cmp ecx,4 / jz / cmp ecx,1 / jz -> test cl,1 / jnz (same target, 0x435AD6). DSHw_StopVoice
   frees a stopped loop's instance itself. */
#define DSHW_STATUS_TEST         0x00435A68u

/* DSHw_FreeSample (0x435460, level changes) releases a sample's buffers but clears a voice only when
   GetStatus is exactly 1 or 4. A sound that ended since the last timer tick (0) or a playing loop (5)
   keeps its voice marked active with its buffer now NULL, and DSHw_TimerService faults on it at
   0x435A5A. The sample's voices are cleared first, with the timer callback held off through its own
   re-entrancy guard, so it can't be halfway through a buffer that is being released. */
#define FN_DSHW_FREESAMPLE       0x00435460u
#define G_SNDTIMERBUSY           ((volatile LONG *)0x00466B6Cu)
#define G_DSHWVOICES             ((volatile DWORD *)0x005A3138u)   /* {flags, sample, instance} */
#define DSHW_VOICES              32
static int (__cdecl *oDshwFreeSample)(int);

static int __cdecl hkDshwFreeSample(int sample)
{
    int i, r, tries = 0, held;
    while (!(held = !InterlockedCompareExchange(G_SNDTIMERBUSY, 1, 0)) && tries++ < 200) Sleep(1);
    for (i = 0; i < DSHW_VOICES; i++)
        if (G_DSHWVOICES[i * 3] && (int)G_DSHWVOICES[i * 3 + 1] == sample) G_DSHWVOICES[i * 3] = 0;
    r = oDshwFreeSample(sample);
    if (held) *G_SNDTIMERBUSY = 0;
    return r;
}

static void dshw_init(void)
{
    static const BYTE origTest[10] = { 0x83, 0xF9, 0x04, 0x74, 0x69, 0x83, 0xF9, 0x01, 0x74, 0x64 };
    static const BYTE newTest[10]  = { 0xF6, 0xC1, 0x01, 0x75, 0x69, 0x90, 0x90, 0x90, 0x90, 0x90 };
    static const BYTE origJz[2] = { 0x74, 0x06 };
    if (code_matches(DSHW_STATUS_TEST, origTest, sizeof(origTest)))
        patch_code(DSHW_STATUS_TEST, newTest, sizeof(newTest));
    else
        log_msg("dshw: unexpected code at %08lX, loop status not patched", (unsigned long)DSHW_STATUS_TEST);
    mh_hook(FN_DSHW_FREESAMPLE, hkDshwFreeSample, (void **)&oDshwFreeSample, "DSHw_FreeSample");
    if (LOBYTE(GetVersion()) < 6) return;             /* XP and older: the game's own test */
    if (!code_matches(SND_EMUL_JZ, origJz, sizeof(origJz))) {
        log_msg("sound: unexpected code at %08lX, backend choice not patched", (unsigned long)SND_EMUL_JZ);
        return;
    }
    mh_hook(FN_SNDDRV_INIT, hkSndDrvInit, (void **)&oSndDrvInit, "SndDrv_Init");
}

/* ---- Hard-right pan in DirectSound-buffer mode (always on) ------------------------------------
   The DirectSound-buffer backend (DSHw_*, used instead of the mixer when the sound card reports an
   emulated driver) pans with a 31-entry table that DSHw_Init (0x435100) fills at 0x5A02B0, indices
   0..30. Index 30 (0x5A0328) is also where every DSHw_* function stores the last DirectSound
   result, so after the first call it holds DS_OK = 0, which is centre: sounds panned hard right,
   such as the right channel of every ambient loop, played in the middle. Nothing reads that result,
   so its 30 stores now go to a dummy variable here and the pan entry keeps its value. */
#define DSHW_PAN_RIGHT           0x005A0328u

static DWORD g_dshwResult;                    /* where DSHw_* now store their DirectSound results */

static void pan_fix(void)
{
    static const DWORD kStores[] = {          /* mov [0x5A0328], eax */
        0x435352, 0x4353C5, 0x435426, 0x43543E, 0x4354CC, 0x4354E2, 0x435510, 0x435602, 0x43561F, 0x43562F,
        0x435648, 0x435665, 0x435685, 0x435696, 0x4357B9, 0x4357D6, 0x4357E6, 0x4357FF, 0x43581E, 0x43583D,
        0x435859, 0x435925, 0x435930, 0x43598E, 0x4359FF, 0x435A63, 0x435AAF, 0x435B3E, 0x435B54, 0x435B7F,
    };
    static const BYTE orig[5] = { 0xA3, 0x28, 0x03, 0x5A, 0x00 };
    DWORD sink = (DWORD)&g_dshwResult;
    int i;
    for (i = 0; i < (int)(sizeof(kStores) / sizeof(kStores[0])); i++)
        if (!code_matches(kStores[i], orig, sizeof(orig))) {
            log_msg("pan: unexpected code at %08lX, not patched", (unsigned long)kStores[i]);
            return;
        }
    for (i = 0; i < (int)(sizeof(kStores) / sizeof(kStores[0])); i++)
        patch_code(kStores[i] + 1, &sink, sizeof(sink));
    log_msg("pan: DirectSound results no longer overwrite the hard-right pan entry (%08lX)", (unsigned long)DSHW_PAN_RIGHT);
}

/* ---- Local CD music ------------------------------------------------------------------------
   The game's music is CD audio played through MCI by track number (CD_PlayTrack 0x40DFF0:
   "set cdaudio time format tmsf" + "play cdaudio from t to t+1 notify"). When the track ends, MCI
   posts MM_MCINOTIFY / MCI_NOTIFY_SUCCESSFUL and Lomax_WndProc calls CD_RestartTrack, which loops
   it. CD_Pause saves "status cdaudio position" (tt:mm:ss:ff) and stops; CD_Resume plays from that
   position. Volume goes through CDAudio_SetVolume(0..255) (0x438460: auxSetVolume on the CD line,
   log curve; Game_UpdateFade fades it with the screen).

   The disc is never used: every "cdaudio" command is answered here instead of by MCI. With
   MUSIC\TRACKnn.* files (nn = disc track, as extracted by LOADER.EXE) a thread plays the track file through
   waveOut. The running mod's MODS\<name>\MUSIC folder is searched after MUSIC, so a mod can add tracks under
   numbers the disc doesn't have (its Music objects ask for them like any other track). WAV (44.1 kHz 16-bit stereo) is read directly; other formats (FLAC, MP3, and OGG when
   its codec is installed) go through Media Foundation, loaded only when needed. At the end of a
   track the same MM_MCINOTIFY is posted, so the game's own looping, pause and resume work as with
   the disc. CDAudio_SetVolume becomes a software gain on the samples. Without the files there is no
   music: CD_VerifyLomaxDisc reports no CD audio, so the game's CD_* functions do nothing. */
#define G_CFGCDVOLUME            ((volatile int *)0x00464574u)   /* 0..100 */
#define MUS_RATE                 44100
#define MUS_BUFS                 4
#define MUS_FRAMES               4410         /* 0.1 s per buffer */
#define CD_FRAMES_PER_SECTOR     588          /* 1/75 s */

typedef void (__cdecl *SetVolume_t)(int);
static SetVolume_t oCDSetVolume;
static int  g_musLocal;                       /* local music active */
/* folders searched for TRACKnn.* in order: MUSIC, then the running mod's MODS\<name>\MUSIC. A mod adds
   tracks under numbers of its own (Lomax's disc has 2-26), which its levels' Music objects play. */
#define MUS_MAX_DIRS 16
static char g_musDirs[MUS_MAX_DIRS][MAX_PATH];
static int  g_musNDirs;

/* decoder: WAV directly, anything else through Media Foundation (IMFSourceReader -> PCM) */
typedef struct {
    HANDLE f; DWORD dataStart, dataBytes, readBytes;          /* WAV */
    IMFSourceReader *rd; BYTE *pend; DWORD pendLen, pendOff;  /* Media Foundation */
} MusDec;

typedef HRESULT (WINAPI *MFStartup_t)(ULONG, DWORD);
typedef HRESULT (WINAPI *MFCreateSourceReaderFromURL_t)(LPCWSTR, IMFAttributes *, IMFSourceReader **);
typedef HRESULT (WINAPI *MFCreateMediaType_t)(IMFMediaType **);
static MFCreateSourceReaderFromURL_t pMFCreateSourceReaderFromURL;
static MFCreateMediaType_t pMFCreateMediaType;

static int mf_load(void)
{
    static int s_state;                       /* 0 untried, 1 ok, -1 unavailable */
    HMODULE plat, rw;
    MFStartup_t start;
    if (s_state) return s_state > 0;
    s_state = -1;
    plat = LoadLibraryA("mfplat.dll");
    rw = LoadLibraryA("mfreadwrite.dll");
    if (!plat || !rw) { log_msg("music: Media Foundation not available"); return 0; }
    start = (MFStartup_t)GetProcAddress(plat, "MFStartup");
    pMFCreateMediaType = (MFCreateMediaType_t)GetProcAddress(plat, "MFCreateMediaType");
    pMFCreateSourceReaderFromURL = (MFCreateSourceReaderFromURL_t)GetProcAddress(rw, "MFCreateSourceReaderFromURL");
    if (!start || !pMFCreateMediaType || !pMFCreateSourceReaderFromURL || FAILED(start(MF_VERSION, MFSTARTUP_LITE)))
        return 0;
    s_state = 1;
    return 1;
}

static void dec_close(MusDec *d)
{
    if (d->f && d->f != INVALID_HANDLE_VALUE) CloseHandle(d->f);
    if (d->rd) IMFSourceReader_Release(d->rd);
    free(d->pend);
    memset(d, 0, sizeof(*d));
}

static int wav_open(MusDec *d, const char *path)
{
    DWORD hdr[3], ck[2], got, pos = 12;
    WORD fmt[8];
    int fmtOk = 0;
    d->f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (d->f == INVALID_HANDLE_VALUE) return 0;
    if (!ReadFile(d->f, hdr, 12, &got, NULL) || got != 12 || hdr[0] != 0x46464952 || hdr[2] != 0x45564157)
        return 0;
    while (ReadFile(d->f, ck, 8, &got, NULL) && got == 8) {
        pos += 8;
        if (ck[0] == 0x20746D66 && ck[1] >= 16) {             /* "fmt " */
            ReadFile(d->f, fmt, 16, &got, NULL);
            fmtOk = fmt[0] == 1 && fmt[1] == 2 && (fmt[2] | (DWORD)fmt[3] << 16) == MUS_RATE && fmt[7] == 16;
        } else if (ck[0] == 0x61746164) {                      /* "data" */
            if (!fmtOk) { log_msg("music: %s is not 44.1 kHz 16-bit stereo PCM", path); return 0; }
            d->dataStart = pos;
            d->dataBytes = ck[1] & ~3u;
            return 1;
        }
        pos += (ck[1] + 1) & ~1u;
        SetFilePointer(d->f, pos, NULL, FILE_BEGIN);
    }
    return 0;
}

static int mf_open(MusDec *d, const char *path)
{
    WCHAR w[MAX_PATH];
    IMFMediaType *mt = NULL, *cur = NULL;
    UINT32 rate = 0, ch = 0, bits = 0;
    int ok = 0;
    if (!mf_load()) return 0;
    MultiByteToWideChar(CP_ACP, 0, path, -1, w, MAX_PATH);
    if (FAILED(pMFCreateSourceReaderFromURL(w, NULL, &d->rd))) { d->rd = NULL; return 0; }
    IMFSourceReader_SetStreamSelection(d->rd, MF_SOURCE_READER_ALL_STREAMS, FALSE);
    IMFSourceReader_SetStreamSelection(d->rd, MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    if (SUCCEEDED(pMFCreateMediaType(&mt))) {
        IMFMediaType_SetGUID(mt, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
        IMFMediaType_SetGUID(mt, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
        IMFMediaType_SetUINT32(mt, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        if (SUCCEEDED(IMFSourceReader_SetCurrentMediaType(d->rd, MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, mt)) &&
            SUCCEEDED(IMFSourceReader_GetCurrentMediaType(d->rd, MF_SOURCE_READER_FIRST_AUDIO_STREAM, &cur))) {
            IMFMediaType_GetUINT32(cur, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
            IMFMediaType_GetUINT32(cur, &MF_MT_AUDIO_NUM_CHANNELS, &ch);
            IMFMediaType_GetUINT32(cur, &MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
            ok = rate == MUS_RATE && ch == 2 && bits == 16;
            if (!ok) log_msg("music: %s decodes to %u Hz %u ch %u bit, needs 44100 Hz stereo 16-bit", path,
                             (unsigned)rate, (unsigned)ch, (unsigned)bits);
            IMFMediaType_Release(cur);
        }
        IMFMediaType_Release(mt);
    }
    return ok;
}

/* opens TRACKnn.* for disc track n, from the first music folder that has it */
static int dec_open(MusDec *d, int track)
{
    static const char *const exts[] = { "WAV", "FLAC", "MP3", "OGG", "M4A", "WMA" };
    char path[MAX_PATH];
    size_t i;
    int k;
    memset(d, 0, sizeof(*d));
    for (k = 0; k < g_musNDirs; k++)
        for (i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
            snprintf(path, sizeof(path), "%s\\TRACK%02d.%s", g_musDirs[k], track, exts[i]);
            if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) continue;
            if (i == 0 ? wav_open(d, path) : mf_open(d, path)) {
                log_msg("music: track %d is %s", track, path);
                return 1;
            }
            log_msg("music: could not open %s", path);
            dec_close(d);
        }
    return 0;
}

/* reads up to n frames; 0 = end of track */
static DWORD dec_read(MusDec *d, short *out, DWORD n)
{
    DWORD got = 0;
    if (d->f) {
        DWORD want = min(n * 4, d->dataBytes - d->readBytes);
        if (want) ReadFile(d->f, out, want, &got, NULL);
        d->readBytes += got;
        return got / 4;
    }
    while (got < n && d->rd) {
        if (d->pendOff < d->pendLen) {
            DWORD k = min(n - got, (d->pendLen - d->pendOff) / 4);
            memcpy(out + got * 2, d->pend + d->pendOff, k * 4);
            d->pendOff += k * 4;
            got += k;
        } else {
            DWORD flags = 0, len = 0;
            IMFSample *smp = NULL;
            IMFMediaBuffer *buf = NULL;
            BYTE *p;
            if (FAILED(IMFSourceReader_ReadSample(d->rd, MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, NULL, &flags, NULL, &smp)) ||
                (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
                if (smp) IMFSample_Release(smp);
                break;
            }
            if (!smp) continue;
            if (SUCCEEDED(IMFSample_ConvertToContiguousBuffer(smp, &buf)) &&
                SUCCEEDED(IMFMediaBuffer_Lock(buf, &p, NULL, &len))) {
                BYTE *np = realloc(d->pend, len);
                if (np) { d->pend = np; memcpy(np, p, len); d->pendLen = len; d->pendOff = 0; }
                IMFMediaBuffer_Unlock(buf);
            }
            if (buf) IMFMediaBuffer_Release(buf);
            IMFSample_Release(smp);
        }
    }
    return got;
}

static void dec_seek(MusDec *d, DWORD frame)
{
    if (d->f) {
        d->readBytes = min(frame * 4, d->dataBytes);
        SetFilePointer(d->f, d->dataStart + d->readBytes, NULL, FILE_BEGIN);
    } else if (d->rd && frame) {
        static short skip[MUS_FRAMES * 2];                    /* decode from the start up to it */
        while (frame) {
            DWORD k = dec_read(d, skip, min(frame, (DWORD)MUS_FRAMES));
            if (!k) break;
            frame -= k;
        }
    }
}

/* player thread; requests come from the game thread (mciSendStringA) */
enum { MUS_NONE, MUS_PLAY, MUS_STOP };
static CRITICAL_SECTION g_musLock;
static HANDLE g_musEvent;                     /* request or waveOut buffer done */
static int    g_musReq, g_musReqTrack;
static DWORD  g_musReqFrame;
static HWND   g_musReqNotify;
static int    g_musTrack = -1;                /* disc track playing (or last played) */
static DWORD  g_musBase;                      /* track frame at waveOut position 0 */
static HWAVEOUT g_musOut;
static volatile LONG g_musGain = 65536;       /* Q16 */

static DWORD WINAPI music_thread(void *arg)
{
    static short pcm[MUS_BUFS][MUS_FRAMES * 2];
    WAVEHDR hdr[MUS_BUFS];
    WAVEFORMATEX wf = { WAVE_FORMAT_PCM, 2, MUS_RATE, MUS_RATE * 4, 4, 16, 0 };
    MusDec dec;
    HWND notify = NULL;
    int playing = 0, eof = 0, i;
    (void)arg;
    memset(&dec, 0, sizeof(dec));
    memset(hdr, 0, sizeof(hdr));
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (waveOutOpen(&g_musOut, WAVE_MAPPER, &wf, (DWORD_PTR)g_musEvent, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        log_msg("music: waveOutOpen failed");
        return 0;
    }
    for (i = 0; i < MUS_BUFS; i++) {
        hdr[i].lpData = (LPSTR)pcm[i];
        hdr[i].dwBufferLength = sizeof(pcm[i]);
        waveOutPrepareHeader(g_musOut, &hdr[i], sizeof(WAVEHDR));
        hdr[i].dwFlags |= WHDR_DONE;
    }
    for (;;) {
        int req, track = 0, busy = 0;
        DWORD frame = 0;
        WaitForSingleObject(g_musEvent, INFINITE);
        EnterCriticalSection(&g_musLock);
        req = g_musReq;
        g_musReq = MUS_NONE;
        if (req == MUS_PLAY) { track = g_musReqTrack; frame = g_musReqFrame; notify = g_musReqNotify; }
        LeaveCriticalSection(&g_musLock);
        if (req != MUS_NONE) {
            waveOutReset(g_musOut);                            /* returns all buffers, position 0 */
            dec_close(&dec);
            playing = eof = 0;
            if (req == MUS_PLAY) {
                if (dec_open(&dec, track)) {
                    dec_seek(&dec, frame);
                    log_msg("music: track %d from %lu.%02lu s", track, (unsigned long)(frame / MUS_RATE),
                            (unsigned long)(frame % MUS_RATE * 100 / MUS_RATE));
                    EnterCriticalSection(&g_musLock);
                    g_musTrack = track;
                    g_musBase = frame;
                    LeaveCriticalSection(&g_musLock);
                    playing = 1;
                } else {
                    log_msg("music: no file for track %d", track);
                }
            }
        }
        if (!playing) continue;
        for (i = 0; i < MUS_BUFS; i++) {
            DWORD n, k;
            LONG gain = g_musGain;
            if (!(hdr[i].dwFlags & WHDR_DONE)) { busy = 1; continue; }
            if (eof) continue;
            n = dec_read(&dec, pcm[i], MUS_FRAMES);
            if (!n) { eof = 1; continue; }
            if (gain != 65536)
                for (k = 0; k < n * 2; k++) pcm[i][k] = (short)((pcm[i][k] * gain) >> 16);
            hdr[i].dwBufferLength = n * 4;
            waveOutWrite(g_musOut, &hdr[i], sizeof(WAVEHDR));
            busy = 1;
        }
        if (eof && !busy) {                                    /* track played to the end */
            int stale;
            playing = 0;
            dec_close(&dec);
            EnterCriticalSection(&g_musLock);
            stale = g_musReq != MUS_NONE;
            LeaveCriticalSection(&g_musLock);
            if (notify && !stale) PostMessageA(notify, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, 1);
        }
    }
}

static void music_request(int req, int track, DWORD frame, HWND notify)
{
    static HANDLE s_thread;
    if (!s_thread) s_thread = CreateThread(NULL, 0, music_thread, NULL, 0, NULL);
    EnterCriticalSection(&g_musLock);
    g_musReq = req;
    g_musReqTrack = track;
    g_musReqFrame = frame;
    g_musReqNotify = notify;
    LeaveCriticalSection(&g_musLock);
    SetEvent(g_musEvent);
}

/* the game's "cdaudio" MCI strings, answered for the local player */
static MCIERROR music_mci(LPCSTR cmd, LPSTR ret, UINT len, HWND cb)
{
    char from[32];
    int to;
    if (!_strnicmp(cmd, "play cdaudio from ", 18) && sscanf(cmd + 18, "%31s to %d", from, &to) == 2) {
        int t = atoi(from), mm = 0, ss = 0, ff = 0;
        sscanf(from, "%d:%d:%d:%d", &t, &mm, &ss, &ff);       /* "t" or "tt:mm:ss:ff" (tmsf) */
        music_request(MUS_PLAY, t, (DWORD)((mm * 60 + ss) * 75 + ff) * CD_FRAMES_PER_SECTOR,
                      strstr(cmd, " notify") ? cb : NULL);
    } else if (!_strnicmp(cmd, "stop cdaudio", 12) || !_strnicmp(cmd, "close cdaudio", 13)) {
        music_request(MUS_STOP, 0, 0, NULL);
    } else if (!_stricmp(cmd, "status cdaudio position") && ret && len) {
        MMTIME mt;
        DWORD frame, sec;
        int track;
        mt.wType = TIME_SAMPLES;
        EnterCriticalSection(&g_musLock);
        track = g_musTrack < 0 ? 1 : g_musTrack;
        frame = g_musBase;
        if (g_musOut && waveOutGetPosition(g_musOut, &mt, sizeof(mt)) == MMSYSERR_NOERROR && mt.wType == TIME_SAMPLES)
            frame += mt.u.sample;
        LeaveCriticalSection(&g_musLock);
        sec = frame / CD_FRAMES_PER_SECTOR;
        snprintf(ret, len, "%02d:%02lu:%02lu:%02lu", track, (unsigned long)(sec / 4500),
                 (unsigned long)(sec / 75 % 60), (unsigned long)(sec % 75));
    } else if (ret && len) {
        ret[0] = 0;
    }
    return 0;
}

static void __cdecl hkCDSetVolume(int vol)
{
    /* CDAudio_SetVolume's curve: aux volume = 65535 * log2(1 + vol/255) */
    g_musGain = (LONG)(65536.0 * log((vol & 0xFF) / 255.0 + 1.0) / log(2.0));
}

/* adds dir to the music folders if it has TRACKnn files */
static void music_add_dir(const char *dir)
{
    char pat[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    snprintf(pat, sizeof(pat), "%s\\TRACK*.*", dir);
    if ((h = FindFirstFileA(pat, &fd)) == INVALID_HANDLE_VALUE) return;
    FindClose(h);
    if (g_musNDirs == MUS_MAX_DIRS) { log_msg("music: more than %d music folders, %s left out", MUS_MAX_DIRS, dir); return; }
    snprintf(g_musDirs[g_musNDirs++], MAX_PATH, "%s", dir);
    log_msg("music: CD tracks from %s", dir);
}

static void music_init(void)
{
    char dir[MAX_PATH];
    snprintf(dir, sizeof(dir), "%s\\MUSIC", g_gameDir);
    music_add_dir(dir);
    if (g_modRunning[0]) {
        snprintf(dir, sizeof(dir), "%s\\MODS\\%s\\MUSIC", g_gameDir, g_modRunning);
        music_add_dir(dir);
    }
    if (!g_musNDirs) {
        log_msg("music: no TRACKnn files in MUSIC or MODS\\<name>\\MUSIC, no music (the disc is not used)");
        return;
    }
    InitializeCriticalSection(&g_musLock);
    g_musEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
    hkCDSetVolume(*G_CFGCDVOLUME * 255 / 100);
    g_musLocal = 1;
}

/* ---- CD check: the disc is never needed ------------------------------------------------- */
static MCIERROR WINAPI hkMciSendStringA(LPCSTR cmd, LPSTR ret, UINT len, HWND cb)
{
    if (cmd && strstr(cmd, "cdaudio")) {
        if (g_musLocal) return music_mci(cmd, ret, len, cb);
        if (ret && len) ret[0] = 0;              /* no music files: no CD audio either */
        return 0;
    }
    return oMciSendStringA(cmd, ret, len, cb);
}

static int __cdecl hkVerifyDisc(void)
{
    *G_BCDAUDIOAVAILABLE = g_musLocal;           /* 0: all CD_* functions do nothing */
    log_msg("CD: disc not needed, %s", g_musLocal ? "music from the music folders" : "no music files: no music");
    return 1;
}

void audio_init(void)
{
    mixer_fix();
    voices_fix();
    dshw_init();
    pan_fix();
    music_init();
    hook_iat("WINMM.dll", "mciSendStringA", hkMciSendStringA, (void **)&oMciSendStringA);
    mh_hook(FN_CD_VERIFY_LOMAX_DISC, hkVerifyDisc, (void **)&oVerifyDisc, "CD_VerifyLomaxDisc");
    if (g_musLocal)
        mh_hook(FN_CDAUDIO_SET_VOLUME, hkCDSetVolume, (void **)&oCDSetVolume, "CDAudio_SetVolume");
}
