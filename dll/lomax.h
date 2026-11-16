/*
 * lomax.h - Lomax+ LOADER.DLL: what the modules share.
 *
 * The DLL is one module per feature (see main.c for the list). Each one keeps its own hooks, state
 * and LOMAX.EXE addresses; this header has the addresses used by more than one, the DirectDraw
 * bits they touch, the configuration, the helpers in main.c and every module's entry points.
 */
#ifndef LOMAX_H
#define LOMAX_H

#define COBJMACROS
#include <windows.h>
#include <prsht.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "minhook.h"

/* ---- LOMAX.EXE (retail, SHA-256 5D5DE600...8CA13A79) addresses -------------------------- */
#define GAME_BASE                0x00400000u
#define FN_DISPLAY_SET_VIDEO_MODE 0x00403FA0u  /* void __cdecl (int mode)            */
#define FN_WNDPROC               0x00404400u  /* LRESULT CALLBACK                   */
#define FN_DDRAW_INIT_SURFACES   0x00407930u  /* int __cdecl (HWND, int bReload)    */
#define FN_D3D_SELECT_HW_DRIVER  0x00405D40u  /* void __cdecl (int bpp)             */
#define FN_D3D_INIT_DEVICE       0x00406AF0u  /* int  __cdecl (void)                */
#define FN_D3D_SELECT_SW_DRIVER  0x00405ED0u  /* void __cdecl (int bpp)             */
#define FN_CD_VERIFY_LOMAX_DISC  0x0040E900u  /* int __cdecl (void)                 */
#define G_BWINDOWED              ((volatile int *)0x00461C64u)
#define G_BCDAUDIOAVAILABLE      ((volatile int *)0x00461C7Cu)
#define G_BCHEATSENABLED         ((volatile int *)0x004645B8u)
#define G_PDD                    ((void *volatile *)0x00469A08u)
#define G_PDDSPRIMARY            ((void *volatile *)0x0046AAACu)
#define G_HWND                   ((HWND volatile *)0x0046A218u)
#define G_HMENU                  ((HMENU volatile *)0x00461CD8u)
#define G_WINX                   ((volatile int *)0x00461CB4u)   /* saved to the registry on exit */
#define G_WINY                   ((volatile int *)0x00461CB8u)
#define G_WINCLIENTW             ((volatile int *)0x00461CE0u)
#define G_WINCLIENTH             ((volatile int *)0x00461CE4u)
#define G_CURSORSHOWCOUNT        ((volatile int *)0x00461D00u)   /* game's ShowCursor() balance */
/* Direct3D driver table filled by D3D_EnumDriversCallback (max 5 entries) */
#define G_NUMD3DDRIVERS          ((volatile int *)0x0046A7D4u)
#define G_CURD3DDRIVER           ((volatile int *)0x0046A020u)
#define G_D3DRENDERMODE          ((volatile int *)0x00461B9Cu)   /* 1 = ramp/mono, 2 = RGB, 3 = HAL */
#define G_D3DDRIVERISHW          ((volatile int *)0x00469F20u)
#define D3D_DESC(i)              ((const BYTE *)0x00469A10u + (i) * 0xAC)   /* D3DDEVICEDESC */
#define D3D_COLORMODEL(i)        (*(const DWORD *)(D3D_DESC(i) + 0x08))    /* 1 MONO, 2 RGB */
#define D3D_RENDERDEPTHS(i)      (*(const DWORD *)(D3D_DESC(i) + 0x9C))    /* DDBD_* */
#define D3D_NAME(i)              ((const char *)0x0046A660u + (i) * 0x40)
#define FN_CDAUDIO_SET_VOLUME    0x00438460u  /* void __cdecl (int vol 0..255)       */
#define FN_GAME_SETUP_NEXT_LEVEL 0x00417BD0u  /* int __cdecl (void): Level_LoadWorld result */
#define FN_GAME_FRAME            0x00404AA0u  /* int __cdecl (void)                  */
#define G_BUSESTRETCH            ((volatile int *)0x00461C6Cu)
#define G_FRAMETIME              ((volatile DWORD *)0x00469EE0u)  /* Game_Frame: last timeGetTime */
#define G_FRAMEACCUM             ((volatile int *)0x0046A904u)    /* Game_Frame: tick remainder */
#define G_BFRAMEREADY            ((volatile int *)0x0059F1DCu)    /* DDraw_Present shows nothing while 0 */
#define FN_TILELAYER_DRAW       0x0040CE80u
#define HFR_MAX_FPS             1000
#define G_SURFACEMEMCAPS         ((volatile DWORD *)0x00461CB0u)   /* 0x800 = DDSCAPS_SYSTEMMEMORY */
#define G_BHWCOLORKEYBLT         ((volatile int *)0x0046A1B0u)
#define G_BD3DTRANSLUCENCY       ((volatile int *)0x00469A00u)
#define D3D_TBLENDCAPS(i)        (*(const DWORD *)(0x00469A9Cu + (i) * 0xAC))
#define D3D_TADDRCAPS(i)         (*(const DWORD *)(0x00469AA0u + (i) * 0xAC))
#define FN_SPRITE_GET_DITHERED   0x0040A510u  /* void * __cdecl (SpriteFrame *)      */
#define FN_RENDER_DISSOLVE_FADE  0x004055C0u  /* void __cdecl (IDirectDrawSurface *) */
#define G_FADESTEP               ((volatile int *)0x0052C5F4u)
#define G_PLANES               0x0049B880u
#define PLANE_STRIDE           0x9C
#define MAX_PLANES             32
#define FN_ACTOR_DRAW           0x0040C270u
#define FN_GETPTD                0x0043CE60u  /* CRT _getptd(); +0x14 = rand seed    */
#define G_PCURPLANE              ((void *volatile *)0x00498F1Cu)   /* current level plane (g_Planes entry) */

/* ---- minimal DirectDraw definitions (only what we touch) --------------------------------- */
#define DDSD_CAPS                0x00000001u
#define DDSD_PIXELFORMAT         0x00001000u
#define DDSCAPS_OFFSCREENPLAIN   0x00000040u
#define DDSCAPS_ZBUFFER          0x00020000u
#define DDSD_ZBUFFERBITDEPTH     0x00000040u
#define DDPF_RGB                 0x00000040u
#define VT_DD_CREATESURFACE      6   /* IDirectDraw2  vtbl index */
#define VT_DDS_BLT               5   /* IDirectDrawSurface vtbl index */
#define VT_DDS_GETDC             17
#define VT_DDS_RELEASEDC         26

typedef struct {               /* DDSURFACEDESC (0x6C bytes) */
    DWORD dwSize, dwFlags, dwHeight, dwWidth, lPitch, dwBackBufferCount, dwRefreshRate;
    DWORD dwAlphaBitDepth, dwReserved; void *lpSurface; DWORD ck[8];
    struct { DWORD dwSize, dwFlags, dwFourCC, dwRGBBitCount, dwR, dwG, dwB, dwA; } pf;
    DWORD dwCaps;
} DDSD;

typedef HRESULT (WINAPI *CreateSurface_t)(void *, DDSD *, void **, void *);
typedef HRESULT (WINAPI *Blt_t)(void *, RECT *, void *, RECT *, DWORD, void *);
typedef HRESULT (WINAPI *GetDC_t)(void *, HDC *);
typedef HRESULT (WINAPI *ReleaseDC_t)(void *, HDC);
typedef void (__cdecl *SetVideoMode_t)(int);
typedef int (__cdecl *IntFn_t)(void);
typedef int (__cdecl *InitSurfaces_t)(HWND, int);
typedef int (WINAPI *GetDeviceCaps_t)(HDC, int);
typedef MCIERROR (WINAPI *MciSendStringA_t)(LPCSTR, LPSTR, UINT, HWND);
/* __thiscall member hooked through __fastcall: ECX = this, EDX unused, callee pops the stack args */

#define VTBL(obj, i) ((*(void ***)(obj))[i])
typedef void  (__cdecl *VoidFn_t)(void);
typedef void  (__cdecl *IntArgFn_t)(int);
typedef void *(__cdecl *GetPtd_t)(void);
typedef int   (__fastcall *PageInit_t)(BYTE *, void *);   /* an MFC page's OnInitDialog (thiscall) */
typedef void  (__fastcall *PageOK_t)(BYTE *, void *);     /* ... and OnOK */

/* maxScrollX of a plane: (tiles - W/16) * 16, negative for planes narrower than the screen */
static __inline int ws_max_scroll(const BYTE *plane) { return *(const short *)(plane + 0x30); }

/* A level plane narrower than the screen gets the narrowed view: black bars over the wrapped tiles
   on both sides, with the HUD's edge counters and the player's screen limit moved inside them
   (ws_inset_update in widescreen.c). The one exception is this width, the Evil Ed arena (26 tiles),
   which shows its map repeated with a solid floor (ws_wraps). Single-screen rooms are 20-24 tiles;
   a mod's maps can be any width. */
#define WS_REPEAT_TILES          26
static __inline int ws_narrowed(const BYTE *plane, int width)
{
    int m = ws_max_scroll(plane);
    return m < 0 && m + width != WS_REPEAT_TILES * 16;
}

/* ---- the Properties pages: their height and where their group boxes are (dialog units) ------- */
#define SYS_DLG_HEIGHT           219           /* the System page's (dialog 102), was 175 */
#define PAGE_BOX_L               7
#define PAGE_BOX_R               188
#define PAGE_BOX_BOTTOM          214           /* the lowest box's bottom */

/* ---- configuration (main.c; filled by extra_load and disp_load) -------------------------- */
enum { TRANSP_AUTO, TRANSP_DITHER, TRANSP_SOLID, TRANSP_BLEND };
typedef struct {
    int startFullscreen;
    int rgbD3D;               /* replace the software "Ramp" (mono) D3D driver with the RGB one */
    int transparency;         /* TRANSP_*: auto, checkerboard/dissolve, opaque/no fades, hardware D3D */
    int cheats;
    int langSelect;           /* Language Select: console language screen before the title */
    int loadScreen;           /* Loading Screen: console loading screen on section changes */
    int log;
    int speed50;              /* 50Hz Mode: PAL timing, the game ticks at 50 Hz */
    int hfrFps;               /* FPS Override: frames drawn per second, 0 = off */
    int vsync;                /* VSync: game ticks locked to the display refresh */
    int fastBlit;             /* Fast Blit: the sprite queue blitted by our own copy loop */
    int d3d11;                /* Renderer Direct3D 11: present through a DXGI flip-model swap chain */
    int dbgTrace, dbgMute;    /* testing aids (Extra key DemoTrace, Mute), off by default */
} Config;
extern Config cfg;
extern char   g_gameDir[MAX_PATH];   /* LOMAX.EXE's folder */
extern int    g_force16;             /* 16-bit emulation active (a desktop that isn't 8/16 bpp) */

/* ---- mods (main.c): the MODS folder as scanned at the start, and the mod chosen on the Extra
   page (registry Extra\Mod, its folder name). A mod is listed when MODS\<folder> holds <folder>.DLL
   and <folder>.INI; it runs when it is the one chosen and has every file it needs. ---------------- */
#define MOD_MAX                  32
#define MOD_FOLDER_LEN           64
#define MOD_NAME_LEN             64
typedef struct { char folder[MOD_FOLDER_LEN], name[MOD_NAME_LEN]; } ModInfo;
extern ModInfo g_mods[MOD_MAX];      /* sorted by name */
extern int     g_nMods;
extern char    g_modChosen[MOD_FOLDER_LEN];   /* registry Extra\Mod at the start ("" = None) */
extern char    g_modRunning[MOD_FOLDER_LEN];  /* the mod loaded this run ("" = none) */

/* ---- helpers (main.c) ---------------------------------------------------------------------- */
void   log_msg(const char *fmt, ...);
void **find_iat_slot(const char *dll, const char *func);
int    hook_iat(const char *dll, const char *func, void *detour, void **orig);
int    mh_hook(DWORD target, void *detour, void **orig, const char *what);
int    hex_to_bytes(const char *hex, unsigned char *out, int max);
int    code_matches(DWORD addr, const void *bytes, size_t n);      /* the bytes at addr are these */
void   patch_code(DWORD addr, const void *bytes, size_t n);
void   patch_code_byte(volatile BYTE *p, BYTE v);
HKEY   reg_open(const char *sub, REGSAM access);
int    reg_get_int(HKEY k, const char *name, int *v);
void   reg_set_int(HKEY k, const char *name, int v);
int    reg_get_str(HKEY k, const char *name, char *s, DWORD size);
void   reg_set_str(HKEY k, const char *name, const char *s);
void    subclass(HWND h, const char *prop, WNDPROC proc);
LRESULT subclass_next(HWND h, const char *prop, UINT msg, WPARAM w, LPARAM l);

/* ---- modules -------------------------------------------------------------------------------- */
/* ui.c: branding, the Properties sheet and its System and Extra pages, the Display and Sound menus */
void ui_init(void);
void extra_load(void);             /* the settings, before the game starts: into cfg */
void disp_load(void);
int  disp_ws_width(void);          /* the widescreen width set (320 = off) */
SIZE text_size(HWND c, const char *text);                  /* in c's font */
void place_dlu(HWND units, HWND c, int l, int t, int r, int b);   /* dialog units of window units */
/* controls.c: keyboard and controller bindings, the Controls page */
void controls_init(void);
/* audio.c: mixer crash, hard-right pan, CD check, local CD music */
void audio_init(void);
/* render.c: 16-bit surfaces, Direct3D driver and transparency, DDraw_InitSurfaces */
void render_init(void);
/* present.c: the windowed present (GDI or Direct3D 11) */
void present_attach(void);
void present_set_renderer(int d3d11);
/* window.c: borderless fullscreen, the window size */
void window_init(void);
void window_surfaces_ready(int ok);
void ws_map_window_size(int toWide);
int  borderless_active(void);
void borderless_toggle(void);
int  window_scale(void);
void set_window_scale(int k);
/* game.c: language select, 50 Hz, loading screen; Game_Frame */
#define GF_TIMING                0x00404BD1u   /* Game_Frame: call timeGetTime ... clamp to 5 ticks */
#define GF_TIMING_LEN            0x58
extern int g_tickHz;               /* game ticks per second: 60, or 50 with Speed50Hz */
extern int g_tickDiv;              /* Game_Frame's step: ms*10 per tick (167 / 200) */
void game_init(void);
void speed50_init(void);
int  game_frame_orig(void);
int  game_timing_check(const char *who);
/* widescreen.c: widescreen and demo playback */
extern int g_wsWidth;              /* active internal width, 0 = widescreen off */
extern BYTE g_wsLayerRepeat[];
void ws_init(int width);
void ws_bars(int *l, int *r);      /* the narrowed view's black bars now, in game pixels (0 = none) */
/* vsync.c */
extern volatile LONG g_vbCount;
extern volatile LONGLONG g_vbQpc;
void   vsync_init(void);
int    vs_load_vblank(void);
double vb_hz(void);
int    vb_follows_frames(double hz);
/* hfr.c: FPS Override; the widescreen mirroring of narrow layers (both work on the sprite queue) */
extern int g_hfrValid;             /* the sprite queue may be redrawn (cleared when surfaces change) */
int  hfr_init(void);
int  hfr_game_frame(void);
void ws_layer_init(void);
/* fastblit.c */
void fastblit_init(void);

#endif
