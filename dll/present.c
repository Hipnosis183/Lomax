/*
 * present.c - the windowed present (primary->Blt): the 16-bit picture to the window through sharp
 * scaling, by GDI or by a Direct3D 11 flip-model swap chain.
 */
#include "lomax.h"

static Blt_t oBlt;

/* Intermediate bitmap for the sharp scaling (source enlarged by an integer factor). */
static HDC     g_sharpDC;
static HBITMAP g_sharpBmp, g_sharpOldBmp;
static int     g_sharpW, g_sharpH;

static HDC sharp_buffer(HDC ref, int w, int h)
{
    if (g_sharpDC && g_sharpW == w && g_sharpH == h) return g_sharpDC;
    if (g_sharpDC) {
        SelectObject(g_sharpDC, g_sharpOldBmp);
        DeleteObject(g_sharpBmp);
        DeleteDC(g_sharpDC);
        g_sharpDC = NULL;
    }
    g_sharpDC = CreateCompatibleDC(ref);
    g_sharpBmp = g_sharpDC ? CreateCompatibleBitmap(ref, w, h) : NULL;
    if (!g_sharpBmp) {
        if (g_sharpDC) DeleteDC(g_sharpDC);
        g_sharpDC = NULL;
        return NULL;
    }
    g_sharpOldBmp = (HBITMAP)SelectObject(g_sharpDC, g_sharpBmp);
    g_sharpW = w; g_sharpH = h;
    return g_sharpDC;
}

/* ---- fast "sharp" scaler --------------------------------------------------------------------
   Gives the same picture as "nearest-neighbour up to k = ceil(d/s), then an area-averaging shrink
   to d", computed directly. For every output column/row it precomputes the source pixel it falls
   in, the neighbour it may straddle and an 8-bit blend weight (0 everywhere except right at a
   source-pixel edge). The 16-bit RGB565 back surface is read with Lock, each source row is scaled
   horizontally once into a 32-bit row cache, output rows are copied (or blended at row edges)
   into a DIB section, and one 1:1 BitBlt puts it in the window. */
#define VT_DDS_LOCK      25
#define VT_DDS_UNLOCK    32
#define DDLOCK_WAIT_     0x00000001u
#define DDLOCK_READONLY_ 0x00000010u
typedef HRESULT (WINAPI *Lock_t)(void *, RECT *, DDSD *, DWORD, HANDLE);
typedef HRESULT (WINAPI *Unlock_t)(void *, void *);

typedef struct { short a, b; unsigned short w; } Tap;   /* out = a*(256-w) + b*w, w in 0..256 */

static Tap   *g_tapX, *g_tapY;
static int    g_tapSW, g_tapSH, g_tapDW, g_tapDH;
static DWORD *g_rowCache;              /* sh rows x dw pixels, horizontally scaled */
static int    g_rowCacheSize;
static DWORD *g_lut565;                /* RGB565 -> 0x00RRGGBB */
static HDC     g_fastDC;
static HBITMAP g_fastBmp, g_fastOldBmp;
static DWORD  *g_fastBits;
static int     g_fastW, g_fastH;

static void build_taps(Tap *t, int s, int d)
{
    int k = (d + s - 1) / s, i;
    double r = (double)s * k / d;              /* output pixel width in pre-scaled pixels */
    for (i = 0; i < d; i++) {
        double x = (i + 0.5) * s / d;          /* source position, pixel n covers [n, n+1) */
        int n = (int)x;
        double wr, wl;
        if (n >= s) n = s - 1;
        wr = ((x - (n + 1)) * k + r / 2) / r;  /* share of the box reaching into pixel n+1 */
        wl = ((n - x) * k + r / 2) / r;        /* share reaching into pixel n-1 */
        t[i].a = t[i].b = (short)n;
        t[i].w = 0;
        if (wr > 0 && n + 1 < s)  { t[i].b = (short)(n + 1); t[i].w = (unsigned short)(wr * 256 + 0.5); }
        else if (wl > 0 && n > 0) { t[i].b = (short)(n - 1); t[i].w = (unsigned short)(wl * 256 + 0.5); }
    }
}

static __inline DWORD blend32(DWORD a, DWORD b, unsigned w)
{
    DWORD rb = (((a & 0xFF00FF) * (256 - w) + (b & 0xFF00FF) * w) >> 8) & 0xFF00FF;
    DWORD g  = (((a & 0x00FF00) * (256 - w) + (b & 0x00FF00) * w) >> 8) & 0x00FF00;
    return rb | g;
}

static int lut565_init(void)
{
    int x;
    if (g_lut565) return 1;
    g_lut565 = (DWORD *)malloc(65536 * sizeof(DWORD));
    if (!g_lut565) return 0;
    for (x = 0; x < 65536; x++) {
        DWORD r = (x >> 11) & 31, g = (x >> 5) & 63, b = x & 31;
        g_lut565[x] = (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
    }
    return 1;
}

static int fast_sharp(void *src, const RECT *sr, HDC wdc, int dx, int dy, int dw, int dh)
{
    int sw = sr->right - sr->left, sh = sr->bottom - sr->top, x, y;
    DDSD d;

    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return 0;
    if (!lut565_init()) return 0;
    if (g_tapSW != sw || g_tapDW != dw) {
        free(g_tapX); g_tapX = (Tap *)malloc(dw * sizeof(Tap));
        if (!g_tapX) { g_tapSW = 0; return 0; }
        build_taps(g_tapX, sw, dw); g_tapSW = sw; g_tapDW = dw;
    }
    if (g_tapSH != sh || g_tapDH != dh) {
        free(g_tapY); g_tapY = (Tap *)malloc(dh * sizeof(Tap));
        if (!g_tapY) { g_tapSH = 0; return 0; }
        build_taps(g_tapY, sh, dh); g_tapSH = sh; g_tapDH = dh;
    }
    if (g_rowCacheSize < sh * dw) {
        free(g_rowCache); g_rowCache = (DWORD *)malloc((size_t)sh * dw * sizeof(DWORD));
        g_rowCacheSize = g_rowCache ? sh * dw : 0;
        if (!g_rowCache) return 0;
    }
    if (!g_fastDC || g_fastW != dw || g_fastH != dh) {
        BITMAPINFO bi;
        if (g_fastDC) {
            SelectObject(g_fastDC, g_fastOldBmp); DeleteObject(g_fastBmp); DeleteDC(g_fastDC);
            g_fastDC = NULL;
        }
        memset(&bi, 0, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = dw;
        bi.bmiHeader.biHeight = -dh;               /* top-down */
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        g_fastDC = CreateCompatibleDC(wdc);
        g_fastBmp = g_fastDC ? CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, (void **)&g_fastBits, NULL, 0) : NULL;
        if (!g_fastBmp) { if (g_fastDC) DeleteDC(g_fastDC); g_fastDC = NULL; return 0; }
        g_fastOldBmp = (HBITMAP)SelectObject(g_fastDC, g_fastBmp);
        g_fastW = dw; g_fastH = dh;
    }

    memset(&d, 0, sizeof(d));
    d.dwSize = sizeof(d);
    if (((Lock_t)VTBL(src, VT_DDS_LOCK))(src, NULL, &d, DDLOCK_WAIT_ | DDLOCK_READONLY_, NULL) != 0) return 0;
    if (d.pf.dwRGBBitCount != 16 || d.pf.dwG != 0x07E0 || !d.lpSurface) {
        ((Unlock_t)VTBL(src, VT_DDS_UNLOCK))(src, NULL);
        return 0;                                   /* not RGB565: let GDI handle it */
    }
    /* horizontal pass: every source row once */
    for (y = 0; y < sh; y++) {
        const WORD *row = (const WORD *)((const BYTE *)d.lpSurface + (sr->top + y) * (int)d.lPitch) + sr->left;
        DWORD *out = g_rowCache + y * dw;
        for (x = 0; x < dw; x++) {
            const Tap *t = &g_tapX[x];
            DWORD a = g_lut565[row[t->a]];
            out[x] = t->w ? blend32(a, g_lut565[row[t->b]], t->w) : a;
        }
    }
    ((Unlock_t)VTBL(src, VT_DDS_UNLOCK))(src, NULL);

    /* vertical pass into the DIB */
    GdiFlush();
    for (y = 0; y < dh; y++) {
        const Tap *t = &g_tapY[y];
        const DWORD *ra = g_rowCache + t->a * dw;
        DWORD *dst = g_fastBits + y * dw;
        if (!t->w) {
            memcpy(dst, ra, dw * sizeof(DWORD));
        } else {
            const DWORD *rb = g_rowCache + t->b * dw;
            for (x = 0; x < dw; x++) dst[x] = blend32(ra[x], rb[x], t->w);
        }
    }
    BitBlt(wdc, dx, dy, dw, dh, g_fastDC, 0, 0, SRCCOPY);
    return 1;
}

/* where the sw x sh picture goes in a cw x ch client area: as large as fits, its shape kept */
static void present_layout(int cw, int ch, int sw, int sh, int *dx, int *dy, int *dw, int *dh)
{
    *dw = cw; *dh = ch;
    if (cw > 0 && ch > 0) {
        if (cw * sh > ch * sw) *dw = ch * sw / sh;
        else                   *dh = cw * sh / sw;
    }
    *dx = (cw - *dw) / 2; *dy = (ch - *dh) / 2;
}

/* widescreen: a level plane narrower than the screen is centred by the camera; the wrapped-around
   tiles on both sides are hidden by black bars, l and r game pixels wide (ws_bars, widescreen.c) */
static void present_ws_bars(int *l, int *r)
{
    ws_bars(l, r);
}

static int present_gdi(void *src, const RECT *srcRect)
{
    HWND hwnd = *G_HWND;
    HDC sdc = NULL, wdc;
    RECT cr, sr = { 0, 0, 320, 240 };
    int cw, ch, sw, sh, dw, dh, dx, dy, k, fast = 0;
    static LARGE_INTEGER s_freq, s_sum;
    static int s_frames;
    LARGE_INTEGER t0, t1;

    if (!hwnd) return 0;
    if (cfg.log) QueryPerformanceCounter(&t0);
    if (srcRect) sr = *srcRect;
    sw = sr.right - sr.left; sh = sr.bottom - sr.top;
    GetClientRect(hwnd, &cr);
    cw = cr.right - cr.left; ch = cr.bottom - cr.top;
    present_layout(cw, ch, sw, sh, &dx, &dy, &dw, &dh);

    wdc = GetDC(hwnd);
    if (!wdc) return 0;
    {
        /* black bars around the picture */
        if (dx > 0) { PatBlt(wdc, 0, 0, dx, ch, BLACKNESS); PatBlt(wdc, dx + dw, 0, cw - dx - dw, ch, BLACKNESS); }
        if (dy > 0) { PatBlt(wdc, dx, 0, dw, dy, BLACKNESS); PatBlt(wdc, dx, dy + dh, dw, ch - dy - dh, BLACKNESS); }

        if (g_force16 && dw > 0 && dh > 0)
            fast = fast_sharp(src, &sr, wdc, dx, dy, dw, dh);
        if (!fast && ((GetDC_t)VTBL(src, VT_DDS_GETDC))(src, &sdc) != 0) {
            ReleaseDC(hwnd, wdc);
            return 0;
        }
        if (fast) {
            /* done: fast_sharp already drew the picture */
        } else if (dw > 0 && dh > 0 && (dw % sw || dh % sh)) {
            /* "sharp bilinear": nearest-neighbour up to the next integer multiple, then an
               area-averaging (HALFTONE) shrink to the final size. Removes the 4/5-pixel shimmer
               of non-integer nearest scaling without blurring the whole image. */
            int kx = (dw + sw - 1) / sw, ky = (dh + sh - 1) / sh;
            HDC mdc;
            k = max(kx, ky);
            mdc = sharp_buffer(wdc, sw * k, sh * k);
            if (mdc) {
                SetStretchBltMode(mdc, COLORONCOLOR);
                StretchBlt(mdc, 0, 0, sw * k, sh * k, sdc, sr.left, sr.top, sw, sh, SRCCOPY);
                SetStretchBltMode(wdc, HALFTONE);
                SetBrushOrgEx(wdc, 0, 0, NULL);
                StretchBlt(wdc, dx, dy, dw, dh, mdc, 0, 0, sw * k, sh * k, SRCCOPY);
            } else {
                SetStretchBltMode(wdc, COLORONCOLOR);
                StretchBlt(wdc, dx, dy, dw, dh, sdc, sr.left, sr.top, sw, sh, SRCCOPY);
            }
        } else {
            /* an exact integer multiple */
            SetStretchBltMode(wdc, COLORONCOLOR);
            StretchBlt(wdc, dx, dy, dw, dh, sdc, sr.left, sr.top, sw, sh, SRCCOPY);
        }
        if (dw > 0) {
            int l, r;                                     /* bar widths in game pixels */
            present_ws_bars(&l, &r);
            if (l > 0) PatBlt(wdc, dx, dy, l * dw / sw, dh, BLACKNESS);
            if (r > 0) PatBlt(wdc, dx + dw - r * dw / sw, dy, r * dw / sw, dh, BLACKNESS);
        }
        ReleaseDC(hwnd, wdc);
    }
    if (sdc) ((ReleaseDC_t)VTBL(src, VT_DDS_RELEASEDC))(src, sdc);
    if (cfg.log) {
        QueryPerformanceCounter(&t1);
        if (!s_freq.QuadPart) QueryPerformanceFrequency(&s_freq);
        s_sum.QuadPart += t1.QuadPart - t0.QuadPart;
        if (++s_frames == 600) {
            log_msg("present: %dx%d -> %dx%d %s, avg %.2f ms", sw, sh, dw, dh,
                    fast ? "fast sharp" : "GDI", s_sum.QuadPart * 1000.0 / s_freq.QuadPart / s_frames);
            s_frames = 0; s_sum.QuadPart = 0;
        }
    }
    return 1;
}

/* ---- Renderer Direct3D 11: present through a DXGI flip-model swap chain ---------------------
   The GDI present scales the picture on the CPU into a DIB and BitBlts it into the window. DWM
   composes the window from that on its own schedule, without knowing when a frame is complete, so a
   copy still in progress when it composes shows as a tear; at 1440p the copy takes 5 ms or more,
   most of a 120 Hz refresh, and the tears are frequent. Here the RGB565 picture is converted into a
   BGRA texture (game size only, 432x240), a pixel shader scales it into the swap chain's back buffer
   with the same layout (shape kept) and sharp scaling (nearest up to the next integer factor and an
   area-average down, as fast_sharp), and Present hands DWM (or, in borderless fullscreen, the display
   directly: independent flip) only finished frames. Present(0): never waits; flip model doesn't tear
   without DXGI_PRESENT_ALLOW_TEARING; the frame pacing (VSync) stays with the game loop. Anything
   that fails falls back to the GDI present for the rest of the session. */
#define DX_SWAP_EFFECT_FLIP_SEQUENTIAL ((DXGI_SWAP_EFFECT)3)
#define DX_SWAP_EFFECT_FLIP_DISCARD    ((DXGI_SWAP_EFFECT)4)
static const GUID kIID_IDXGIDevice1   = { 0x77db970f, 0x6276, 0x48ba, { 0xba, 0x28, 0x07, 0x01, 0x43, 0xb4, 0x39, 0x2c } };
static const GUID kIID_IDXGIFactory2  = { 0x50c83a1c, 0xe072, 0x4c48, { 0x87, 0xb0, 0x36, 0x30, 0xfa, 0x36, 0xa6, 0xd0 } };
static const GUID kIID_ID3D11Texture2D = { 0x6f15aaf2, 0xd208, 0x4e89, { 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c } };
typedef HRESULT (WINAPI *D3DCompile_t)(LPCVOID, SIZE_T, LPCSTR, const void *, void *, LPCSTR, LPCSTR, UINT, UINT,
                                       ID3DBlob **, ID3DBlob **);

static const char kDxShader[] =
    "Texture2D t : register(t0);\n"
    "SamplerState sl : register(s0);\n"            /* linear */
    "cbuffer C : register(b0) { float4 p0; float4 p1; };\n"   /* p0: tex w, h, scale x, y; p1: -, bar l, bar r */
    "struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "V vs(uint id : SV_VertexID) {\n"
    "  V o; float2 u = float2((id << 1) & 2, id & 2);\n"
    "  o.uv = u; o.pos = float4(u * float2(2, -2) + float2(-1, 1), 0, 1); return o;\n"
    "}\n"
    "float4 ps(V i) : SV_Target {\n"
    "  if (i.uv.x < p1.y || i.uv.x > p1.z) return float4(0, 0, 0, 1);\n"
    "  if (p0.z < 1 || p0.w < 1) return t.Sample(sl, i.uv);\n"
    "  float2 texel = i.uv * p0.xy, fl = floor(texel), s = texel - fl - 0.5;\n"
    "  float2 range = 0.5 - 0.5 / p0.zw;\n"     /* blend only within one output pixel of an edge */
    "  float2 f = (s - clamp(s, -range, range)) * p0.zw + 0.5;\n"
    "  return t.Sample(sl, (fl + f) / p0.xy);\n"
    "}\n";

static struct {
    int                      failed;
    HWND                     hwnd;
    ID3D11Device            *dev;
    ID3D11DeviceContext     *ctx;
    IDXGISwapChain1         *sc;
    ID3D11RenderTargetView  *rtv;
    ID3D11Texture2D         *tex;
    ID3D11ShaderResourceView *srv;
    ID3D11VertexShader      *vs;
    ID3D11PixelShader       *ps;
    ID3D11SamplerState      *samp;
    ID3D11Buffer            *cb;
    int                      bw, bh, tw, th;
} g_dx;

#define DX_RELEASE(p) do { if (p) { IUnknown_Release((IUnknown *)(p)); (p) = NULL; } } while (0)

static void dx_release(void)
{
    DX_RELEASE(g_dx.cb);
    DX_RELEASE(g_dx.samp);
    DX_RELEASE(g_dx.ps);
    DX_RELEASE(g_dx.vs);
    DX_RELEASE(g_dx.srv);
    DX_RELEASE(g_dx.tex);
    DX_RELEASE(g_dx.rtv);
    DX_RELEASE(g_dx.sc);
    if (g_dx.ctx) ID3D11DeviceContext_ClearState(g_dx.ctx);
    DX_RELEASE(g_dx.ctx);
    DX_RELEASE(g_dx.dev);
    g_dx.hwnd = NULL;
    g_dx.bw = g_dx.bh = g_dx.tw = g_dx.th = 0;
}

static int dx_fail(const char *what, HRESULT hr)
{
    log_msg("Renderer=d3d11: %s failed (%08lX), GDI present from now on", what, (unsigned long)hr);
    dx_release();
    g_dx.failed = 1;
    return 0;
}

static int dx_init(HWND hwnd, int cw, int ch)
{
    static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    PFN_D3D11_CREATE_DEVICE create;
    D3DCompile_t compile;
    HMODULE m;
    IDXGIDevice1 *xdev = NULL;
    IDXGIAdapter *adapter = NULL;
    IDXGIFactory2 *factory = NULL;
    DXGI_SWAP_CHAIN_DESC1 sd;
    D3D11_SAMPLER_DESC smp;
    D3D11_BUFFER_DESC bd;
    ID3DBlob *code = NULL, *err = NULL;
    HRESULT hr;
    int i;

    if (!(m = LoadLibraryA("d3d11.dll")) || !(create = (PFN_D3D11_CREATE_DEVICE)GetProcAddress(m, "D3D11CreateDevice")))
        return dx_fail("loading d3d11.dll", 0);
    if (!(m = LoadLibraryA("d3dcompiler_47.dll")) || !(compile = (D3DCompile_t)GetProcAddress(m, "D3DCompile")))
        return dx_fail("loading d3dcompiler_47.dll", 0);
    hr = create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                sizeof(levels) / sizeof(levels[0]), D3D11_SDK_VERSION, &g_dx.dev, NULL, &g_dx.ctx);
    if (FAILED(hr)) return dx_fail("D3D11CreateDevice", hr);
    hr = ID3D11Device_QueryInterface(g_dx.dev, &kIID_IDXGIDevice1, (void **)&xdev);
    if (SUCCEEDED(hr)) {
        IDXGIDevice1_SetMaximumFrameLatency(xdev, 1);
        hr = IDXGIDevice1_GetAdapter(xdev, &adapter);
    }
    if (SUCCEEDED(hr)) hr = IDXGIAdapter_GetParent(adapter, &kIID_IDXGIFactory2, (void **)&factory);
    DX_RELEASE(adapter);
    DX_RELEASE(xdev);
    if (FAILED(hr)) return dx_fail("getting the DXGI 1.2 factory", hr);

    memset(&sd, 0, sizeof(sd));
    sd.Width = cw;
    sd.Height = ch;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.SwapEffect = DX_SWAP_EFFECT_FLIP_DISCARD;            /* Windows 10 */
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    hr = IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown *)g_dx.dev, hwnd, &sd, NULL, NULL, &g_dx.sc);
    if (FAILED(hr)) {
        sd.SwapEffect = DX_SWAP_EFFECT_FLIP_SEQUENTIAL;      /* Windows 8 */
        hr = IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown *)g_dx.dev, hwnd, &sd, NULL, NULL, &g_dx.sc);
    }
    if (SUCCEEDED(hr))   /* Alt+Enter and window changes stay with the loader's borderless fullscreen */
        IDXGIFactory2_MakeWindowAssociation(factory, hwnd, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);
    DX_RELEASE(factory);
    if (FAILED(hr)) return dx_fail("CreateSwapChainForHwnd", hr);

    for (i = 0; i < 2; i++) {
        hr = compile(kDxShader, sizeof(kDxShader) - 1, "present", NULL, NULL, i ? "ps" : "vs",
                     i ? "ps_4_0" : "vs_4_0", 0, 0, &code, &err);
        if (FAILED(hr)) {
            if (err) log_msg("Renderer=d3d11: shader: %s", (const char *)ID3D10Blob_GetBufferPointer(err));
            DX_RELEASE(err);
            return dx_fail("D3DCompile", hr);
        }
        if (i) hr = ID3D11Device_CreatePixelShader(g_dx.dev, ID3D10Blob_GetBufferPointer(code),
                                                   ID3D10Blob_GetBufferSize(code), NULL, &g_dx.ps);
        else   hr = ID3D11Device_CreateVertexShader(g_dx.dev, ID3D10Blob_GetBufferPointer(code),
                                                    ID3D10Blob_GetBufferSize(code), NULL, &g_dx.vs);
        DX_RELEASE(code);
        DX_RELEASE(err);
        if (FAILED(hr)) return dx_fail("creating the shaders", hr);
    }
    memset(&smp, 0, sizeof(smp));
    smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    smp.MaxLOD = D3D11_FLOAT32_MAX;
    smp.ComparisonFunc = D3D11_COMPARISON_NEVER;
    smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    hr = ID3D11Device_CreateSamplerState(g_dx.dev, &smp, &g_dx.samp);
    if (FAILED(hr)) return dx_fail("CreateSamplerState", hr);
    memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = 32;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = ID3D11Device_CreateBuffer(g_dx.dev, &bd, NULL, &g_dx.cb);
    if (FAILED(hr)) return dx_fail("CreateBuffer", hr);
    g_dx.hwnd = hwnd;
    g_dx.bw = cw;
    g_dx.bh = ch;
    log_msg("Renderer=d3d11: %s swap chain, %dx%d", sd.SwapEffect == DX_SWAP_EFFECT_FLIP_DISCARD ?
            "flip-discard" : "flip-sequential", cw, ch);
    return 1;
}

/* draws the sw x sh RGB565 picture into the swap chain and presents it; 0 = use GDI */
static int dx_present(void *src, const RECT *sr, HWND hwnd, int cw, int ch)
{
    int sw = sr->right - sr->left, sh = sr->bottom - sr->top, dx, dy, dw, dh, l, r, x, y;
    D3D11_MAPPED_SUBRESOURCE ms;
    D3D11_VIEWPORT vp;
    DDSD d;
    HRESULT hr;
    static const float black[4] = { 0, 0, 0, 1 };

    if (g_dx.failed || !lut565_init()) return 0;
    if (cw <= 0 || ch <= 0) return 1;                        /* minimised */
    if (g_dx.hwnd && g_dx.hwnd != hwnd) dx_release();
    if (!g_dx.dev && !dx_init(hwnd, cw, ch)) return 0;
    if (!g_dx.rtv || g_dx.bw != cw || g_dx.bh != ch) {
        ID3D11Texture2D *bb = NULL;
        DX_RELEASE(g_dx.rtv);
        if (g_dx.bw != cw || g_dx.bh != ch) {
            ID3D11DeviceContext_OMSetRenderTargets(g_dx.ctx, 0, NULL, NULL);
            hr = IDXGISwapChain1_ResizeBuffers(g_dx.sc, 0, cw, ch, DXGI_FORMAT_UNKNOWN, 0);
            if (FAILED(hr)) return dx_fail("ResizeBuffers", hr);
            g_dx.bw = cw;
            g_dx.bh = ch;
        }
        hr = IDXGISwapChain1_GetBuffer(g_dx.sc, 0, &kIID_ID3D11Texture2D, (void **)&bb);
        if (SUCCEEDED(hr)) hr = ID3D11Device_CreateRenderTargetView(g_dx.dev, (ID3D11Resource *)bb, NULL, &g_dx.rtv);
        DX_RELEASE(bb);
        if (FAILED(hr)) return dx_fail("creating the render target", hr);
    }
    if (!g_dx.tex || g_dx.tw != sw || g_dx.th != sh) {
        D3D11_TEXTURE2D_DESC td;
        DX_RELEASE(g_dx.srv);
        DX_RELEASE(g_dx.tex);
        memset(&td, 0, sizeof(td));
        td.Width = sw;
        td.Height = sh;
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DYNAMIC;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = ID3D11Device_CreateTexture2D(g_dx.dev, &td, NULL, &g_dx.tex);
        if (SUCCEEDED(hr)) hr = ID3D11Device_CreateShaderResourceView(g_dx.dev, (ID3D11Resource *)g_dx.tex, NULL, &g_dx.srv);
        if (FAILED(hr)) return dx_fail("creating the picture texture", hr);
        g_dx.tw = sw;
        g_dx.th = sh;
    }

    /* the game's RGB565 picture into the texture */
    memset(&d, 0, sizeof(d));
    d.dwSize = sizeof(d);
    if (((Lock_t)VTBL(src, VT_DDS_LOCK))(src, NULL, &d, DDLOCK_WAIT_ | DDLOCK_READONLY_, NULL) != 0) return 0;
    if (d.pf.dwRGBBitCount != 16 || d.pf.dwG != 0x07E0 || !d.lpSurface ||
        FAILED(ID3D11DeviceContext_Map(g_dx.ctx, (ID3D11Resource *)g_dx.tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        ((Unlock_t)VTBL(src, VT_DDS_UNLOCK))(src, NULL);
        return 0;
    }
    for (y = 0; y < sh; y++) {
        const WORD *row = (const WORD *)((const BYTE *)d.lpSurface + (sr->top + y) * (int)d.lPitch) + sr->left;
        DWORD *out = (DWORD *)((BYTE *)ms.pData + y * ms.RowPitch);
        for (x = 0; x < sw; x++) out[x] = g_lut565[row[x]] | 0xFF000000u;
    }
    ID3D11DeviceContext_Unmap(g_dx.ctx, (ID3D11Resource *)g_dx.tex, 0);
    ((Unlock_t)VTBL(src, VT_DDS_UNLOCK))(src, NULL);

    present_layout(cw, ch, sw, sh, &dx, &dy, &dw, &dh);
    present_ws_bars(&l, &r);
    if (SUCCEEDED(ID3D11DeviceContext_Map(g_dx.ctx, (ID3D11Resource *)g_dx.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        float *c = (float *)ms.pData;
        c[0] = (float)sw;
        c[1] = (float)sh;
        c[2] = (float)dw / sw;
        c[3] = (float)dh / sh;
        c[4] = 0;
        c[5] = (float)l / sw;
        c[6] = 1.0f - (float)r / sw;
        c[7] = 0;
        ID3D11DeviceContext_Unmap(g_dx.ctx, (ID3D11Resource *)g_dx.cb, 0);
    }
    vp.TopLeftX = (float)dx;
    vp.TopLeftY = (float)dy;
    vp.Width = (float)dw;
    vp.Height = (float)dh;
    vp.MinDepth = 0;
    vp.MaxDepth = 1;
    ID3D11DeviceContext_OMSetRenderTargets(g_dx.ctx, 1, &g_dx.rtv, NULL);
    ID3D11DeviceContext_ClearRenderTargetView(g_dx.ctx, g_dx.rtv, black);
    ID3D11DeviceContext_RSSetViewports(g_dx.ctx, 1, &vp);
    ID3D11DeviceContext_IASetInputLayout(g_dx.ctx, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(g_dx.ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(g_dx.ctx, g_dx.vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(g_dx.ctx, g_dx.ps, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(g_dx.ctx, 0, 1, &g_dx.srv);
    ID3D11DeviceContext_PSSetSamplers(g_dx.ctx, 0, 1, &g_dx.samp);
    ID3D11DeviceContext_PSSetConstantBuffers(g_dx.ctx, 0, 1, &g_dx.cb);
    ID3D11DeviceContext_Draw(g_dx.ctx, 3, 0);
    hr = IDXGISwapChain1_Present(g_dx.sc, 0, 0);
    if (FAILED(hr)) return dx_fail("Present", hr);
    return 1;
}

static HRESULT WINAPI hkBlt(void *dst, RECT *dr, void *src, RECT *sr, DWORD flags, void *fx)
{
    /* The windowed present is primary->Blt(clientRect, back/stretch, 320x240, ...). The primary is
       32-bit and our surfaces are 16-bit, so let GDI (or Direct3D 11) do the conversion instead. */
    if (g_force16 && src && dst == *G_PDDSPRIMARY && *G_BWINDOWED && cfg.d3d11 && !g_dx.failed && *G_HWND) {
        RECT cr, r = { 0, 0, 320, 240 };
        static LARGE_INTEGER s_freq, s_sum, t0, t1;
        static int s_frames;
        if (cfg.log) QueryPerformanceCounter(&t0);
        if (sr) r = *sr;
        GetClientRect(*G_HWND, &cr);
        if (dx_present(src, &r, *G_HWND, cr.right - cr.left, cr.bottom - cr.top)) {
            if (cfg.log) {
                QueryPerformanceCounter(&t1);
                if (!s_freq.QuadPart) QueryPerformanceFrequency(&s_freq);
                s_sum.QuadPart += t1.QuadPart - t0.QuadPart;
                if (++s_frames == 600) {
                    log_msg("present: %dx%d -> %ldx%ld d3d11, avg %.2f ms", (int)(r.right - r.left),
                            (int)(r.bottom - r.top), cr.right - cr.left, cr.bottom - cr.top,
                            s_sum.QuadPart * 1000.0 / s_freq.QuadPart / s_frames);
                    s_frames = 0; s_sum.QuadPart = 0;
                }
            }
            return 0;
        }
    }
    if (g_force16 && src && dst == *G_PDDSPRIMARY && *G_BWINDOWED && present_gdi(src, sr))
        return 0;
    return oBlt(dst, dr, src, sr, flags, fx);
}

/* render.c's DDraw_InitSurfaces hook: the primary exists now */
void present_attach(void)
{
    if (g_force16 && !oBlt && *G_PDDSPRIMARY)
        mh_hook((DWORD)VTBL(*G_PDDSPRIMARY, VT_DDS_BLT), hkBlt, (void **)&oBlt, "IDirectDrawSurface::Blt");
}

void present_set_renderer(int d3d11)          /* Properties > System: at once */
{
    cfg.d3d11 = d3d11;
    if (!d3d11) dx_release();
    g_dx.failed = 0;
    log_msg("Renderer: %s", d3d11 ? "d3d11" : "gdi");
}
