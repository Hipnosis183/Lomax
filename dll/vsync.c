/*
 * vsync.c - VSync: game ticks on the display refresh.
 */
#include "lomax.h"

/* ---- VSync: game ticks locked to the display refresh ---------------------------------
   Game_Frame times its ticks with timeGetTime (accum += ms*10, ticks = accum / 167, 59.88 Hz) and
   presents each tick's frame at once, without waiting for the display. Whole milliseconds put the
   frames 16 or 17 ms apart (sometimes 15 or 18), and 59.88 Hz drifts against a 60/120 Hz display
   (a full cycle every ~8 s at 60 Hz). Whenever the frames come close to a refresh, the jitter puts
   them on alternate sides of it, so for a moment frames are shown twice and skipped: the stutter,
   with the FPS counter (presents per second) still at 60.
   VSync replaces that timing block (0x404BD1, up to the 5-tick clamp) with vsync_ticks. A thread
   (vb_thread) waits on the display driver's vertical blank (D3DKMTWaitForVerticalBlankEvent, on the
   monitor the game window is on) and timestamps every refresh. With a refresh rate within 2% of a
   multiple of the tick rate (60, 120, 180, 240 Hz, 59.94 Hz; 50, 100 Hz with Speed50Hz), one tick
   runs every N refreshes, drawn and presented right after the refresh, so every frame has a whole
   refresh to reach the screen and stays there for N refreshes. The game then runs at refresh / N
   ticks per second (60 instead of 59.88 at 60 Hz, 0.2% faster). After a stall, up to 5 ticks catch
   up, as in the original. Other rates (75, 144 Hz), no vblank events, or none for 100 ms: the
   original timer ticks. The loading screen keeps its own timer.
   Earlier versions used DWM: DWM_TIMING_INFO.cRefresh only moves when DWM composes (the game ran far
   too fast or slow), and frames timed halfway between refreshes from qpcVBlank skipped about one in
   five at 120 Hz. */
#define VS_TICK_LOOP            0x00404C29u   /* clear the sprite queue, run ESI ticks, build, present */
typedef struct { HDC hDc; UINT hAdapter; LUID AdapterLuid; UINT VidPnSourceId; } VbOpenFromHdc;
typedef struct { UINT hAdapter; UINT hDevice; UINT VidPnSourceId; } VbWait;
typedef struct { UINT hAdapter; } VbClose;
typedef LONG (APIENTRY *VbOpen_t)(VbOpenFromHdc *);
typedef LONG (APIENTRY *VbWait_t)(const VbWait *);
typedef LONG (APIENTRY *VbClose_t)(const VbClose *);
static VbOpen_t       pVbOpen;
static VbWait_t       pVbWait;
static VbClose_t      pVbClose;
volatile LONG  g_vbCount;           /* refreshes seen by vb_thread */
volatile LONGLONG g_vbQpc;          /* QPC at the last one */
static volatile LONGLONG g_vbPeriod;       /* refresh period in QPC units, smoothed */
static volatile DWORD g_vbTime;            /* timeGetTime at the last one */
static volatile int   g_vbModeHz;          /* the display mode's refresh rate, 0 = unknown */
static volatile int   g_vbVrr;             /* the refreshes follow the frames (vb_follows_frames) */
static int            g_vsN = -1;          /* refreshes per tick in use, 0 = timer ticks */
static LONG           g_vsLast;            /* g_vbCount at the last call */
static int            g_vsPending;         /* refreshes counted towards the next tick */
static DWORD          g_vsStatTime;        /* log: ticks, refreshes and calls every 5 s */
static int            g_vsStatTicks, g_vsStatRefresh, g_vsStatCalls, g_vsStatTimer;

/* timestamps every refresh of the monitor the game window is on */
static DWORD WINAPI vb_thread(LPVOID unused)
{
    HMONITOR mon = NULL;
    VbWait wv;
    LONGLONG last = 0, period = 0;
    DWORD check = 0;
    int open = 0;
    (void)unused;
    memset(&wv, 0, sizeof(wv));
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    for (;;) {
        LARGE_INTEGER q, f;
        DWORD now = timeGetTime();
        if (!open || now - check > 1000) {               /* follow the window to another monitor */
            HMONITOR m = MonitorFromWindow(*G_HWND ? *G_HWND : GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
            check = now;
            if (!open || m != mon) {
                MONITORINFOEXA mi;
                DEVMODEA dm;
                VbOpenFromHdc oa;
                HDC dc;
                if (open) { VbClose c = { wv.hAdapter }; pVbClose(&c); open = 0; }
                mi.cbSize = sizeof(mi);
                memset(&oa, 0, sizeof(oa));
                memset(&dm, 0, sizeof(dm));
                dm.dmSize = sizeof(dm);
                if (GetMonitorInfoA(m, (MONITORINFO *)&mi) && (dc = CreateDCA(NULL, mi.szDevice, NULL, NULL))) {
                    oa.hDc = dc;
                    if (!pVbOpen(&oa)) {
                        open = 1;
                        mon = m;
                        wv.hAdapter = oa.hAdapter;
                        wv.hDevice = 0;
                        wv.VidPnSourceId = oa.VidPnSourceId;
                        QueryPerformanceFrequency(&f);   /* first estimate: the mode's refresh rate */
                        period = EnumDisplaySettingsA(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) &&
                                 dm.dmDisplayFrequency > 1 ? f.QuadPart / dm.dmDisplayFrequency : 0;
                        last = 0;
                        g_vbModeHz = dm.dmDisplayFrequency > 1 ? (int)dm.dmDisplayFrequency : 0;
                        g_vbVrr = 0;
                        log_msg("VSync: vertical blank events from %s (%lu Hz mode)", mi.szDevice,
                                (unsigned long)dm.dmDisplayFrequency);
                    }
                    DeleteDC(dc);
                }
                if (!open) { Sleep(100); continue; }
            }
        }
        if (pVbWait(&wv)) { VbClose c = { wv.hAdapter }; pVbClose(&c); open = 0; Sleep(50); continue; }
        QueryPerformanceCounter(&q);
        if (last) {                                      /* smoothed period, missed refreshes ignored */
            LONGLONG d = q.QuadPart - last;
            if (!period) period = d;
            else if (d > period / 2 && d < period * 3 / 2) period += (d - period) / 16;
        }
        last = q.QuadPart;
        InterlockedExchange64(&g_vbPeriod, period);
        InterlockedExchange64(&g_vbQpc, q.QuadPart);
        g_vbTime = timeGetTime();
        InterlockedIncrement(&g_vbCount);
    }
    return 0;
}

/* refresh rate from vb_thread, 0 = no recent vblank events */
double vb_hz(void)
{
    LARGE_INTEGER f;
    LONGLONG p = InterlockedCompareExchange64(&g_vbPeriod, 0, 0);
    if (g_vbCount < 8 || !p || timeGetTime() - g_vbTime > 100) return 0;
    QueryPerformanceFrequency(&f);
    return (double)f.QuadPart / p;
}

/* Variable refresh rate (G-SYNC, FreeSync): in borderless fullscreen the D3D11 swap chain gets the
   display to itself, and the display then refreshes when a frame arrives instead of at the mode's
   rate. Frames drawn after a refresh then wait on refreshes that wait on frames: each refresh is the
   scan-out plus the frame's drawing (at 180 Hz about 100 refreshes, and frames, per second), and the
   ticks counted on them slowed the game to 55%. Refreshes measured clearly below the mode's rate
   (180 Hz mode: 179.99 Hz before the swap chain, 174-176 with it, 100 later) mean exactly that: frames
   and ticks are timed by the clock from then on (the display follows them, and flip-model presents
   don't tear). Kept until the window moves to another monitor. */
int vb_follows_frames(double hz)
{
    int mode = g_vbModeHz;
    if (!g_vbVrr && mode && hz > 0 && hz < mode * 0.985) {
        g_vbVrr = 1;
        log_msg("VSync: the %d Hz display refreshes at %.2f Hz, following the frames (variable refresh "
                "rate): frames and ticks timed by the clock", mode, hz);
    }
    return g_vbVrr;
}

int vs_load_vblank(void)
{
    HMODULE gdi = GetModuleHandleA("gdi32.dll");
    static int started;
    pVbOpen = (VbOpen_t)GetProcAddress(gdi, "D3DKMTOpenAdapterFromHdc");
    pVbWait = (VbWait_t)GetProcAddress(gdi, "D3DKMTWaitForVerticalBlankEvent");
    pVbClose = (VbClose_t)GetProcAddress(gdi, "D3DKMTCloseAdapter");
    if (!pVbOpen || !pVbWait || !pVbClose) { log_msg("VSync: no D3DKMT vertical blank functions, off"); return 0; }
    if (!started) {
        HANDLE t = CreateThread(NULL, 0, vb_thread, NULL, 0, NULL);
        if (!t) return 0;
        CloseHandle(t);
        started = 1;
    }
    return 1;
}

static int vs_timer_ticks(void)           /* what Game_Frame did */
{
    DWORD now = timeGetTime();
    int acc = *G_FRAMEACCUM + (int)(now - *G_FRAMETIME) * 10;
    *G_FRAMETIME = now;
    *G_FRAMEACCUM = acc % g_tickDiv;
    return acc / g_tickDiv;
}

/* Game_Frame's timing (patched in at GF_TIMING): ticks to run, or -1 for none (no frame either) */
static int __cdecl vsync_ticks(void)
{
    double hz = vb_hz();
    int n = 0, ticks;
    LONG c = g_vbCount;
    DWORD now;
    if (vb_follows_frames(hz)) hz = 0;
    if (hz) {
        n = (int)(hz / g_tickHz + 0.5);
        if (n < 1 || fabs(hz / n - g_tickHz) > g_tickHz * 0.02) n = 0;
    }
    if (n != g_vsN) {
        if (n) log_msg("VSync: %.2f Hz display, one tick every %d refresh(es)", hz, n);
        else   log_msg("VSync: %.2f Hz display (or no vertical blank events), timer ticks", hz);
        g_vsN = n;
        g_vsLast = c;
        g_vsPending = n - 1;                             /* first tick at the next refresh */
    }
    if (n) {
        LONG d = c - g_vsLast;
        g_vsLast = c;
        g_vsPending += d > 5 * n ? 5 * n : d;
        g_vsStatRefresh += d;
        ticks = g_vsPending / n;
        g_vsPending %= n;
        *G_FRAMETIME = timeGetTime();                    /* the timer path resumes from here */
        *G_FRAMEACCUM = 0;
    } else {
        ticks = vs_timer_ticks();
        if (ticks > 5) ticks = 5;
        g_vsStatTimer++;
    }
    if (cfg.log) {
        now = timeGetTime();
        g_vsStatCalls++;
        if (ticks > 0) g_vsStatTicks += ticks;
        if (now - g_vsStatTime >= 5000) {
            if (g_vsStatTime)
                log_msg("VSync: %d ticks, %d refreshes in %lu ms (%d calls, %d on the timer)", g_vsStatTicks,
                        g_vsStatRefresh, (unsigned long)(now - g_vsStatTime), g_vsStatCalls, g_vsStatTimer);
            g_vsStatTime = now;
            g_vsStatTicks = g_vsStatRefresh = g_vsStatCalls = g_vsStatTimer = 0;
        }
    }
    return ticks > 0 ? ticks : -1;
}

void vsync_init(void)
{
    BYTE code[GF_TIMING_LEN];
    int p = 0;
    if (!game_timing_check("VSync") || !vs_load_vblank()) return;
    memset(code, 0xCC, sizeof(code));
    code[p++] = 0xE8;                                   /* call vsync_ticks */
    *(DWORD *)(code + p) = (DWORD)vsync_ticks - (GF_TIMING + p + 4); p += 4;
    code[p++] = 0x85; code[p++] = 0xC0;                 /* test eax, eax */
    code[p++] = 0x7F; code[p++] = 0x10;                 /* jg   +16 */
    code[p++] = 0xB8; *(DWORD *)(code + p) = 1; p += 4; /* mov  eax, 1 */
    code[p++] = 0x5D; code[p++] = 0x5F; code[p++] = 0x5E; code[p++] = 0x5B;   /* pop ebp, edi, esi, ebx */
    code[p++] = 0x81; code[p++] = 0xC4; *(DWORD *)(code + p) = 0x94; p += 4;  /* add esp, 0x94 */
    code[p++] = 0xC3;                                   /* ret: no tick, no frame (as before) */
    code[p++] = 0x8B; code[p++] = 0xF0;                 /* mov  esi, eax */
    code[p++] = 0xE9;                                   /* jmp  VS_TICK_LOOP */
    *(DWORD *)(code + p) = VS_TICK_LOOP - (GF_TIMING + p + 4); p += 4;
    patch_code(GF_TIMING, code, GF_TIMING_LEN);
    log_msg("VSync: Game_Frame ticks follow the display refresh when it is a multiple of %d Hz", g_tickHz);
}
