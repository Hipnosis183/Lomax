/*
 * fastblit.c - Fast Blit: the sprite queue blitted by our own copy loop.
 */
#include "lomax.h"

/* ---- Fast Blit: the sprite queue blitted by our own copy loop -----------------------------------
   Game_Frame draws its queue bucket by bucket: every tile and sprite is one IDirectDrawSurface::Blt
   (DDBLT_KEYSRC, DDBLT_DDFX for mirroring, stretched when the rects differ), the D3D quads of a bucket
   are executed after its blits. On current Windows 16-bit DirectDraw is emulated on the CPU and each
   Blt costs far more than the copy itself (locks, format and rect checks, colour key setup), so a
   busy screen (hundreds of blits per frame, every frame again at high frame rates) runs out of time.
   The Blt call in that loop (SB_BLT_SITE) is replaced by sb_blt: the target and each source surface
   are locked once per frame (kept locked until a bucket's D3D quads are executed, SB_EXEC_SITE, and
   after the last bucket, SB_END_SITE), and the pixels are copied by a plain loop with the same rules:
   source colour key (range), left-right / up-down mirroring, nearest-pixel stretching, and nothing
   drawn for rects DirectDraw would refuse (empty, or outside a surface; there is no clipper). Anything
   else (other flags, not 16-bit, different pixel formats, a failed lock) goes to the real Blt.
   FastBlit=0 keeps the real Blt; the log then still times the loop, for comparison. */
#define SB_BLT_SITE             0x00404DFDu   /* mov eax,[esi]; push edx; push esi; call [eax+0x14] */
#define SB_EXEC_SITE            0x00404E13u   /* mov eax,[0x46a020]: a bucket's D3D quads are executed */
#define SB_END_SITE             0x00404EB9u   /* mov eax,[0x469a00]: after the last bucket */
#define SB_MAX_SURF             256
#define DDLOCK_WAIT_            0x00000001u
#define DDLOCK_READONLY_        0x00000010u
#define DDCKEY_SRCBLT_          0x00000008u
#define DDBLT_DDFX_             0x00000800u
#define DDBLT_KEYSRC_           0x00008000u
#define DDBLT_WAIT_             0x01000000u
#define VT_DDS_GETCOLORKEY      16
#define VT_DDS_LOCK             25
#define VT_DDS_UNLOCK           32
typedef HRESULT (WINAPI *GetColorKey_t)(void *, DWORD, DWORD *);
typedef HRESULT (WINAPI *Lock_t)(void *, RECT *, DDSD *, DWORD, HANDLE);
typedef HRESULT (WINAPI *Unlock_t)(void *, void *);
typedef struct {
    void *surf;
    BYTE *bits;               /* while locked */
    int   pitch, w, h;
    DWORD rmask;
    int   key;                /* has a source colour key */
    WORD  klo, khi;
    int   locked, bad;        /* bad: not 16-bit or the lock failed (real Blt this frame) */
} SbSurf;
static SbSurf   g_sb[SB_MAX_SURF];
static int      g_sbN;
static LONGLONG g_sbT0, g_sbTime;
static int      g_sbFrames, g_sbBlits, g_sbSlow;
static DWORD    g_sbStatTime;

void sb_unlock_all(void)
{
    int i;
    for (i = 0; i < g_sbN; i++)
        if (g_sb[i].locked) {
            ((Unlock_t)VTBL(g_sb[i].surf, VT_DDS_UNLOCK))(g_sb[i].surf, NULL);
            g_sb[i].locked = 0;
            g_sb[i].bits = NULL;
        }
}

/* the surface, locked (cached for this frame), or NULL */
static SbSurf *sb_get(void *surf, int dst)
{
    SbSurf *s = NULL;
    DDSD sd;
    int i;
    for (i = 0; i < g_sbN; i++)
        if (g_sb[i].surf == surf) { s = &g_sb[i]; break; }
    if (s) {
        if (s->bad) return NULL;
        if (s->locked) return s;
    } else {
        DWORD ck[2];
        if (g_sbN >= SB_MAX_SURF) return NULL;
        s = &g_sb[g_sbN++];
        memset(s, 0, sizeof(*s));
        s->surf = surf;
        if (((GetColorKey_t)VTBL(surf, VT_DDS_GETCOLORKEY))(surf, DDCKEY_SRCBLT_, ck) == 0) {
            s->key = 1;
            s->klo = (WORD)ck[0];
            s->khi = (WORD)ck[1];
        }
    }
    memset(&sd, 0, sizeof(sd));
    sd.dwSize = sizeof(sd);
    if (((Lock_t)VTBL(surf, VT_DDS_LOCK))(surf, NULL, &sd, DDLOCK_WAIT_ | (dst ? 0 : DDLOCK_READONLY_), NULL)) {
        s->bad = 1;
        return NULL;
    }
    if (sd.pf.dwRGBBitCount != 16 || !sd.lpSurface) {
        ((Unlock_t)VTBL(surf, VT_DDS_UNLOCK))(surf, NULL);
        s->bad = 1;
        return NULL;
    }
    s->locked = 1;
    s->bits = (BYTE *)sd.lpSurface;
    s->pitch = (int)sd.lPitch;
    s->w = (int)sd.dwWidth;
    s->h = (int)sd.dwHeight;
    s->rmask = sd.pf.dwR;
    return s;
}

/* 1 = done (drawn, or nothing to draw as DirectDraw would refuse it), 0 = leave it to the real Blt */
static int sb_draw(const SbSurf *d, const RECT *dr, const SbSurf *s, const RECT *sr, DWORD flags, const DWORD *fx)
{
    int dw = dr->right - dr->left, dh = dr->bottom - dr->top, sw = sr->right - sr->left, sh = sr->bottom - sr->top;
    int mx = 0, my = 0, key = (flags & DDBLT_KEYSRC_) != 0, x, y;
    WORD klo = s->klo, khi = s->khi;
    if (flags & ~(DDBLT_KEYSRC_ | DDBLT_DDFX_ | DDBLT_WAIT_)) return 0;
    if (flags & DDBLT_DDFX_) {
        if (!fx || (fx[1] & ~6u)) return 0;               /* DDBLTFX.dwDDFX: 2 mirror left-right, 4 up-down */
        mx = (fx[1] & 2) != 0;
        my = (fx[1] & 4) != 0;
    }
    if (key && !s->key) return 0;
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return 1;
    if (dr->left < 0 || dr->top < 0 || dr->right > d->w || dr->bottom > d->h ||
        sr->left < 0 || sr->top < 0 || sr->right > s->w || sr->bottom > s->h) return 1;
    for (y = 0; y < dh; y++) {
        int sy = sh == dh ? y : (int)(((2 * y + 1) * (LONGLONG)sh) / (2 * dh));
        const WORD *sp = (const WORD *)(s->bits + (sr->top + (my ? sh - 1 - sy : sy)) * s->pitch) + sr->left;
        WORD *dp = (WORD *)(d->bits + (dr->top + y) * d->pitch) + dr->left;
        if (sw == dw && !mx) {
            if (!key)
                memcpy(dp, sp, dw * 2);
            else if (klo == khi)
                for (x = 0; x < dw; x++) { WORD p = sp[x]; if (p != klo) dp[x] = p; }
            else
                for (x = 0; x < dw; x++) { WORD p = sp[x]; if (p < klo || p > khi) dp[x] = p; }
        } else {
            DWORD step = (DWORD)(((LONGLONG)sw << 16) / dw), acc = step / 2;
            for (x = 0; x < dw; x++, acc += step) {
                int sx = (int)(acc >> 16);
                WORD p = sp[mx ? sw - 1 - sx : sx];
                if (!key || p < klo || p > khi) dp[x] = p;
            }
        }
    }
    return 1;
}

/* replaces the queue's Blt call (same arguments, stdcall) */
static HRESULT WINAPI sb_blt(void *dst, RECT *dr, void *src, RECT *sr, DWORD flags, DWORD *fx)
{
    SbSurf *d, *s;
    if (!g_sbT0) {
        LARGE_INTEGER q;
        QueryPerformanceCounter(&q);
        g_sbT0 = q.QuadPart;
    }
    g_sbBlits++;
    if (cfg.fastBlit && src && dr && sr && src != dst && (d = sb_get(dst, 1)) && (s = sb_get(src, 0)) &&
        d->rmask == s->rmask && sb_draw(d, dr, s, sr, flags, fx))
        return 0;
    if (cfg.fastBlit) {
        sb_unlock_all();
        g_sbSlow++;
    }
    return ((Blt_t)VTBL(dst, VT_DDS_BLT))(dst, dr, src, sr, flags, fx);
}

void sb_end_frame(void)
{
    LARGE_INTEGER q, f;
    DWORD now;
    sb_unlock_all();
    g_sbN = 0;
    if (!g_sbT0) return;
    QueryPerformanceCounter(&q);
    g_sbTime += q.QuadPart - g_sbT0;
    g_sbT0 = 0;
    g_sbFrames++;
    now = timeGetTime();
    if (now - g_sbStatTime >= 5000) {
        if (g_sbStatTime && cfg.log && g_sbFrames) {
            QueryPerformanceFrequency(&f);
            log_msg("FastBlit%s: sprite loop %.3f ms per frame, %d blits per frame (%d by DirectDraw), %d frames",
                    cfg.fastBlit ? "" : "=0", g_sbTime * 1000.0 / f.QuadPart / g_sbFrames, g_sbBlits / g_sbFrames,
                    g_sbSlow / g_sbFrames, g_sbFrames);
        }
        g_sbStatTime = now;
        g_sbTime = 0;
        g_sbFrames = g_sbBlits = g_sbSlow = 0;
    }
}

/* SB_EXEC_SITE / SB_END_SITE: a call to these replaces the 5-byte "mov eax, [addr]" */
__asm__(".globl _sb_exec_thunk\n"
        "_sb_exec_thunk:\n"
        "  pushal\n"
        "  call _sb_unlock_all\n"
        "  popal\n"
        "  movl 0x0046A020, %eax\n"
        "  ret\n"
        ".globl _sb_end_thunk\n"
        "_sb_end_thunk:\n"
        "  pushal\n"
        "  call _sb_end_frame\n"
        "  popal\n"
        "  movl 0x00469A00, %eax\n"
        "  ret\n");
extern void sb_exec_thunk(void), sb_end_thunk(void);

void fastblit_init(void)
{
    static const struct { DWORD addr; const char *orig; } kSites[] = {
        { SB_BLT_SITE,  "8b065256ff5014" },
        { SB_EXEC_SITE, "a120a04600" },
        { SB_END_SITE,  "a1009a4600" },
    };
    BYTE want[8];
    DWORD old;
    size_t i;
    int n;
    for (i = 0; i < sizeof(kSites) / sizeof(kSites[0]); i++) {
        n = hex_to_bytes(kSites[i].orig, want, sizeof(want));
        if (n < 0 || !code_matches(kSites[i].addr, want, n)) {
            log_msg("FastBlit: unexpected code at %08lX, off", (unsigned long)kSites[i].addr);
            return;
        }
    }
    VirtualProtect((void *)SB_BLT_SITE, 0xC0, PAGE_EXECUTE_READWRITE, &old);
    {
        BYTE *p = (BYTE *)SB_BLT_SITE;                       /* push edx; push esi; call sb_blt */
        p[0] = 0x52; p[1] = 0x56; p[2] = 0xE8;
        *(DWORD *)(p + 3) = (DWORD)sb_blt - (SB_BLT_SITE + 7);
        p = (BYTE *)SB_EXEC_SITE;
        p[0] = 0xE8; *(DWORD *)(p + 1) = (DWORD)sb_exec_thunk - (SB_EXEC_SITE + 5);
        p = (BYTE *)SB_END_SITE;
        p[0] = 0xE8; *(DWORD *)(p + 1) = (DWORD)sb_end_thunk - (SB_END_SITE + 5);
    }
    VirtualProtect((void *)SB_BLT_SITE, 0xC0, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)SB_BLT_SITE, 0xC0);
    log_msg("FastBlit: %s", cfg.fastBlit ? "sprite queue blitted by the loader" : "off (DirectDraw Blt, timed)");
}
