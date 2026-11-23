/*
 * hfr.c - FPS Override (frames between the game's ticks, interpolated), and the widescreen mirroring
 * of narrow background layers; both work on the sprite queue.
 */
#include "lomax.h"

/* ---- FPS Override: high frame rate --------------------------------------------------------------
   The game logic keeps its fixed 60 Hz tick (50 with Speed50Hz); only drawing gets faster. Game_Frame
   runs its ticks, then builds a frame: Planes_UpdateDepth, Actors_DrawAndCull (actor draw callbacks,
   which also cull and keep per-tick state) and Planes_DrawTilemaps fill the sprite queue at
   0x4777D8 ([0] entry count, [1..256] bucket lists, 0x50-byte entries from +0x404: [1] surface,
   [2] DDBLTFX or 0, [3..6] source rect, [7..10] screen rect, clipped to the screen; D3D quads have
   +0x44 set and four (x, y, z) short vertices from +0x2C), which is then blitted, faded and presented.
   Frames in between ticks run no game code at all: the timing block of Game_Frame (0x404BD1, up to
   the 5-tick clamp) is replaced by a call to hfr_begin_frame, and when no tick is due but a frame is,
   Game_Frame jumps straight to its blit (0x404CB0) and redraws the queue of the last tick with every
   entry moved. So nothing that depends on how often frames are drawn changes, and demos keep their
   sync; the fades' rand() use is undone for those frames.
   Movement: while a tick's frame is built, each entry is tagged with what queued it, an Actor_Draw
   call (actor + n-th call this frame, its screen anchor computed as Actor_Draw does: x - plane scroll,
   or x for screen-space actors) or a TileLayer_Draw call (the layer, anchor = -scroll); entries
   queued any other way follow the current plane's camera. Matching tags of the previous tick give
   each one its motion per tick, and every frame is drawn at the state alpha of the way from the
   previous tick to the last (alpha = time since the last tick / tick length). That is one tick
   behind the game, the price of interpolating. Jumps over HFR_MAX_JUMP px per tick (teleports,
   wrapping planes, reused actor slots) are not interpolated.
   Screen edges: the game clips entries to the screen. Tiles are full 16x16 (12x12 at 0.75 zoom), so a
   clipped edge tile is widened back before it moves, and the ring of tiles just outside the screen
   is queued too (hfr_layer_ring), so a layer moving in uncovers tiles rather than the last frame's
   pixels; zoomed layers too (scaled 16x16 tiles). Sprites have one surface per frame, so a sprite cut
   at the screen edge gets the cut part back from its surface (hfr_move_entry).
   Scale: sources also record the scale they are drawn at around their anchor (layers and actors on
   the depth planes, whose zoom changes while Lomax walks between them; actors drawn scaled; Lomax
   himself during that walk), so an in-between frame shows the zoom part of the way too. Before, the
   zoomed planes' anchor (scroll * zoom) moved several px per tick while the picture hardly did, and
   the in-between frames shook it sideways.
   D3D quads: D3D_EmitTexturedQuad clears the entry's +0x44 flag once drawn, so it is restored with the
   rects each frame (else the quad was blitted as a plain sprite on in-between frames and flickered).
   Mesh quads (Sprite_Queue3DQuad: arches, towers, plane doors) are 3D, projected by Direct3D with a
   z-buffer that Game_Frame clears only when G_HAS3DQUADS is set, and it resets that flag on every call:
   in-between frames drew them against the last frame's depths and they flickered. The flag is set
   again for those frames, and the quads are anchored at their mesh's origin (with depth).
   FPS = frames per second to draw (more than the tick rate; up to HFR_MAX_FPS). */
#define HFR_TICK_LOOP           0x00404C29u   /* clear the queue, run ESI ticks, build the frame */
#define HFR_BLIT                0x00404CB0u   /* blit the queue, fade, present */
#define FN_PLANES_UPDATE_DEPTH  0x0040F420u
#define FN_PLANES_DRAW_TILEMAPS 0x0040F4C0u
#define G_SPRITEQUEUE           ((int *)0x004777D8u)
#define SQ_ENTRY(i)             ((int *)((BYTE *)G_SPRITEQUEUE + 0x404 + (i) * 0x50))
#define G_BSKIPSPRITEQUEUE      ((volatile int *)0x00461C8Cu)
#define G_LEVELPENDING          ((volatile int *)0x0052C59Cu)   /* Game_Frame sets up a level first */
#define G_PLAYERACTOR           ((int *)0x0059F200u)
#define G_PLAYERMAPZOOM         ((volatile int *)0x0052C568u)   /* world map drop: player drawn zoomed */
#define HFR_MAX_ENTRIES         1700          /* the queue's pool: 0x477BDC up to the next global, 0x498F1C */
#define HFR_MAX_SRC             2048
#define HFR_HASH                4096          /* power of two, > 2 * HFR_MAX_SRC */
#define HFR_MAX_JUMP            48            /* px per tick */
#define HFR_SAVE_OFS            0x0C          /* entry bytes restored each frame: rects, vertices and */
#define HFR_SAVE_LEN            0x3A          /* the D3D flag (+0x44), which D3D_EmitTexturedQuad clears */
#define G_TILESURFVID           ((BYTE *const *)0x0046AAB0u)    /* per tile sheet, 16x16 tiles */
#define G_TILESURFSYS           ((BYTE *const *)0x00469F40u)
#define G_ZOOMTILESURFVID       ((BYTE *const *)0x0046A7D8u)    /* 12x12 tiles (0.75 zoom) */
#define G_ZOOMTILESURFSYS       ((BYTE *const *)0x0046AA58u)

enum { HFR_SRC_ACTOR, HFR_SRC_LAYER, HFR_SRC_CAMERA, HFR_SRC_SCREEN, HFR_SRC_FIXED, HFR_SRC_MESH };
#define FN_TEXT_DRAW_STRING     0x0041F030u   /* int (Actor *, short x, short y, char *) */
#define FN_SPRITE_QUEUE_SCREEN  0x0040A7B0u   /* (SpriteFrame *, flags, short x, short y): screen space */
typedef int (__cdecl *TextDrawFn_t)(int *, int, int, const char *);
typedef int (__cdecl *QueueScreenFn_t)(void *, unsigned, int, int);
static TextDrawFn_t    oHfrTextDraw;
static QueueScreenFn_t oHfrQueueScreen;
static int             g_hfrInText;           /* inside Text_DrawString */
#define FN_SPRITE_QUEUE_3DQUAD  0x0040B9D0u   /* (plane, SpriteFrame *, flags, short *verts[4]): mesh quad */
#define G_HAS3DQUADS            ((volatile int *)0x00461C88u)   /* clear the z-buffer before the blit */
#define G_MESHORIGIN            ((short *const volatile *)0x0052C578u)   /* x, y, z of the mesh drawn */
#define G_DEPTHANGLE            ((volatile short *)0x0052C5C4u)  /* rotation of the depth planes */
#define G_MESHYPERANGLE         ((volatile int *)0x00461C94u)
#define G_MESHZPERANGLE         ((volatile int *)0x00461C98u)
#define G_MESHZSCALE            ((volatile int *)0x00461C9Cu)
#define G_ZOOMPLANE             ((BYTE *const volatile *)0x0059E774u)   /* plane the player zooms from */
#define G_ZOOMYOFS              ((volatile int *)0x004777C4u)
#define G_VIEWHEIGHT            ((volatile int *)0x00464424u)
typedef void (__cdecl *Quad3DFn_t)(void *, void *, unsigned, short **);
static Quad3DFn_t oHfrQueue3DQuad;
typedef struct {
    const void *key;          /* actor or layer */
    short kind, ord;          /* ord: n-th Actor_Draw of that actor in this frame */
    short tile;               /* layer: full tile size, 0 = scaled (no edge fix) */
    short valid;              /* anchor known */
    short plane;              /* actor placed in its plane's world: plane index + 1, else 0 */
    unsigned text;            /* text: hash of the characters; moves only between equal strings */
    int ax, ay, az;           /* screen anchor (az: mesh depth) */
    int z;                    /* scale around the anchor, per mille; 0 = unknown (moves only) */
    int dx, dy, dz;           /* motion since the previous tick's frame */
    int r;                    /* previous scale / this one, 16.16 */
} HfrSrc;

typedef void (__cdecl *ActorDrawFn_t)(int *, int, int, int);
typedef void *(__cdecl *LayerDrawFn_t)(BYTE *);
static ActorDrawFn_t oHfrActorDraw;
static LayerDrawFn_t oHfrLayerDraw;
static VoidFn_t      oHfrUpdateDepth, oHfrDrawTilemaps;

static HfrSrc   g_hfrSrc[2][HFR_MAX_SRC];      /* this frame's sources and the previous one's */
static int      g_hfrNSrc[2], g_hfrCur;
static short    g_hfrPrevHash[HFR_HASH];       /* previous frame: (key, kind, ord) -> index + 1 */
static struct { const void *key; int n; } g_hfrOrd[HFR_HASH];   /* Actor_Draw calls per actor */
static short    g_hfrTag[HFR_MAX_ENTRIES];     /* entry -> source index, -1 = camera */
static BYTE     g_hfrRing[HFR_MAX_ENTRIES];    /* entry added off screen by hfr_layer_ring */
static BYTE     g_hfrSave[HFR_MAX_ENTRIES][HFR_SAVE_LEN];
static int      g_hfrNEntries, g_hfrRec, g_hfrReplay, g_hfrAlpha;
static int      g_hfrTicks;                    /* ticks run by this Game_Frame call (0: none) */
static int      g_hfrSpan = 1;                 /* ticks between the last two recorded frames */
static LONGLONG g_hfrPeriod, g_hfrNext;
static DWORD    g_hfrSeed;
static int      g_hfrStatReal, g_hfrStatMid;
static HfrSrc  *g_hfrLastMesh;                 /* source of the last Sprite_Queue3DQuad call */
static int      g_hfrHas3D;                    /* G_HAS3DQUADS of the last tick's frame */
static DWORD    g_hfrStatTime;

static unsigned hfr_hash(const void *key, int kind, int ord)
{
    unsigned h = (unsigned)(DWORD)key * 2654435761u ^ (unsigned)(kind * 31 + ord) * 40503u;
    return (h ^ (h >> 15)) & (HFR_HASH - 1);
}

static HfrSrc *hfr_new_src(const void *key, int kind)
{
    HfrSrc *s;
    if (g_hfrNSrc[g_hfrCur] >= HFR_MAX_SRC) return NULL;
    s = &g_hfrSrc[g_hfrCur][g_hfrNSrc[g_hfrCur]++];
    memset(s, 0, sizeof(*s));
    s->key = key;
    s->kind = (short)kind;
    return s;
}

/* tags the entries queued since `before` that no inner call tagged yet */
static void hfr_tag(int before, const HfrSrc *s)
{
    int i, n = G_SPRITEQUEUE[0], idx = (int)(s - g_hfrSrc[g_hfrCur]);
    for (i = before < 0 ? 0 : before; i < n && i < HFR_MAX_ENTRIES; i++)
        if (g_hfrTag[i] == -1) g_hfrTag[i] = (short)idx;
}

static short hfr_next_ord(const void *key)     /* n-th call for key in this frame */
{
    unsigned h;
    for (h = hfr_hash(key, 0, 0); g_hfrOrd[h].key && g_hfrOrd[h].key != key; h = (h + 1) & (HFR_HASH - 1)) ;
    g_hfrOrd[h].key = key;
    return (short)g_hfrOrd[h].n++;
}

/* Text_DrawString(actor, x, y, string) queues each letter with Sprite_QueueScreen at screen (x, y):
   anchored there, so text stays put while the camera moves and scrolling text (credits) moves
   smoothly. Sprite_QueueScreen calls from elsewhere (Ui_DrawWindowFrame) are screen-fixed: no motion.
   Before these hooks both followed the camera on in-between frames and flickered. */
static int __cdecl hkHfrTextDraw(int *a, int x, int y, const char *str)
{
    HfrSrc *s;
    int before, r;
    if (!g_hfrRec || *G_BSKIPSPRITEQUEUE || !(s = hfr_new_src(a, HFR_SRC_SCREEN))) return oHfrTextDraw(a, x, y, str);
    s->ord = hfr_next_ord((const BYTE *)a + 1);    /* own count, apart from Actor_Draw's */
    s->ax = (short)x;
    s->ay = (short)y;
    s->valid = 1;
    {   /* letter-by-letter reveals (the title's author credits) redraw the string one character later
           and 8 px to the right: a new string, not movement */
        const char *c = str;
        unsigned hash = 2166136261u;
        while (c && *c && c - str < 64) hash = (hash ^ (BYTE)*c++) * 16777619u;
        s->text = hash ^ (unsigned)(c - str);
    }
    before = G_SPRITEQUEUE[0];
    g_hfrInText++;
    r = oHfrTextDraw(a, x, y, str);
    g_hfrInText--;
    hfr_tag(before, s);
    return r;
}

static int __cdecl hkHfrQueueScreen(void *frame, unsigned flags, int x, int y)
{
    HfrSrc *s;
    int before, r;
    if (!g_hfrRec || g_hfrInText || !(s = hfr_new_src(frame, HFR_SRC_FIXED))) return oHfrQueueScreen(frame, flags, x, y);
    before = G_SPRITEQUEUE[0];                     /* valid = 0: never moved */
    r = oHfrQueueScreen(frame, flags, x, y);
    hfr_tag(before, s);
    return r;
}

/* Sprite_Queue3DQuad: one quad of a 'SURF' mesh (arches, towers, plane doors), queued as a D3D quad
   with 3D vertices: the mesh's own (rotated) vertices plus its origin (G_MESHORIGIN, the object's
   position minus its plane's scroll) and a lift and depth from the depth planes' rotation; Direct3D
   projects them. Anchored at that 3D offset, so the quads move with their plane (not the player's)
   and in depth as the planes turn. Consecutive quads of one mesh share a source. */
static void __cdecl hkHfrQueue3DQuad(void *plane, void *frame, unsigned flags, short **verts)
{
    HfrSrc *s = NULL, *last = g_hfrLastMesh;
    const short *o = *G_MESHORIGIN;
    int before, ang, ax, ay, az;
    if (!g_hfrRec || *G_BSKIPSPRITEQUEUE || !o) { oHfrQueue3DQuad(plane, frame, flags, verts); return; }
    ang = *G_DEPTHANGLE;
    ax = o[0];
    ay = o[1] + *G_MESHYPERANGLE * ang / -1000;
    az = (short)(*G_MESHZPERANGLE * ang / 100 + o[2]) * *G_MESHZSCALE / 100;
    if (last && last->key == o && last->ax == ax && last->ay == ay && last->az == az)
        s = last;
    else if ((s = hfr_new_src(o, HFR_SRC_MESH)) != NULL) {
        s->ord = hfr_next_ord(o);
        s->ax = ax; s->ay = ay; s->az = az;
        s->valid = 1;
        g_hfrLastMesh = s;
    }
    before = G_SPRITEQUEUE[0];
    oHfrQueue3DQuad(plane, frame, flags, verts);
    if (s) hfr_tag(before, s);
}

static void __cdecl hkHfrActorDraw(int *a, int scale, int angle, int alpha)
{
    HfrSrc *s;
    int before;
    if (!g_hfrRec || *G_BSKIPSPRITEQUEUE || !(s = hfr_new_src(a, HFR_SRC_ACTOR))) {
        oHfrActorDraw(a, scale, angle, alpha);
        return;
    }
    s->ord = hfr_next_ord(a);
    {   /* the screen position Actor_Draw starts from, and its scale around it */
        const BYTE *pl = (const BYTE *)(G_PLANES + *(short *)((BYTE *)a + 0x5E) * PLANE_STRIDE);
        int flags = a[0x16], pz = *(const short *)(pl + 0x42);
        if (a == G_PLAYERACTOR && *G_PLAYERMAPZOOM) {       /* walking between depth planes, world map drop */
            const BYTE *zp = *G_ZOOMPLANE;
            int den = zp ? *(const short *)(zp + 0x40) + 1000000 / *G_PLAYERMAPZOOM + *G_DEPTHANGLE - 1000 : 0;
            if (den > 0) {
                int z = 1000000 / den, W = g_wsWidth ? g_wsWidth : 320, vh = *G_VIEWHEIGHT;
                s->ax = (short)(a[0xF] * z / 1000 + *(const int *)(zp + 4) * z / -1000 + (z * -W / 1000 + W) / 2);
                s->ay = (short)((a[0x10] - *G_ZOOMYOFS) * z / 1000 + (vh * z / -1000 + vh) / 3 -
                                *(const int *)(zp + 8) * z / 1000);
                s->z = z;
                s->valid = 1;
            }
        } else if (flags & 0x80) {
            s->ax = a[0xF]; s->ay = a[0x10]; s->valid = 1;
        } else if (flags & 0x40) {
            s->ax = a[0xF]; s->ay = a[0x10]; s->z = scale; s->valid = 1;
        } else if (*(const int *)(pl + 0x3C) && pz != 1000 && (short)a[0x18] >= 0) {
            if (pz > 0) {                                   /* zoomed rotating plane */
                s->ax = (short)(a[0xF] * pz / 1000) - *(const short *)(pl + 0xC);
                s->ay = (short)(a[0x10] * pz / 1000) - *(const short *)(pl + 0xE);
                s->z = scale * pz / 1000;
                s->valid = 1;
            }
        } else {
            s->ax = a[0xF] - *(const int *)(pl + 4);
            s->ay = a[0x10] - *(const int *)(pl + 8);
            s->z = scale;
            s->valid = 1;
            s->plane = (short)(*(short *)((BYTE *)a + 0x5E) + 1);
        }
    }
    before = G_SPRITEQUEUE[0];
    oHfrActorDraw(a, scale, angle, alpha);
    hfr_tag(before, s);
}

static int hfr_floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static int hfr_mod(int a, int b)      { a %= b; return a < 0 ? a + b : a; }

/* Queues the ring of tiles just outside the screen that TileLayer_Draw skipped, so moving the layer
   on an in-between frame uncovers tiles instead of the last frame's pixels. They sit off screen
   (clipped to nothing) until moved. Same lookup as TileLayer_Draw: the map wraps, tile def = 16 bytes
   {sheet, srcX, srcY}, index 0 = empty. Tile k of a row starts at floor(k * T * z / 1000) - sx: T = 16,
   or 12 for the pre-shrunk 750 tiles (z = 1000 for both); other zooms scale the 16 px tiles, from the
   video-memory sheet only when the card stretches blits (g_bHWStretchBlt), as TileLayer_Draw does. */
#define G_BHWSTRETCHBLT         ((volatile int *)0x00461C84u)
static int hfr_tile_pos(int k, int T, int z) { return hfr_floordiv(k * T * z, 1000); }

static void hfr_layer_ring(BYTE *layer, int T, int z, int sx, int sy)
{
    int W = g_wsWidth ? g_wsWidth : 320, H = 240, step = T * z;
    int wT = *(const int *)(layer + 0x10), hT = *(const int *)(layer + 0x14), k, r, k0, k1, r0, r1;
    const unsigned short *map = *(const unsigned short *const *)(layer + 0x18);
    const BYTE *defs = *(const BYTE *const *)(layer + (T == 16 ? 0x24 : 0x28));
    BYTE *const *vid = T == 16 ? G_TILESURFVID : G_ZOOMTILESURFVID;
    BYTE *const *sys = T == 16 ? G_TILESURFSYS : G_ZOOMTILESURFSYS;
    int *head = G_SPRITEQUEUE + *(const short *)(layer + 0x38) + 1;
    int useVid = z == 1000 ? !*G_BUSESTRETCH : *G_BHWSTRETCHBLT != 0;
    if (!map || !defs || wT <= 0 || hT <= 0 || step <= 0) return;
    k0 = hfr_floordiv(sx * 1000, step) - 1; k1 = hfr_floordiv((sx + W - 1) * 1000, step) + 1;
    r0 = hfr_floordiv(sy * 1000, step) - 1; r1 = hfr_floordiv((sy + H - 1) * 1000, step) + 1;
    for (r = r0; r <= r1; r++)
        for (k = k0; k <= k1; k++) {
            int x0 = hfr_tile_pos(k, T, z) - sx, x1 = hfr_tile_pos(k + 1, T, z) - sx;
            int y0 = hfr_tile_pos(r, T, z) - sy, y1 = hfr_tile_pos(r + 1, T, z) - sy;
            int n = G_SPRITEQUEUE[0], *e;
            unsigned idx;
            const unsigned short *def;
            BYTE *surf;
            if (r != r0 && r != r1 && k != k0 && k != k1) continue;
            if (x1 > 0 && x0 < W && y1 > 0 && y0 < H) continue;     /* on screen: the game drew it */
            if (x1 <= x0 || y1 <= y0) continue;
            idx = map[wT * hfr_mod(r, hT) + hfr_mod(k, wT)];
            if (!idx) continue;
            def = (const unsigned short *)(defs + idx * 16);
            surf = useVid && vid[def[0]] ? vid[def[0]] : sys[def[0]];
            if (!surf || n >= HFR_MAX_ENTRIES - 1) continue;
            e = SQ_ENTRY(n);
            G_SPRITEQUEUE[0] = n + 1;
            e[0] = *head;
            *head = (int)e;
            e[1] = (int)surf;
            e[2] = 0;
            e[3] = def[1]; e[4] = def[2]; e[5] = def[1] + T; e[6] = def[2] + T;
            e[7] = x0;     e[8] = y0;     e[9] = x1;         e[10] = y1;
            *(short *)((BYTE *)e + 0x44) = 0;
            g_hfrRing[n] = 1;
        }
}

/* Widescreen Background tile layers narrower than the screen (parallax planes such as the clouds and
   castles of 3.2: 24 tiles, 384 px) only ever showed up to 320 px of themselves; on a wider screen
   TileLayer_Draw repeats the map and the picture breaks where the repeat starts. Past the map's end the
   tiles are replaced by the mirrored ones instead (column 2w-1-k at column k, drawn mirrored; rows the
   same), so the picture continues without a seam, as the backdrop sprites do (ParallaxSprite_Draw).
   Layers the original already showed repeated (g_wsLayerRepeat: 2.1's forest and hills) keep the repeat
   on that axis. Only unzoomed layers that aren't the current plane (which has its own handling: centring, black bars or
   walkable repeated floor). Applied to every entry the layer queued, the high frame rate's ring too. */
int g_hfrValid;

#define G_BLTMIRRORFX           ((void *const *)0x00461B88u)   /* DDBLTFX * by mirror bits: 1 = x, 2 = y */
static LayerDrawFn_t oWsLayerDraw;

static void ws_layer_mirror(BYTE *layer, int before)
{
    int W = g_wsWidth, wT, hT, n, i, sx, sy, plane, rep;
    const unsigned short *map;
    const BYTE *defs;
    if (!W || layer == (BYTE *)*G_PCURPLANE || (*(const int *)(layer + 0x3C) && *(const short *)(layer + 0x42) != 1000))
        return;
    plane = (int)((DWORD)layer - G_PLANES) / PLANE_STRIDE;
    rep = plane >= 0 && plane < MAX_PLANES ? g_wsLayerRepeat[plane] : 0;
    if (rep == 3) return;
    wT = *(const int *)(layer + 0x10);
    hT = *(const int *)(layer + 0x14);
    map = *(const unsigned short *const *)(layer + 0x18);
    defs = *(const BYTE *const *)(layer + 0x24);
    if (!map || !defs || wT <= 0 || hT <= 0 || (wT * 16 >= W && hT * 16 >= 240)) return;
    sx = *(const int *)(layer + 4);
    sy = *(const int *)(layer + 8);
    n = G_SPRITEQUEUE[0];
    if (n > HFR_MAX_ENTRIES) n = HFR_MAX_ENTRIES;
    for (i = before < 0 ? 0 : before; i < n; i++) {
        int *e = SQ_ENTRY(i), *sr = e + 3, *d = e + 7;
        int k, r, mk, mr, fx = 0, la, lb, ta, tb;
        unsigned idx;
        const unsigned short *def;
        BYTE *surf;
        if (*(short *)((BYTE *)e + 0x44) || d[2] <= d[0] || d[3] <= d[1]) continue;
        k = hfr_floordiv((d[0] + d[2]) / 2 + sx, 16);      /* the map cell the entry's centre is in */
        r = hfr_floordiv((d[1] + d[3]) / 2 + sy, 16);
        if (k >= 0 && k < wT && r >= 0 && r < hT) continue;
        if (((rep & 1) || (k >= 0 && k < wT)) && ((rep & 2) || (r >= 0 && r < hT))) continue;
        mk = hfr_mod(k, (rep & 1 ? 1 : 2) * wT);
        mr = hfr_mod(r, (rep & 2 ? 1 : 2) * hT);
        if (mk >= wT) { mk = 2 * wT - 1 - mk; fx |= 1; }
        if (mr >= hT) { mr = 2 * hT - 1 - mr; fx |= 2; }
        idx = map[wT * mr + mk];
        if (!idx) { d[0] = d[1] = d[2] = d[3] = 0; continue; }   /* empty: nothing drawn */
        def = (const unsigned short *)(defs + idx * 16);
        surf = !*G_BUSESTRETCH && G_TILESURFVID[def[0]] ? G_TILESURFVID[def[0]] : G_TILESURFSYS[def[0]];
        if (!surf) continue;
        la = sr[0] & 15; lb = la + (sr[2] - sr[0]);           /* the part of the 16x16 tile shown */
        ta = sr[1] & 15; tb = ta + (sr[3] - sr[1]);
        if (fx & 1) { int t = 16 - lb; lb = 16 - la; la = t; }
        if (fx & 2) { int t = 16 - tb; tb = 16 - ta; ta = t; }
        e[1] = (int)surf;
        e[2] = (int)G_BLTMIRRORFX[fx];
        sr[0] = def[1] + la; sr[2] = def[1] + lb;
        sr[1] = def[2] + ta; sr[3] = def[2] + tb;
    }
}

static void *__cdecl hkWsLayerDraw(BYTE *layer)       /* without the high frame rate hook */
{
    int before = G_SPRITEQUEUE[0];
    void *r = oWsLayerDraw(layer);
    ws_layer_mirror(layer, before);
    return r;
}

static void *__cdecl hkHfrLayerDraw(BYTE *layer)
{
    HfrSrc *s;
    int before, zoom;
    void *r;
    if (!g_hfrRec || !(s = hfr_new_src(layer, HFR_SRC_LAYER))) {
        before = G_SPRITEQUEUE[0];
        r = oHfrLayerDraw(layer);
        ws_layer_mirror(layer, before);
        return r;
    }
    zoom = *(const int *)(layer + 0x3C) ? *(const short *)(layer + 0x42) : 1000;
    if (zoom == 1000) {
        s->ax = -*(const int *)(layer + 4);
        s->ay = -*(const int *)(layer + 8);
    } else {
        s->ax = -*(const short *)(layer + 0xC);
        s->ay = -*(const short *)(layer + 0xE);
    }
    s->tile = zoom == 750 ? 12 : 16;          /* source tile size */
    s->z = zoom;
    s->valid = zoom > 0;
    before = G_SPRITEQUEUE[0];
    r = oHfrLayerDraw(layer);
    if (s->valid)
        hfr_layer_ring(layer, s->tile, zoom == 1000 || zoom == 750 ? 1000 : zoom, -s->ax, -s->ay);
    ws_layer_mirror(layer, before);
    hfr_tag(before, s);
    return r;
}

static void __cdecl hkHfrUpdateDepth(void)
{
    if (g_hfrTicks > 0) {                     /* a tick's frame is being built: record it */
        g_hfrRec = 1;
        g_hfrCur ^= 1;
        g_hfrNSrc[g_hfrCur] = 0;
        memset(g_hfrOrd, 0, sizeof(g_hfrOrd));
        memset(g_hfrTag, 0xFF, sizeof(g_hfrTag));
        memset(g_hfrRing, 0, sizeof(g_hfrRing));
        g_hfrLastMesh = NULL;
    }
    oHfrUpdateDepth();
}

/* Motion of one axis on an in-between frame. A source drawn at scale z around its anchor a showed a
   point now at v at (v - a) * r + a - d in the previous tick's frame (r = previous z / z, d = anchor
   motion); the frame shows it back / span of the way there: off(v) = (v * k + b) / 2^32. */
typedef struct { LONGLONG k, b; } HfrAxis;

static void hfr_axis(HfrAxis *t, int a, int d, int r, LONGLONG back)
{
    t->k = (LONGLONG)(r - 65536) * back / g_hfrSpan;
    t->b = -(LONGLONG)d * 65536 * back / g_hfrSpan - a * t->k;
}

static int hfr_off(const HfrAxis *t, int v) { return (int)((v * t->k + t->b + 0x80000000LL) >> 32); }

/* moves one entry (and its depth by oz, mesh quads), re-clipping it to the screen. Entries of a scaled
   source get both edges moved; the others keep their size and all move by (ox, oy), the offset of one
   point of their source, so the blit needs no stretching and tiles stay edge to edge. (Deciding by the
   entry's own size left 1 px seams: a zoomed tile can come out exactly 16 px wide.)
   Screen edges: the game cut what it queued at the screen edge, so whatever moves inward would leave a
   strip of the previous frame there. Tiles (tile = their source size, 16 or 12) get back the part the
   game cut off, scaled as drawn. Sprites (unclip) live one frame per surface with their rect starting
   at (0, 0): a side cut at the screen edge is extended back to the surface's bound (by up to
   HFR_UNCLIP px of the source), but only when the opposite side is at its bound or cut as well, which a
   piece of a shared sheet wouldn't be. */
#define HFR_UNCLIP              64
#define VT_DDS_GETSURFACEDESC   22
typedef HRESULT (WINAPI *GetSurfaceDesc_t)(void *, DDSD *);

/* extends one axis of a sprite cut at the screen edge back towards its surface bound. Sprite_CreateSurface
   rounds the width up to a multiple of 8 and fills the extra columns with palette index 0 (transparent):
   on the x axis, a side up to pad (7) px short of the surface's right bound counts as at the bound. */
static void hfr_unclip_axis(int *s, int *d, int lim, int size, int mirror, int pad)
{
    int w = s[2] - s[0], dw = d[2] - d[0];
    int capLo = mirror ? size - s[2] : s[0], capHi = mirror ? s[0] : size - s[2];  /* at screen 0 / lim */
    int boundLo = capLo <= (mirror ? pad : 0), boundHi = capHi <= (mirror ? 0 : pad);
    int cutLo = d[0] == 0 && capLo > 0, cutHi = d[2] == lim && capHi > 0;
    if (w <= 0 || dw <= 0) return;
    if (cutLo && (boundHi || cutHi)) {
        int c = capLo < HFR_UNCLIP ? capLo : HFR_UNCLIP;
        if (mirror) s[2] += c; else s[0] -= c;
        d[0] -= (c * dw + w / 2) / w;
    }
    if (cutHi && (boundLo || cutLo)) {
        int c = capHi < HFR_UNCLIP ? capHi : HFR_UNCLIP;
        if (mirror) s[0] -= c; else s[2] += c;
        d[2] += (c * dw + w / 2) / w;
    }
}

static void hfr_move_entry(int *e, const HfrAxis *tx, const HfrAxis *ty, int oz, int tile, int unclip,
                           int scaled, int ox, int oy)
{
    int W = g_wsWidth ? g_wsWidth : 320, H = 240;
    int *s = e + 3, *d = e + 7, sw, sh, dw, dh;
    DWORD fx;
    if (*(short *)((BYTE *)e + 0x44)) {        /* D3D quad: move the vertices */
        short *v = (short *)((BYTE *)e + 0x2C);
        int k;
        for (k = 0; k < 4; k++) {
            v[k * 3]     = (short)(v[k * 3] + hfr_off(tx, v[k * 3]));
            v[k * 3 + 1] = (short)(v[k * 3 + 1] + hfr_off(ty, v[k * 3 + 1]));
            v[k * 3 + 2] = (short)(v[k * 3 + 2] + oz);
        }
        return;
    }
    fx = e[2] ? ((const DWORD *)e[2])[1] : 0;  /* DDBLTFX.dwDDFX: 2 mirror left-right, 4 up-down */
    if (tile) {                                /* widen tiles the game clipped at the screen edge */
        int w = s[2] - s[0], h = s[3] - s[1], dw0 = d[2] - d[0], dh0 = d[3] - d[1];
        if (w > 0 && w < tile && dw0 > 0) {        /* mirrored tiles (ws_layer_mirror) show it reversed */
            int c = tile - w, dc = (c * dw0 + w / 2) / w, *lo = fx & 2 ? &s[2] : &s[0], *hi = fx & 2 ? &s[0] : &s[2];
            int sg = fx & 2 ? -1 : 1;
            if (d[0] == 0)      { *lo -= sg * c; d[0] -= dc; }
            else if (d[2] == W) { *hi += sg * c; d[2] += dc; }
        }
        if (h > 0 && h < tile && dh0 > 0) {
            int c = tile - h, dc = (c * dh0 + h / 2) / h, *lo = fx & 4 ? &s[3] : &s[1], *hi = fx & 4 ? &s[1] : &s[3];
            int sg = fx & 4 ? -1 : 1;
            if (d[1] == 0)      { *lo -= sg * c; d[1] -= dc; }
            else if (d[3] == H) { *hi += sg * c; d[3] += dc; }
        }
    } else if (unclip && e[1] && (d[0] == 0 || d[2] == W || d[1] == 0 || d[3] == H)) {
        DDSD sd;
        memset(&sd, 0, sizeof(sd));
        sd.dwSize = sizeof(sd);
        if (((GetSurfaceDesc_t)VTBL((void *)e[1], VT_DDS_GETSURFACEDESC))((void *)e[1], &sd) == 0) {
            hfr_unclip_axis(s, d, W, (int)sd.dwWidth, (fx & 2) != 0, 7);         /* [0], [2]: x */
            hfr_unclip_axis(s + 1, d + 1, H, (int)sd.dwHeight, (fx & 4) != 0, 0); /* [1], [3]: y */
        }
    }
    sw = s[2] - s[0]; sh = s[3] - s[1];
    if (!scaled) {
        d[0] += ox; d[2] += ox;
        d[1] += oy; d[3] += oy;
    } else {
        d[0] += hfr_off(tx, d[0]); d[2] += hfr_off(tx, d[2]);
        d[1] += hfr_off(ty, d[1]); d[3] += hfr_off(ty, d[3]);
    }
    dw = d[2] - d[0]; dh = d[3] - d[1];
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    if (d[0] < 0) { int c = -d[0] * sw / dw;      if (fx & 2) s[2] -= c; else s[0] += c; d[0] = 0; }
    if (d[2] > W) { int c = (d[2] - W) * sw / dw; if (fx & 2) s[0] += c; else s[2] -= c; d[2] = W; }
    if (d[1] < 0) { int c = -d[1] * sh / dh;      if (fx & 4) s[3] -= c; else s[1] += c; d[1] = 0; }
    if (d[3] > H) { int c = (d[3] - H) * sh / dh; if (fx & 4) s[1] += c; else s[3] -= c; d[3] = H; }
    if (d[2] <= d[0] || d[3] <= d[1] || s[2] <= s[0] || s[3] <= s[1])
        d[0] = d[1] = d[2] = d[3] = 0;          /* off screen: Blt refuses the empty rect */
}

/* puts every entry where it is alpha of the way from the previous tick to the last one */
static void hfr_apply(void)
{
    const HfrSrc *src = g_hfrSrc[g_hfrCur];
    LONGLONG back = 65536 - g_hfrAlpha;           /* how far back from the last tick, 16.16 */
    HfrAxis tx = { 0, 0 }, ty = { 0, 0 };
    int i, n = g_hfrNEntries, last = -1, oz = 0, scaled = 0, ox = 0, oy = 0, W = g_wsWidth ? g_wsWidth : 320;
    for (i = 0; i < n; i++) {
        int *e = SQ_ENTRY(i), t = g_hfrTag[i];
        const HfrSrc *s = &src[t];
        memcpy((BYTE *)e + HFR_SAVE_OFS, g_hfrSave[i], HFR_SAVE_LEN);
        if (!s->dx && !s->dy && !s->dz && s->r == 65536 && !g_hfrRing[i]) continue;
        if (t != last) {                       /* ring tiles are clipped away when they don't move in */
            hfr_axis(&tx, s->ax, s->dx, s->r, back);
            hfr_axis(&ty, s->ay, s->dy, s->r, back);
            oz = (int)((-back * s->dz / g_hfrSpan + 32768) >> 16);
            scaled = s->kind == HFR_SRC_LAYER ? s->z != 1000 && s->z != 750 : s->z > 0 && s->z != 1000;
            if (s->kind == HFR_SRC_LAYER) {    /* one offset for a whole unscaled layer: its screen centre */
                ox = hfr_off(&tx, W / 2);
                oy = hfr_off(&ty, 120);
            } else {
                ox = hfr_off(&tx, s->ax);
                oy = hfr_off(&ty, s->ay);
            }
            last = t;
        }
        hfr_move_entry(e, &tx, &ty, oz, s->kind == HFR_SRC_LAYER && !g_hfrRing[i] ? s->tile : 0,
                       s->kind == HFR_SRC_ACTOR || s->kind == HFR_SRC_CAMERA, scaled, ox, oy);
    }
}

/* An actor drawn for the first time has no previous position. It takes its plane's camera motion, so
   for that tick it stays put in the world; left still on screen it slid against the background on the
   in-between frames and snapped back at the next tick (seen when a frozen ghost becomes the real actor). */
static int g_hfrScroll[MAX_PLANES][2], g_hfrScrollValid;

static void hfr_first_motion(HfrSrc *s, int lim)
{
    const BYTE *pl;
    int dx, dy;
    if (!g_hfrScrollValid || s->plane < 1 || s->plane > MAX_PLANES) return;
    pl = (const BYTE *)(G_PLANES + (s->plane - 1) * PLANE_STRIDE);
    dx = g_hfrScroll[s->plane - 1][0] - *(const int *)(pl + 4);
    dy = g_hfrScroll[s->plane - 1][1] - *(const int *)(pl + 8);
    if (abs(dx) <= lim && abs(dy) <= lim) { s->dx = dx; s->dy = dy; }
}

static void __cdecl hkHfrDrawTilemaps(void)
{
    HfrSrc *cur, *prev = g_hfrSrc[g_hfrCur ^ 1], *cam;
    const BYTE *plane = (const BYTE *)*G_PCURPLANE;
    int i, n, nCur, W = g_wsWidth ? g_wsWidth : 320;
    oHfrDrawTilemaps();
    if (!g_hfrRec) return;
    g_hfrRec = 0;
    g_hfrHas3D = *G_HAS3DQUADS;
    g_hfrSpan = g_hfrTicks;
    if (!g_hfrValid) {                           /* new surfaces/level: no motion */
        memset(g_hfrPrevHash, 0, sizeof(g_hfrPrevHash));
        g_hfrScrollValid = 0;
    }
    if ((cam = hfr_new_src(NULL, HFR_SRC_CAMERA)) && plane) {   /* entries no hook saw */
        cam->ax = -*(const int *)(plane + 4);
        cam->ay = -*(const int *)(plane + 8);
        cam->valid = 1;
    }
    cur = g_hfrSrc[g_hfrCur];
    nCur = g_hfrNSrc[g_hfrCur];
    for (i = 0; i < nCur; i++) {                 /* motion: the same source in the previous frame */
        HfrSrc *s = &cur[i];
        unsigned h = hfr_hash(s->key, s->kind, s->ord);
        int lim = HFR_MAX_JUMP * g_hfrTicks, found = 0;
        s->dx = s->dy = s->dz = 0;
        s->r = 65536;
        if (!s->valid) continue;
        for (; g_hfrPrevHash[h]; h = (h + 1) & (HFR_HASH - 1)) {
            const HfrSrc *p = &prev[g_hfrPrevHash[h] - 1];
            if (p->key != s->key || p->kind != s->kind || p->ord != s->ord) continue;
            found = 1;
            if (p->valid && p->text == s->text) {
                /* the jump limit applies to the motion at the anchor, or the nearest screen point */
                int r = s->z > 0 && p->z > 0 ? (int)((LONGLONG)p->z * 65536 / s->z) : 65536;
                int vx = s->ax < 0 ? 0 : s->ax > W ? W : s->ax, vy = s->ay < 0 ? 0 : s->ay > 240 ? 240 : s->ay;
                int dx = s->ax - p->ax, dy = s->ay - p->ay, dz = s->az - p->az;
                int mx = (int)(((LONGLONG)(vx - s->ax) * (r - 65536)) >> 16) - dx;
                int my = (int)(((LONGLONG)(vy - s->ay) * (r - 65536)) >> 16) - dy;
                if (r >= 49152 && r <= 87381 && abs(mx) <= lim && abs(my) <= lim && abs(dz) <= 4 * lim) {
                    s->dx = dx; s->dy = dy; s->dz = dz;
                    s->r = r;
                }
            }
            break;
        }
        if (!found) hfr_first_motion(s, lim);
    }
    for (i = 0; i < MAX_PLANES; i++) {
        const BYTE *pl = (const BYTE *)(G_PLANES + i * PLANE_STRIDE);
        g_hfrScroll[i][0] = *(const int *)(pl + 4);
        g_hfrScroll[i][1] = *(const int *)(pl + 8);
    }
    g_hfrScrollValid = 1;
    memset(g_hfrPrevHash, 0, sizeof(g_hfrPrevHash));
    for (i = 0; i < nCur; i++) {
        unsigned h = hfr_hash(cur[i].key, cur[i].kind, cur[i].ord);
        while (g_hfrPrevHash[h]) h = (h + 1) & (HFR_HASH - 1);
        g_hfrPrevHash[h] = (short)(i + 1);
    }
    n = G_SPRITEQUEUE[0];
    if (n > HFR_MAX_ENTRIES) n = HFR_MAX_ENTRIES;
    for (i = 0; i < n; i++) {
        if (g_hfrTag[i] == -1) g_hfrTag[i] = cam ? (short)(cam - cur) : 0;
        memcpy(g_hfrSave[i], (BYTE *)SQ_ENTRY(i) + HFR_SAVE_OFS, HFR_SAVE_LEN);
    }
    g_hfrNEntries = cam ? n : 0;
    g_hfrValid = cam != NULL;
    hfr_apply();
}

/* With VSync: frames and ticks on the refreshes vb_thread sees. A frame is drawn right after
   every K-th refresh, K = refresh / FPS rounded, at least 1 (every refresh with FPS at or above the
   refresh rate), so it has a whole refresh to reach the screen. Ticks are counted on the same
   refreshes: one every N refreshes when the rate is within 2% of a multiple of the tick rate (as VSync
   without HighFrameRate; alpha 0 and 1/2 at 120 Hz), else every original tick length (16.7 ms) of the
   refreshes' timestamps, so every frame shows the game exactly one frame period further on (144 Hz:
   2.4 frames per tick). Returns 1 = draw now (*ticks, *alpha set), 0 = not yet, -1 = no vertical
   blank events (the timer path decides). */
static int       g_hfrVblank;                /* VSync: vb_thread runs */
static int       g_hvN = -1, g_hvK;          /* refreshes per tick (0 = original tick length), per frame */
static LONG      g_hvLast;                   /* g_vbCount at the last call */
static LONGLONG  g_hvLastQ, g_hvQAcc, g_hvTickQ0;   /* last refresh time, time towards a tick, tick length */
static int       g_hvRefAcc, g_hvFrame;      /* refreshes towards a tick / a frame */

static int hfr_vs_clock(int *ticks, int *alpha)
{
    double hz = vb_hz();
    LONGLONG q;
    LONG c, d;
    int n, k;
    if (!hz || vb_follows_frames(hz)) { g_hvN = -1; return -1; }
    n = (int)(hz / g_tickHz + 0.5);
    if (n < 1 || fabs(hz / n - g_tickHz) > g_tickHz * 0.02) n = 0;
    k = (int)(hz / cfg.hfrFps + 0.5);
    if (k < 1) k = 1;
    c = g_vbCount;
    q = InterlockedCompareExchange64(&g_vbQpc, 0, 0);
    if (n != g_hvN || k != g_hvK) {
        log_msg("HighFrameRate+VSync: %.2f Hz display, a frame every %d refresh(es), %s", hz, k,
                n ? "a tick every N refreshes" : "ticks at the original length");
        g_hvN = n;
        g_hvK = k;
        g_hvLast = c;
        g_hvLastQ = q;
        g_hvRefAcc = 0;
        g_hvQAcc = 0;
        g_hvFrame = k - 1;                               /* first frame at the next refresh */
        return 0;
    }
    if (c == g_hvLast) return 0;
    d = c - g_hvLast > 30 ? 30 : c - g_hvLast;
    g_hvLast = c;
    g_hvRefAcc += d;
    g_hvQAcc += q - g_hvLastQ;
    g_hvLastQ = q;
    if (g_hvQAcc > 5 * g_hvTickQ0) g_hvQAcc = 5 * g_hvTickQ0;
    g_hvFrame += d;
    if (g_hvFrame < k) return 0;                         /* the ticks wait for the frame */
    g_hvFrame = 0;
    if (n) {
        *ticks = g_hvRefAcc / n;
        g_hvRefAcc %= n;
        *alpha = g_hvRefAcc * 65536 / n;
    } else {
        *ticks = (int)(g_hvQAcc / g_hvTickQ0);
        g_hvQAcc %= g_hvTickQ0;
        *alpha = (int)(g_hvQAcc * 65536 / g_hvTickQ0);
    }
    *G_FRAMETIME = timeGetTime();                        /* the timer path resumes from here */
    *G_FRAMEACCUM = 0;
    return 1;
}

/* Game_Frame's timing (patched in at GF_TIMING): ticks to run (> 0, then the frame is built as
   usual), 0 = redraw the last tick's queue for an in-between frame, -1 = nothing to do yet */
/* without VSync: the 1/FPS slot of a frame that is drawn is used up. Only then: while no frame can be
   drawn (no tick due and nothing to redraw, as right after a level loads) the game calls this
   millions of times a second, and moving the slot on each call put the next one hours ahead (the
   game froze once the redraws were possible again). */
static void hfr_slot_used(LONGLONG q)
{
    g_hfrNext += g_hfrPeriod;
    if (g_hfrNext <= q) g_hfrNext = q + g_hfrPeriod;   /* fell behind: restart */
}

static int __cdecl hfr_begin_frame(void)
{
    DWORD now;
    int acc, ticks, alpha, vs = g_hfrVblank ? hfr_vs_clock(&ticks, &alpha) : -1;
    LARGE_INTEGER q;
    q.QuadPart = 0;
    if (vs == 0) return -1;
    if (vs < 0) {
        /* Every frame, with or without a tick, waits for its 1/FPS slot. A tick that falls due in
           between waits for the next slot too (at most one frame); it still counts from when it was
           due, so the game runs at the same speed. Pacing only the in-between frames gave a third,
           very short frame before most ticks (140+ frames per second at 120). */
        QueryPerformanceCounter(&q);
        if (g_hfrValid && q.QuadPart < g_hfrNext) return -1;
        now = timeGetTime();
        acc = *G_FRAMEACCUM + (int)(now - *G_FRAMETIME) * 10;
        *G_FRAMETIME = now;
        ticks = acc / g_tickDiv;
        acc %= g_tickDiv;
        *G_FRAMEACCUM = acc;
        alpha = acc * 65536 / g_tickDiv;
    }
    now = timeGetTime();
    if (ticks > 5) ticks = 5;
    if (cfg.log && now - g_hfrStatTime >= 5000) {
        if (g_hfrStatTime)
            log_msg("HighFrameRate: %d tick frames + %d in-between frames in %lu ms", g_hfrStatReal,
                    g_hfrStatMid, (unsigned long)(now - g_hfrStatTime));
        g_hfrStatTime = now;
        g_hfrStatReal = g_hfrStatMid = 0;
    }
    g_hfrAlpha = alpha;
    if (ticks > 0) {
        if (vs < 0) hfr_slot_used(q.QuadPart);
        g_hfrTicks = ticks;
        g_hfrStatReal++;
        return ticks;
    }
    if (!g_hfrValid || !*G_BFRAMEREADY || G_SPRITEQUEUE[0] != g_hfrNEntries) return -1;
    if (vs < 0) hfr_slot_used(q.QuadPart);
    hfr_apply();
    *G_HAS3DQUADS = g_hfrHas3D;     /* Game_Frame cleared it: the z-buffer is cleared for the quads again */
    g_hfrSeed = *(DWORD *)((BYTE *)((GetPtd_t)FN_GETPTD)() + 0x14);   /* the dissolve fade uses rand() */
    g_hfrReplay = 1;
    g_hfrStatMid++;
    return 0;
}

/* Game_Frame nests: its ticks handle the pause key, and Game_Pause / Game_Resume draw a frame by calling
   Game_Frame again. The inner call used to reset g_hfrTicks, so the outer frame was not recorded and
   the next in-between frame laid the inner frame's saved rects and D3D flags over a different queue (a
   plain sprite's surface taken for a texture handle: crash in D3DIM). The outer call's state is kept,
   the inner one draws no in-between frame, and the motion restarts from the outer frame. */
static int g_hfrDepth;

int hfr_game_frame(void)
{
    int r, ticks = g_hfrTicks;
    if (*G_LEVELPENDING && !*G_BFRAMEREADY) g_hfrValid = 0;     /* a level is set up first */
    if (g_hfrDepth) g_hfrValid = 0;
    g_hfrDepth++;
    g_hfrTicks = 0;
    r = game_frame_orig();
    if (g_hfrReplay) {
        *(DWORD *)((BYTE *)((GetPtd_t)FN_GETPTD)() + 0x14) = g_hfrSeed;
        g_hfrReplay = 0;
    }
    if (--g_hfrDepth) {
        g_hfrTicks = ticks;
        g_hfrValid = 0;
    }
    return r;
}

int hfr_init(void)                 /* 1 = Game_Frame's timing is ours */
{
    BYTE code[GF_TIMING_LEN];
    LARGE_INTEGER f;
    int p = 0;
    if (cfg.hfrFps <= g_tickHz) { log_msg("HighFrameRate: FPS=%d is not above the tick rate, off", cfg.hfrFps); return 0; }
    if (!game_timing_check("HighFrameRate")) return 0;
    if (!mh_hook(FN_ACTOR_DRAW, hkHfrActorDraw, (void **)&oHfrActorDraw, "Actor_Draw") ||
        !mh_hook(FN_TILELAYER_DRAW, hkHfrLayerDraw, (void **)&oHfrLayerDraw, "TileLayer_Draw") ||
        !mh_hook(FN_PLANES_UPDATE_DEPTH, hkHfrUpdateDepth, (void **)&oHfrUpdateDepth, "Planes_UpdateDepth") ||
        !mh_hook(FN_PLANES_DRAW_TILEMAPS, hkHfrDrawTilemaps, (void **)&oHfrDrawTilemaps, "Planes_DrawTilemaps") ||
        !mh_hook(FN_TEXT_DRAW_STRING, hkHfrTextDraw, (void **)&oHfrTextDraw, "Text_DrawString") ||
        !mh_hook(FN_SPRITE_QUEUE_SCREEN, hkHfrQueueScreen, (void **)&oHfrQueueScreen, "Sprite_QueueScreen") ||
        !mh_hook(FN_SPRITE_QUEUE_3DQUAD, hkHfrQueue3DQuad, (void **)&oHfrQueue3DQuad, "Sprite_Queue3DQuad"))
        return 0;                              /* recording needs hfr_begin_frame: the hooks stay idle */
    g_hfrVblank = cfg.vsync && vs_load_vblank();
    memset(code, 0xCC, sizeof(code));
    code[p++] = 0xE8;                                   /* call hfr_begin_frame */
    *(DWORD *)(code + p) = (DWORD)hfr_begin_frame - (GF_TIMING + p + 4); p += 4;
    code[p++] = 0x85; code[p++] = 0xC0;                 /* test eax, eax */
    code[p++] = 0x7D; code[p++] = 0x10;                 /* jge  +16 */
    code[p++] = 0xB8; *(DWORD *)(code + p) = 1; p += 4; /* mov  eax, 1 */
    code[p++] = 0x5D; code[p++] = 0x5F; code[p++] = 0x5E; code[p++] = 0x5B;   /* pop ebp, edi, esi, ebx */
    code[p++] = 0x81; code[p++] = 0xC4; *(DWORD *)(code + p) = 0x94; p += 4;  /* add esp, 0x94 */
    code[p++] = 0xC3;                                   /* ret: no frame (as before when no tick) */
    code[p++] = 0x8B; code[p++] = 0xF0;                 /* mov  esi, eax */
    code[p++] = 0x85; code[p++] = 0xF6;                 /* test esi, esi */
    code[p++] = 0x0F; code[p++] = 0x85;                 /* jnz  HFR_TICK_LOOP */
    *(DWORD *)(code + p) = HFR_TICK_LOOP - (GF_TIMING + p + 4); p += 4;
    code[p++] = 0xE9;                                   /* jmp  HFR_BLIT */
    *(DWORD *)(code + p) = HFR_BLIT - (GF_TIMING + p + 4); p += 4;
    patch_code(GF_TIMING, code, GF_TIMING_LEN);
    QueryPerformanceFrequency(&f);
    g_hfrPeriod = f.QuadPart / cfg.hfrFps;
    g_hvTickQ0 = f.QuadPart * g_tickDiv / 10000;        /* the original tick: 16.7 ms (20 at 50 Hz) */
    timeBeginPeriod(1);
    log_msg("HighFrameRate: %d frames per second, game logic at %d Hz%s", cfg.hfrFps, g_tickHz,
            g_hfrVblank ? ", frames on display refreshes (VSync)" : "");
    return 1;
}

void ws_layer_init(void)                       /* widescreen without FPS Override: its own layer hook */
{
    if (g_wsWidth && !oHfrLayerDraw)
        mh_hook(FN_TILELAYER_DRAW, hkWsLayerDraw, (void **)&oWsLayerDraw, "TileLayer_Draw");
}
