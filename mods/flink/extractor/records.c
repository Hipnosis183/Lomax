/* records.c - Flink animation records as Lomax records (lomax_record and the record
   helpers), and the types every map gets from MAIN.WLD: the spirits, the magic glass, the spells,
   PLATFORM's cloud, the stones, the landing dust and the chests (spirit_type ... chest_type). */
#include "convert.h"
#include <stdlib.h>
#include <string.h>

MainWorld g_main;
LomaxDigits g_digits;

static int is_sprite(const Piece *p) { return !(p->a & 0x80) && !(p->f & 0x80); }

/* a Flink record's frames: lists of its pieces (anim, frame, dx, dy, flags), attach points included */
int flink_frames(const Rec *r, int *starts)
{
    int n = 0, i, cur = 0;
    if (r->npc > 4094) die("record %.4s: %d pieces", r->tag, r->npc);
    starts[0] = 0;
    for (i = 0; i < r->npc; i++) {
        cur++;
        if (r->pc[i].fl & 0x80) { starts[++n] = i + 1; cur = 0; }
    }
    if (cur) starts[++n] = r->npc;
    return n;
}

static SKey piece_key(int a, int world) { return world ? sk(SK_MAIN, a, 0, 0) : SK_A(a); }

/* Flink pieces -> Lomax entries without the end-of-frame flag; attach points are left out.
   world: the ANIMs are another world's (Sprites key (world, n)). */
void lomax_pieces(const Piece *pc, int n, Sprites *s, int world, Ents *out)
{
    int i;
    for (i = 0; i < n; i++)
        if (is_sprite(&pc[i])) {
            Ent e;
            e.bank = sprites_bank(s, piece_key(pc[i].a, world));
            e.frame = pc[i].f + 1; e.dx = pc[i].dx; e.dy = pc[i].dy; e.fl = pc[i].fl & 0x18;
            vpush(*out, e);
        }
}

void end_frame(Ents *e, int from)
{
    if (e->n > from) e->v[e->n - 1].fl |= 0x80;
}

/* Flink animation records -> one Lomax record, a frame per Flink record (usually one): u32 FourCC
   (as an LE int), u32 0x1C, u16 entries, u16 speed, s16 hitbox[4], s16 attack box[4], 8-byte entries
   {u8 bank, u8 frame (from 1), s16 dx, s16 dy, u16 flags (0x80 last of a frame, 0x08/0x10 flips,
   as in Flink)}. The hitbox (the first record's) is a platform's standing surface:
   Platform_MoveAndCarryPlayer reads x1, y1, x2. The speed (animation step, 0x100 = a frame a tick)
   is the first record's: Flink's has the same scale (64 = a frame every 4 ticks).
   recs: (record, frame) pairs, no frame = all of the record's frames, n = only its n-th; a record
   NULL is a blank frame (a transparent pixel).
   mirror: flipped left-right, as Flink draws a type facing the other way (placement flags can't
   set a Lomax actor's facing): pieces flipped and moved to 1 - width - dx, hitbox x negated.
   double: the sprites repeated twice side by side (Sprites key ('x2', n)).
   repeat: every frame's pieces drawn again repeat px lower.
   edge: px added to each side of the hitbox (platforms; 0 for enemies, whose hitbox is Flink's).
   slots: keep the pieces' hide selector (flags & 7). Flink's Actor_DrawFrame (0x328C8) skips a
   piece with selector n when bit 7 + n of the actor's state word (+2) is set; Lomax's Actor_Draw
   skips one with slot n when bit n of the actor's +0x70 is set, and FLINK.DLL sets that (the
   Firemonster's attack face). Not for platforms, whose Lomax behaviours may set +0x70 themselves.
   hide: a mask of selectors whose pieces are left out.
   world: the ANIMs are another world's (Sprites key (world, n)), as in lomax_pieces. */
/* lomax_record: Flink records -> one Lomax record, a frame per Flink record frame. recs: (record,
   only) pairs, only = the n-th frame alone (-1 and out of range: the last); a record NULL
   is a blank frame. mirror: flipped left-right (pieces at 1 - width - dx, hitbox x negated). dbl:
   the sprites repeated twice side by side. repeat: each frame's pieces again repeat px lower. edge:
   px added to each side of the hitbox. slots: keep the pieces' hide selector (flags & 7). hide: a
   mask of selectors left out. world: MAIN.WLD's ANIMs. */
LRec lomax_record(const char *fourcc, const RecSel *recs, int nrecs, Sprites *s, int mirror, int dbl,
                  int repeat, int edge, int slots, int hide, int world)
{
    Ents ents = { 0 };
    LRec out;
    int r, i, speed, hb[4];
    for (r = 0; r < nrecs; r++) {
        const Rec *rec = recs[r].rec;
        Ents fr = { 0 };
        VEC(int) starts = { 0 };               /* frame k: fr.v[starts[k] .. starts[k + 1]) */
        int nf, k0, k1, k;
        if (!rec) {
            Ent e = { sprites_bank(s, sk(SK_BLANK, 0, 0, 0)), 1, 0, 0, 0x80 };
            vpush(ents, e);
            continue;
        }
        vpush(starts, 0);
        for (i = 0; i < rec->npc; i++) {
            const Piece *p = &rec->pc[i];
            int fl = p->fl;
            if (is_sprite(p)) {
                SKey key = world ? sk(SK_MAIN, p->a, 0, 0) : dbl ? sk(SK_X2, p->a, 0, 0) : SK_A(p->a);
                Ent e;
                e.bank = sprites_bank(s, key);
                e.dx = p->dx;
                if (mirror) { e.dx = 1 - sprites_width(s, key, p->f) - p->dx; fl ^= 0x08; }
                e.frame = p->f + 1; e.dy = p->dy;
                e.fl = fl & (slots ? 0x1F : 0x18);
                if (!((hide >> (fl & 7)) & 1) || !(fl & 7)) vpush(fr, e);
            }
            if (fl & 0x80) vpush(starts, fr.n);
        }
        if (starts.v[starts.n - 1] != fr.n) vpush(starts, fr.n);
        nf = starts.n - 1;
        if (!recs[r].hasOnly) { k0 = 0; k1 = nf; }
        else {
            int idx = recs[r].only < 0 ? nf + recs[r].only : recs[r].only;
            if (recs[r].only >= 0 && idx < nf) { k0 = idx; k1 = idx + 1; }
            else if (recs[r].only < 0 && idx >= 0 && idx < nf && recs[r].only != -1) { k0 = idx; k1 = idx + 1; }
            else { k0 = nf ? nf - 1 : 0; k1 = nf; }
        }
        for (k = k0; k < k1; k++) {
            int a = starts.v[k], b = starts.v[k + 1], from = ents.n, j;
            for (j = a; j < b; j++) vpush(ents, fr.v[j]);
            if (repeat) for (j = a; j < b; j++) { Ent e = fr.v[j]; e.dy += repeat; vpush(ents, e); }
            end_frame(&ents, from);
        }
        free(fr.v);
        free(starts.v);
    }
    if (recs[0].rec) {
        speed = recs[0].rec->speed;
        memcpy(hb, recs[0].rec->hb, sizeof(hb));
    } else {
        speed = 0x40;
        memset(hb, 0, sizeof(hb));
    }
    if (mirror) { int l = hb[0]; hb[0] = -hb[2]; hb[2] = -l; }
    /* Flink lands on a platform its body overlaps, Lomax only where his centre is (strictly inside
       x1..x2, Platform_MoveAndCarryPlayer): the standing surface is widened by edge px a side */
    hb[0] -= edge;
    hb[2] += edge;
    out = pack_record(fourcc, speed, hb, ents.v, ents.n);
    free(ents.v);
    return out;
}

LRec lomax_record1(const char *fourcc, const Rec *rec, Sprites *s, int edge, int world)
{
    RecSel r = { rec, 0, 0 };
    return lomax_record(fourcc, &r, 1, s, 0, 0, 0, edge, 0, 0, world);
}

static const Piece *first_sprite(const Rec *r)
{
    int i;
    for (i = 0; i < r->npc; i++) if (is_sprite(&r->pc[i])) return &r->pc[i];
    die("record %.4s has no sprite", r->tag);
    return NULL;
}

/* A chain link as Flink's chain draws place it (Sprite_DrawScreen 0x3430E, its RING sprite): the
   first hardware sprite's top-left 8 px up-left of the link's point, whatever the record's offsets. */
/* a chain link as Flink's chain draws place it (Sprite_DrawScreen 0x3430E): the first hardware
   sprite's top-left 8 px up-left of the link's point */
LRec ring_record(const FWorld *fw, const Rec *rec, Sprites *s)
{
    const Piece *p = first_sprite(rec);
    int xo, yo;
    Ent e;
    anim_piece_offset(fw->anims.v[p->a], p->f, &xo, &yo);
    e.bank = sprites_bank(s, SK_A(p->a)); e.frame = p->f + 1; e.dx = -8 - xo; e.dy = -8 - yo; e.fl = 0x80;
    return pack_record("RING", 0x40, rec->hb, &e, 1);
}

/* (dx, dy) of the first attach point with flags & 7 = hook in a Flink record */
void attach_point(const Rec *rec, int hook, int *dx, int *dy)
{
    int i;
    for (i = 0; i < rec->npc; i++) {
        const Piece *p = &rec->pc[i];
        if ((p->a & 0x80 || p->f & 0x80) && (p->fl & 7) == hook) { *dx = p->dx; *dy = p->dy; return; }
    }
    *dx = *dy = 0;
}

/* each frame's pieces (attach points left out), each frame ended */
static void all_frames(const Rec *rec, Sprites *s, int world, Ents *ents)
{
    int st[4096], n = flink_frames(rec, st), k;
    for (k = 0; k < n; k++) {
        int from = ents->n;
        lomax_pieces(rec->pc + st[k], st[k + 1] - st[k], s, world, ents);
        end_frame(ents, from);
    }
}

static const Rec *need(const Recs *rs, const char *tag, const char *what)
{
    const Rec *r = rec_get(rs, tag);
    if (!r) die("%s: no %s record", what, tag);
    return r;
}

/* Grabbing and throwing (FLINK.DLL grab.c). A light object thrown straight on becomes Flink's spinning
   shot (0x331B6: MAIN.WLD's Shot type, SPIN, or SMAL for a small one: +6 bit 1), so the types that can
   be thrown that way get those records, SMAL as 'SHSM' (a stone's own SMAL is its sprite). */
/* a light object thrown straight on becomes Flink's spinning shot (MAIN.WLD's Shot SPIN, or SMAL,
   as 'SHSM', for a small one) */
void shot_record(const char *tag, Sprites *s, LRec *out)
{
    const Rec *rec = need(g_main.shot, tag, "MAIN.WLD Shot");
    Ents ents = { 0 };
    all_frames(rec, s, 1, &ents);
    *out = pack_record(!strcmp(tag, "SMAL") ? "SHSM" : tag, rec->speed, rec->hb, ents.v, ents.n);
    free(ents.v);
}

/* Flink's Apple (big) and App2 (bigapple.c): the draw 0x245FE puts a rotated frame's top-left at the
   actor's point + (x, y) and, for the upside-down half, y + d7; phase 8 is ROLL itself flipped (Actor_Draw
   with +3 bits 3 and 4, its point moved by -d7, -d6 for it): 0x2463A for the Apple (big), 0x2461A App2.

   ROLL at 16 phases (one frame each, rolled_frames), and for App2 DEAD and the Shot's SPIN */
/* bigapple_records: ROLL at 16 phases (rolled_frames), placed as Flink's draw places them
   (ROLL_DRAW), and for App2 DEAD and the Shot's SPIN */
int bigapple_records(const char *name, const Recs *recs, Sprites *s, LRec *out)
{
    const Rec *roll = need(recs, "ROLL", name);
    const Piece *p = first_sprite(roll);
    Frames fr = flink_anim_frames(s->w->anims.v[p->a]);
    int w = fr.f[p->f].w, h = fr.f[p->f].h, x, y, d6, d7, b, k, n = 0;
    Ent e[16];
    if (!strcmp(name, "Apple (big)")) { x = -0x13; y = -0x26; d6 = 0x25; d7 = 0; }
    else if (!strcmp(name, "App2")) { x = -0xA; y = -0x16; d6 = 0x15; d7 = -2; }
    else { x = -0x18; y = -0x2F; d6 = 0x2E; d7 = 0; }
    b = sprites_bank(s, sk(SK_ROLLED, p->a, p->f, 0));
    for (k = 0; k < 16; k++) {
        e[k].bank = b; e[k].frame = k + 1; e[k].fl = 0x80;
        if (k == 0) { e[k].dx = p->dx; e[k].dy = p->dy; }
        else if (k < 8) { e[k].dx = x; e[k].dy = y; }
        else if (k == 8) { e[k].dx = 1 - w - p->dx - d7; e[k].dy = 1 - h - p->dy - d6; }
        else { e[k].dx = x; e[k].dy = y + d7; }
    }
    out[n++] = pack_record("ROLL", roll->speed, roll->hb, e, 16);
    if (!strcmp(name, "App2")) {
        out[n++] = lomax_record1("DEAD", need(recs, "DEAD", name), s, 0, 0);
        shot_record("SPIN", s, &out[n++]);
    }
    return n;
}

/* Flink's Snake, Snake (0x251BA, think 0x25254 / 0x2527E, draw 0x252D8, FOREST): see snake.c
   in FLINK.DLL. Its head and up to 11 body segments fly out of the background towards Flink, growing
   from 32% to 125% of their size: Flink scales its HEAD and BODY sprites once (0x25590) into 32 sprite
   lists and draws segment z with list 31 - z / 16 (0x2542A). Here those are 32 frames of a HEAD and a
   BODY record, frame k at SNAKE_SIZES[k], placed as Flink places them: centred on the unscaled
   sprite's middle (0x1DEEA), the head's left edge (half a face, mirrored on the left) at the
   segment's point and 5 px higher, the body 16 px left of it. BUB1 (the bubbles where it rises) and
   BUB2 (its splash in front) as they are. 'Spik Snake' (a Lomax decoration) with params height,
   length and delay.
 */
/* snake_records: MAIN blank, BUB1, BUB2, and HEAD / BODY at the 32 SNAKE_SIZES (scaled_frames),
   centred as Flink places them */
int snake_records(const Recs *recs, Sprites *s, LRec *out)
{
    static const char *const BUB[2] = { "BUB1", "BUB2" };
    static const char *const SEG[2] = { "HEAD", "BODY" };
    RecSel blank = { NULL, 0, 0 };
    int n = 0, i, k, st[4096];
    out[n++] = lomax_record("MAIN", &blank, 1, s, 0, 0, 0, 0, 0, 0, 0);
    for (i = 0; i < 2; i++) {
        const Rec *r = need(recs, BUB[i], "Snake");
        int nf = flink_frames(r, st);
        if (nf != 2) die("Snake %s: %d frames; FLINK.DLL expects 2", BUB[i], nf);
        out[n++] = lomax_record1(BUB[i], r, s, 0, 0);
    }
    for (i = 0; i < 2; i++) {
        const Rec *r = need(recs, SEG[i], "Snake");
        int mirror = i == 0, a = r->pc[0].a, f = r->pc[0].f, b, w, h;
        Frames fr = flink_anim_frames(s->w->anims.v[a]);
        Ent e[32];
        w = fr.f[f].w; h = fr.f[f].h;
        b = sprites_bank(s, sk(SK_SCALED, a, mirror, 0));
        for (k = 0; k < 32; k++) {
            int sz = 125 - 3 * k, sw = imax(1, w * sz / 100), sh = imax(1, h * sz / 100);
            e[k].bank = b; e[k].frame = k + 1; e[k].fl = 0x80;
            e[k].dx = mirror ? -sw : -16 + fdiv(w - sw, 2);
            e[k].dy = fdiv(h - sh, 2) - (mirror ? 5 : 0);
        }
        out[n++] = pack_record(SEG[i], r->speed, r->hb, e, 32);
    }
    return n;
}

/* ---- the types every map gets ---- */
static void make_type(BufVec *types, BankSet *banks, const char *name, int np, LRec *r, int n)
{
    int i;
    Buf t = { 0 };
    lomax_type(&t, name, np, 0, r, n);
    vpush(*types, t);
    for (i = 0; i < n; i++) { bank_union(banks, &r[i].banks); free_lrec(&r[i]); }
}

/* Spirits and the magic glass (README "Spirits"), in every map whether or not it has enemies: FLINK.DLL
   finds them by name (ObjType_FindByName), they are never placed.
   'Spik Spirit': MAIN.WLD's Spirit (0x2EDAA), SPIR and TAIL; a defeated enemy sets one free (spirits.c).
   'Spik Glass': the HUD's magic glass, MAIN.WLD's Display BOTT (ANIM 59 frame 1, its piece at -16, -32)
   at each fill level. Flink's Hud_UpdateMagic (0x34348) fills it from the bottom, g_MagicPower + 1 of
   its 32 rows (0x34372) taken from a full copy, made at 0x343E0 by OR-ing colour 0xA into the empty
   one's pixels under a 32 x 32 1-bit mask (at 0x407C8, 4 bytes a row, high bit first: 0x3445C). The
   mask's rows with bits (GLASS_LEVELS of them) give the visible levels: frame k fills the lowest k of
   them; FLINK.DLL picks the frame from the spirits held, 0..99 (spirits.c, GLASS_LEVELS there too). */
/* 'Spik Spirit': MAIN.WLD's Spirit SPIR and TAIL */
void spirit_type(Sprites *s, BufVec *types, BankSet *banks)
{
    LRec r[2];
    r[0] = lomax_record1("SPIR", need(g_main.spirit, "SPIR", "Spirit"), s, 0, 1);
    r[1] = lomax_record1("TAIL", need(g_main.spirit, "TAIL", "Spirit"), s, 0, 1);
    make_type(types, banks, "Spik Spirit", 0, r, 2);
}

/* 'Spik Glass': the HUD's magic glass at each fill level (glass_frames) */
void glass_type(Sprites *s, BufVec *types, BankSet *banks)
{
    const Rec *bott = need(g_main.glass, "BOTT", "Display");
    const Piece *p = NULL;
    Ent e[30];
    LRec r;
    int i, b, zero[4] = { 0, 0, 0, 0 };
    for (i = 0; i < bott->npc; i++) if (!(bott->pc[i].a & 0x80)) p = &bott->pc[i];
    b = sprites_bank(s, sk(SK_GLASS, 0, 0, 0));
    for (i = 0; i < 30; i++) { e[i].bank = b; e[i].frame = i + 1; e[i].dx = p->dx; e[i].dy = p->dy; e[i].fl = 0x80; }
    r = pack_record("BOTT", 0, zero, e, 30);
    make_type(types, banks, "Spik Glass", 0, &r, 1);
}

/* red_digits: Lomax's KLEI frames tinted red into palette 0's free entries (pal is changed) */
static Frames red_digits(unsigned *pal)
{
    int used[256] = { 0 }, n, k, i, nused = 0, remap[256];
    Frames out;
    for (k = 0; k < g_digits.frames.n; k++)
        for (i = 0; i < g_digits.frames.f[k].w * g_digits.frames.f[k].h; i++)
            if (g_digits.frames.f[k].px[i]) used[g_digits.frames.f[k].px[i]] = 1;
    for (n = FREE_COLOURS; n < WATER_TINT && pal[n]; n++) ;
    for (i = 0; i < 256; i++) nused += used[i];
    if (n + nused > WATER_TINT) die("the red digits: no free palette-0 entries");
    memset(remap, 0, sizeof(remap));
    for (i = 1, k = 0; i < 256; i++)
        if (used[i]) {
            unsigned c = g_digits.pal[i], r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
            unsigned t = (r + 4 < 31 ? r + 4 : 31) << 10 | (g / 2) << 5 | b / 2;
            remap[i] = n + k;
            pal[n + k] = t ? t : 1;
            k++;
        }
    out.n = g_digits.frames.n;
    out.f = xalloc((size_t)out.n * sizeof(Frame));
    for (k = 0; k < out.n; k++) {
        int w = g_digits.frames.f[k].w, h = g_digits.frames.f[k].h;
        out.f[k].w = w; out.f[k].h = h;
        out.f[k].px = xalloc((size_t)(w * h != 0 ? w * h : 1));
        for (i = 0; i < w * h; i++) out.f[k].px[i] = (u8)remap[g_digits.frames.f[k].px[i]];
    }
    return out;
}

#define SPELL_ICONS 12
#define BLAD_FRAMES 4
/* Flink's spells (README "Spells"): 'Spik Spells', in every map, never placed; FLINK.DLL finds it by
   name (spells.c) and draws the spell wheel and the HUD's chosen spell with it.
   - SPEL: frame n is spell n's icon (n = Flink's spell number, 0..SPELL_ICONS - 1), MAIN.WLD's
     'Available Ingredients' INGR frame 30 + n, the icon Flink's HUD shows for the chosen spell (0x33FFA:
     INGR, frame g_SelectedSpell + 0x1E) and its pause menu lists. Each is centred on the actor's point.
   - KLEI: Lomax's small HUD digits (his MAIN.WLD's Display KLEI, the coins' and enemies' digits) tinted red, the
     spell's cost flashing when it can't be paid. Their colours (Lomax's MAIN.WLD palette, 8 greys) are
     put into palette 0's free entries after the template sprites' (red_digits).
   - The spells cast anywhere (spellfx.c): LIGHTNING's bolts, MAIN.WLD's Lightning FLA1, FLA2 (its
     hitbox, below the bolt's point, is what it hits), FLA3; DUST DEVIL's Tornado DUST and its blades
     BLAD (BLAD_FRAMES frames of one piece, drawn by FLINK.DLL at each blade); DEMON's Demon FLY1, ATK1
     (small) and FLY2, ATK2 (grown).
   'Spik Cloud': PLATFORM's cloud, MAIN.WLD's Platform WOLK as its MAIN record (Lomax's standing surface
   is the MAIN record's hitbox, widened by PLATFORM_EDGE as the platforms'), WARN and PUFF. */
/* 'Spik Spells': SPEL the spell icons (INGR frames 30 + n, centred), KLEI Lomax's digits in red, and
   the spells' actors (Lightning FLA1-3, Tornado DUST BLAD, Demon FLY1 ATK1 FLY2 ATK2) */
void spells_type(Sprites *s, unsigned *pal, BufVec *types, BankSet *banks)
{
    const Rec *ingr = need(g_main.ingr, "INGR", "Available Ingredients"), *blad;
    static const char *const LT[] = { "FLA1", "FLA2", "FLA3" }, *const TT[] = { "DUST", "BLAD" },
                             *const DT[] = { "FLY1", "ATK1", "FLY2", "ATK2" };
    int st[4096], nf = flink_frames(ingr, st), k, i, red, zero[4] = { 0, 0, 0, 0 }, n = 0;
    Ent ents[SPELL_ICONS];
    Ents dig = { 0 };
    LRec r[2 + 3 + 2 + 4];
    if (nf < 30 + SPELL_ICONS) die("INGR has %d frames", nf);
    for (k = 0; k < SPELL_ICONS; k++) {
        const Piece *p = NULL;
        int cnt = 0, cx, cy;
        for (i = st[30 + k]; i < st[31 + k]; i++) if (is_sprite(&ingr->pc[i])) { p = &ingr->pc[i]; cnt++; }
        if (cnt != 1) die("INGR frame %d: %d sprites", 30 + k, cnt);
        sprites_centre(s, sk(SK_MAIN, p->a, 0, 0), p->f, p->fl, &cx, &cy);
        ents[k].bank = sprites_bank(s, sk(SK_MAIN, p->a, 0, 0));
        ents[k].frame = p->f + 1; ents[k].dx = -cx; ents[k].dy = -cy; ents[k].fl = (p->fl & 0x18) | 0x80;
    }
    red = sprites_add(s, sk(SK_ADDED, 1, 0, 0), red_digits(pal));
    for (i = 0; i < g_digits.nents; i++) { Ent e = g_digits.ents[i]; e.bank = red; vpush(dig, e); }
    r[n++] = pack_record("SPEL", 0, zero, ents, SPELL_ICONS);
    r[n++] = pack_record("KLEI", 0, zero, dig.v, dig.n);
    free(dig.v);
    blad = need(g_main.tornado, "BLAD", "Tornado");
    nf = flink_frames(blad, st);
    for (k = 0; k < nf; k++) {
        Ents e = { 0 };
        lomax_pieces(blad->pc + st[k], st[k + 1] - st[k], s, 1, &e);
        if (e.n != 1) die("Tornado BLAD: a frame of %d pieces; FLINK.DLL expects one", e.n);
        free(e.v);
    }
    if (nf != BLAD_FRAMES) die("Tornado BLAD: %d frames; FLINK.DLL expects %d", nf, BLAD_FRAMES);
    for (i = 0; i < 3; i++) r[n++] = lomax_record1(LT[i], need(g_main.lightning, LT[i], "Lightning"), s, 0, 1);
    for (i = 0; i < 2; i++) r[n++] = lomax_record1(TT[i], need(g_main.tornado, TT[i], "Tornado"), s, 0, 1);
    for (i = 0; i < 4; i++) r[n++] = lomax_record1(DT[i], need(g_main.demon, DT[i], "Demon"), s, 0, 1);
    make_type(types, banks, "Spik Spells", 0, r, n);
}

/* 'Spik Cloud': PLATFORM's cloud, MAIN.WLD's Platform WOLK (as MAIN), WARN and PUFF */
void cloud_type(Sprites *s, BufVec *types, BankSet *banks)
{
    LRec r[3];
    r[0] = lomax_record1("MAIN", need(g_main.platform, "WOLK", "Platform"), s, PLATFORM_EDGE, 1);
    r[1] = lomax_record1("WARN", need(g_main.platform, "WARN", "Platform"), s, PLATFORM_EDGE, 1);
    r[2] = lomax_record1("PUFF", need(g_main.platform, "PUFF", "Platform"), s, PLATFORM_EDGE, 1);
    make_type(types, banks, "Spik Cloud", 0, r, 3);
}

/* Flink's Stone (0x2E6DC; MAIN.WLD's Stones records, from 0x8646): a thrown object at rest that Lomax
   can pick up and throw (FLINK.DLL grab.c, stone_spawn). Checkbox 1 makes it a small stone (SMAL,
   light, comes back when thrown away), else a big one (BIGG, heavy). A 'Spik Stone' (Lomax's
   decoration, object 51, whose spawn FLINK.DLL wraps) with BIGG, SMAL and the small shot SHSM;
   param 0 = small. */
/* 'Spik Stone': MAIN.WLD's Stones BIGG and SMAL, and the small shot SHSM */
void stone_type(Sprites *s, BufVec *types, BankSet *banks)
{
    static const char *const T[2] = { "BIGG", "SMAL" };
    LRec r[3];
    int i;
    for (i = 0; i < 2; i++) {
        const Rec *rec = need(g_main.stones, T[i], "Stones");
        Ents e = { 0 };
        all_frames(rec, s, 1, &e);
        r[i] = pack_record(T[i], rec->speed, rec->hb, e.v, e.n);
        free(e.v);
    }
    shot_record("SMAL", s, &r[2]);
    make_type(types, banks, "Spik Stone", 1, r, 3);
}

/* Flink's landing dust (Fx_SpawnDust 0x33688, drawn by 0x336F0): 'Spik Dustcloud', in every map, never
   placed; FLINK.DLL (dustcloud.c) puts one at Lomax's feet when he lands. DUST is MAIN.WLD's Dustcloud
   smoke puff (8 frames, the descriptors at 0x84E4), which Flink draws as one hardware sprite whose
   top-left is the point it computes: one piece a frame at (0, 0), as the Firemonster's DUST. */
/* 'Spik Dustcloud': MAIN.WLD's Dustcloud DUST, one piece a frame at (0, 0) */
void dustcloud_type(Sprites *s, BufVec *types, BankSet *banks)
{
    const Rec *rec = need(g_main.dust, "DUST", "Dustcloud");
    int st[4096], nf = flink_frames(rec, st), k;
    Ents ents = { 0 };
    LRec r;
    for (k = 0; k < nf; k++) {
        Ents e = { 0 };
        int from = ents.n;
        lomax_pieces(rec->pc + st[k], st[k + 1] - st[k], s, 1, &e);
        if (e.n) { Ent x = e.v[0]; x.dx = x.dy = 0; vpush(ents, x); }
        end_frame(&ents, from);
        free(e.v);
    }
    r = pack_record("DUST", rec->speed, rec->hb, ents.v, ents.n);
    free(ents.v);
    make_type(types, banks, "Spik Dustcloud", 0, &r, 1);
}

/* 'Pots Chest' (params content, locked): CHES, OPEN, LOCK and ERDE (MAIN.WLD's Dustcloud clods) */
void chest_type(Sprites *s, BufVec *types, BankSet *banks)
{
    static const char *const T[4] = { "CHES", "OPEN", "LOCK", "ERDE" };
    LRec r[4];
    int i;
    for (i = 0; i < 4; i++) {
        const Rec *rec = need(i < 3 ? g_main.chest : g_main.dust, T[i], i < 3 ? "Chest" : "Dustcloud");
        Ents e = { 0 };
        all_frames(rec, s, 1, &e);
        r[i] = pack_record(T[i], rec->speed, rec->hb, e.v, e.n);
        free(e.v);
    }
    make_type(types, banks, "Pots Chest", 2, r, 4);
}
