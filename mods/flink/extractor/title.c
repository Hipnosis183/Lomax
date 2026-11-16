/* title.c - Flink's title screen as TITLE.WLD (README "Title screen").

   Flink's title is built into FLINK.PRG: world 6 at 0x40E3C, its map 2 over map 1. Three planes as a
   level's (priority tiles, play map, background), with Lomax's title objects (WORLDS\TITLE.WLD:
   TITLE_KEEP, so its control, PRESS BUTTON TO START and copyright), a Music trigger (Lomax's title
   track), a Start that only marks the play plane (never spawned: 0x2000), and Flink's logo as 'Spik
   Logo' (MAIN.WLD's Logo record), which FLINK.DLL (title.c) drops with Lomax hanging from it. The
   Logo (init 0x2C748, think 0x2C8C0, draw 0x2CA18): x = camera + 0xA0, y from -50 down to the map's
   height - 0x87; the hero hangs at (x - 0x49, y + 0x3C), and the camera keeps him 0x96 px down. */
#include "convert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TITLE_WORLD 0x40E3C
#define TITLE_MAP 2
#define TITLE_BG 1
#define TITLE_MUSIC 20
#define LOGO_SCREEN_X 0xA0
#define LOGO_START_Y (-50)
#define LOGO_STOP 0x87
#define LOGO_HANG_X (-0x49)
#define LOGO_HANG_Y 0x3C
#define LOGO_HERO_Y 0x96

static unsigned le16(const u8 *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }

/* Flink's title is built into FLINK.PRG: world 6 at 0x40E3C (system
   types only, Flink's decompilation.md 3.7), its map 2 over map 1 (the entry before
   LevelSequenceTable, 0x44168 = {6, 2, 1}, loaded for the title / attract mode). Map 2 (704 x 352)
   places a Logo (MAIN.WLD type 102, init 0x2C748), the License line, and Forest Dummies, an
   Ingredient Holder and a Thief that only Flink's title demo walks to (it plays recorded input,
   Demo_StartPlayback 0x22AAE, from the Logo's init); Flink is the Hero.
   The Logo (init 0x2C748, think 0x2C8C0, draw 0x2CA18): x = camera + 0xA0, y from -50 down 1 px a
   frame to the map's height - 0x87; the hero hangs from it, held at (x - 0x49, y + 0x3C) with
   $828E bits 4 and 5 (the camera's dead zone is 1 px then, 0x1A064), and drops when it stops; the
   camera keeps him 0x96 px down the screen ($872E -> $872A, 0x19E08), so the screen follows him down
   from the map's top as the logo falls (camera y: Flink's 0x1A040, toward hero y - 0x96 at a speed
   of distance / 16, changed by at most 0.25 px a frame (0.75 when turning), inside 0..height - 224).
   After 6000 frames it plays the next attract demo; a button starts the game, and the logo flies up
   6 px a frame (0x2CA0C).

   Flink's title screen as a Lomax world (out_dir/TITLE.WLD) for the title's sub-level 1 ->
   LEVELS.INI lines ([Title]). Three planes as convert's (priority tiles, play map, background), with
   Lomax's title objects from tpl_path (WORLDS/TITLE.WLD: TITLE_KEEP, so its control, PRESS BUTTON
   TO START and copyright), a Music trigger (Lomax's title track), a Start that only marks the play
   plane (never spawned: 0x2000, as Lomax's title has), and Flink's logo as 'Spik Logo' (MAIN.WLD's
   Logo record), which FLINK.DLL (title.c) drops with Lomax hanging from it. Flink's enemies, License
   and title demo are left out. */
void title(const char *outDir, Buf *ini)
{
    static const char *const KEEP[3] = { "TitleControl", "Message", "Copyright" };
    static int usedP[8192], usedB[8192];
    FWorld tw;
    const FMap *play, *bg;
    int w8, h8, bw8, bh8, mw, mh, i, k, nmy_end, T_SCROLL = -1, T_START = -1, T_MUSIC = -1, t_logo, stop_y, hx, floor, g;
    unsigned pal[256];
    Ctx ctx;
    u8 *tplData, animbits[32];
    size_t tplLen;
    LChunks tpl;
    const LChunk *tn, *th, *tg;
    LTypes types = { 0 };
    Objs kept = { 0 }, objs = { 0 }, sfg = { 0 }, sbg = { 0 };
    Sprites sprites;
    LRec logo;
    Buf nmy = { 0 }, out = { 0 }, ttab_fg = { 0 }, ttab_pl = { 0 }, ttab_bg = { 0 }, map_fg = { 0 }, map_pl = { 0 }, map_bg = { 0 };
    BufVec tpl_anims = { 0 };
    Cells cells;
    IntVec meta_fg = { 0 }, meta_pl = { 0 }, meta_bg = { 0 };
    int scrollFg[7] = { 0, 100, 0, 100, 0, 0, 0 }, scrollBg[7] = { 0, 50, 0, 0, 0, 0, 0 }, span, nbw;
    char path[MAX_PATH];

    flink_world(&tw, g_flinkPrg + TITLE_WORLD - PRG_LOAD, g_flinkPrgSize - (TITLE_WORLD - PRG_LOAD));
    if (tw.maps.n < 2) die("FLINK.PRG: the title world has %d maps", tw.maps.n);
    play = &tw.maps.v[TITLE_MAP - 1];
    bg = &tw.maps.v[TITLE_BG - 1];
    w8 = (int)be32(play->head.p); h8 = (int)be32(play->head.p + 4);
    bw8 = (int)be32(bg->head.p); bh8 = (int)be32(bg->head.p + 4);
    for (i = 0; i < 256; i++) pal[i] = 0;
    for (i = 0; i < 64; i++) pal[i] = vdp_to_15(be16(play->head.p + 8 + 2 * i));
    pal[BACKDROP] = pal[0];
    pal[0] = 0;
    mw = w8 / 2; mh = h8 / 2;
    memset(&ctx, 0, sizeof(ctx));
    ctx.w = mw; ctx.h = mh;
    ctx.words = xalloc((size_t)(mw * mh) * sizeof(unsigned));
    for (i = 0; i < mw * mh; i++) ctx.words[i] = be16(play->map.p + 2 * i);

    tplData = read_game_world("TITLE.WLD", &tplLen);
    tpl = lomax_chunks(tplData, tplLen);
    tn = lchunk(&tpl, "NMY "); th = lchunk(&tpl, "HEAD"); tg = lchunk(&tpl, "GNMY");
    if (!tn || !th || !tg) die("WORLDS\\TITLE.WLD: no NMY, HEAD or GNMY");
    nmy_end = lomax_types(tn->p.p, tn->p.n, &types);
    for (i = 0; i < types.n; i++) {
        if (T_SCROLL < 0 && !strcmp(types.v[i].name, "ScrollMap") && types.v[i].nparams == 7) T_SCROLL = i;
        if (T_START < 0 && !strcmp(types.v[i].name, "Start")) T_START = i;
        if (T_MUSIC < 0 && !strcmp(types.v[i].name, "Music")) T_MUSIC = i;
    }
    if (T_SCROLL < 0 || T_START < 0 || T_MUSIC < 0) die("WORLDS\\TITLE.WLD: no ScrollMap, Start or Music type");
    {                                                       /* (the first plane's: the title's play plane) */
        size_t q = 0;
        const u8 *gp = tg->p.p;
        while (q + 2 <= tg->p.n) {
            unsigned size = le16(gp + q), t;
            int p[24], np = 0;
            if (!size) break;
            t = le16(gp + q + 2);
            for (i = 0; i < ((int)size - 10) / 2 && np < 24; i++) p[np++] = (int)le16(gp + q + 10 + 2 * i);
            for (k = 0; k < 3; k++)
                if ((int)t < types.n && !strcmp(types.v[t].name, KEEP[k])) {
                    Obj o = obj((int)t, (short)le16(gp + q + 4), (short)le16(gp + q + 6), (int)le16(gp + q + 8) | 0x4000, p, np, NULL);
                    vpush(kept, o);                       /* (all at the start) */
                }
            q += size;
        }
    }

    memset(&sprites, 0, sizeof(sprites));
    sprites.w = &tw;
    {
        const Rec *r = rec_get(g_main.logo, "LOGO");
        if (!r) die("MAIN.WLD: no Logo LOGO");
        logo = lomax_record1("LOGO", r, &sprites, 0, 1);
    }
    t_logo = types.n;
    buf_add(&nmy, tn->p.p, (size_t)nmy_end);
    lomax_type(&nmy, "Spik Logo", 0, 0, &logo, 1);
    buf_add(&nmy, NULL, 2);
    stop_y = h8 * 8 - LOGO_STOP;
    hx = LOGO_SCREEN_X + LOGO_HANG_X;
    floor = ground_at(&ctx, hx, stop_y + LOGO_HANG_Y, &g) && g ? g : h8 * 8 - 32;
    {
        int mu = TITLE_MUSIC;
        Obj o;
        o = obj(T_START, hx, floor, 0x2000, NULL, 0, NULL); vpush(objs, o);
        o = obj(T_MUSIC, hx, floor, 0x4000, &mu, 1, NULL); vpush(objs, o);
        o = obj(t_logo, LOGO_SCREEN_X, stop_y, 0x4000, NULL, 0, NULL); vpush(objs, o);
        for (i = 0; i < kept.n; i++) vpush(objs, kept.v[i]);
    }
    memset(animbits, 0, sizeof(animbits));
    for (i = 0; i < kept.n; i++) for (k = 0; k < 32; k++) animbits[k] |= types.v[kept.v[i].t].banks.b[k];
    for (k = 0; k < 32; k++) animbits[k] |= logo.banks.b[k];
    for (i = 0; i < tpl.n; i++) {
        int b;
        if (strcmp(tpl.v[i].tag, "ANIM")) continue;
        b = (int)le16(tpl.v[i].p.p + 8) + 1;
        if (b < 256 && animbits[b >> 3] & (0x80 >> (b & 7))) { Buf a = { 0 }; buf_add(&a, tpl.v[i].p.p, tpl.v[i].p.n); vpush(tpl_anims, a); }
    }
    template_sprite_colours(&tpl_anims, th->p, pal);

    cells_init(&cells, tw.cells);
    head_used_cells(play->head, usedP);
    head_used_cells(bg->head, usedB);
    convert_layer(play, usedP, &cells, keep_fg, 0, 0, &ttab_fg, &meta_fg);
    convert_layer(play, usedP, &cells, keep_pl, 0, 0, &ttab_pl, &meta_pl);
    convert_layer(bg, usedB, &cells, keep_all, 0, 1, &ttab_bg, &meta_bg);
    for (i = 0; i < meta_bg.n; i++) if (!meta_bg.v[i]) meta_bg.v[i] = meta_bg.v[0];
    for (i = 0; i < mw * mh; i++) {
        u8 b2[2];
        int a = meta_fg.v[ctx.words[i] & 0x7FF], b = meta_pl.v[ctx.words[i] & 0x7FF];
        b2[0] = (u8)(a >> 8); b2[1] = (u8)a; buf_add(&map_fg, b2, 2);
        b2[0] = (u8)(b >> 8); b2[1] = (u8)b; buf_add(&map_pl, b2, 2);
    }
    buf_add(&map_fg, NULL, (size_t)(mw * mh));
    for (i = 0; i < mw * mh; i++) { u8 c = (u8)((ctx.words[i] >> 8) & 0xF8); buf_add(&map_pl, &c, 1); }
    nbw = bw8 / 2 * (bh8 / 2);
    for (i = 0; i < nbw; i++) { u8 b2[2]; int a = meta_bg.v[be16(bg->map.p + 2 * i) & 0x7FF]; b2[0] = (u8)(a >> 8); b2[1] = (u8)a; buf_add(&map_bg, b2, 2); }
    buf_add(&map_bg, NULL, (size_t)nbw);
    span = h8 * 8 - 240;                                    /* (as convert's) */
    scrollBg[3] = span > 0 ? imax(0, imin(100, fdiv((bh8 * 8 - 240) * 100, span))) : 0;
    { Obj o = obj(T_SCROLL, 16, 16, 0x4000, scrollFg, 7, NULL); vpush(sfg, o); }
    { Obj o = obj(T_SCROLL, 16, 16, 0x4000, scrollBg, 7, NULL); vpush(sbg, o); }

    { u8 w[8] = { 'P', 'S', '-', 'X', 0xD2, 0x04, 0, 0 }; chunk(&out, "WRLD", w, 8); }
    chunk(&out, "NMY ", nmy.p, nmy.n);
    build_head(&out, w8, h8, pal, th->p, animbits);
    chunk(&out, "TTAB", ttab_fg.p, ttab_fg.n); chunk(&out, "MAP ", map_fg.p, map_fg.n); gnmy(&out, &sfg);
    build_head(&out, w8, h8, pal, th->p, animbits);
    chunk(&out, "TTAB", ttab_pl.p, ttab_pl.n); chunk(&out, "MAP ", map_pl.p, map_pl.n); gnmy(&out, &objs);
    build_head(&out, bw8, bh8, pal, th->p, animbits);
    chunk(&out, "TTAB", ttab_bg.p, ttab_bg.n); chunk(&out, "MAP ", map_bg.p, map_bg.n); gnmy(&out, &sbg);
    chunk(&out, "CELL", cells.data.p, cells.data.n);
    for (i = 0; i < tpl_anims.n; i++) chunk(&out, "ANIM", tpl_anims.v[i].p, tpl_anims.v[i].n);
    sprites_chunks(&sprites, &out);
    chunk(&out, "END ", NULL, 0);
    snprintf(path, sizeof(path), "%s\\TITLE.WLD", outDir);
    write_file(path, out.p, out.n);
    printf("  title TITLE.WLD: map %d over map %d, %dx%d px, %d cells, logo stops at y %d, Lomax lands at (%d,%d), %d bytes\n",
           TITLE_MAP, TITLE_BG, w8 * 8, h8 * 8, cells.n, stop_y, hx, floor, (int)out.n);
    ini_add(ini, "; Flink's title screen for the title's sub-level 1 (FLINK.DLL title.c): Logo = x on the screen,");
    ini_add(ini, "; first y, last y (the map's); Hang = Lomax's feet from the logo while he hangs from it;");
    ini_add(ini, "; HeroY = his height on the screen that the camera keeps");
    ini_add(ini, "[Title]");
    ini_add(ini, "File=TITLE.WLD");
    ini_add(ini, "Logo=%d,%d,%d", LOGO_SCREEN_X, LOGO_START_Y, stop_y);
    ini_add(ini, "Hang=%d,%d", LOGO_HANG_X, LOGO_HANG_Y);
    ini_add(ini, "HeroY=%d", LOGO_HERO_Y);
    free_lrec(&logo);
    buf_free(&nmy); buf_free(&out); buf_free(&ttab_fg); buf_free(&ttab_pl); buf_free(&ttab_bg);
    buf_free(&map_fg); buf_free(&map_pl); buf_free(&map_bg);
    for (i = 0; i < tpl_anims.n; i++) buf_free(&tpl_anims.v[i]);
    free(tpl_anims.v); free(kept.v); free(objs.v); free(sfg.v); free(sbg.v); free(ctx.words); free(tplData);
    free(meta_fg.v); free(meta_pl.v); free(meta_bg.v);
}
