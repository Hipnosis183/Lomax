/*
 * ui.c - the user interface: Lomax+ branding, the Properties sheet and its pages (System: the
 * display settings and the Display menu; Extra; the Controls page is controls.c's), the game
 * window's Display and Sound menus, and the restart note.
 *
 *   helpers            text size, control placement in dialog units
 *   restart note       "Some settings need a restart to take effect."
 *   Properties > System   the Display box, its settings, the Display menu
 *   Properties > Extra    the page and its settings
 *   branding           "Lomax+" in titles, menus, the About dialog and message boxes
 *   the sheet          the Extra page added, the tab area fitted to the pages
 *   Sound menu         and "Use 4 channels only" removed
 *   the game window    its menus and the restart note
 */
#include "lomax.h"

/* ---- helpers for the Properties pages ---------------------------------------------------- */
SIZE text_size(HWND c, const char *text)
{
    HDC dc = GetDC(c);
    HFONT f = (HFONT)SelectObject(dc, (HFONT)SendMessageA(c, WM_GETFONT, 0, 0));
    SIZE sz = { 0, 0 };
    GetTextExtentPoint32A(dc, text, lstrlenA(text), &sz);
    SelectObject(dc, f);
    ReleaseDC(c, dc);
    return sz;
}

static void child_rect(HWND c, RECT *r)
{
    GetWindowRect(c, r);
    MapWindowPoints(NULL, GetParent(c), (POINT *)r, 2);
}

/* c to the rectangle l, t - r, b in the dialog units of the window units (as one rectangle, so
   controls placed this way line up across pages) */
void place_dlu(HWND units, HWND c, int l, int t, int r, int b)
{
    RECT rc;
    SetRect(&rc, l, t, r, b);
    MapDialogRect(units, &rc);
    SetWindowPos(c, NULL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

/* ---- the restart note ----------------------------------------------------------------------
   OK on the System or Extra page with a setting that only applies at the next start: one note,
   posted to the game window so it shows once the sheet has closed. */
#define WM_RESTART_NOTE          (WM_APP + 0x4C)
static int g_restartPosted;

static void restart_note_post(void)
{
    if (!g_restartPosted && *G_HWND) g_restartPosted = PostMessageA(*G_HWND, WM_RESTART_NOTE, 0, 0);
}

/* ---- Properties > System: the Display box; the Display menu ----------------------------------
   The display settings live in the System page's Display box and are saved next to the game's own
   values in HKCU\Software\Psygnosis\Lomax\System (g_disp, read before the game starts):
     Renderer         0 = Direct3D 11, 1 = GDI                                  applied at once
     StartFullscreen  1 = borderless fullscreen after the first surface init
     Widescreen       0 = off (320), 1 = 16:9 (432), 2 = max (512), 3 = custom   next start
     WidescreenWidth  the custom width (320..512, a multiple of 16)              next start
     VSync            game ticks on the display refresh                          next start
     FPSOverride, FPS frames drawn per second above the tick rate                next start
   The screen size is the window's client size (the game saves it as Settings\WinW/WinH): 1x..4x
   the internal picture, set at once. The template (dialog 102) is changed in memory before MFC
   loads it: the page grows to fit, the Display box takes the new rows, the Sound box moves down
   and "Use 4 channels only" is hidden. The new controls are created after the page's OnInitDialog
   (CSystemTab_OnInitDialog 0x4016A0); after CSystemTab_OnOK (0x4018C0) they are read. Its
   screen-size index (+0x8C, which Menu_OnProperties would pass on to Display_SetVideoMode) is
   cleared and the size applied here instead. The Display menu (Screen Size 1x..4x, Fullscreen)
   does the same at once. */
#define FN_SYS_ONINITDIALOG      0x004016A0u   /* BOOL __thiscall (CSystemTab *) */
#define FN_SYS_ONOK              0x004018C0u   /* void __thiscall (CSystemTab *) */
#define SYS_DLG_ID               102
#define IDC_SYS_SCREEN           1003          /* the game's combo box (+0xAC), "Screen Size" */
#define IDC_SYS_MUSIC            1002
#define IDC_DISP_RENDERER        1100
#define IDC_DISP_STARTFULL       1101
#define IDC_DISP_WS              1102
#define IDC_DISP_WIDTH           1103
#define IDC_DISP_VSYNC           1104
#define IDC_DISP_FPSON           1105
#define IDC_DISP_FPS             1106
#define ID_DISP_SIZE1            0x8110        /* ..0x8113: 1x..4x */
#define ID_DISP_FULLSCREEN       0x8114
#define SYS_PROP                 "LomaxPlus.SysPage"
static PageInit_t oSysInit;
static PageOK_t   oSysOK;
static int        g_sysTemplate;               /* dialog 102 changed: the page gets the new controls */
static int        g_startWs;                   /* the widescreen width this session started with */

/* as saved in the registry */
static struct {
    int gdi, startFull;
    int ws, wsCustom;         /* 0 off, 1 16:9, 2 max, 3 custom (wsCustom) */
    int vsync, fpsOn, fps;
} g_disp = { 0, 0, 0, 0, 1, 0, 120 };

static const char *const kScreenSizes[4] = { "1x (320 x 240)", "2x (640 x 480)", "3x (960 x 720)", "4x (1280 x 960)" };
static const char *const kWsModes[4] = { "Disabled (320)", "16:9 (432)", "Max (512)", "Custom (320 - 512)" };
static const int kWsPreset[3] = { 320, 432, 512 };
static const char *const kDispNames[7] = { "Renderer", "StartFullscreen", "Widescreen", "WidescreenWidth",
                                            "VSync", "FPSOverride", "FPS" };

static int *disp_val(int i)
{
    int *v[7] = { &g_disp.gdi, &g_disp.startFull, &g_disp.ws, &g_disp.wsCustom, &g_disp.vsync, &g_disp.fpsOn,
                  &g_disp.fps };
    return v[i];
}

static int ws_round(int w);

void disp_load(void)                           /* before the game starts: into cfg */
{
    HKEY k = reg_open("System", KEY_QUERY_VALUE);
    int i;
    if (k) {
        for (i = 0; i < 7; i++) reg_get_int(k, kDispNames[i], disp_val(i));
        RegCloseKey(k);
    }
    if (g_disp.ws < 0 || g_disp.ws > 3) g_disp.ws = 0;
    if (g_disp.fps < 1 || g_disp.fps > HFR_MAX_FPS) g_disp.fps = 120;
    cfg.d3d11           = !g_disp.gdi;
    cfg.startFullscreen = g_disp.startFull;
    cfg.vsync           = g_disp.vsync;
    cfg.hfrFps          = g_disp.fpsOn ? g_disp.fps : 0;
    g_startWs           = disp_ws_width();
}

static void disp_save(void)
{
    HKEY k = reg_open("System", KEY_SET_VALUE);
    int i;
    if (!k) return;
    for (i = 0; i < 7; i++) reg_set_int(k, kDispNames[i], *disp_val(i));
    RegCloseKey(k);
}

static int ws_round(int w)                     /* nearest multiple of 16 in 320..512 */
{
    w = (w + 8) & ~15;
    return w < 320 ? 320 : w > 512 ? 512 : w;
}

int disp_ws_width(void)                       /* the widescreen width set (320 = off) */
{
    return g_disp.ws == 3 ? ws_round(g_disp.wsCustom) : kWsPreset[g_disp.ws];
}

/* ---- the Display menu ---- */
static HMENU disp_menu_create(void)
{
    HMENU m = CreatePopupMenu(), sizes = CreatePopupMenu();
    int i;
    if (!m || !sizes) return NULL;
    for (i = 0; i < 4; i++) {
        char t[32];
        wsprintfA(t, "&%s", kScreenSizes[i]);
        AppendMenuA(sizes, MF_STRING, ID_DISP_SIZE1 + i, t);
    }
    AppendMenuA(m, MF_POPUP | MF_STRING, (UINT_PTR)sizes, "&Screen Size");
    AppendMenuA(m, MF_STRING, ID_DISP_FULLSCREEN, "&Fullscreen\tAlt+Enter");
    return m;
}

static void disp_menu_init(HMENU m)             /* WM_INITMENUPOPUP: any of the menu's popups */
{
    int i, win = *G_BWINDOWED != 0;
    if (GetMenuState(m, ID_DISP_SIZE1, MF_BYCOMMAND) != (UINT)-1) {
        CheckMenuRadioItem(m, ID_DISP_SIZE1, ID_DISP_SIZE1 + 3, ID_DISP_SIZE1 + window_scale() - 1, MF_BYCOMMAND);
        for (i = 0; i < 4; i++) EnableMenuItem(m, ID_DISP_SIZE1 + i, MF_BYCOMMAND | (win ? MF_ENABLED : MF_GRAYED));
    }
    if (GetMenuState(m, ID_DISP_FULLSCREEN, MF_BYCOMMAND) != (UINT)-1) {
        CheckMenuItem(m, ID_DISP_FULLSCREEN, MF_BYCOMMAND | (borderless_active() ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(m, ID_DISP_FULLSCREEN, MF_BYCOMMAND | (g_force16 ? MF_ENABLED : MF_GRAYED));
    }
}

static int disp_menu_cmd(int id)                /* 1 if it was a Display menu command */
{
    if (id >= ID_DISP_SIZE1 && id <= ID_DISP_SIZE1 + 3) {
        set_window_scale(id - ID_DISP_SIZE1 + 1);
        return 1;
    }
    if (id == ID_DISP_FULLSCREEN) {
        if (g_force16) borderless_toggle();
        return 1;
    }
    return 0;
}

/* ---- dialog 102 in memory ---- */
static const WORD *dlg_skip_sz(const WORD *p)  /* a template string or 0xFFFF ordinal */
{
    if (*p == 0xFFFF) return p + 2;
    while (*p++) ;
    return p;
}

static void sys_template_patch(void)
{
    /* every item's original and new rectangle (dialog units); -1 = keep */
    static const short kItems[19][8] = {
        {   7,   7, 181, 61,   -1,  -1,  -1, 121 },    /* Display box */
        {   7,  73, 181, 95,   -1, 133,  -1,  81 },    /* Sound box: to below the checkboxes */
        {  23,  45, 149, 63,   16,  57,  77,  -1 },    /* Screen Size combo */
        {  23,  34,  62,  8,   16,  47,  -1,  -1 },    /* "Screen Size" */
        {  47, 135,  40, 10,   -1, 195,  -1,  -1 },    /* Effects (centred at run time) */
        {  92, 135,  41, 10,   -1, 195,  -1,  -1 },    /* Ambient */
        { 135, 135,  44, 10,   -1, 195,  -1,  -1 },    /* CD Music -> Music */
        {  63,  82,  17, 50,   -1, 142,  -1,  -1 },    /* sliders */
        { 107,  82,  17, 50,   -1, 142,  -1,  -1 },
        { 152,  82,  17, 50,   -1, 142,  -1,  -1 },
        { 152,  17,  18, 20,   -1,  -1,  -1,  -1 },    /* display icon */
        { 131,  88,  21, 20,   -1, 148,  -1,  -1 },    /* volume icons */
        {  85,  88,  21, 20,   -1, 148,  -1,  -1 },
        {  41,  88,  21, 20,   -1, 148,  -1,  -1 },
        {  13,  87,  18, 20,   -1, 147,  -1,  -1 },    /* speaker icon */
        {  47, 151,  80, 10,   -1, 211,  -1,  -1 },    /* Use 4 channels only (hidden) */
        {  41, 107,  21, 20,   -1, 167,  -1,  -1 },
        {  85, 107,  21, 20,   -1, 167,  -1,  -1 },
        { 131, 107,  21, 20,   -1, 167,  -1,  -1 },
    };
    HMODULE exe = GetModuleHandleA(NULL);
    HRSRC r = FindResourceA(exe, MAKEINTRESOURCEA(SYS_DLG_ID), (LPCSTR)RT_DIALOG);
    HGLOBAL g = r ? LoadResource(exe, r) : NULL;
    DLGTEMPLATE *d = g ? (DLGTEMPLATE *)LockResource(g) : NULL;
    DLGITEMTEMPLATE *it[19];
    const WORD *p;
    DWORD size = r ? SizeofResource(exe, r) : 0, old;
    int i, j;
    if (!d || d->cdit != 19 || d->cy != 175 || !(d->style & DS_SETFONT)) goto bad;   /* the retail template */
    p = dlg_skip_sz(dlg_skip_sz(dlg_skip_sz((const WORD *)(d + 1))));   /* menu, class, title */
    p = dlg_skip_sz(p + 1);                                              /* point size, face */
    for (i = 0; i < 19; i++) {
        p = (const WORD *)(((ULONG_PTR)p + 3) & ~(ULONG_PTR)3);
        if ((const BYTE *)p + sizeof(DLGITEMTEMPLATE) > (const BYTE *)d + size) goto bad;
        it[i] = (DLGITEMTEMPLATE *)p;
        if (it[i]->x != kItems[i][0] || it[i]->y != kItems[i][1] || it[i]->cx != kItems[i][2] ||
            it[i]->cy != kItems[i][3])
            goto bad;
        p = dlg_skip_sz(dlg_skip_sz((const WORD *)(it[i] + 1)));        /* class, title */
        p = (const WORD *)((const BYTE *)p + 2 + *p);                    /* creation data */
    }
    if (!VirtualProtect(d, size, PAGE_READWRITE, &old)) goto bad;
    d->cy = SYS_DLG_HEIGHT;
    for (i = 0; i < 19; i++) {
        short *f = &it[i]->x;
        for (j = 0; j < 4; j++)
            if (kItems[i][4 + j] >= 0) f[j] = kItems[i][4 + j];
    }
    it[15]->style &= ~WS_VISIBLE;
    VirtualProtect(d, size, old, &old);
    g_sysTemplate = 1;
    return;
bad:
    log_msg("System page: unexpected dialog 102, the Display box stays as it was");
}

/* ---- the page ---- */
static HWND sys_ctl(HWND page, HWND after, const char *cls, const char *text, DWORD style, DWORD ex, int id,
                    int x, int y, int cx, int cy)
{
    RECT r;
    HWND h;
    SetRect(&r, x, y, x + cx, y + cy);
    MapDialogRect(page, &r);
    h = CreateWindowExA(ex | WS_EX_NOPARENTNOTIFY, cls, text, WS_CHILD | WS_VISIBLE | style, r.left, r.top,
                        r.right - r.left, r.bottom - r.top, page, (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL);
    if (!h) return after;
    SendMessageA(h, WM_SETFONT, SendMessageA(page, WM_GETFONT, 0, 0), FALSE);
    SetWindowPos(h, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);   /* tab order */
    return h;
}

static int dlu_x(HWND page, int x)
{
    RECT r = { 0, 0, x, 0 };
    MapDialogRect(page, &r);
    return r.right;
}

/* a checkbox just as wide as its box and text: left edge at x, or centred on cx when x < 0;
   centred on the height mid, or kept where it is when mid < 0 */
static void sys_fit_check(HWND c, int x, int cx, int mid)
{
    char t[64];
    RECT r;
    int w, h;
    GetWindowTextA(c, t, sizeof(t));
    child_rect(c, &r);
    w = GetSystemMetrics(SM_CXMENUCHECK) + 6 + text_size(c, t).cx;
    h = r.bottom - r.top;
    if (x < 0) x = cx - w / 2;
    SetWindowPos(c, NULL, x, mid >= 0 ? mid - h / 2 : r.top, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void sys_ws_changed(HWND page)
{
    int sel = (int)SendDlgItemMessageA(page, IDC_DISP_WS, CB_GETCURSEL, 0, 0);
    if (sel >= 0 && sel < 3) SetDlgItemInt(page, IDC_DISP_WIDTH, kWsPreset[sel], FALSE);
    else if (sel == 3 && g_disp.wsCustom) SetDlgItemInt(page, IDC_DISP_WIDTH, ws_round(g_disp.wsCustom), FALSE);
    EnableWindow(GetDlgItem(page, IDC_DISP_WIDTH), sel == 3);
}

static LRESULT CALLBACK sys_page_proc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == WM_COMMAND && LOWORD(w) == IDC_DISP_WS && HIWORD(w) == CBN_SELCHANGE)
        sys_ws_changed(h);
    else if (msg == WM_COMMAND && LOWORD(w) == IDC_DISP_FPSON && HIWORD(w) == BN_CLICKED)
        EnableWindow(GetDlgItem(h, IDC_DISP_FPS), IsDlgButtonChecked(h, IDC_DISP_FPSON) == BST_CHECKED);
    return subclass_next(h, SYS_PROP, msg, w, l);
}

static void sys_page_build(HWND pw)
{
    static const int sliders[3] = { 1004, 1005, 1006 };
    const DWORD label = WS_GROUP | SS_LEFT, combo = WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                check = WS_TABSTOP | BS_AUTOCHECKBOX, edit = WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER;
    HWND box = NULL, scr = GetDlgItem(pw, IDC_SYS_SCREEN), c, at, rnd, ws, wd, fps;
    RECT r, e;
    int i, ch;
    for (c = GetWindow(pw, GW_CHILD); c && !box; c = GetWindow(c, GW_HWNDNEXT)) {
        char t[16];
        if ((GetWindowLongA(c, GWL_STYLE) & 0xF) == BS_GROUPBOX && GetWindowTextA(c, t, sizeof(t)) &&
            !strcmp(t, "Display"))
            box = c;
    }
    if (!box || !scr) return;

    /* the boxes as one mapped rectangle, like the other pages' (the template rounds x and cx apart,
       one pixel wider) */
    for (c = GetWindow(pw, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if ((GetWindowLongA(c, GWL_STYLE) & 0xF) != BS_GROUPBOX) continue;
        if (c == box) place_dlu(pw, c, PAGE_BOX_L, 7, PAGE_BOX_R, 128);
        else place_dlu(pw, c, PAGE_BOX_L, 133, PAGE_BOX_R, PAGE_BOX_BOTTOM);   /* Sound */
    }

    /* Renderer, before the game's Screen Size row */
    at  = sys_ctl(pw, box, "STATIC", "Renderer", label, 0, -1, 16, 19, 60, 8);
    rnd = sys_ctl(pw, at, "COMBOBOX", "", combo, 0, IDC_DISP_RENDERER, 16, 29, 77, 60);
    SendMessageA(rnd, CB_ADDSTRING, 0, (LPARAM)"Direct3D 11");
    SendMessageA(rnd, CB_ADDSTRING, 0, (LPARAM)"GDI");
    SendMessageA(rnd, CB_SETCURSEL, cfg.d3d11 ? 0 : 1, 0);
    EnableWindow(rnd, g_force16);

    /* the game's Screen Size combo: 1x..4x; Start in Fullscreen beside it */
    SendMessageA(scr, CB_RESETCONTENT, 0, 0);
    for (i = 0; i < 4; i++) SendMessageA(scr, CB_ADDSTRING, 0, (LPARAM)kScreenSizes[i]);
    SendMessageA(scr, CB_SETCURSEL, window_scale() - 1, 0);
    child_rect(scr, &r);
    ch = r.bottom - r.top;                      /* the closed combo's height: the edits get the same */
    at = sys_ctl(pw, scr, "BUTTON", "Start in Fullscreen", check, 0, IDC_DISP_STARTFULL, 102, 58, 80, 10);
    CheckDlgButton(pw, IDC_DISP_STARTFULL, g_disp.startFull ? BST_CHECKED : BST_UNCHECKED);
    EnableWindow(at, g_force16);
    sys_fit_check(at, r.right + dlu_x(pw, 9), 0, (r.top + r.bottom) / 2);

    /* Widescreen and Width */
    at = sys_ctl(pw, at, "STATIC", "Widescreen", label, 0, -1, 16, 75, 80, 8);
    ws = sys_ctl(pw, at, "COMBOBOX", "", combo, 0, IDC_DISP_WS, 16, 85, 100, 70);
    for (i = 0; i < 4; i++) SendMessageA(ws, CB_ADDSTRING, 0, (LPARAM)kWsModes[i]);
    SendMessageA(ws, CB_SETCURSEL, g_disp.ws, 0);
    at = sys_ctl(pw, ws, "STATIC", "Width", label, 0, -1, 125, 75, 40, 8);
    wd = sys_ctl(pw, at, "EDIT", "", edit, WS_EX_CLIENTEDGE, IDC_DISP_WIDTH, 125, 85, 41, 12);
    SendMessageA(wd, EM_LIMITTEXT, 3, 0);
    SetDlgItemInt(pw, IDC_DISP_WIDTH, disp_ws_width(), FALSE);
    EnableWindow(wd, g_disp.ws == 3);
    child_rect(ws, &r);
    child_rect(wd, &e);
    SetWindowPos(wd, NULL, e.left, r.top, e.right - e.left, ch, SWP_NOZORDER | SWP_NOACTIVATE);

    /* VSync, FPS Override and its value */
    at = sys_ctl(pw, wd, "BUTTON", "VSync", check, 0, IDC_DISP_VSYNC, 16, 105, 40, 10);
    CheckDlgButton(pw, IDC_DISP_VSYNC, g_disp.vsync ? BST_CHECKED : BST_UNCHECKED);
    c = sys_ctl(pw, at, "BUTTON", "FPS Override", check, 0, IDC_DISP_FPSON, 59, 105, 60, 10);
    CheckDlgButton(pw, IDC_DISP_FPSON, g_disp.fpsOn ? BST_CHECKED : BST_UNCHECKED);
    fps = sys_ctl(pw, c, "EDIT", "", edit, WS_EX_CLIENTEDGE, IDC_DISP_FPS, 125, 104, 41, 12);
    SendMessageA(fps, EM_LIMITTEXT, 4, 0);
    SetDlgItemInt(pw, IDC_DISP_FPS, g_disp.fps, FALSE);
    EnableWindow(fps, g_disp.fpsOn);
    child_rect(fps, &e);
    SetWindowPos(fps, NULL, e.left, e.top, e.right - e.left, ch, SWP_NOZORDER | SWP_NOACTIVATE);
    child_rect(at, &r);
    sys_fit_check(at, r.left, 0, e.top + ch / 2);
    child_rect(c, &r);
    sys_fit_check(c, r.left, 0, e.top + ch / 2);

    /* Sound: "Music", each checkbox centred under its slider */
    SetDlgItemTextA(pw, IDC_SYS_MUSIC, "Music");
    for (i = 0; i < 3; i++) {
        HWND s = GetDlgItem(pw, sliders[i]), b = GetDlgItem(pw, 1000 + i);
        if (!s || !b) continue;
        child_rect(s, &r);
        sys_fit_check(b, -1, (r.left + r.right) / 2, -1);
    }

    subclass(pw, SYS_PROP, sys_page_proc);
}

static int __fastcall hkSysInit(BYTE *page, void *edx)
{
    int ret = oSysInit(page, edx);              /* attaches the controls (DDX), fills the combo */
    if (g_sysTemplate) sys_page_build(*(HWND *)(page + 0x1C));
    return ret;
}

static void __fastcall hkSysOK(BYTE *page, void *edx)
{
    HWND pw = *(HWND *)(page + 0x1C);
    BOOL ok;
    int k, v, gdi;
    oSysOK(page, edx);                          /* sound flags and volumes; +0x8C = screen size index */
    if (!g_sysTemplate || !GetDlgItem(pw, IDC_DISP_WS)) return;
    k = *(int *)(page + 0x8C) + 1;
    *(int *)(page + 0x8C) = 0;                  /* Menu_OnProperties: no Display_SetVideoMode */

    gdi = (int)SendDlgItemMessageA(pw, IDC_DISP_RENDERER, CB_GETCURSEL, 0, 0) == 1;
    g_disp.startFull = IsDlgButtonChecked(pw, IDC_DISP_STARTFULL) == BST_CHECKED;
    g_disp.ws = (int)SendDlgItemMessageA(pw, IDC_DISP_WS, CB_GETCURSEL, 0, 0);
    if (g_disp.ws < 0 || g_disp.ws > 3) g_disp.ws = 0;
    if (g_disp.ws == 3) {
        v = (int)GetDlgItemInt(pw, IDC_DISP_WIDTH, &ok, FALSE);
        g_disp.wsCustom = ws_round(ok ? v : 432);
    }
    g_disp.vsync = IsDlgButtonChecked(pw, IDC_DISP_VSYNC) == BST_CHECKED;
    g_disp.fpsOn = IsDlgButtonChecked(pw, IDC_DISP_FPSON) == BST_CHECKED;
    v = (int)GetDlgItemInt(pw, IDC_DISP_FPS, &ok, FALSE);
    if (ok) g_disp.fps = v < 1 ? 1 : v > HFR_MAX_FPS ? HFR_MAX_FPS : v;

    if (gdi != g_disp.gdi && g_force16) present_set_renderer(!gdi);   /* the renderer changes at once */
    g_disp.gdi = gdi;
    disp_save();
    if (k >= 1 && k <= 4 && k != window_scale()) set_window_scale(k);

    /* settings that differ from what this session runs with */
    if (disp_ws_width() != g_startWs || g_disp.vsync != cfg.vsync || (g_disp.fpsOn ? g_disp.fps : 0) != cfg.hfrFps)
        restart_note_post();
}

static void sys_page_init(void)
{
    static const BYTE sysInit[6] = { 0x56, 0x57, 0x8B, 0xF1, 0xE8, 0x74 };
    sys_template_patch();
    if (!g_sysTemplate) return;
    if (!code_matches(FN_SYS_ONINITDIALOG, sysInit, 6)) {
        log_msg("System page: unexpected code at %08lX", (unsigned long)FN_SYS_ONINITDIALOG);
        g_sysTemplate = 0;
        return;
    }
    mh_hook(FN_SYS_ONINITDIALOG, hkSysInit, (void **)&oSysInit, "CSystemTab_OnInitDialog");
    mh_hook(FN_SYS_ONOK, hkSysOK, (void **)&oSysOK, "CSystemTab_OnOK");
}

/* ---- Properties > Extra -----------------------------------------------------------------------
   A third page of the Properties sheet, after Controls, with the options that used to be in
   LOADER.INI (now gone). They are saved in HKCU\Software\Psygnosis\Lomax\Extra (g_extra, read before the game
   starts):
     LoadingScreen, LanguageSelect, Speed50Hz   console loading screen, language menu, PAL
     Cheats                                     the built-in cheat keys (applied at once)
     RGBEmulation, FastBlit, Transparency       transparency 0 auto, 1 solid, 2 dither, 3 blend
     Log                                        LOADER.LOG (applied at once)
     DemoTrace, Mute                            testing aids, not on the page
   The page is added when the game's sheet ("Lomax Properties", System + Controls) reaches
   PropertySheetA: MFC passes its pages as a PROPSHEETPAGE array (PSH_PROPSHEETPAGE), so a copy gets
   one more entry with a template built here (PSP_DLGINDIRECT) and its own dialog procedure, which
   reads the controls on PSN_APPLY. Settings that only take effect at the next start bring up the
   restart note, as on the System page. */
#define IDC_EXTRA_LOADSCREEN     1200
#define IDC_EXTRA_LANGSELECT     1201
#define IDC_EXTRA_SPEED50        1202
#define IDC_EXTRA_CHEATS         1203
#define IDC_EXTRA_RGB            1204
#define IDC_EXTRA_FASTBLIT       1205
#define IDC_EXTRA_TRANSP         1206
#define IDC_EXTRA_LOG            1207
static const int kTranspMode[4] = { TRANSP_AUTO, TRANSP_SOLID, TRANSP_DITHER, TRANSP_BLEND };   /* by value */
static const char *const kTranspNames[4] = { "Automatic", "Solid", "Dither", "Blend" };
static const char *const kExtraNames[10] = { "LoadingScreen", "LanguageSelect", "Speed50Hz", "Cheats",
                                             "RGBEmulation", "FastBlit", "Transparency", "Log",
                                             "DemoTrace", "Mute" };
static DWORD g_extraTpl[256];                  /* the page's dialog template */

/* as saved in the registry */
static struct {
    int loadScreen, langSelect, speed50, cheats;
    int rgbD3D, fastBlit, transp;  /* transp: 0 auto, 1 solid, 2 dither, 3 blend */
    int log;
    int dbgTrace, dbgMute;         /* testing aids: registry only */
} g_extra = { 0, 0, 0, 0, 1, 1, 0, 0, 0, 0 };

static int *extra_val(int i)
{
    int *v[10] = { &g_extra.loadScreen, &g_extra.langSelect, &g_extra.speed50, &g_extra.cheats, &g_extra.rgbD3D,
                   &g_extra.fastBlit, &g_extra.transp, &g_extra.log, &g_extra.dbgTrace, &g_extra.dbgMute };
    return v[i];
}

void extra_load(void)                          /* before the game starts: into cfg */
{
    HKEY k = reg_open("Extra", KEY_QUERY_VALUE);
    int i;
    if (k) {
        for (i = 0; i < 10; i++) reg_get_int(k, kExtraNames[i], extra_val(i));
        RegCloseKey(k);
    }
    if (g_extra.transp < 0 || g_extra.transp > 3) g_extra.transp = 0;
    cfg.log          = g_extra.log;
    cfg.rgbD3D       = g_extra.rgbD3D;
    cfg.transparency = kTranspMode[g_extra.transp];
    cfg.dbgTrace     = g_extra.dbgTrace;
    cfg.dbgMute      = g_extra.dbgMute;
    cfg.cheats       = g_extra.cheats;
    cfg.langSelect   = g_extra.langSelect;
    cfg.loadScreen   = g_extra.loadScreen;
    cfg.speed50      = g_extra.speed50;
    cfg.fastBlit     = g_extra.fastBlit;
}

static void extra_save(void)                   /* the page's eight, not the testing aids */
{
    HKEY k = reg_open("Extra", KEY_SET_VALUE);
    int i;
    if (!k) return;
    for (i = 0; i < 8; i++) reg_set_int(k, kExtraNames[i], *extra_val(i));
    RegCloseKey(k);
}

/* ---- the page's template ---- */
static WORD *tpl_str(WORD *p, const char *s)
{
    while ((*p++ = (BYTE)*s++) != 0) ;
    return p;
}

static WORD *tpl_item(WORD *p, WORD cls, DWORD style, int x, int y, int cx, int cy, int id, const char *text)
{
    DLGITEMTEMPLATE *it = (DLGITEMTEMPLATE *)(((ULONG_PTR)p + 3) & ~(ULONG_PTR)3);
    it->style = WS_CHILD | WS_VISIBLE | style;
    it->dwExtendedStyle = 0;
    it->x = (short)x; it->y = (short)y; it->cx = (short)cx; it->cy = (short)cy;
    it->id = (WORD)id;
    p = (WORD *)(it + 1);
    *p++ = 0xFFFF;                              /* predefined class */
    *p++ = cls;
    p = tpl_str(p, text);
    *p++ = 0;                                   /* no creation data */
    return p;
}

/* the page's controls in dialog units (the game's margins), in tab order */
static const struct { WORD cls; DWORD style; short x, y, cx, cy; int id; const char *text; } kExtraItems[12] = {
    { 0x80, BS_GROUPBOX,               PAGE_BOX_L,   7, PAGE_BOX_R - PAGE_BOX_L, 53, -1, "Game" },
    { 0x80, WS_TABSTOP | BS_AUTOCHECKBOX,                16,  23,  70, 10, IDC_EXTRA_LOADSCREEN, "Loading Screen" },
    { 0x80, WS_TABSTOP | BS_AUTOCHECKBOX,               102,  23,  75, 10, IDC_EXTRA_LANGSELECT, "Language Select" },
    { 0x80, WS_TABSTOP | BS_AUTOCHECKBOX,                16,  42,  70, 10, IDC_EXTRA_SPEED50, "50Hz Mode" },
    { 0x80, WS_TABSTOP | BS_AUTOCHECKBOX,               102,  42,  75, 10, IDC_EXTRA_CHEATS, "Cheat Mode" },
    { 0x80, BS_GROUPBOX,               PAGE_BOX_L,  66, PAGE_BOX_R - PAGE_BOX_L, 63, -1, "Fixes" },
    { 0x80, WS_TABSTOP | BS_AUTOCHECKBOX,                16,  82,  70, 10, IDC_EXTRA_RGB, "RGB Emulation" },
    { 0x80, WS_TABSTOP | BS_AUTOCHECKBOX,               102,  82,  75, 10, IDC_EXTRA_FASTBLIT, "Fast Blit" },
    { 0x82, WS_GROUP | SS_LEFT,                          16,  98,  60,  8, -1, "Transparency" },
    { 0x85, WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,  16, 108,  78, 60, IDC_EXTRA_TRANSP, "" },
    { 0x80, BS_GROUPBOX,               PAGE_BOX_L, 134, PAGE_BOX_R - PAGE_BOX_L, 33, -1, "Other" },
    { 0x80, WS_TABSTOP | BS_AUTOCHECKBOX,                16, 150,  80, 10, IDC_EXTRA_LOG, "Loader Debug Log" },
};

static void extra_template(void)
{
    DLGTEMPLATE *d = (DLGTEMPLATE *)g_extraTpl;
    WORD *p;
    int i;
    d->style = WS_CHILD | WS_DISABLED | WS_CAPTION | DS_SETFONT;
    d->dwExtendedStyle = 0;
    d->cdit = 12;
    d->x = d->y = 0; d->cx = 195; d->cy = SYS_DLG_HEIGHT;
    p = (WORD *)(d + 1);
    *p++ = 0;                                   /* no menu */
    *p++ = 0;                                   /* default class */
    p = tpl_str(p, "Extra");
    *p++ = 8;
    p = tpl_str(p, "MS Sans Serif");
    for (i = 0; i < 12; i++)
        p = tpl_item(p, kExtraItems[i].cls, kExtraItems[i].style, kExtraItems[i].x, kExtraItems[i].y,
                     kExtraItems[i].cx, kExtraItems[i].cy, kExtraItems[i].id, kExtraItems[i].text);
}

/* MFC gives its pages the sheet's font, which this page's template can't name: take the System
   page's font and place the controls with its dialog units, so both pages look the same */
static void extra_match_font(HWND h)
{
    HWND sys = (HWND)SendMessageA(GetParent(h), PSM_INDEXTOHWND, 0, 0), c;
    HFONT f = sys ? (HFONT)SendMessageA(sys, WM_GETFONT, 0, 0) : NULL;
    int i = 0;
    if (!f) return;
    SendMessageA(h, WM_SETFONT, (WPARAM)f, FALSE);
    for (c = GetWindow(h, GW_CHILD); c && i < 12; c = GetWindow(c, GW_HWNDNEXT), i++) {
        SendMessageA(c, WM_SETFONT, (WPARAM)f, FALSE);
        place_dlu(sys, c, kExtraItems[i].x, kExtraItems[i].y, kExtraItems[i].x + kExtraItems[i].cx,
                  kExtraItems[i].y + kExtraItems[i].cy);
    }
}

/* ---- the page ---- */
static INT_PTR CALLBACK extra_proc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    static const struct { int id, *v; } kChecks[7] = {
        { IDC_EXTRA_LOADSCREEN, &g_extra.loadScreen }, { IDC_EXTRA_LANGSELECT, &g_extra.langSelect },
        { IDC_EXTRA_SPEED50, &g_extra.speed50 }, { IDC_EXTRA_CHEATS, &g_extra.cheats },
        { IDC_EXTRA_RGB, &g_extra.rgbD3D }, { IDC_EXTRA_FASTBLIT, &g_extra.fastBlit }, { IDC_EXTRA_LOG, &g_extra.log },
    };
    int i;
    (void)w;
    if (msg == WM_INITDIALOG) {
        for (i = 0; i < 7; i++) CheckDlgButton(h, kChecks[i].id, *kChecks[i].v ? BST_CHECKED : BST_UNCHECKED);
        for (i = 0; i < 4; i++) SendDlgItemMessageA(h, IDC_EXTRA_TRANSP, CB_ADDSTRING, 0, (LPARAM)kTranspNames[i]);
        SendDlgItemMessageA(h, IDC_EXTRA_TRANSP, CB_SETCURSEL, g_extra.transp, 0);
        extra_match_font(h);
        return TRUE;
    }
    if (msg == WM_NOTIFY && ((NMHDR *)l)->code == PSN_APPLY) {
        int cheats = g_extra.cheats, t;
        for (i = 0; i < 7; i++) *kChecks[i].v = IsDlgButtonChecked(h, kChecks[i].id) == BST_CHECKED;
        t = (int)SendDlgItemMessageA(h, IDC_EXTRA_TRANSP, CB_GETCURSEL, 0, 0);
        if (t >= 0 && t <= 3) g_extra.transp = t;
        extra_save();
        if (g_extra.cheats != cheats) *G_BCHEATSENABLED = g_extra.cheats;    /* at once */
        if (g_extra.log != cfg.log) {
            cfg.log = g_extra.log;
            log_msg("log on (Properties > Extra)");
        }
        if (g_extra.loadScreen != cfg.loadScreen || g_extra.langSelect != cfg.langSelect ||
            g_extra.speed50 != cfg.speed50 || g_extra.rgbD3D != cfg.rgbD3D || g_extra.fastBlit != cfg.fastBlit ||
            kTranspMode[g_extra.transp] != cfg.transparency)
            restart_note_post();
        SetWindowLongPtrA(h, DWLP_MSGRESULT, PSNRET_NOERROR);
        return TRUE;
    }
    return FALSE;
}

/* the sheet (below): the game's sheet gets the Extra page after its two (into the caller's buffer) */
static int extra_add_page(PROPSHEETHEADERA *h, BYTE *buf, size_t size)
{
    DWORD stride;
    PROPSHEETPAGEA *pg;
    if (!(h->dwFlags & PSH_PROPSHEETPAGE) || h->nPages != 2 || !h->ppsp ||
        (stride = h->ppsp->dwSize) < PROPSHEETPAGEA_V1_SIZE || stride * 3 > size) {
        log_msg("Properties: unexpected sheet (flags %08lX, %u pages), no Extra page",
                (unsigned long)h->dwFlags, (unsigned)h->nPages);
        return 0;
    }
    if (!((DLGTEMPLATE *)g_extraTpl)->cdit) extra_template();
    memcpy(buf, h->ppsp, stride * 2);
    pg = (PROPSHEETPAGEA *)(buf + stride * 2);
    memset(pg, 0, stride);
    pg->dwSize = stride;
    pg->dwFlags = PSP_DLGINDIRECT;
    pg->hInstance = GetModuleHandleA(NULL);
    pg->pResource = (LPCDLGTEMPLATEA)g_extraTpl;
    pg->pfnDlgProc = extra_proc;
    h->ppsp = (LPCPROPSHEETPAGEA)buf;
    h->nPages = 3;
    return 1;
}

/* ---- Lomax+ branding -----------------------------------------------------------------------
   The window title ("Lomax", CreateWindowExA in CLomaxApp_InitInstance 0x4033B5), the menu's
   "&About Lomax..." (menu 135) and the About dialog (template 141: its caption and "Lomax for
   Windows 95"; MFC creates it through CreateDialogIndirectParamA), the Properties sheet's caption
   ("Lomax Properties", PropertySheetA: see the sheet below), dialogs titled "Lomax" (the exit question)
   and the message boxes' title (MFC's AfxMessageBox uses the app name "Lomax") say "Lomax+"
   instead. Message texts stay as they are. The same hooks add the Display and Sound menus to the
   main menu and subclass the game window (game_wndproc). */
typedef HWND  (WINAPI *CreateWindowExA_t)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU,
                                          HINSTANCE, LPVOID);
typedef HMENU (WINAPI *LoadMenuA_t)(HINSTANCE, LPCSTR);
typedef HWND  (WINAPI *CreateDialogIndirectParamA_t)(HINSTANCE, LPCDLGTEMPLATEA, HWND, DLGPROC, LPARAM);
typedef int   (WINAPI *MessageBoxA_t)(HWND, LPCSTR, LPCSTR, UINT);
static CreateWindowExA_t            oCreateWindowExA;
static LoadMenuA_t                  oLoadMenuA;
static CreateDialogIndirectParamA_t oCreateDialogIndirectParamA;
static MessageBoxA_t                oMessageBoxA;

static void menus_add(HMENU m);
static void game_subclass(HWND h);

static int brand(const char *in, char *out, int n)    /* "+" after every word "Lomax"; 1 if changed */
{
    int o = 0, changed = 0;
    while (*in && o < n - 1) {
        if (!strncmp(in, "Lomax", 5) && in[5] != '+' && !isalnum((unsigned char)in[5]) && o + 6 < n) {
            memcpy(out + o, "Lomax+", 6);
            o += 6; in += 5; changed = 1;
        } else {
            out[o++] = *in++;
        }
    }
    out[o] = 0;
    return changed;
}

static void brand_window(HWND h)
{
    char s[512], t[512];
    if (GetWindowTextA(h, s, sizeof(s)) && brand(s, t, sizeof(t))) SetWindowTextA(h, t);
}

static void brand_menu(HMENU m)
{
    int i, n = GetMenuItemCount(m);
    for (i = 0; i < n; i++) {
        char s[128], t[128];
        HMENU sub = GetSubMenu(m, i);
        if (sub) brand_menu(sub);
        if (GetMenuStringA(m, i, s, sizeof(s), MF_BYPOSITION) > 0 && brand(s, t, sizeof(t))) {
            MENUITEMINFOA mi;
            memset(&mi, 0, sizeof(mi));
            mi.cbSize = sizeof(mi);
            mi.fMask = MIIM_STRING;
            mi.dwTypeData = t;
            SetMenuItemInfoA(m, i, TRUE, &mi);
        }
    }
}

static HWND WINAPI hkCreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int w, int h,
                                     HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
    char t[256];
    HWND r;
    HMENU m;
    if (name && !IS_INTRESOURCE(name) && brand(name, t, sizeof(t))) name = t;
    r = oCreateWindowExA(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
    if (r && !(style & WS_CHILD) && (m = GetMenu(r)) != NULL) {   /* the class's menu */
        brand_menu(m);
        menus_add(m);
        DrawMenuBar(r);
    }
    if (r && cls && !IS_INTRESOURCE(cls) && !strcmp(cls, "LomaxClass")) game_subclass(r);
    return r;
}

static HMENU WINAPI hkLoadMenuA(HINSTANCE inst, LPCSTR name)
{
    HMENU m = oLoadMenuA(inst, name);
    if (m) {
        brand_menu(m);
        menus_add(m);
    }
    return m;
}

static HWND WINAPI hkCreateDialogIndirectParamA(HINSTANCE inst, LPCDLGTEMPLATEA tpl, HWND parent, DLGPROC proc,
                                               LPARAM lp)
{
    HWND h = oCreateDialogIndirectParamA(inst, tpl, parent, proc, lp), c;
    char s[64];
    if (h && GetWindowTextA(h, s, sizeof(s)) && !strncmp(s, "About ", 6)) {   /* still hidden here */
        brand_window(h);
        for (c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
            brand_window(c);
    } else if (h && !strcmp(s, "Lomax")) {                                   /* e.g. the exit question */
        SetWindowTextA(h, "Lomax+");
    }
    return h;
}

static int WINAPI hkMessageBoxA(HWND w, LPCSTR text, LPCSTR caption, UINT type)
{
    char t[128];
    if (caption && brand(caption, t, sizeof(t))) caption = t;
    return oMessageBoxA(w, text, caption, type);
}

/* ---- the Properties sheet ------------------------------------------------------------------
   "Lomax Properties" (System, Controls): its caption says Lomax+, it gets the Extra page, and its
   tab area is fitted to the pages. */
typedef INT_PTR (WINAPI *PropertySheetA_t)(LPCPROPSHEETHEADERA);
static PropertySheetA_t oPropertySheetA;

/* the sheet sizes its tab area wider than the pages, which leaves a gap right of the group boxes:
   narrow the tab control (and the sheet, its buttons) to the left gap. Pages get the sheet's old
   width when they are created, so every sheet message ends with the current page trimmed. */
#define SHEET_PROP               "LomaxPlus.Sheet"
static int g_pageCx;

static void sheet_trim_page(HWND s)
{
    HWND pg = (HWND)SendMessageA(s, PSM_GETCURRENTPAGEHWND, 0, 0);
    RECT r;
    if (pg && GetWindowRect(pg, &r) && r.right - r.left > g_pageCx)
        SetWindowPos(pg, NULL, 0, 0, g_pageCx, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static LRESULT CALLBACK sheet_proc(HWND s, UINT msg, WPARAM w, LPARAM l)
{
    static int busy;                            /* sheet_trim_page sends the sheet messages too */
    LRESULT r = subclass_next(s, SHEET_PROP, msg, w, l);
    if (msg != WM_NCDESTROY && !busy) {
        busy = 1;
        sheet_trim_page(s);
        busy = 0;
    }
    return r;
}

static void sheet_fit(HWND s)
{
    HWND tab = (HWND)SendMessageA(s, PSM_GETTABCONTROL, 0, 0);
    HWND pg = (HWND)SendMessageA(s, PSM_GETCURRENTPAGEHWND, 0, 0), c;
    RECT t, b, p, w;
    int cut;
    if (!tab || !pg) return;
    for (c = GetWindow(pg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if ((GetWindowLongA(c, GWL_STYLE) & 0xF) == BS_GROUPBOX) break;
    if (!c) return;
    GetWindowRect(tab, &t);
    GetWindowRect(c, &b);
    cut = (t.right - b.right) - (b.left - t.left);
    if (cut <= 0) return;
    for (c = GetWindow(s, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {   /* OK, Cancel (Apply, Help) */
        char cls[16];
        RECT r;
        GetClassNameA(c, cls, sizeof(cls));
        if (_stricmp(cls, "Button")) continue;
        child_rect(c, &r);
        SetWindowPos(c, NULL, r.left - cut, r.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    SetWindowPos(tab, NULL, 0, 0, t.right - t.left - cut, t.bottom - t.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    GetWindowRect(pg, &p);
    g_pageCx = p.right - p.left - cut;
    sheet_trim_page(s);
    GetWindowRect(s, &w);                       /* the same centre */
    SetWindowPos(s, NULL, w.left + cut / 2, w.top, w.right - w.left - cut, w.bottom - w.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    subclass(s, SHEET_PROP, sheet_proc);
}

static INT_PTR WINAPI hkPropertySheetA(LPCPROPSHEETHEADERA psh)
{
    PROPSHEETHEADERA h;
    DWORD pages[3 * sizeof(PROPSHEETPAGEA) / sizeof(DWORD) + 16];
    char t[128];
    int game;
    INT_PTR r;
    if (!psh || !psh->pszCaption || IS_INTRESOURCE(psh->pszCaption)) return oPropertySheetA(psh);
    memset(&h, 0, sizeof(h));
    memcpy(&h, psh, psh->dwSize < sizeof(h) ? psh->dwSize : sizeof(h));
    game = !strcmp(psh->pszCaption, "Lomax Properties");
    if (brand(psh->pszCaption, t, sizeof(t))) h.pszCaption = t;
    if (game) extra_add_page(&h, (BYTE *)pages, sizeof(pages));
    r = oPropertySheetA(&h);
    if (game && (h.dwFlags & PSH_MODELESS) && r && r != -1) sheet_fit((HWND)r);   /* MFC: modeless */
    return r;
}

/* ---- Sound menu; no "Use 4 channels only" ----------------------------------------------------
   The Properties dialog's System page (CSystemTab, dialog 102) has the Effects / Ambient / Music
   checkboxes (g_cfgUseEffects / g_cfgUseAmbient / g_cfgUseCD) and "Use 4 channels only"
   (g_cfg4Channels: the mixer limited to 4 voices, no ambient). After OK, Menu_OnProperties
   (0x40E2C0) compares them with their old values and starts or stops what changed: the music
   (CD_PlayTrackWrapper with the level's track / CD_Stop), the sound driver (SndDrv_Start when the
   first of effects and ambient comes on, SndDrv_Pause when both are off), the looping effect voices
   (Sound_StopVoice) and the ambient sound (Sound_StartAmbient / Sound_StopAmbient).
   "Use 4 channels only" is gone: the checkbox is hidden (see the System page above) and g_cfg4Channels stays
   0 (InitInstance no longer stores the saved value, so the game writes back 0 on exit). A Sound
   menu between Display and Language toggles the three with the same code as the dialog
   (sound_apply); its items are checked from the flags and greyed when the sound or the music isn't
   available. The Display menu (between File and Sound) is with the System page above. */
#define SYS_4CH_STORE            0x0040300Au   /* InitInstance: mov [g_cfg4Channels], eax (5 bytes) */
#define FN_CD_PLAYTRACK          0x0040E0B0u   /* void (int track) */
#define FN_CD_STOP               0x0040E0E0u
#define FN_SNDDRV_START          0x00438750u
#define FN_SNDDRV_PAUSE          0x00438770u
#define FN_SOUND_STOPVOICE       0x0040D840u   /* void (int voice) */
#define FN_SOUND_STARTAMBIENT    0x0040D9A0u
#define FN_SOUND_STOPAMBIENT     0x0040DA90u
#define G_CFGUSEEFFECTS          ((volatile int *)0x00461CECu)
#define G_CFGUSEAMBIENT          ((volatile int *)0x00461CF0u)
#define G_CFGUSECD               ((volatile int *)0x00461CF4u)
#define G_CFG4CHANNELS           ((volatile int *)0x00461CF8u)
#define G_CDTRACK                ((volatile int *)0x00461CFCu)   /* the level's music track */
#define G_BSOUNDAVAILABLE        ((volatile int *)0x00461C78u)
#define G_LOOPVOICE1             ((volatile int *)0x004645CCu)   /* looping effects (helicopter...) */
#define G_LOOPVOICE2             ((volatile int *)0x004645D0u)
#define ID_SOUND_EFFECTS         0x8100
#define ID_SOUND_AMBIENT         0x8101
#define ID_SOUND_CD              0x8102

/* what Menu_OnProperties does after OK, for the three flags' old values */
static void sound_apply(int oldFx, int oldAmb, int oldCD)
{
    int fx = *G_CFGUSEEFFECTS, amb = *G_CFGUSEAMBIENT, cd = *G_CFGUSECD;
    if (!oldCD && cd) ((IntArgFn_t)FN_CD_PLAYTRACK)(*G_CDTRACK);
    if (oldCD && !cd) ((VoidFn_t)FN_CD_STOP)();
    if (!(oldFx || oldAmb) && (fx || amb)) ((VoidFn_t)FN_SNDDRV_START)();
    if ((oldFx || oldAmb) && !(fx || amb)) ((VoidFn_t)FN_SNDDRV_PAUSE)();
    if (oldFx && !fx) {
        ((IntArgFn_t)FN_SOUND_STOPVOICE)(*G_LOOPVOICE1);
        *G_LOOPVOICE1 = -1;
        ((IntArgFn_t)FN_SOUND_STOPVOICE)(*G_LOOPVOICE2);
        *G_LOOPVOICE2 = -1;
    }
    if (!oldAmb && amb) ((VoidFn_t)FN_SOUND_STARTAMBIENT)();
    if (oldAmb && !amb) ((VoidFn_t)FN_SOUND_STOPAMBIENT)();
}

static void sound_menu_init(HMENU m)            /* WM_INITMENUPOPUP */
{
    int snd = *G_BSOUNDAVAILABLE != 0, cd = *G_BCDAUDIOAVAILABLE != 0;
    CheckMenuItem(m, ID_SOUND_EFFECTS, MF_BYCOMMAND | (snd && *G_CFGUSEEFFECTS ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, ID_SOUND_AMBIENT, MF_BYCOMMAND | (snd && *G_CFGUSEAMBIENT ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, ID_SOUND_CD, MF_BYCOMMAND | (cd && *G_CFGUSECD ? MF_CHECKED : MF_UNCHECKED));
    EnableMenuItem(m, ID_SOUND_EFFECTS, MF_BYCOMMAND | (snd ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, ID_SOUND_AMBIENT, MF_BYCOMMAND | (snd ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, ID_SOUND_CD, MF_BYCOMMAND | (cd ? MF_ENABLED : MF_GRAYED));
}

static int sound_menu_cmd(int id)               /* 1 if it was a Sound menu command */
{
    int fx = *G_CFGUSEEFFECTS, amb = *G_CFGUSEAMBIENT, cd = *G_CFGUSECD;
    if (id < ID_SOUND_EFFECTS || id > ID_SOUND_CD) return 0;
    if (id == ID_SOUND_CD ? !*G_BCDAUDIOAVAILABLE : !*G_BSOUNDAVAILABLE) return 1;
    if (id == ID_SOUND_EFFECTS) *G_CFGUSEEFFECTS = !fx;
    if (id == ID_SOUND_AMBIENT) *G_CFGUSEAMBIENT = !amb;
    if (id == ID_SOUND_CD)      *G_CFGUSECD = !cd;
    sound_apply(fx, amb, cd);
    return 1;
}

static void menus_add(HMENU m)             /* the main menu: Display and Sound between File and Language */
{
    char s[32];
    HMENU sub, disp;
    if (GetMenuItemCount(m) < 2 || GetMenuStringA(m, 0, s, sizeof(s), MF_BYPOSITION) <= 0 || !strstr(s, "File") ||
        GetMenuState(m, ID_SOUND_EFFECTS, MF_BYCOMMAND) != (UINT)-1 || !(sub = CreatePopupMenu()))
        return;
    AppendMenuA(sub, MF_STRING, ID_SOUND_EFFECTS, "&Effects");
    AppendMenuA(sub, MF_STRING, ID_SOUND_AMBIENT, "&Ambient");
    AppendMenuA(sub, MF_STRING, ID_SOUND_CD, "&Music");
    InsertMenuA(m, 1, MF_BYPOSITION | MF_POPUP | MF_STRING, (UINT_PTR)sub, "&Sound");
    if ((disp = disp_menu_create()) != NULL)
        InsertMenuA(m, 1, MF_BYPOSITION | MF_POPUP | MF_STRING, (UINT_PTR)disp, "&Display");
}

/* ---- the game window (class LomaxClass) ---- */
#define GAME_PROP                "LomaxPlus.GameWnd"

static LRESULT CALLBACK game_wndproc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == WM_INITMENUPOPUP) {
        sound_menu_init((HMENU)w);
        disp_menu_init((HMENU)w);
    } else if (msg == WM_COMMAND && HIWORD(w) == 0 && (disp_menu_cmd(LOWORD(w)) || sound_menu_cmd(LOWORD(w)))) {
        return 0;
    } else if (msg == WM_RESTART_NOTE) {
        g_restartPosted = 0;
        MessageBoxA(h, "Some settings need a restart to take effect.", "Lomax+", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    return subclass_next(h, GAME_PROP, msg, w, l);
}

static void game_subclass(HWND h)
{
    subclass(h, GAME_PROP, game_wndproc);
}

static void four_channels_off(void)
{
    static const BYTE store[5] = { 0xA3, 0xF8, 0x1C, 0x46, 0x00 };
    static const BYTE nops[5] = { 0x90, 0x90, 0x90, 0x90, 0x90 };
    if (!code_matches(SYS_4CH_STORE, store, 5)) {
        log_msg("sound: unexpected code at %08lX, 4 channels not forced off", (unsigned long)SYS_4CH_STORE);
        return;
    }
    patch_code(SYS_4CH_STORE, nops, 5);         /* g_cfg4Channels keeps its 0 */
    *G_CFG4CHANNELS = 0;
}

void ui_init(void)
{
    hook_iat("USER32.dll", "CreateWindowExA", hkCreateWindowExA, (void **)&oCreateWindowExA);
    hook_iat("USER32.dll", "LoadMenuA", hkLoadMenuA, (void **)&oLoadMenuA);
    hook_iat("USER32.dll", "CreateDialogIndirectParamA", hkCreateDialogIndirectParamA,
             (void **)&oCreateDialogIndirectParamA);
    hook_iat("COMCTL32.dll", "PropertySheetA", hkPropertySheetA, (void **)&oPropertySheetA);
    hook_iat("USER32.dll", "MessageBoxA", hkMessageBoxA, (void **)&oMessageBoxA);
    sys_page_init();
    four_channels_off();
}
