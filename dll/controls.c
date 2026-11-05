/*
 * controls.c - a keyboard and a controller input for every action: the bindings, the controller,
 * the Controls page.
 */
#include "lomax.h"

/* ---- Controls: a keyboard and a controller input for every action (always on) ---------------
   The game binds one input per action: g_KeyBinds[13] (Left, Right, Up, Down, Jump, Shoot, Ability,
   Select, Run, Helm, LookDown, LookUp, Pause) holds indices into g_DIKeyState, DirectInput scancodes
   1..0xFF for keys and 0x100..0x10C for joystick 0 (Joy-Left/Right/Up/Down from the X/Y axes,
   Joy-Button1..9; Input_ReadKeyboardAndJoystick). The Controls page (CControlsTab) copies them to
   page+0x94 and its 20 ms timer (CControlsTab_OnTimer) gives the focused field the first new press,
   unless another action already has it.
   Here every action has a keyboard slot (g_KeyBinds, saved by the game as before) and a controller
   slot (g_padBinds, saved as Pad<action> next to the game's values in HKCU\Software\Psygnosis\
   Lomax\Controls). The controller is read here as well (pad_poll, joystick 0 through joyGetPosEx):
   besides the game's X/Y axes and buttons 1..9, buttons 10..16, the POV hat (D-pad), the Z axis
   (an Xbox controller's triggers: LT up, RT down) and the U/R axes (its right stick). Controller
   codes keep the game's numbers (0x100..0x10C) and go on above them (0x110..0x121).
   On the page a key sets the focused field's keyboard slot and a controller input its controller
   slot; an input another action has is swapped with it. Right-click empties a field, the Reset
   button (added to the Directional Buttons box) puts back the default bindings. The fields show both
   ("W / A"). In play Input_Update reads g_DIKeyState[g_KeyBinds[i]]: right after it polls the
   devices, an action whose controller input is down reads PAD_HELD instead (a byte the game clears
   on every read, set here), and g_KeyBinds is put back afterwards. With no saved controller slots
   yet, joystick inputs bound in g_KeyBinds move to the controller slot (the keyboard slot gets its
   default key back when free) and the others get a default layout (left stick, A jump, B shoot,
   X ability, LB select, Y helmet, RB run, Start pause, in the order Windows numbers an Xbox
   controller's buttons). */
#define FN_INPUT_UPDATE          0x00416660u   /* void (void): builds g_InputHeld from g_KeyBinds */
#define FN_INPUT_READ            0x004068C0u   /* void (void): joystick 0 + keyboard into g_DIKeyState */
#define FN_INPUT_FIRST_PRESS     0x00406A90u   /* int (void): first index newly down, 0 = none */
#define FN_CTL_ONINITDIALOG      0x00402290u   /* BOOL __thiscall (CControlsTab *) */
#define FN_CTL_ONOK              0x00402390u   /* void __thiscall (CControlsTab *) */
#define FN_CTL_ONTIMER           0x00402220u   /* void __thiscall (CControlsTab *, UINT_PTR id) */
#define FN_CTL_UPDATETEXT        0x00402170u   /* void __thiscall (CControlsTab *): field text = key name */
#define FN_CWND_DEFAULT          0x0044E8F3u   /* LRESULT __thiscall CWnd::Default (CWnd *) */
#define G_KEYBINDS               ((int *)0x004643C0u)
#define G_DIKEYSTATE             ((BYTE *)0x0046A0A0u)
#define G_KEYNAMES               ((const char *const *)0x00461480u)   /* by index, NULL = none */
#define CTL_N                    13
#define PAD_FIRST                0x100
#define PAD_LAST                 0x121
#define PAD_N                    (PAD_LAST - PAD_FIRST + 1)
#define PAD_HELD                 0x10F         /* g_DIKeyState byte the game clears and never binds */
#define CTL_RESET_ID             0x4C0
typedef void    (__fastcall *CtlTimer_t)(BYTE *, void *, int);
typedef LRESULT (__fastcall *CWndDefault_t)(BYTE *, void *);
static VoidFn_t   oInputUpdate, oInputRead;
static PageInit_t oCtlInit;
static PageOK_t   oCtlOk;
static CtlTimer_t oCtlTimer;
static PageOK_t   oCtlText;
static int g_padBinds[CTL_N], g_ctlPad[CTL_N];   /* controller slots: in use / on the open page */
static int g_ctlLoaded, g_ctlInUpdate;
static BYTE   *g_ctlPage;                        /* the open Controls page */
#define CTL_PAGE_PROP            "LomaxPlus.CtlPage"
#define CTL_FIELD_PROP           "LomaxPlus.CtlField"
#define CTL_INDEX_PROP           "LomaxPlus.CtlIndex"   /* a field's action + 1 */
static const char *const kCtlNames[CTL_N] = { "Left", "Right", "Up", "Down", "Jump", "Shoot", "Ability",
                                              "Select", "Run", "Helm", "LookDown", "LookUp", "Pause" };
static const int kCtlKeyDefault[CTL_N] = { 0xCB, 0xCD, 0xC8, 0xD0, 0x11, 0x12, 0x10, 0x39, 0x2A, 0x1F, 0xD1,
                                           0xC9, 0x19 };   /* the game's, but Run on L-Shift (R-Control) */
static const int kCtlPadDefault[CTL_N] = { 0x100, 0x101, 0x102, 0x103, 0x104, 0x105, 0x106, 0x108, 0x109,
                                           0x107, 0, 0, 0x10B };
static const int kCtlPadDefaultV1[CTL_N] = { 0x100, 0x101, 0x102, 0x103, 0x104, 0x106, 0x105, 0x107, 0x109,
                                             0x108, 0, 0, 0x10B };   /* the first release's defaults */
static const int kCtlEdit[CTL_N] = { 0x2B8, 0x27C, 0x18C, 0x36C, 0x2F4, 0x1C8, 0x3A8, 0x204, 0x240, 0x330,
                                     0x150, 0x114, 0xD8 };   /* the fields' CWnd in the page */
static const char *const kPadNames[PAD_N] = {
    "LS-Left", "LS-Right", "LS-Up", "LS-Down",                         /* 0x100..0x103 */
    "A", "B", "X", "Y", "LB", "RB", "Back", "Start", "L3",             /* buttons 1..9 */
    NULL, NULL, NULL,                                                  /* 0x10D..0x10F */
    "R3", "J11", "J12", "J13", "J14", "J15", "J16", NULL,              /* buttons 10..16 */
    "D-Up", "D-Right", "D-Down", "D-Left", "LT", "RT",                 /* 0x118..0x11D */
    "RS-Left", "RS-Right", "RS-Up", "RS-Down" };                       /* 0x11E..0x121 */

/* the fields name the inputs as the controller in use labels them: the codes are Xbox positions, a
   Sony pad's are translated to them (pad_poll), other controllers' are their raw numbers */
static const char *const kPadNamesSony[PAD_N] = {
    "LS-Left", "LS-Right", "LS-Up", "LS-Down",
    "Cross", "Circle", "Square", "Triangle", "L1", "R1", "Create", "Options", "L3",
    NULL, NULL, NULL,
    "R3", "PS", "Touchpad", "Mute", "J15", "J16", "J17", NULL,
    "D-Up", "D-Right", "D-Down", "D-Left", "L2", "R2",
    "RS-Left", "RS-Right", "RS-Up", "RS-Down" };
static const char *const kPadNamesGeneric[PAD_N] = {
    "LS-Left", "LS-Right", "LS-Up", "LS-Down",
    "J1", "J2", "J3", "J4", "J5", "J6", "J7", "J8", "J9",
    NULL, NULL, NULL,
    "J10", "J11", "J12", "J13", "J14", "J15", "J16", NULL,
    "D-Up", "D-Right", "D-Down", "D-Left", "LT", "RT",
    "RS-Left", "RS-Right", "RS-Up", "RS-Down" };
enum { PAD_XBOX, PAD_SONY, PAD_GENERIC };
static int g_padStyle;                          /* of the last controller seen; Xbox before any */

static int pad_valid(int c) { return c >= PAD_FIRST && c <= PAD_LAST && kPadNames[c - PAD_FIRST]; }

static const char *pad_name(int c)
{
    const char *const *t = g_padStyle == PAD_SONY ? kPadNamesSony :
                           g_padStyle == PAD_GENERIC ? kPadNamesGeneric : kPadNames;
    return t[c - PAD_FIRST];
}

/* ---- the controller: joystick 0, everything joyGetPosEx reports ---- */
static BYTE     g_padDown[PAD_N], g_padNew[PAD_N];
static JOYCAPSA g_padCaps;
static int      g_padCapsOk, g_padWait;

static int      g_padSony;                       /* a Sony pad: translated to the Xbox positions */

/* Sony pads (DualShock 4, DualSense, DualSense Edge) number their inputs differently: buttons 1..14
   Square, Cross, Circle, Triangle, L1, R1, L2, R2, Create/Share, Options, L3, R3, PS, touchpad
   (15: the DualSense's mute), the right stick on Z (across) and R (down), the triggers' travel on U/V.
   They are translated to the codes of the Xbox positions (A = Cross, B = Circle, X = Square,
   Y = Triangle, ...), so both share one layout; L2/R2 come from their buttons. */
static const WORD kSonyPids[] = { 0x05C4, 0x09CC, 0x0BA0, 0x0CE6, 0x0DF2 };
static const BYTE kSonyButtons[15] = { 0x06, 0x04, 0x05, 0x07, 0x08, 0x09, 0x1C, 0x1D, 0x0A, 0x0B, 0x0C,
                                       0x10, 0x11, 0x12, 0x13 };   /* raw button -> code - PAD_FIRST */

static int pad_axis(DWORD v, UINT lo, UINT hi)   /* -1 / 0 / 1: past 30% of the range from the centre */
{
    DWORD r = hi > lo ? hi - lo : 65535, p = v > lo ? v - lo : 0;
    return p < r * 3 / 10 ? -1 : p > r * 7 / 10 ? 1 : 0;
}

static void pad_poll(void)
{
    BYTE now[PAD_N];
    JOYINFOEX j;
    int i, x;
    memset(now, 0, sizeof(now));
    memset(&j, 0, sizeof(j));
    j.dwSize = sizeof(j);
    j.dwFlags = JOY_RETURNALL;
    if (g_padWait) {
        g_padWait--;
    } else if (joyGetPosEx(JOYSTICKID1, &j) != JOYERR_NOERROR) {
        g_padWait = 120;                        /* as the game: try again about every 2 s */
        g_padCapsOk = 0;
    } else {
        const JOYCAPSA *c = &g_padCaps;
        if (!g_padCapsOk && joyGetDevCapsA(JOYSTICKID1, &g_padCaps, sizeof(g_padCaps)) == JOYERR_NOERROR) {
            size_t k;
            g_padCapsOk = 1;
            g_padSony = 0;
            for (k = 0; k < sizeof(kSonyPids) / sizeof(kSonyPids[0]); k++)
                if (c->wMid == 0x054C && c->wPid == kSonyPids[k]) g_padSony = 1;
            g_padStyle = g_padSony ? PAD_SONY : c->wMid == 0x045E ? PAD_XBOX : PAD_GENERIC;   /* 045E: Microsoft */
            log_msg("controls: controller '%s' (%04X:%04X), %u buttons%s%s%s%s%s%s", c->szPname, c->wMid, c->wPid,
                    c->wNumButtons, c->wCaps & JOYCAPS_HASZ ? ", Z" : "", c->wCaps & JOYCAPS_HASR ? ", R" : "",
                    c->wCaps & JOYCAPS_HASU ? ", U" : "", c->wCaps & JOYCAPS_HASV ? ", V" : "",
                    c->wCaps & JOYCAPS_HASPOV ? ", POV" : "",
                    g_padSony ? ": Sony layout, read as Xbox positions" : "");
        }
        if (g_padCapsOk) {
            x = pad_axis(j.dwXpos, c->wXmin, c->wXmax); now[0x00] = x < 0; now[0x01] = x > 0;
            x = pad_axis(j.dwYpos, c->wYmin, c->wYmax); now[0x02] = x < 0; now[0x03] = x > 0;
            for (i = 0; i < 16; i++) {
                if (!(j.dwButtons & (1u << i))) continue;
                if (!g_padSony) now[i < 9 ? 0x04 + i : 0x07 + i] = 1;   /* 1..9: 0x104.., 10..16: 0x110.. */
                else if (i < 15) now[kSonyButtons[i]] = 1;
            }
            if ((c->wCaps & JOYCAPS_HASPOV) && j.dwPOV < 36000) {
                DWORD p = j.dwPOV;              /* hundredths of a degree, 0 = up; diagonals give both */
                now[0x18] = p >= 31500 || p <= 4500;
                now[0x19] = p >= 4500 && p <= 13500;
                now[0x1A] = p >= 13500 && p <= 22500;
                now[0x1B] = p >= 22500 && p <= 31500;
            }
            if (g_padSony) {                    /* right stick: Z across, R down */
                if (c->wCaps & JOYCAPS_HASZ) {
                    x = pad_axis(j.dwZpos, c->wZmin, c->wZmax); now[0x1E] = x < 0; now[0x1F] = x > 0;
                }
                if (c->wCaps & JOYCAPS_HASR) {
                    x = pad_axis(j.dwRpos, c->wRmin, c->wRmax); now[0x20] = x < 0; now[0x21] = x > 0;
                }
            } else {                            /* Xbox: triggers on Z, right stick on U / R */
                if (c->wCaps & JOYCAPS_HASZ) {
                    x = pad_axis(j.dwZpos, c->wZmin, c->wZmax); now[0x1C] = x > 0; now[0x1D] = x < 0;
                }
                if (c->wCaps & JOYCAPS_HASU) {
                    x = pad_axis(j.dwUpos, c->wUmin, c->wUmax); now[0x1E] = x < 0; now[0x1F] = x > 0;
                }
                if (c->wCaps & JOYCAPS_HASR) {
                    x = pad_axis(j.dwRpos, c->wRmin, c->wRmax); now[0x20] = x < 0; now[0x21] = x > 0;
                }
            }
        }
    }
    for (i = 0; i < PAD_N; i++) {
        g_padNew[i] = now[i] && !g_padDown[i];
        g_padDown[i] = now[i];
    }
}

/* ---- the bindings ---- */
/* puts code in slots[i]; another action that had it gets slots[i]'s old input */
static void ctl_assign(int *slots, int i, int code)
{
    int j, old = slots[i];
    for (j = 0; j < CTL_N; j++)
        if (j != i && slots[j] == code) slots[j] = old;
    slots[i] = code;
}

static void ctl_save(void)
{
    HKEY k = reg_open("Controls", KEY_SET_VALUE);
    char name[32];
    int i;
    if (!k) return;
    for (i = 0; i < CTL_N; i++) {
        wsprintfA(name, "Pad%s", kCtlNames[i]);
        reg_set_int(k, name, g_padBinds[i]);
    }
    RegCloseKey(k);
}

static void ctl_load(void)                /* once the game has read g_KeyBinds */
{
    HKEY k;
    char name[32];
    int i, j, saved = 0, moved = 0;
    if (g_ctlLoaded) return;
    g_ctlLoaded = 1;
    memcpy(g_padBinds, kCtlPadDefault, sizeof(g_padBinds));
    if ((k = reg_open("Controls", KEY_QUERY_VALUE)) != NULL) {
        for (i = 0; i < CTL_N; i++) {
            int v;
            wsprintfA(name, "Pad%s", kCtlNames[i]);
            if (reg_get_int(k, name, &v)) {
                g_padBinds[i] = pad_valid(v) ? v : 0;
                saved = 1;
            }
        }
        RegCloseKey(k);
    }
    if (saved && !memcmp(g_padBinds, kCtlPadDefaultV1, sizeof(g_padBinds))) {
        memcpy(g_padBinds, kCtlPadDefault, sizeof(g_padBinds));   /* untouched old defaults: the new ones */
        moved++;
    }
    for (i = 0; i < CTL_N; i++) {             /* joystick inputs in the keyboard slots */
        int c = G_KEYBINDS[i];
        if (c < PAD_FIRST) continue;
        if (pad_valid(c)) ctl_assign(g_padBinds, i, c);
        moved++;
        G_KEYBINDS[i] = kCtlKeyDefault[i];
        for (j = 0; j < CTL_N; j++)
            if (j != i && G_KEYBINDS[j] == kCtlKeyDefault[i]) G_KEYBINDS[i] = 0;
    }
    if (!saved || moved) ctl_save();
    log_msg("controls: keyboard and controller bindings (%s controller slots%s)", saved ? "saved" : "default",
            moved ? ", updated" : "");
}

static void ctl_text(BYTE *page, int i)    /* "W / A": the keyboard and the controller input */
{
    HWND h = *(HWND *)(page + kCtlEdit[i] + 0x1C);
    char s[64], cur[64];
    int kb = ((const int *)(page + 0x94))[i], pad = g_ctlPad[i];
    if (!h) return;
    s[0] = cur[0] = 0;
    if (kb > 0 && kb < PAD_FIRST && G_KEYNAMES[kb]) lstrcpynA(s, G_KEYNAMES[kb], 32);
    if (pad_valid(pad)) {
        if (s[0]) lstrcatA(s, " / ");
        lstrcatA(s, pad_name(pad));
    }
    GetWindowTextA(h, cur, sizeof(cur));
    if (strcmp(cur, s)) SetWindowTextA(h, s);
}

static void ctl_text_all(BYTE *page)
{
    int i;
    for (i = 0; i < CTL_N; i++) ctl_text(page, i);
}

/* the fields: right-click empties both slots (and no context menu) */
static LRESULT CALLBACK ctl_edit_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    int i = (int)(INT_PTR)GetPropA(h, CTL_INDEX_PROP) - 1;
    switch (m) {
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: case WM_CONTEXTMENU:
        return 0;
    case WM_RBUTTONUP:
        if (g_ctlPage && i >= 0 && i < CTL_N) {
            ((int *)(g_ctlPage + 0x94))[i] = 0;
            g_ctlPad[i] = 0;
            ctl_text(g_ctlPage, i);
        }
        return 0;
    case WM_NCDESTROY:
        RemovePropA(h, CTL_INDEX_PROP);
        break;
    }
    return subclass_next(h, CTL_FIELD_PROP, m, w, l);
}

/* the page: the Reset button */
static LRESULT CALLBACK ctl_page_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_COMMAND && LOWORD(w) == CTL_RESET_ID && HIWORD(w) == BN_CLICKED && g_ctlPage) {
        memcpy(g_ctlPage + 0x94, kCtlKeyDefault, sizeof(kCtlKeyDefault));
        memcpy(g_ctlPad, kCtlPadDefault, sizeof(g_ctlPad));
        ctl_text_all(g_ctlPage);
        return 0;
    }
    if (m == WM_NCDESTROY) g_ctlPage = NULL;
    return subclass_next(h, CTL_PAGE_PROP, m, w, l);
}

/* the page's layout: Directional Buttons with the icons and Reset on the left and the four fields in
   a column, Action Buttons as a pad (Select / Spare Helmet / Run, Ability and Shoot, Jump, Look Up /
   Look Down / Pause). Pixels at 96 DPI, scaled with the dialog font's base units (6 x 13 there). */
static int ctl_px(int v, int base, int base96) { return MulDiv(v, base, base96); }

static void ctl_label(HWND pw, const char *text, int align, int x, int y, int bx, int by)
{
    HWND c;
    for (c = GetWindow(pw, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        char cls[16], t[32];
        GetClassNameA(c, cls, sizeof(cls));
        if (!_stricmp(cls, "Static") && GetWindowTextA(c, t, sizeof(t)) && !strcmp(t, text)) break;
    }
    if (c) {
        SIZE sz = text_size(c, text);
        int w;
        x = ctl_px(x, bx, 6);
        w = sz.cx + 1;
        if (align == 'R') x -= w;               /* x: right edge, centre or left edge */
        else if (align == 'C') x -= w / 2;
        SetWindowPos(c, NULL, x, ctl_px(y, by, 13), w, sz.cy, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

static void ctl_layout(HWND pw, HWND reset)
{
    /* fields in tab order: x, y, width (height 20) */
    static const struct { int id, x, y, w; } kFields[CTL_N] = {
        { 1019, 166,  29, 100 }, { 1018, 166,  59, 100 }, { 1020, 166,  89, 100 }, { 1021, 166, 119, 100 },
        { 1024,  32, 197,  70 }, { 1026, 112, 197,  70 }, { 1027, 192, 197,  70 },          /* Select, Helmet, Run */
        { 1025,  72, 227,  70 }, { 1023, 152, 227,  70 },                                   /* Ability, Shoot */
        { 1022, 112, 257,  70 },                                                            /* Jump */
        { 1029,  32, 303,  70 }, { 1028, 112, 303,  70 }, { 1030, 192, 303,  70 },          /* Look Up, Down, Pause */
    };
    /* labels: right edge (R) 6 px left of a field, left edge (L) 6 px right of one, or centred (C)
       4 px above or below one; tops 4 px into a field's row */
    static const struct { const char *text; int align, x, y; } kLabels[] = {
        { "Up", 'R', 160, 33 }, { "Down", 'R', 160, 63 }, { "Left", 'R', 160, 93 }, { "Right", 'R', 160, 123 },
        { "Select", 'C', 67, 180 }, { "Spare Helmet", 'C', 147, 180 }, { "Run", 'C', 227, 180 },
        { "Ability", 'R', 66, 231 }, { "Shoot", 'L', 228, 231 },
        { "Jump 'n' Spin", 'C', 147, 281 },
        { "Look Up", 'C', 67, 327 }, { "Look Down", 'C', 147, 327 }, { "Pause", 'C', 227, 327 },
    };
    RECT u = { 0, 0, 4, 8 };
    HWND c, prev = HWND_TOP, icons[2] = { NULL, NULL };
    int bx, by, i, n = 0, left, right;
    MapDialogRect(pw, &u);
    bx = u.right; by = u.bottom;                /* pixels per 4 x 8 dialog units */

    for (c = GetWindow(pw, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        char t[32];
        LONG st = GetWindowLongA(c, GWL_STYLE);
        GetWindowTextA(c, t, sizeof(t));
        if ((st & 0xF) == BS_GROUPBOX && (!strcmp(t, "Directional Buttons") || !strcmp(t, "Action Buttons"))) {
            if (t[0] == 'D') place_dlu(pw, c, PAGE_BOX_L, 7, PAGE_BOX_R, 93);
            else place_dlu(pw, c, PAGE_BOX_L, 99, PAGE_BOX_R, PAGE_BOX_BOTTOM);   /* ends where the System page's does */
        } else if ((st & SS_TYPEMASK) == SS_ICON && n < 2) {
            icons[n++] = c;
        }
    }
    if (n == 2) {                               /* keyboard first (it was above the joystick) */
        RECT a, b;
        GetWindowRect(icons[0], &a);
        GetWindowRect(icons[1], &b);
        if (b.top < a.top) { c = icons[0]; icons[0] = icons[1]; icons[1] = c; }
        SetWindowPos(icons[0], NULL, ctl_px(33, bx, 6), ctl_px(38, by, 13), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(icons[1], NULL, ctl_px(81, bx, 6), ctl_px(38, by, 13), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    left = ctl_px(33, bx, 6);                   /* Reset centred under the two icons, on the Left row */
    right = ctl_px(81 + 32, bx, 6);
    if (reset)
        SetWindowPos(reset, NULL, (left + right) / 2 - ctl_px(33, bx, 6), ctl_px(89, by, 13), ctl_px(66, bx, 6),
                     ctl_px(21, by, 13), SWP_NOZORDER | SWP_NOACTIVATE);

    for (i = 0; i < CTL_N; i++) {
        HWND e = GetDlgItem(pw, kFields[i].id);
        if (!e) continue;
        SetWindowPos(e, prev, ctl_px(kFields[i].x, bx, 6), ctl_px(kFields[i].y, by, 13),
                     ctl_px(kFields[i].w, bx, 6), ctl_px(20, by, 13), SWP_NOACTIVATE);
        prev = e;
        if (i == 3 && reset) {                  /* tab order: the directions, Reset, the actions */
            SetWindowPos(reset, prev, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            prev = reset;
        }
    }
    for (i = 0; i < (int)(sizeof(kLabels) / sizeof(kLabels[0])); i++)
        ctl_label(pw, kLabels[i].text, kLabels[i].align, kLabels[i].x, kLabels[i].y, bx, by);
}

static int __fastcall hkCtlInit(BYTE *page, void *edx)
{
    HWND pw = *(HWND *)(page + 0x1C), b;
    RECT r = { 0, 0, 44, 14 };                  /* dialog units; placed by ctl_layout */
    int ret, i;
    ctl_load();
    memcpy(g_ctlPad, g_padBinds, sizeof(g_ctlPad));
    ret = oCtlInit(page, edx);
    ctl_text_all(page);
    g_ctlPage = page;
    for (i = 0; i < CTL_N; i++) {
        HWND e = *(HWND *)(page + kCtlEdit[i] + 0x1C);
        if (!e) continue;
        SetPropA(e, CTL_INDEX_PROP, (HANDLE)(INT_PTR)(i + 1));
        subclass(e, CTL_FIELD_PROP, ctl_edit_proc);
    }
    subclass(pw, CTL_PAGE_PROP, ctl_page_proc);
    MapDialogRect(pw, &r);
    b = CreateWindowExA(0, "BUTTON", "Reset", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, r.left, r.top,
                        r.right - r.left, r.bottom - r.top, pw, (HMENU)(INT_PTR)CTL_RESET_ID,
                        GetModuleHandleA(NULL), NULL);
    if (b) SendMessageA(b, WM_SETFONT, (WPARAM)SendMessageA(pw, WM_GETFONT, 0, 0), TRUE);
    ctl_layout(pw, b);
    return ret;
}

/* CControlsTab_UpdateKeyText: called for the focused field (page+0xD4) by OnInitDialog, OnTimer and
   every field's EN_CHANGE handler; it set the text to the keyboard key's name, which fought with
   ctl_text's "W / A" (each change set off the other: the text flickered). Both slots here. */
static void __fastcall hkCtlText(BYTE *page, void *edx)
{
    int i = *(const int *)(page + 0xD4);
    (void)edx;
    if (i >= 0 && i < CTL_N) ctl_text(page, i);
}

static void __fastcall hkCtlOk(BYTE *page, void *edx)
{
    oCtlOk(page, edx);                          /* keyboard slots to g_KeyBinds */
    memcpy(g_padBinds, g_ctlPad, sizeof(g_padBinds));
    ctl_save();
}

static int pad_first_new(void)
{
    int i;
    for (i = 0; i < PAD_N; i++)
        if (g_padNew[i] && kPadNames[i]) return PAD_FIRST + i;
    return 0;
}

static void __fastcall hkCtlTimer(BYTE *page, void *edx, int id)
{
    static int shown = -1;
    int i = *(const int *)(page + 0xD4), code;
    if (id != *(const int *)(page + 0x8C)) return;
    if (shown != g_padStyle) {                  /* another kind of controller: its names */
        shown = g_padStyle;
        ctl_text_all(page);
    }
    if (!*(void *const *)(page + 0x90)) return;
    code = ((int (__cdecl *)(void))FN_INPUT_FIRST_PRESS)();   /* polls the controller too */
    if (code >= PAD_FIRST || !G_KEYNAMES[code]) code = 0;     /* the game's joystick inputs: ours below */
    if (!code) code = pad_first_new();
    if (i >= 0 && i < CTL_N) {
        if (code) {
            ctl_assign(code >= PAD_FIRST ? g_ctlPad : (int *)(page + 0x94), i, code);
            ctl_text_all(page);
        }
        ctl_text(page, i);                      /* also undoes characters typed into the field */
    }
    ((CWndDefault_t)FN_CWND_DEFAULT)(page, edx);
}

/* in play, a left stick direction and the same D-pad direction count as one input: an action bound
   to either also follows the other */
static int pad_down(int c)
{
    static const BYTE twin[4] = { 0x1B, 0x19, 0x18, 0x1A };   /* LS-Left/Right/Up/Down -> D-Left/Right/Up/Down */
    int i = c - PAD_FIRST, k;
    if (g_padDown[i]) return 1;
    for (k = 0; k < 4; k++) {
        if (i == k) return g_padDown[twin[k]];
        if (i == twin[k]) return g_padDown[k];
    }
    return 0;
}

static void __cdecl hkInputRead(void)
{
    int i, held = 0;
    oInputRead();
    pad_poll();
    if (!g_ctlInUpdate) return;
    for (i = 0; i < CTL_N; i++)
        if (pad_valid(g_padBinds[i]) && pad_down(g_padBinds[i])) {
            G_KEYBINDS[i] = PAD_HELD;
            held = 1;
        }
    if (held) G_DIKEYSTATE[PAD_HELD] = 0x80;
}

static void __cdecl hkInputUpdate(void)
{
    int kb[CTL_N];
    ctl_load();
    memcpy(kb, G_KEYBINDS, sizeof(kb));
    g_ctlInUpdate = 1;
    oInputUpdate();
    g_ctlInUpdate = 0;
    memcpy(G_KEYBINDS, kb, sizeof(kb));
}

void controls_init(void)
{
    static const struct { DWORD addr; BYTE b[6]; } sig[] = {
        { FN_INPUT_UPDATE,     { 0xFF, 0x15, 0xC8, 0x89, 0x5A, 0x00 } },
        { FN_INPUT_READ,       { 0x83, 0xEC, 0x34, 0xB8, 0x4E, 0x61 } },
        { FN_CTL_ONTIMER,      { 0x8B, 0x44, 0x24, 0x04, 0x53, 0x56 } },
        { FN_CTL_ONINITDIALOG, { 0x83, 0xEC, 0x34, 0x53, 0x56, 0x8D } },
        { FN_CTL_ONOK,         { 0x56, 0x8D, 0x91, 0x94, 0x00, 0x00 } },
        { FN_INPUT_FIRST_PRESS, { 0x81, 0xEC, 0x10, 0x01, 0x00, 0x00 } },
        { FN_CTL_UPDATETEXT,   { 0x83, 0xEC, 0x40, 0x53, 0x56, 0x57 } },
    };
    size_t i;
    for (i = 0; i < sizeof(sig) / sizeof(sig[0]); i++)
        if (!code_matches(sig[i].addr, sig[i].b, 6)) {
            log_msg("controls: unexpected code at %08lX, keyboard + controller bindings off", (unsigned long)sig[i].addr);
            return;
        }
    mh_hook(FN_INPUT_UPDATE, hkInputUpdate, (void **)&oInputUpdate, "Input_Update");
    mh_hook(FN_INPUT_READ, hkInputRead, (void **)&oInputRead, "Input_ReadKeyboardAndJoy");
    mh_hook(FN_CTL_ONINITDIALOG, hkCtlInit, (void **)&oCtlInit, "CControlsTab_OnInitDialog");
    mh_hook(FN_CTL_ONOK, hkCtlOk, (void **)&oCtlOk, "CControlsTab_OnOK");
    mh_hook(FN_CTL_ONTIMER, hkCtlTimer, (void **)&oCtlTimer, "CControlsTab_OnTimer");
    mh_hook(FN_CTL_UPDATETEXT, hkCtlText, (void **)&oCtlText, "CControlsTab_UpdateKeyText");
    if (G_KEYBINDS[8] == 0x9D) G_KEYBINDS[8] = 0x2A;   /* Run's default (no saved key yet): L-Shift */
}
