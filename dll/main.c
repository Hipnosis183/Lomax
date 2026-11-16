/*
 * LOADER.DLL - Lomax+: in-memory fixes for Lomax (Psygnosis, 1997), retail LOMAX.EXE.
 *
 * Injected into the suspended game process by LOADER.EXE, which then calls
 * LomaxFix_Init before the game's main thread starts. The options are set in the game's
 * Properties: the display ones on the System page, the rest on the Extra page (saved in the
 * registry next to the game's own settings).
 *
 * Modules (lomax.h declares what they share; each installs its own hooks from its *_init):
 *   main.c        the entry point, the configuration, the log and hooking helpers
 *   ui.c          Lomax+ branding, the Properties sheet and its System (display settings) and Extra
 *                 pages, the Display and Sound menus, the restart note
 *   controls.c    keyboard and controller bindings, the Controls page
 *   audio.c       mixer crash, hard-right pan, CD check, CD music from files
 *   render.c      16-bit surfaces, Direct3D driver and transparency, shared tiles, surface (re)initialisation
 *   present.c     the windowed present: sharp scaling through GDI or Direct3D 11
 *   window.c      borderless fullscreen, the window size
 *   game.c        language select, 50 Hz mode, loading screen
 *   widescreen.c  widescreen (and its patch table) and demo playback
 *   vsync.c       VSync
 *   hfr.c         FPS Override; narrow layers mirrored in widescreen
 *   fastblit.c    Fast Blit
 *
 * Fixes:
 *   CD audio     - the disc is never needed or read: no "insert CD" / "wrong CD" dialogs, and the
 *                  music plays from MUSIC\TRACKnn files (none: no music).
 *   16-bit emulation - on desktops that aren't 8/16 bpp (every modern PC): every offscreen surface
 *                  the game creates is forced to 16-bit RGB565 (hooked IDirectDraw2::CreateSurface)
 *                  and the windowed present (primary->Blt) is replaced by the loader's presenter.
 *   Fullscreen   - while the 16-bit emulation is active the game's exclusive fullscreen (which needs
 *                  320x240 8/16-bit modes that modern DirectDraw doesn't offer) is replaced:
 *                  F7/F8, Properties and Alt+Enter switch to a borderless window
 *                  covering the monitor. Esc or Alt+Enter leaves borderless fullscreen.
 *   Scaling      - sharp scaling (nearest to the next integer factor, then an area-average shrink), 4:3 kept.
 *   Display box  - Properties > System: renderer, screen size 1x..4x, start in fullscreen,
 *                  widescreen, VSync, FPS (registry); a Display menu for the size and fullscreen.
 *   Renderer     - the picture goes to the window through a Direct3D 11 flip-model swap chain
 *                  (GPU scaling, no tearing) or, with GDI chosen or as a fallback, through GDI.
 *   Transparency - translucent sprites and fades: the card's choice, checkerboard/dissolve, opaque
 *                  sprites without fades, or the hardware Direct3D path with real alpha blending.
 *   Widescreen   - widens the internal 320x240 screen (a patch table + hooks; demos keep the
 *                  original logic camera so the recordings stay in sync).
 *   VSync        - game ticks on the display's refreshes (driver vertical blank events).
 *   FPS          - frames drawn per second above the tick rate: the frames between ticks redraw the
 *                  last tick's sprite queue with everything moved part of the way (interpolated).
 *   FastBlit     - the sprite queue is copied by our own loop instead of one DirectDraw Blt per
 *                  sprite (far faster on emulated 16-bit DirectDraw).
 *   Extra page   - loading screen, language menu, PAL timing, cheats, the fixes above, the log.
 *   Mods         - each folder MODS\<name> with <name>.DLL and <name>.INI is a mod, listed on the
 *                  Extra page (Load Mod). The one chosen there runs from the next start: its DLL
 *                  is loaded after the hooks are installed (load_mod), and its MUSIC folder adds
 *                  CD tracks (audio.c). A mod missing a file it needs is not loaded (a message
 *                  box says which). A mod does its own patching; Lomax+ only loads it.
 *
 * Build (MSYS2 mingw32): see build.bat.
 */
#include "lomax.h"

static const struct { DWORD addr; unsigned char bytes[6]; } kSignature[] = {
    { FN_DISPLAY_SET_VIDEO_MODE, { 0x8B, 0x44, 0x24, 0x04, 0x83, 0xEC } },
    { FN_WNDPROC,                { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 } },
    { FN_DDRAW_INIT_SURFACES,    { 0x81, 0xEC, 0xF4, 0x04, 0x00, 0x00 } },
    { FN_CD_VERIFY_LOMAX_DISC,   { 0x81, 0xEC, 0x4C, 0x01, 0x00, 0x00 } },
    { FN_D3D_SELECT_HW_DRIVER,   { 0x53, 0xA1, 0xAC, 0x1C, 0x46, 0x00 } },
    { FN_D3D_SELECT_SW_DRIVER,   { 0x8B, 0x44, 0x24, 0x04, 0x53, 0x56 } },
    { 0x00406AF0u /* D3D_InitDevice */,            { 0x81, 0xEC, 0x98, 0x00, 0x00, 0x00 } },
    { 0x0040A510u /* Sprite_GetDitheredSurface */, { 0x81, 0xEC, 0x48, 0x01, 0x00, 0x00 } },
    { 0x004055C0u /* Render_DissolveFade */,       { 0x81, 0xEC, 0x94, 0x00, 0x00, 0x00 } },
    { 0x00438460u /* CDAudio_SetVolume */,         { 0x83, 0xEC, 0x08, 0xA1, 0x68, 0x6B } },
    { 0x00417BD0u /* Game_SetupNextLevel */,       { 0x83, 0xEC, 0x40, 0xA1, 0xCC, 0x45 } },
    { 0x00404AA0u /* Game_Frame */,                { 0x81, 0xEC, 0x94, 0x00, 0x00, 0x00 } },
};
Config cfg;
char   g_gameDir[MAX_PATH];
int    g_force16;
static char g_logPath[MAX_PATH];

/* ---- helpers ------------------------------------------------------------------------------- */
void log_msg(const char *fmt, ...)
{
    if (!cfg.log) return;
    FILE *f = fopen(g_logPath, "a");
    if (!f) return;
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f);
    fclose(f);
}

/* ---- import table hooking (only LOMAX.EXE's own imports) ---------------------------------- */
void **find_iat_slot(const char *dll, const char *func)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress);
    for (; imp->Name; imp++) {
        if (_stricmp((char *)(base + imp->Name), dll)) continue;
        IMAGE_THUNK_DATA *name = (IMAGE_THUNK_DATA *)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        IMAGE_THUNK_DATA *addr = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; name->u1.AddressOfData; name++, addr++) {
            if (IMAGE_SNAP_BY_ORDINAL(name->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME *)(base + name->u1.AddressOfData);
            if (!strcmp((char *)ibn->Name, func)) return (void **)&addr->u1.Function;
        }
    }
    return NULL;
}

int hook_iat(const char *dll, const char *func, void *detour, void **orig)
{
    void **slot = find_iat_slot(dll, func);
    DWORD old;
    if (!slot) { log_msg("IAT: %s!%s not found", dll, func); return 0; }
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) return 0;
    *orig = *slot;
    *slot = detour;
    VirtualProtect(slot, sizeof(void *), old, &old);
    log_msg("IAT: hooked %s!%s", dll, func);
    return 1;
}

int mh_hook(DWORD target, void *detour, void **orig, const char *what)
{
    MH_STATUS s = MH_CreateHook((void *)target, detour, orig);
    if (s == MH_OK) s = MH_EnableHook((void *)target);
    log_msg("hook %-22s @%08lX: %s", what, (unsigned long)target, MH_StatusToString(s));
    return s == MH_OK;
}

/* hex bytes ("8b 44 24") into out; -1 when there are more than max (never truncated) */
int hex_to_bytes(const char *hex, unsigned char *out, int max)
{
    int n = 0;
    while (*hex && n < max) {
        unsigned v;
        while (*hex == ' ') hex++;
        if (!*hex || sscanf(hex, "%2x", &v) != 1) break;
        out[n++] = (unsigned char)v;
        hex += 2;
    }
    while (*hex == ' ') hex++;
    return *hex ? -1 : n;
}

int code_matches(DWORD addr, const void *bytes, size_t n)
{
    return !memcmp((const void *)addr, bytes, n);
}

void patch_code(DWORD addr, const void *bytes, size_t n)
{
    DWORD old;
    VirtualProtect((void *)addr, n, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)addr, bytes, n);
    VirtualProtect((void *)addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)addr, n);
}

void patch_code_byte(volatile BYTE *p, BYTE v)
{
    if (*p != v) patch_code((DWORD)p, &v, 1);
}

/* REG_DWORD values in HKCU\Software\Psygnosis\Lomax\<sub>, next to the game's own keys */
HKEY reg_open(const char *sub, REGSAM access)
{
    char path[64];
    HKEY k;
    snprintf(path, sizeof(path), "Software\\Psygnosis\\Lomax\\%s", sub);
    return RegCreateKeyExA(HKEY_CURRENT_USER, path, 0, NULL, 0, access, NULL, &k, NULL) ? NULL : k;
}

int reg_get_int(HKEY k, const char *name, int *v)
{
    DWORD d, type, n = sizeof(d);
    if (RegQueryValueExA(k, name, NULL, &type, (BYTE *)&d, &n) || type != REG_DWORD) return 0;
    *v = (int)d;
    return 1;
}

void reg_set_int(HKEY k, const char *name, int v)
{
    DWORD d = (DWORD)v;
    RegSetValueExA(k, name, 0, REG_DWORD, (const BYTE *)&d, sizeof(d));
}

/* REG_SZ values: s is "" when the value is missing or isn't a string */
int reg_get_str(HKEY k, const char *name, char *s, DWORD size)
{
    DWORD type, n = size - 1;
    s[0] = 0;
    if (RegQueryValueExA(k, name, NULL, &type, (BYTE *)s, &n) || type != REG_SZ) { s[0] = 0; return 0; }
    s[n < size ? n : size - 1] = 0;
    return 1;
}

void reg_set_str(HKEY k, const char *name, const char *s)
{
    RegSetValueExA(k, name, 0, REG_SZ, (const BYTE *)s, (DWORD)strlen(s) + 1);
}

/* window subclassing: the previous procedure is kept in the window property prop; subclass_next
   calls it and, at WM_NCDESTROY, puts it back */
void subclass(HWND h, const char *prop, WNDPROC proc)
{
    if (h && !GetPropA(h, prop)) SetPropA(h, prop, (HANDLE)SetWindowLongPtrA(h, GWLP_WNDPROC, (LONG_PTR)proc));
}

LRESULT subclass_next(HWND h, const char *prop, UINT msg, WPARAM w, LPARAM l)
{
    WNDPROC old = (WNDPROC)GetPropA(h, prop);
    if (msg == WM_NCDESTROY) {
        RemovePropA(h, prop);
        if (old) SetWindowLongPtrA(h, GWLP_WNDPROC, (LONG_PTR)old);
    }
    return old ? CallWindowProcA(old, h, msg, w, l) : DefWindowProcA(h, msg, w, l);
}

/* ---- mods ------------------------------------------------------------------------------------
   A mod is a folder MODS\<folder> holding <folder>.DLL and <folder>.INI, whose [Lomax] section
   names it (Name, shown on the Extra page's Load Mod list; Author, Description) and may list the
   other files it needs (Files, paths in the mod's folder separated by commas). At most one mod
   runs: the one chosen on the Extra page (registry Extra\Mod, its folder name; "" or missing =
   None), from the next start. mods_init scans MODS for the list and checks the chosen mod's
   files; with any missing it says so in a message box and the game starts without the mod.
   load_mod then loads its DLL (which patches what it needs from DllMain). */
ModInfo g_mods[MOD_MAX];
int     g_nMods;
char    g_modChosen[MOD_FOLDER_LEN];
char    g_modRunning[MOD_FOLDER_LEN];

static int file_exists(const char *path)
{
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

/* MODS\<folder>\<folder>.<ext> */
static void mod_file(char *out, size_t size, const char *folder, const char *ext)
{
    snprintf(out, size, "%s\\MODS\\%s\\%s.%s", g_gameDir, folder, folder, ext);
}

static void mods_scan(void)
{
    char pattern[MAX_PATH], dll[MAX_PATH], ini[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int i, j;
    snprintf(pattern, sizeof(pattern), "%s\\MODS\\*", g_gameDir);
    if ((h = FindFirstFileA(pattern, &fd)) == INVALID_HANDLE_VALUE) return;
    do {
        ModInfo *m;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
        if (strlen(fd.cFileName) >= MOD_FOLDER_LEN) { log_msg("mod %s: folder name too long, skipped", fd.cFileName); continue; }
        mod_file(dll, sizeof(dll), fd.cFileName, "DLL");
        mod_file(ini, sizeof(ini), fd.cFileName, "INI");
        if (!file_exists(dll) || !file_exists(ini)) {
            log_msg("mod %s: no %s.DLL or %s.INI in it, not listed", fd.cFileName, fd.cFileName, fd.cFileName);
            continue;
        }
        if (g_nMods == MOD_MAX) { log_msg("mod %s: more than %d mods, not listed", fd.cFileName, MOD_MAX); continue; }
        m = &g_mods[g_nMods++];
        snprintf(m->folder, sizeof(m->folder), "%s", fd.cFileName);
        GetPrivateProfileStringA("Lomax", "Name", "", m->name, sizeof(m->name), ini);
        if (!m->name[0]) snprintf(m->name, sizeof(m->name), "%s", fd.cFileName);
        log_msg("mod %s: listed as \"%s\"", m->folder, m->name);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    for (i = 1; i < g_nMods; i++)                /* by name */
        for (j = i; j > 0 && _stricmp(g_mods[j - 1].name, g_mods[j].name) > 0; j--) {
            ModInfo t = g_mods[j]; g_mods[j] = g_mods[j - 1]; g_mods[j - 1] = t;
        }
}

/* the chosen mod's missing files, one per line, into miss ("" when it has them all) */
static void mod_missing(const char *folder, char *miss, size_t size)
{
    char path[MAX_PATH], ini[MAX_PATH], files[1024], *f;
    size_t n = 0;
    miss[0] = 0;
    snprintf(path, sizeof(path), "%s\\MODS\\%s", g_gameDir, folder);
    if (!file_exists(path)) { snprintf(miss, size, "  MODS\\%s\n", folder); return; }
    mod_file(path, sizeof(path), folder, "DLL");
    if (!file_exists(path)) n += snprintf(miss + n, size - n, "  MODS\\%s\\%s.DLL\n", folder, folder);
    mod_file(ini, sizeof(ini), folder, "INI");
    if (!file_exists(ini)) { n += snprintf(miss + n, size - n, "  MODS\\%s\\%s.INI\n", folder, folder); return; }
    GetPrivateProfileStringA("Lomax", "Files", "", files, sizeof(files), ini);
    for (f = strtok(files, ",;"); f && n < size; f = strtok(NULL, ",;")) {
        char *e;
        while (*f == ' ' || *f == '\t') f++;
        for (e = f + strlen(f); e > f && (e[-1] == ' ' || e[-1] == '\t'); ) *--e = 0;
        if (!*f) continue;
        snprintf(path, sizeof(path), "%s\\MODS\\%s\\%s", g_gameDir, folder, f);
        if (!file_exists(path)) n += snprintf(miss + n, size - n, "  MODS\\%s\\%s\n", folder, f);
    }
}

/* The message when the chosen mod can't be loaded (its files missing, or its DLL failing to load);
   the details go to the log only. The choice goes back to None (registry Extra\Mod ""), so the
   message comes once, not at every start. */
static void mod_failed(const char *name)
{
    char msg[MOD_NAME_LEN + 128];
    HKEY k = reg_open("Extra", KEY_SET_VALUE);
    if (k) { reg_set_str(k, "Mod", ""); RegCloseKey(k); }
    g_modChosen[0] = 0;
    log_msg("mod: choice reset to None");
    snprintf(msg, sizeof(msg), "The selected mod \"%s\" couldn't be loaded. "
             "Lomax will start normally without any mods loaded.", name);
    MessageBoxA(NULL, msg, "Lomax+", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);
}

static void mods_init(void)
{
    char miss[2048], name[MOD_NAME_LEN], ini[MAX_PATH];
    HKEY k = reg_open("Extra", KEY_QUERY_VALUE);
    if (k) { reg_get_str(k, "Mod", g_modChosen, sizeof(g_modChosen)); RegCloseKey(k); }
    mods_scan();
    if (!g_modChosen[0]) { log_msg("mod: None"); return; }
    mod_missing(g_modChosen, miss, sizeof(miss));
    if (miss[0]) {
        mod_file(ini, sizeof(ini), g_modChosen, "INI");
        GetPrivateProfileStringA("Lomax", "Name", g_modChosen, name, sizeof(name), ini);
        log_msg("mod %s: files missing, not loaded:\n%s", g_modChosen, miss);
        mod_failed(name);
        return;
    }
    snprintf(g_modRunning, sizeof(g_modRunning), "%s", g_modChosen);
}

static void load_mod(void)
{
    char path[MAX_PATH], ini[MAX_PATH], name[MOD_NAME_LEN];
    if (!g_modRunning[0]) return;
    mod_file(path, sizeof(path), g_modRunning, "DLL");
    if (LoadLibraryA(path)) { log_msg("mod %s: loaded", g_modRunning); return; }
    log_msg("mod %s: failed to load (error %lu)", g_modRunning, GetLastError());
    mod_file(ini, sizeof(ini), g_modRunning, "INI");
    GetPrivateProfileStringA("Lomax", "Name", g_modRunning, name, sizeof(name), ini);
    mod_failed(name);
    g_modRunning[0] = 0;
}

/* ---- entry point called by the loader ------------------------------------------------------ */
__declspec(dllexport) DWORD WINAPI LomaxFix_Init(LPVOID unused)
{
    size_t i;
    HDC screen;
    int bpp;

    (void)unused;
    GetModuleFileNameA(NULL, g_gameDir, MAX_PATH);
    *strrchr(g_gameDir, '\\') = 0;
    snprintf(g_logPath, sizeof(g_logPath), "%s\\LOADER.LOG", g_gameDir);
    extra_load();                               /* the options: Properties > Extra and > System */
    disp_load();
    if (cfg.log) DeleteFileA(g_logPath);

    for (i = 0; i < sizeof(kSignature) / sizeof(kSignature[0]); i++) {
        if (memcmp((void *)kSignature[i].addr, kSignature[i].bytes, sizeof(kSignature[i].bytes))) {
            log_msg("unexpected code at %08lX - not the retail LOMAX.EXE (or already patched); no hooks installed",
                 (unsigned long)kSignature[i].addr);
            return 2;
        }
    }

    screen = GetDC(NULL);
    bpp = GetDeviceCaps(screen, BITSPIXEL);
    ReleaseDC(NULL, screen);
    g_force16 = bpp != 8 && bpp != 16;
    log_msg("LOADER.DLL: %s, desktop=%d bpp, 16-bit emulation %s", g_gameDir, bpp,
         g_force16 ? "active" : "inactive");

    if (MH_Initialize() != MH_OK) { log_msg("MH_Initialize failed"); return 3; }

    mods_init();                                /* the list and the chosen mod: before audio_init */

    /* the order matters only where noted */
    ui_init();
    controls_init();
    audio_init();
    render_init();
    game_init();
    window_init();
    ws_init(disp_ws_width());
    speed50_init();                             /* after the widescreen patch */
    if (!(cfg.hfrFps && hfr_init()) && cfg.vsync) vsync_init();   /* FPS Override has its own VSync */
    ws_layer_init();                            /* after hfr_init: its layer hook mirrors too */
    fastblit_init();
    load_mod();
    log_msg("init done");
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(inst);
    return TRUE;
}
