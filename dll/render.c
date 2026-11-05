/*
 * render.c - rendering on current Windows: 16-bit surfaces on 32-bit desktops, the Direct3D driver
 * and transparency, and DDraw_InitSurfaces, where the surface-dependent hooks are set up.
 */
#include "lomax.h"

static GetDeviceCaps_t oGetDeviceCaps;
static CreateSurface_t oCreateSurface;
static InitSurfaces_t  oInitSurfaces;
static SetVideoMode_t  oSelectHwDriver, oSelectSwDriver;

/* ---- 16-bit rendering on 32-bit desktops --------------------------------------------------
   The game refuses desktops that aren't 8 or 16 bpp (GetDeviceCaps BITSPIXEL); every offscreen
   surface it creates is made 16-bit RGB565 (IDirectDraw2::CreateSurface), and the windowed present
   (primary->Blt, present.c) converts to the desktop's format. */
static int WINAPI hkGetDeviceCaps(HDC dc, int index)
{
    int v = oGetDeviceCaps(dc, index);
    if (g_force16 && index == BITSPIXEL && v != 8 && v != 16) return 16;
    return v;
}

static HRESULT WINAPI hkCreateSurface(void *dd, DDSD *d, void **pp, void *outer)
{
    if (g_force16 && d && d->dwSize == sizeof(DDSD) && (d->dwFlags & DDSD_CAPS) &&
        !(d->dwFlags & DDSD_PIXELFORMAT) && (d->dwCaps & DDSCAPS_OFFSCREENPLAIN)) {
        d->dwFlags |= DDSD_PIXELFORMAT;
        memset(&d->pf, 0, sizeof(d->pf));
        d->pf.dwSize = sizeof(d->pf);
        d->pf.dwFlags = DDPF_RGB;
        d->pf.dwRGBBitCount = 16;
        d->pf.dwR = 0xF800; d->pf.dwG = 0x07E0; d->pf.dwB = 0x001F;
    }
    if (d && d->dwSize == sizeof(DDSD) && (d->dwFlags & DDSD_CAPS) && (d->dwCaps & DDSCAPS_ZBUFFER) &&
        (d->dwFlags & DDSD_ZBUFFERBITDEPTH)) {
        /* D3D_InitDevice asks for the deepest z-buffer the driver lists (32-bit for the HAL), which
           current drivers refuse next to the 16-bit render target: fall back to 16, then 24 bits */
        static const DWORD depths[] = { 16, 24 };
        DWORD want = d->dwRefreshRate;   /* union: dwZBufferBitDepth */
        HRESULT r = oCreateSurface(dd, d, pp, outer);
        size_t i;
        for (i = 0; r != 0 && i < sizeof(depths) / sizeof(depths[0]); i++) {
            if (depths[i] == want) continue;
            d->dwRefreshRate = depths[i];
            r = oCreateSurface(dd, d, pp, outer);
            log_msg("z-buffer: %lu-bit refused, %lu-bit -> %08lX", (unsigned long)want,
                    (unsigned long)depths[i], (unsigned long)r);
        }
        return r;
    }
    return oCreateSurface(dd, d, pp, outer);
}

/* ---- Direct3D driver choice ----------------------------------------------------------------
   For software rendering the game prefers the "Ramp" (monochrome) driver, which needs 8-bit
   palettized textures. Those are filled with a GDI BitBlt from the 16-bit sprite into the
   palettized surface's DC; on current Windows that DC doesn't use the attached palette, so
   textured sprites (zooming logo, rocking boat, ...) come out purple. The RGB software driver
   uses 16-bit textures (exact copy, no palette). Neither software driver blends alpha (their
   blend caps are 0), so translucent sprites get the game's checkerboard and fades its dissolve.

   Transparency=blend: DirectX_Init only puts the game's surfaces in video memory (and so lets
   D3D_SelectHardwareDriver pick the "Direct3D HAL" driver) when DirectDraw reports hardware
   colour-keyed blits, which current Windows doesn't. Clearing its system-memory choice before
   the surfaces are created makes the game take its own hardware path: HAL device on the 16-bit
   back buffer, 16-bit and alpha textures, real alpha blending for translucent sprites and fades.
   With the Intel drivers the first draw then crashes inside the driver (igc32.dll), whatever
   the render states, so the default is auto (the game's own choice). */
static DWORD ddbd_for(int bpp)
{
    return bpp == 8 ? 0x800 : bpp == 16 ? 0x400 : bpp == 24 ? 0x200 : bpp == 32 ? 0x100 : 0;
}

static void prefer_rgb_driver(int bpp, const char *who)
{
    int cur = *G_CURD3DDRIVER, n = *G_NUMD3DDRIVERS, i;
    if (cur < 0 || cur >= n || G_D3DDRIVERISHW[cur] || *G_D3DRENDERMODE != 1) return;
    for (i = 0; i < n; i++) {
        if (!G_D3DDRIVERISHW[i] && (D3D_COLORMODEL(i) & 2) && (D3D_RENDERDEPTHS(i) & ddbd_for(bpp))) {
            log_msg("%s(%d): D3D driver %d '%s' (ramp) -> %d '%s' (RGB)", who, bpp, cur, D3D_NAME(cur), i, D3D_NAME(i));
            *G_CURD3DDRIVER = i;
            *G_D3DRENDERMODE = 2;
            return;
        }
    }
    log_msg("%s(%d): no RGB software D3D driver available, keeping '%s'", who, bpp, D3D_NAME(cur));
}

static void __cdecl hkSelectHwDriver(int bpp)
{
    oSelectHwDriver(bpp);
    prefer_rgb_driver(bpp, "D3D_SelectHardwareDriver");
}

static void __cdecl hkSelectSwDriver(int bpp)
{
    oSelectSwDriver(bpp);
    prefer_rgb_driver(bpp, "D3D_SelectSoftwareDriver");
}

/* Hardware translucency: sprites and fade quads carry their opacity in the vertex alpha, on
   textures whose alpha bit only marks the colour key (Texture_ApplyAlphaMask). D3D_InitDevice sets
   TEXTUREMAPBLEND = MODULATE, which by the spec takes alpha from the texture whenever it has one,
   so the vertex alpha is ignored: water comes out solid and fades fully black or missing (current
   NVIDIA drivers; many 1997 drivers multiplied the two). MODULATEALPHA multiplies texture and vertex
   alpha, as the game expects. Patched in the immediate of D3D_InitDevice's initial state block.

   Quad edges: the sprite queue builds each translucent quad from x to x+w-1 and y to y+h-1, with
   texture coordinates 0..(w-1)/T, 0..(h-1)/T, i.e. inclusive end pixels, which 1997 rasterizers
   filled. Current ones follow the top-left fill rule and leave the last column and row out, so
   tiled translucent sprites (the 64-px water tiles) showed a 1-pixel gap every tile. On a hardware
   driver the quads end at x+w / y+h (lea reg,[a+b-1] -> [a+b+0]) and the texture spans w/T, h/T
   (dec edi / dec esi -> nop in Texture_CreateFromFrame), keeping one texel per pixel.

   Pixel centres sit on integer coordinates, i.e. exactly on texel borders of a 1:1 sprite quad,
   so rounding can pick the neighbouring texel for a whole column. On a hardware driver the view
   matrix translation (_41/_42, 0 in the game) moves all geometry by -1/2 pixel in x and y, so
   pixels sample texel centres. Sprite quads are at z = 0, where 1 view unit in x is 1 pixel
   (dvScaleX * _11 / 160 = 1 also in widescreen) and 1 unit in y is _22 * 120 / 160 pixels.

   The game never sets TEXTUREADDRESS (WRAP). Each quad's first column samples on the border of
   texel 0, so as a safeguard the state block's ANTIALIAS = NONE entry (the default anyway) becomes
   TEXTUREADDRESS = CLAMP: "mov [esi-8], 2" -> 3 and "mov [esi-4], ecx (0)" -> eax (3). */
#define G_D3DTEXBLENDIMM         ((volatile BYTE *)0x00407239u)    /* mov [esi+4], 2 at 0x407236 */
#define G_D3DSTATE2IMM           ((volatile BYTE *)0x00407285u)    /* mov [esi-8], 2 at 0x407282 */
#define G_D3DSTATE2VALMODRM      ((volatile BYTE *)0x0040728Au)    /* mov [esi-4], ecx at 0x407289 */
#define G_D3DVIEW                ((volatile float *)0x00461BE0u)   /* view matrix, row-major 4x4 */
static const struct { DWORD addr; BYTE orig, hw; } kQuadEdgePatches[] = {
    { 0x0040A8D8u, 0xFF, 0x00 },   /* Sprite_QueueScreen:      right  = x + w - 1 */
    { 0x0040A8F9u, 0xFF, 0x00 },   /*                          bottom = y + h - 1 */
    { 0x0040ACD9u, 0xFF, 0x00 },   /* Sprite_QueueWorldScaled: right  (1:1 path) */
    { 0x0040ACFAu, 0xFF, 0x00 },   /*                          bottom */
    { 0x0040CCBDu, 0xFF, 0x00 },   /* Actor_Draw:              right */
    { 0x0040CCE7u, 0xFF, 0x00 },   /*                          bottom */
    { 0x00408F82u, 0x4F, 0x90 },   /* Texture_CreateFromFrame: u extent (w-1)/T: dec edi */
    { 0x00408F9Fu, 0x4E, 0x90 },   /*                          v extent (h-1)/T: dec esi */
};
#define D3DTBLEND_MODULATE       2
#define D3DTBLEND_MODULATEALPHA  4
#define D3DPTBLENDCAPS_MODULATEALPHA 0x08
#define D3DPTADDRESSCAPS_CLAMP   0x04
static IntFn_t oD3DInitDevice;

static int __cdecl hkD3DInitDevice(void)
{
    int cur = *G_CURD3DDRIVER;
    int hw = cur >= 0 && cur < *G_NUMD3DDRIVERS && G_D3DDRIVERISHW[cur];
    int modAlpha = hw && (D3D_TBLENDCAPS(cur) & D3DPTBLENDCAPS_MODULATEALPHA);
    int clamp = hw && (D3D_TADDRCAPS(cur) & D3DPTADDRESSCAPS_CLAMP);
    static int s_logged = -1;
    size_t i;
    int r;
    G_D3DVIEW[12] = hw ? -0.5f : 0.0f;                             /* _41 */
    G_D3DVIEW[13] = hw ? 0.5f * 160.0f / (G_D3DVIEW[5] * 120.0f) : 0.0f;   /* _42 (y up) */
    for (i = 0; i < sizeof(kQuadEdgePatches) / sizeof(kQuadEdgePatches[0]); i++)
        patch_code_byte((volatile BYTE *)kQuadEdgePatches[i].addr,
                        hw ? kQuadEdgePatches[i].hw : kQuadEdgePatches[i].orig);
    patch_code_byte(G_D3DTEXBLENDIMM, modAlpha ? D3DTBLEND_MODULATEALPHA : D3DTBLEND_MODULATE);
    patch_code_byte(G_D3DSTATE2IMM, clamp ? 3 : 2);            /* TEXTUREADDRESS : ANTIALIAS */
    patch_code_byte(G_D3DSTATE2VALMODRM, clamp ? 0x46 : 0x4E); /* eax (CLAMP=3) : ecx (0)   */
    if (hw && s_logged != cur) {
        s_logged = cur;
        log_msg("D3D_InitDevice: driver '%s', texture blend %s, texture address %s, full-size quads, "
                "half-pixel offset (%.4f, %.4f)", D3D_NAME(cur),
                modAlpha ? "MODULATEALPHA (vertex alpha kept)" : "MODULATE", clamp ? "CLAMP" : "WRAP",
                (double)G_D3DVIEW[12], (double)G_D3DVIEW[13]);
    }
    r = oD3DInitDevice();
    /* Transparency=dither/solid: the game picks the checkerboard sprites (Sprite_GetDitheredSurface)
       and the tile dissolve (Render_DissolveFade) whenever g_bD3DTranslucency is 0 */
    if ((cfg.transparency == TRANSP_DITHER || cfg.transparency == TRANSP_SOLID) && *G_BD3DTRANSLUCENCY) {
        *G_BD3DTRANSLUCENCY = 0;
        log_msg("Transparency=%s: alpha blending available but turned off",
                cfg.transparency == TRANSP_SOLID ? "solid" : "dither");
    }
    return r;
}

/* Transparency=solid: translucent sprites are drawn opaque and fades are not drawn.
   Sprite_GetDitheredSurface returns the sprite's own surface (+0x18), as the game itself does when
   it can't create the checkerboard copy. Render_DissolveFade draws nothing, but still makes the
   same calls to the game's rand() (32x32 per frame while a fade runs and the game isn't paused):
   game logic uses the same generator (spring boxes, particles, bosses), so skipping them would
   change what happens next and desync the attract demos. */
#define FN_GAME_RAND             0x0043AAD0u  /* int __cdecl (void), CRT rand       */
#define G_BPAUSED                ((volatile int *)0x0052C5F8u)
#define G_COLORDEPTH             ((volatile int *)0x00461CE8u)
static void *oGetDitheredSurface, *oDissolveFade;

static DWORD __cdecl hkGetDitheredSurface(const BYTE *frame)
{
    return *(const DWORD *)(frame + 0x18);
}

static void __cdecl hkDissolveFade(void *target)
{
    int step = *G_FADESTEP, level = step > 0 ? 64 - step : step < 0 ? -step : -1, i;
    (void)target;
    if (level >= 0 && !*G_BPAUSED && (*G_COLORDEPTH == 8 || *G_COLORDEPTH == 16))
        for (i = 0; i < 32 * 32; i++)
            ((IntFn_t)FN_GAME_RAND)();
}

/* ---- Shared tiles (always on) -----------------------------------------------------------------
   Plane_BuildTileSurfaces (0x4154B0) draws each plane's 16x16 metatiles (2x2 8x8 cells, each with
   flip bits and a palette, TTAB) into the tile pages, but reuses the drawing of an identical metatile
   of an earlier plane. "Identical" compared the four cells and their flips, not their palettes, so a
   background tile made of the same cells as a level tile got the level's colours: in level 1.2 the
   tree canopy's top row (background plane, tile 10) showed black blocks, the level plane having that
   cell combination with a palette that makes it black (1.1's hasn't, so its canopy is right). The
   attribute kept for the comparison now includes the palette (and bx,0x6000 -> 0x6F00, the flip switch
   after it masks ebx with 0x6000 instead of 0xFFFF) and the four compares mask 0x6F00. */
static void tiles_fix(void)
{
    static const struct { DWORD addr; BYTE orig[6], fix[6]; BYTE n; } kSites[] = {
        { 0x00415634u, { 0x66, 0x81, 0xE3, 0x00, 0x60 },       { 0x66, 0x81, 0xE3, 0x00, 0x6F },       5 },
        { 0x00415640u, { 0x81, 0xE3, 0xFF, 0xFF, 0x00, 0x00 }, { 0x81, 0xE3, 0x00, 0x60, 0x00, 0x00 }, 6 },
        { 0x00415761u, { 0x66, 0x81, 0xE5, 0x00, 0x60 },       { 0x66, 0x81, 0xE5, 0x00, 0x6F },       5 },
        { 0x00415771u, { 0x66, 0x81, 0xE5, 0x00, 0x60 },       { 0x66, 0x81, 0xE5, 0x00, 0x6F },       5 },
        { 0x00415781u, { 0x66, 0x81, 0xE5, 0x00, 0x60 },       { 0x66, 0x81, 0xE5, 0x00, 0x6F },       5 },
        { 0x00415791u, { 0x66, 0x81, 0xE5, 0x00, 0x60 },       { 0x66, 0x81, 0xE5, 0x00, 0x6F },       5 },
    };
    size_t i;
    for (i = 0; i < sizeof(kSites) / sizeof(kSites[0]); i++)
        if (!code_matches(kSites[i].addr, kSites[i].orig, kSites[i].n)) {
            log_msg("tiles: unexpected code at %08lX, shared tiles not fixed", (unsigned long)kSites[i].addr);
            return;
        }
    for (i = 0; i < sizeof(kSites) / sizeof(kSites[0]); i++)
        patch_code(kSites[i].addr, kSites[i].fix, kSites[i].n);
    log_msg("tiles: metatiles shared between planes only with the same palettes");
}

/* ---- DDraw_InitSurfaces: (re)creates the game's surfaces -------------------------------------- */
static int __cdecl hkInitSurfaces(HWND hwnd, int reload)
{
    int r;
    static int s_firstInit = 1;
    if (s_firstInit) {
        s_firstInit = 0;
        ws_map_window_size(1);
        if (cfg.dbgMute) {             /* Mute (testing aid): settings were read from the registry by now.
                                          Volumes only: sounds still play, so the game behaves the same */
            *(volatile int *)0x00464578u = 0;   /* g_cfgEffectsVolume */
            *(volatile int *)0x0046457Cu = 0;   /* g_cfgAmbientVolume */
            *(volatile int *)0x00464574u = 0;   /* g_cfgCDVolume */
        }
    }
    if (cfg.transparency == TRANSP_BLEND && *G_SURFACEMEMCAPS) {
        log_msg("Transparency=blend: video-memory surfaces (game chose %08lX)", (unsigned long)*G_SURFACEMEMCAPS);
        *G_SURFACEMEMCAPS = 0;
        *G_BHWCOLORKEYBLT = 1;
    } else if (cfg.transparency == TRANSP_BLEND) {
        log_msg("Transparency=blend: nothing to do, the game already chose video memory (hardware colour-key blits)");
    }
    if (g_force16 && !oCreateSurface && *G_PDD)
        mh_hook((DWORD)VTBL(*G_PDD, VT_DD_CREATESURFACE), hkCreateSurface, (void **)&oCreateSurface,
                "IDirectDraw2::CreateSurface");
    g_hfrValid = 0;                    /* FPS Override: the queued sprites' surfaces are recreated */
    r = oInitSurfaces(hwnd, reload);
    present_attach();
    log_msg("DDraw_InitSurfaces(reload=%d) -> %d (windowed=%d)", reload, r, *G_BWINDOWED);
    if (cfg.log) {
        int drv = *(int *)0x0046A020, fmt = *(int *)0x00469EB0, i;
        for (i = 0; i < *G_NUMD3DDRIVERS; i++) {
            /* triangle D3DPRIMCAPS of the driver's device desc (stride 0xAC), as D3D_InitDevice reads
               them to decide alpha blending: src/dest blend, shade, texture caps */
            const BYTE *dd = (const BYTE *)0x00469A00u + i * 0xAC;
            log_msg("  D3D enum #%d '%s' hw=%d colorModel=%lu depths=%03lX srcBlend=%08lX destBlend=%08lX "
                    "shade=%08lX tex=%08lX texBlend=%08lX texAddr=%08lX", i, D3D_NAME(i), G_D3DDRIVERISHW[i],
                    D3D_COLORMODEL(i), D3D_RENDERDEPTHS(i), *(const DWORD *)(dd + 0x84),
                    *(const DWORD *)(dd + 0x88), *(const DWORD *)(dd + 0x90), *(const DWORD *)(dd + 0x94),
                    D3D_TBLENDCAPS(i), D3D_TADDRCAPS(i));
        }
        log_msg("  g_SurfaceMemCaps %08lX", *(DWORD *)0x00461CACu);
        const DWORD *pf = (const DWORD *)(0x0046A220 + fmt * 0x6C + 0x48);
        log_msg("  D3D driver %d hw=%d translucency=%d alphaTex=%d | tex fmt #%d flags=%08lX bpp=%lu "
                "R=%08lX G=%08lX B=%08lX A=%08lX | colorKey=%08lX transparent=%08lX texKey=%08lX",
                drv, ((int *)0x00469F20)[drv], *(int *)0x00469A00, *(int *)0x0046AAA8, fmt,
                pf[1], pf[3], pf[4], pf[5], pf[6], pf[7],
                *(DWORD *)0x00469FA8, *(DWORD *)0x0046A8BC, *(DWORD *)0x00469ED4);
    }
    window_surfaces_ready(r);
    return r;
}

void render_init(void)
{
    tiles_fix();
    mh_hook(FN_D3D_INIT_DEVICE, hkD3DInitDevice, (void **)&oD3DInitDevice, "D3D_InitDevice");
    if (cfg.transparency == TRANSP_SOLID) {
        mh_hook(FN_SPRITE_GET_DITHERED, hkGetDitheredSurface, &oGetDitheredSurface, "Sprite_GetDitheredSurface");
        mh_hook(FN_RENDER_DISSOLVE_FADE, hkDissolveFade, &oDissolveFade, "Render_DissolveFade");
    }
    if (g_force16) {
        hook_iat("GDI32.dll", "GetDeviceCaps", hkGetDeviceCaps, (void **)&oGetDeviceCaps);
        mh_hook(FN_DDRAW_INIT_SURFACES, hkInitSurfaces, (void **)&oInitSurfaces, "DDraw_InitSurfaces");
        if (cfg.rgbD3D) {
            mh_hook(FN_D3D_SELECT_HW_DRIVER, hkSelectHwDriver, (void **)&oSelectHwDriver, "D3D_SelectHardwareDriver");
            mh_hook(FN_D3D_SELECT_SW_DRIVER, hkSelectSwDriver, (void **)&oSelectSwDriver, "D3D_SelectSoftwareDriver");
        }
    }
}
