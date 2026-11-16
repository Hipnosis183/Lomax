/*
 * LOADER.EXE - Lomax+: starts LOMAX.EXE with LOADER.DLL injected.
 *
 * Both files sit next to this EXE (the game folder). The options are set in the game's
 * Properties dialog and kept in the registry by the DLL.
 * 0. Without LOMAX.EXE, installs the game from a .cue/.bin disc image found the same way as below
 *    (install_game). With no MUSIC\TRACKnn.* files yet, extracts the CD audio tracks from
 *    a .cue/.bin disc image in the game folder (or one folder below) to MUSIC\TRACKnn.WAV. Without
 *    a .cue, or when a file it names is missing, nothing happens.
 * 1. Creates the game process suspended and lets its main thread run up to the EXE's entry point
 *    (run_to_entry), so the process and its DLLs are initialised on the game's own thread.
 * 2. Loads the DLL in the game with a remote LoadLibraryA thread, then runs its exported
 *    LomaxFix_Init in a second remote thread, still before the game's own code runs.
 * 3. Resumes the game.
 */
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(HANDLE proc, const char *fmt, ...)
{
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (proc) TerminateProcess(proc, 1);
    MessageBoxA(NULL, msg, "Lomax+", MB_ICONERROR);
    ExitProcess(1);
}

static DWORD run_remote(HANDLE proc, LPTHREAD_START_ROUTINE fn, void *arg)
{
    DWORD code = 0;
    HANDLE t = CreateRemoteThread(proc, NULL, 0, fn, arg, 0, NULL);
    if (!t) return 0;
    WaitForSingleObject(t, INFINITE);
    GetExitCodeThread(t, &code);
    CloseHandle(t);
    return code;
}

/* Let the game's own thread start the process: Windows initialises the process and every DLL it
   imports (DllMain) on the first thread that runs. A remote thread created first would do it
   instead, and on Windows XP the common controls then register their window classes where the
   game's thread can't use them (Properties: no tab control, the sheet doesn't open). So a jmp $ is
   put on the EXE's entry point, the main thread runs until it sits there, and is suspended again
   with the original bytes back: the DLLs are initialised, the game's own code hasn't run. */
static void run_to_entry(PROCESS_INFORMATION *pi, const char *exe)
{
    static const BYTE loop[2] = { 0xEB, 0xFE };
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS32 nt;
    BYTE orig[2];
    CONTEXT ctx;
    DWORD entry, n, t;
    FILE *f = fopen(exe, "rb");
    if (!f || fread(&dos, sizeof(dos), 1, f) != 1 || fseek(f, dos.e_lfanew, SEEK_SET) ||
        fread(&nt, sizeof(nt), 1, f) != 1) {
        if (f) fclose(f);
        fail(pi->hProcess, "Could not read the headers of\n%s", exe);
    }
    fclose(f);
    entry = nt.OptionalHeader.ImageBase + nt.OptionalHeader.AddressOfEntryPoint;
    if (!ReadProcessMemory(pi->hProcess, (void *)entry, orig, 2, &n) ||
        !WriteProcessMemory(pi->hProcess, (void *)entry, loop, 2, &n))
        fail(pi->hProcess, "Could not prepare the game's entry point (error %lu)", GetLastError());
    FlushInstructionCache(pi->hProcess, (void *)entry, 2);
    ResumeThread(pi->hThread);
    for (t = 0; ; t += 10) {
        Sleep(10);
        ctx.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(pi->hThread, &ctx) && ctx.Eip == entry) break;
        if (t > 15000) fail(pi->hProcess, "The game did not reach its entry point.");
    }
    SuspendThread(pi->hThread);
    if (!WriteProcessMemory(pi->hProcess, (void *)entry, orig, 2, &n))
        fail(pi->hProcess, "Could not restore the game's entry point (error %lu)", GetLastError());
    FlushInstructionCache(pi->hProcess, (void *)entry, 2);
}

/* ---- CD audio extraction ---------------------------------------------------------------------
   The disc's MUSIC\*.DAT entries point at the audio tracks but count 2048 bytes per sector, so a
   copy made through the file system lacks 304 of every 2352 bytes. The tracks are read from the
   raw image instead: 2352 bytes per sector, 44.1 kHz 16-bit stereo. A track runs from its INDEX 01
   to the next track's INDEX 01 in the same file (what "play cdaudio from n to n+1" plays), or to
   the end of its file. */
#define CUE_MAX_TRACKS 99
typedef struct { int num, audio, secSize; char file[MAX_PATH]; DWORD start; } CueTrack;

static int parse_cue(const char *cuePath, CueTrack *t)
{
    char line[512], dir[MAX_PATH], file[MAX_PATH] = "";
    int n = 0;
    FILE *f = fopen(cuePath, "r");
    if (!f) return 0;
    snprintf(dir, sizeof(dir), "%s", cuePath);
    *(strrchr(dir, '\\') + 1) = 0;
    while (fgets(line, sizeof(line), f)) {
        char *p = line, *q;
        int num, mm, ss, ff;
        while (*p == ' ' || *p == '\t') p++;
        if (!_strnicmp(p, "FILE ", 5)) {
            p += 5;
            if (*p == '"') { p++; q = strchr(p, '"'); } else q = strchr(p, ' ');
            if (q) *q = 0;
            snprintf(file, sizeof(file), "%s%s", strchr(p, ':') ? "" : dir, p);
        } else if (!_strnicmp(p, "TRACK ", 6) && n < CUE_MAX_TRACKS && sscanf(p + 6, "%d", &num) == 1) {
            t[n].num = num;
            t[n].audio = strstr(p, "AUDIO") != NULL;
            q = strchr(p, '/');                                /* MODE1/2048, MODE2/2352, ... */
            t[n].secSize = q ? atoi(q + 1) : 2352;
            snprintf(t[n].file, MAX_PATH, "%s", file);
            t[n].start = (DWORD)-1;
            n++;
        } else if (!_strnicmp(p, "INDEX 01 ", 9) && n && sscanf(p + 9, "%d:%d:%d", &mm, &ss, &ff) == 3) {
            t[n - 1].start = (DWORD)((mm * 60 + ss) * 75 + ff);
        }
    }
    fclose(f);
    return n;
}

static HWND progress_window(void)
{
    HWND w = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, "STATIC", "",
                             WS_POPUP | WS_BORDER | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
                             (GetSystemMetrics(SM_CXSCREEN) - 420) / 2, (GetSystemMetrics(SM_CYSCREEN) - 60) / 2,
                             420, 60, NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (w) SendMessageA(w, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), 0);
    return w;
}

static void progress_text(HWND w, const char *fmt, ...)
{
    char s[256];
    MSG m;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s, sizeof(s), fmt, ap);
    va_end(ap);
    if (w) SetWindowTextA(w, s);
    while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&m);
}

static int extract_track(const CueTrack *t, DWORD end, const char *out)
{
    static BYTE buf[2352 * 75];
    DWORD bytes = (end - t->start) * 2352, done = 0, hdr[11];
    LARGE_INTEGER pos;
    HANDLE in = CreateFileA(t->file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL), o;
    if (in == INVALID_HANDLE_VALUE) return 0;
    o = CreateFileA(out, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (o == INVALID_HANDLE_VALUE) { CloseHandle(in); return 0; }
    hdr[0] = 0x46464952; hdr[1] = 36 + bytes; hdr[2] = 0x45564157;        /* RIFF size WAVE */
    hdr[3] = 0x20746D66; hdr[4] = 16; hdr[5] = 0x00020001;                 /* fmt, PCM, 2 ch */
    hdr[6] = 44100; hdr[7] = 44100 * 4; hdr[8] = 0x00100004;               /* rate, bytes/s, align 4, 16 bit */
    hdr[9] = 0x61746164; hdr[10] = bytes;                                  /* data */
    WriteFile(o, hdr, sizeof(hdr), &done, NULL);
    pos.QuadPart = (LONGLONG)t->start * 2352;
    SetFilePointerEx(in, pos, NULL, FILE_BEGIN);
    for (done = 0; done < bytes; ) {
        DWORD want = min(bytes - done, (DWORD)sizeof(buf)), got = 0, put;
        if (!ReadFile(in, buf, want, &got, NULL) || !got) break;
        WriteFile(o, buf, got, &put, NULL);
        done += got;
    }
    CloseHandle(in);
    CloseHandle(o);
    if (done != bytes) { DeleteFileA(out); return 0; }
    return 1;
}

static int find_cue(const char *dir, char *cue)
{
    char pat[MAX_PATH], sub[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    snprintf(pat, sizeof(pat), "%s\\*.cue", dir);
    if ((h = FindFirstFileA(pat, &fd)) != INVALID_HANDLE_VALUE) {
        snprintf(cue, MAX_PATH, "%s\\%s", dir, fd.cFileName);
        FindClose(h);
        return 1;
    }
    snprintf(pat, sizeof(pat), "%s\\*", dir);
    if ((h = FindFirstFileA(pat, &fd)) == INVALID_HANDLE_VALUE) return 0;
    do {
        HANDLE h2;
        WIN32_FIND_DATAA fd2;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
        snprintf(sub, sizeof(sub), "%s\\%s\\*.cue", dir, fd.cFileName);
        if ((h2 = FindFirstFileA(sub, &fd2)) != INVALID_HANDLE_VALUE) {
            snprintf(cue, MAX_PATH, "%s\\%s\\%s", dir, fd.cFileName, fd2.cFileName);
            FindClose(h2);
            FindClose(h);
            return 1;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return 0;
}

static void extract_music(const char *dir)
{
    static CueTrack t[CUE_MAX_TRACKS];
    char pat[MAX_PATH], cue[MAX_PATH], out[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    HWND w;
    int n, i, audio = 0, ok = 0;
    snprintf(pat, sizeof(pat), "%s\\MUSIC\\TRACK??.*", dir);
    if ((h = FindFirstFileA(pat, &fd)) != INVALID_HANDLE_VALUE) { FindClose(h); return; }
    if (!find_cue(dir, cue) || (n = parse_cue(cue, t)) == 0) return;
    for (i = 0; i < n; i++) {
        if (!t[i].audio) continue;
        if (GetFileAttributesA(t[i].file) == INVALID_FILE_ATTRIBUTES) return;   /* image incomplete: skip */
        audio++;
    }
    if (!audio) return;
    snprintf(out, sizeof(out), "%s\\MUSIC", dir);
    CreateDirectoryA(out, NULL);
    w = progress_window();
    for (i = 0; i < n; i++) {
        LARGE_INTEGER size;
        DWORD end;
        HANDLE f;
        if (!t[i].audio || t[i].start == (DWORD)-1) continue;
        if (i + 1 < n && !_stricmp(t[i + 1].file, t[i].file) && t[i + 1].start != (DWORD)-1) {
            end = t[i + 1].start;
        } else {
            f = CreateFileA(t[i].file, 0, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            if (f == INVALID_HANDLE_VALUE || !GetFileSizeEx(f, &size)) { if (f != INVALID_HANDLE_VALUE) CloseHandle(f); continue; }
            CloseHandle(f);
            end = (DWORD)(size.QuadPart / 2352);
        }
        if (end <= t[i].start) continue;
        progress_text(w, "Lomax+: extracting CD music, track %d of %d...", ++ok, audio);
        snprintf(out, sizeof(out), "%s\\MUSIC\\TRACK%02d.WAV", dir, t[i].num);
        if (!extract_track(&t[i], end, out)) ok--;
    }
    if (w) DestroyWindow(w);
    if (ok < audio)
        MessageBoxA(NULL, "Some CD music tracks could not be extracted from the disc image.", "Lomax+",
                    MB_ICONWARNING);
}

/* ---- Install from the disc image -------------------------------------------------------------
   Without LOMAX.EXE next to the loader, the game is installed from a .cue/.bin image (same search
   as the music): its data track holds an ISO 9660 file system, read sector by sector (2048 user
   bytes at offset 16 of a raw MODE1/2352 sector, 24 for MODE2/2352 form 1, 0 for /2048 images).
   Copied, as the original SETUP.EXE does: LOMAX.EXE, GAME.TXT, the help files and the AMBIENT, DEMOS,
   JINGLES, SFX and WORLDS folders. Left out: DIRECTX (the DirectX 3 setup), MUSIC (entries pointing
   at the audio tracks, which extract_music takes from the image), SETUP.EXE, REMLOMAX.EXE, the
   DSETUP DLLs, AUTORUN.INF, LOMAX.ICO and BLANK.BIN (padding). The game needs nothing else from the
   installer: its registry settings (HKCU\Software\Psygnosis\Lomax) all have defaults, and it opens
   its data relative to its folder. LOMAX.EXE is written last, so an interrupted install runs again. */
static const char *const kInstallItems[] = {
    "GAME.TXT", "LOMAX.CNT", "LOMAX.GID", "LOMAX.HLP", "AMBIENT", "DEMOS", "JINGLES", "SFX", "WORLDS",
    "LOMAX.EXE",
};
#define ISO_CHUNK 32                                 /* sectors per read */

typedef struct {
    HANDLE f;
    LONGLONG base;                                   /* byte offset of the track's first sector */
    int secSize, dataOff;                            /* raw sector size, user data offset in it */
} IsoImage;

typedef struct { DWORD lba, size; BYTE dir, date[7]; char name[64]; } IsoEntry;

/* reads n 2048-byte sectors from lba */
static int iso_read(IsoImage *im, DWORD lba, int n, BYTE *out)
{
    static BYTE raw[2352 * ISO_CHUNK];
    LARGE_INTEGER pos;
    DWORD got;
    int i;
    pos.QuadPart = im->base + (LONGLONG)lba * im->secSize;
    if (!SetFilePointerEx(im->f, pos, NULL, FILE_BEGIN) ||
        !ReadFile(im->f, raw, (DWORD)(n * im->secSize), &got, NULL) || got != (DWORD)(n * im->secSize))
        return 0;
    for (i = 0; i < n; i++) memmove(out + i * 2048, raw + i * im->secSize + im->dataOff, 2048);
    return 1;
}

/* directory records of the directory at lba (size bytes), without "." and ".." */
static int iso_list(IsoImage *im, DWORD lba, DWORD size, IsoEntry *e, int max)
{
    BYTE sec[2048];
    DWORD off;
    int n = 0;
    for (off = 0; off < size; off += 2048) {
        int p = 0;
        if (!iso_read(im, lba + off / 2048, 1, sec)) return -1;
        while (p + 33 < 2048 && sec[p]) {
            int len = sec[p], nl = sec[p + 32];
            char *q;
            if (len < 34 || p + len > 2048 || 33 + nl > len) break;
            if (!(nl == 1 && sec[p + 33] <= 1) && n < max) {
                IsoEntry *x = &e[n++];
                memcpy(&x->lba, sec + p + 2, 4);
                memcpy(&x->size, sec + p + 10, 4);
                memcpy(x->date, sec + p + 18, 7);
                x->dir = (sec[p + 25] & 2) != 0;
                if (nl > 63) nl = 63;
                memcpy(x->name, sec + p + 33, nl);
                x->name[nl] = 0;
                if ((q = strchr(x->name, ';'))) *q = 0;                  /* "LOMAX.EXE;1" */
                if (nl && x->name[strlen(x->name) - 1] == '.') x->name[strlen(x->name) - 1] = 0;
            }
            p += len;
        }
    }
    return n;
}

static void iso_file_time(HANDLE h, const BYTE *d)
{
    SYSTEMTIME st = { (WORD)(1900 + d[0]), d[1], 0, d[2], d[3], d[4], d[5], 0 };
    FILETIME ft;
    ULARGE_INTEGER u;
    if (!SystemTimeToFileTime(&st, &ft)) return;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    u.QuadPart -= (LONGLONG)(signed char)d[6] * 15 * 60 * 10000000;  /* GMT offset, 15 min units */
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    SetFileTime(h, &ft, NULL, &ft);
}

static int iso_copy_file(IsoImage *im, const IsoEntry *e, const char *out)
{
    static BYTE buf[2048 * ISO_CHUNK];
    DWORD left = e->size, lba = e->lba, put;
    HANDLE o = CreateFileA(out, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (o == INVALID_HANDLE_VALUE) return 0;
    while (left) {
        int n = (int)min((left + 2047) / 2048, (DWORD)ISO_CHUNK);
        DWORD bytes = min(left, (DWORD)n * 2048);
        if (!iso_read(im, lba, n, buf) || !WriteFile(o, buf, bytes, &put, NULL) || put != bytes) break;
        left -= bytes;
        lba += n;
    }
    iso_file_time(o, e->date);
    CloseHandle(o);
    if (left) { DeleteFileA(out); return 0; }
    return 1;
}

/* copies (or with w == NULL only counts) the files of an entry, recursing into folders */
static int iso_copy_tree(IsoImage *im, const IsoEntry *e, const char *out, HWND w, int *done, int total)
{
    IsoEntry *sub;
    char path[MAX_PATH];
    int n, i, ok = 1;
    if (!e->dir) {
        if (!w) { ++*done; return 1; }
        progress_text(w, "Lomax+: installing the game from the disc image, file %d of %d...", ++*done, total);
        return iso_copy_file(im, e, out);
    }
    if (w) CreateDirectoryA(out, NULL);
    sub = malloc(1024 * sizeof(IsoEntry));
    if (!sub || (n = iso_list(im, e->lba, e->size, sub, 1024)) < 0) { free(sub); return 0; }
    for (i = 0; i < n && ok; i++) {
        snprintf(path, sizeof(path), "%s\\%s", out ? out : "", sub[i].name);
        ok = iso_copy_tree(im, &sub[i], path, w, done, total);
    }
    free(sub);
    return ok;
}

static void install_game(const char *dir)
{
    static CueTrack t[CUE_MAX_TRACKS];
    static IsoEntry root[256];
    char cue[MAX_PATH], out[MAX_PATH];
    const CueTrack *data = NULL;
    IsoImage im;
    BYTE pvd[2048], hdr[16];
    DWORD got;
    HWND w;
    int n, i, j, nRoot, done = 0, total = 0, sel[sizeof(kInstallItems) / sizeof(kInstallItems[0])];
    IsoEntry rootDir;

    if (!find_cue(dir, cue) || (n = parse_cue(cue, t)) == 0)
        fail(NULL, "LOMAX.EXE was not found in\n%s\n\nPut the loader files in the installed game's folder, or put the "
                   "Lomax PC disc image (.cue and .bin) here to install the game from it.", dir);
    for (i = 0; i < n && !data; i++)
        if (!t[i].audio) data = &t[i];
    if (!data || data->start == (DWORD)-1 ||
        (data->secSize != 2048 && data->secSize != 2336 && data->secSize != 2352))
        fail(NULL, "LOMAX.EXE was not found, and %s has no data track that can be read.", cue);
    im.f = CreateFileA(data->file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (im.f == INVALID_HANDLE_VALUE)
        fail(NULL, "LOMAX.EXE was not found, and the disc image named by %s is missing:\n%s", cue, data->file);
    im.secSize = data->secSize;
    im.base = (LONGLONG)data->start * im.secSize;
    im.dataOff = im.secSize == 2048 ? 0 : im.secSize == 2336 ? 8 : 16;
    if (im.secSize == 2352) {                         /* raw: the header's mode byte decides */
        LARGE_INTEGER pos;
        pos.QuadPart = im.base + 16LL * 2352;
        if (SetFilePointerEx(im.f, pos, NULL, FILE_BEGIN) && ReadFile(im.f, hdr, 16, &got, NULL) && got == 16 &&
            hdr[15] == 2)
            im.dataOff = 24;
    }
    if (!iso_read(&im, 16, 1, pvd) || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5))
        fail(NULL, "LOMAX.EXE was not found, and %s does not hold a readable CD file system.", data->file);
    memset(&rootDir, 0, sizeof(rootDir));
    memcpy(&rootDir.lba, pvd + 156 + 2, 4);
    memcpy(&rootDir.size, pvd + 156 + 10, 4);
    rootDir.dir = 1;
    nRoot = iso_list(&im, rootDir.lba, rootDir.size, root, 256);
    for (j = 0; j < (int)(sizeof(kInstallItems) / sizeof(kInstallItems[0])); j++) {
        sel[j] = -1;
        for (i = 0; i < nRoot; i++)
            if (!_stricmp(root[i].name, kInstallItems[j])) sel[j] = i;
    }
    if (sel[sizeof(kInstallItems) / sizeof(kInstallItems[0]) - 1] < 0)        /* LOMAX.EXE */
        fail(NULL, "LOMAX.EXE was not found, and %s is not the Lomax PC disc.", data->file);
    for (j = 0; j < (int)(sizeof(kInstallItems) / sizeof(kInstallItems[0])); j++)
        if (sel[j] >= 0) iso_copy_tree(&im, &root[sel[j]], NULL, NULL, &total, 0);
    w = progress_window();
    for (j = 0; j < (int)(sizeof(kInstallItems) / sizeof(kInstallItems[0])); j++) {
        if (sel[j] < 0) continue;
        snprintf(out, sizeof(out), "%s\\%s", dir, root[sel[j]].name);
        if (!iso_copy_tree(&im, &root[sel[j]], out, w, &done, total)) {
            if (w) DestroyWindow(w);
            CloseHandle(im.f);
            fail(NULL, "Installing from the disc image failed at\n%s", out);
        }
    }
    if (w) DestroyWindow(w);
    CloseHandle(im.f);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdLine, int show)
{
    char dir[MAX_PATH], exe[MAX_PATH], dll[MAX_PATH], cmd[MAX_PATH + 2];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    (void)inst; (void)prev; (void)cmdLine; (void)show;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    GetModuleFileNameA(NULL, dir, MAX_PATH);
    *strrchr(dir, '\\') = 0;
    snprintf(exe, MAX_PATH, "%s\\LOMAX.EXE", dir);
    snprintf(dll, MAX_PATH, "%s\\LOADER.DLL", dir);
    snprintf(cmd, sizeof(cmd), "\"%s\"", exe);

    if (GetFileAttributesA(dll) == INVALID_FILE_ATTRIBUTES)
        fail(NULL, "Hook DLL not found:\n%s", dll);
    if (GetFileAttributesA(exe) == INVALID_FILE_ATTRIBUTES) install_game(dir);
    extract_music(dir);

    /* Export offset of LomaxFix_Init, computed from a local, non-initialised mapping of the DLL. */
    HMODULE local = LoadLibraryExA(dll, NULL, DONT_RESOLVE_DLL_REFERENCES);
    FARPROC initLocal = local ? GetProcAddress(local, "LomaxFix_Init@4") : NULL;
    if (!initLocal && local) initLocal = GetProcAddress(local, "LomaxFix_Init");
    if (!initLocal) fail(NULL, "%s does not export LomaxFix_Init", dll);
    DWORD initRva = (DWORD)initLocal - (DWORD)local;
    FreeLibrary(local);

    if (!CreateProcessA(exe, cmd, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, dir, &si, &pi))
        fail(NULL, "Could not start the game:\n%s\n(error %lu)", exe, GetLastError());
    run_to_entry(&pi, exe);

    /* The DLL path, for LoadLibraryA */
    size_t dllLen = strlen(dll) + 1;
    char *remote = VirtualAllocEx(pi.hProcess, NULL, dllLen, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote || !WriteProcessMemory(pi.hProcess, remote, dll, dllLen, NULL))
        fail(pi.hProcess, "Could not write into the game process (error %lu)", GetLastError());

    /* kernel32 is mapped at the same address in every process of the session. */
    LPTHREAD_START_ROUTINE loadLib =
        (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    DWORD remoteBase = run_remote(pi.hProcess, loadLib, remote);
    if (!remoteBase)
        fail(pi.hProcess, "Could not load the hook DLL into the game:\n%s", dll);

    DWORD rc = run_remote(pi.hProcess, (LPTHREAD_START_ROUTINE)(remoteBase + initRva), NULL);
    VirtualFreeEx(pi.hProcess, remote, 0, MEM_RELEASE);
    if (rc != 1)
        fail(pi.hProcess, "LOADER.DLL initialisation failed (code %lu).\n"
                          "Turn on Loader Log in Properties > Extra and check LOADER.LOG.", rc);

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
