/*
 * window.c - borderless fullscreen in place of the game's exclusive fullscreen, and the window size.
 */
#include "lomax.h"

static SetVideoMode_t oSetVideoMode;
static WNDPROC        oWndProc;

/* borderless fullscreen state */
static int  g_fakeFull;       /* window currently covers the monitor */
static int  g_startPending;   /* go fullscreen after the first surface init */
static LONG g_savedStyle;
static RECT g_savedRect;
static int  g_savedWinX, g_savedWinY, g_savedClientW, g_savedClientH;

/* ---- borderless fullscreen ------------------------------------------------------------------
   The game keeps running on its windowed path (16-bit surfaces + GDI present); the window just
   loses its frame/menu and covers the monitor. No display mode change. */
static void game_cursor(int show)
{
    if (show) { while (*G_CURSORSHOWCOUNT < 0)  { ShowCursor(TRUE);  (*G_CURSORSHOWCOUNT)++; } }
    else      { while (*G_CURSORSHOWCOUNT >= 0) { ShowCursor(FALSE); (*G_CURSORSHOWCOUNT)--; } }
}

static void cover_monitor(HWND h)
{
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    GetMonitorInfoA(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
    SetWindowPos(h, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                 mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

static void enter_borderless(void)
{
    HWND h = *G_HWND;
    if (!h || g_fakeFull) return;
    if (IsIconic(h) || IsZoomed(h)) ShowWindow(h, SW_RESTORE);
    g_savedStyle = GetWindowLongA(h, GWL_STYLE);
    GetWindowRect(h, &g_savedRect);
    g_savedWinX = *G_WINX; g_savedWinY = *G_WINY;
    g_savedClientW = *G_WINCLIENTW; g_savedClientH = *G_WINCLIENTH;
    g_fakeFull = 1;
    SetMenu(h, NULL);
    SetWindowLongA(h, GWL_STYLE, (g_savedStyle & ~(LONG)WS_OVERLAPPEDWINDOW) | (LONG)WS_POPUP);
    cover_monitor(h);
    game_cursor(0);
    log_msg("borderless fullscreen on (window was %ldx%ld at %ld,%ld)",
            g_savedRect.right - g_savedRect.left, g_savedRect.bottom - g_savedRect.top,
            g_savedRect.left, g_savedRect.top);
}

static void restore_window_globals(void)
{
    /* DDraw_Present copies the window position/size into these; keep the windowed values so the
       registry (WinX/WinY/WinW/WinH, saved on exit) never stores the fullscreen geometry. */
    *G_WINX = g_savedWinX; *G_WINY = g_savedWinY;
    *G_WINCLIENTW = g_savedClientW; *G_WINCLIENTH = g_savedClientH;
}

static void leave_borderless(void)
{
    HWND h = *G_HWND;
    if (!h || !g_fakeFull) return;
    g_fakeFull = 0;
    SetWindowLongA(h, GWL_STYLE, g_savedStyle);
    SetMenu(h, *G_HMENU);
    SetWindowPos(h, HWND_NOTOPMOST, g_savedRect.left, g_savedRect.top,
                 g_savedRect.right - g_savedRect.left, g_savedRect.bottom - g_savedRect.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    restore_window_globals();
    game_cursor(1);
    log_msg("borderless fullscreen off");
}

/* Widescreen: the window size comes from the registry (WinW/WinH, saved on exit). Map the 4:3
   screen sizes (1x..4x) to the widescreen ones on start, and back on exit so the regular build
   keeps finding normal values. */
void ws_map_window_size(int toWide)
{
    int w = *G_WINCLIENTW, h = *G_WINCLIENTH, W = g_wsWidth, k;
    if (!W) return;
    for (k = 1; k <= 4; k++) {
        if (h != 240 * k) continue;
        if (toWide && w == 320 * k)  *G_WINCLIENTW = W * k;
        if (!toWide && w == W * k)   *G_WINCLIENTW = 320 * k;
    }
}

/* render.c's DDraw_InitSurfaces hook, after the game's: keep or start borderless fullscreen */
void window_surfaces_ready(int ok)
{
    if (ok && *G_BWINDOWED) {
        if (g_fakeFull) {
            /* surface re-init (lost surfaces / mode change) resized the window: cover again */
            cover_monitor(*G_HWND);
            game_cursor(0);
        } else if (g_startPending) {
            g_startPending = 0;
            enter_borderless();
        }
    }
}

int borderless_active(void)
{
    return g_fakeFull;
}

void borderless_toggle(void)                   /* Alt+Enter, the Display menu */
{
    if (g_fakeFull) leave_borderless(); else enter_borderless();
}

/* ---- the window size: 1x..4x the internal picture ---- */
int window_scale(void)                         /* 1..4, from the windowed client height */
{
    int h = g_fakeFull ? g_savedClientH : *G_WINCLIENTH, k = (h + 120) / 240;
    return k < 1 ? 1 : k > 4 ? 4 : k;
}

void set_window_scale(int k)                   /* leaves fullscreen; client k * (W x 240) */
{
    HWND h = *G_HWND;
    RECT r;
    int W = g_wsWidth ? g_wsWidth : 320;
    if (!h || !*G_BWINDOWED || k < 1 || k > 4) return;
    leave_borderless();
    if (IsZoomed(h) || IsIconic(h)) ShowWindow(h, SW_RESTORE);
    *G_WINCLIENTW = k * W;
    *G_WINCLIENTH = k * 240;
    SetRect(&r, 0, 0, k * W, k * 240);
    AdjustWindowRect(&r, (DWORD)GetWindowLongA(h, GWL_STYLE), GetMenu(h) != NULL);
    SetWindowPos(h, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW);
    log_msg("screen size %dx: %dx%d window", k, k * W, k * 240);
}

static void __cdecl hkSetVideoMode(int mode)
{
    if (g_force16) {
        if (mode >= 2) enter_borderless();   /* F7/F8: fullscreen 8/16-bit */
        else set_window_scale(mode + 1);     /* F5/F6: a 1x / 2x window (widescreen included) */
        return;
    }
    oSetVideoMode(mode);
}

static LRESULT CALLBACK hkWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_force16) {
        switch (msg) {
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
            if (LOWORD(wp) != VK_RETURN) break;
            if (msg == WM_SYSKEYUP) borderless_toggle();   /* Alt+Enter */
            return 0;
        case WM_SYSCHAR:
            if (wp == '\r') return 0;                     /* no "ding" on Alt+Enter */
            break;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE && g_fakeFull) { leave_borderless(); return 0; }
            break;
        case WM_DESTROY:
            if (g_fakeFull) restore_window_globals();     /* before the game saves settings */
            ws_map_window_size(0);
            break;
        }
    }
    return oWndProc(h, msg, wp, lp);
}

void window_init(void)
{
    if (!g_force16) return;
    mh_hook(FN_DISPLAY_SET_VIDEO_MODE, hkSetVideoMode, (void **)&oSetVideoMode, "Display_SetVideoMode");
    mh_hook(FN_WNDPROC, hkWndProc, (void **)&oWndProc, "Lomax_WndProc");
    g_startPending = cfg.startFullscreen;
}
