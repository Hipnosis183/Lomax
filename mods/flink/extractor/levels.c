/* levels.c - the converter's inputs and its run over Flink's levels (the 'all' run,
   flink_main_levels, side_rooms, level_exits; their comments there explain Flink's world map path,
   the side rooms and the play order, as README "Levels").

   Flink's files come from the disc image (disc.c): FLINK.PRG and the world files NAME.WLD. Lomax's
   come from the game folder: WORLDS\MEDIEVAL.WLD (the template: object types, their sprites, palettes
   1-9) and WORLDS\MAIN.WLD (the HUD's small digits). */
#include "convert.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LFile g_tpl;
static Disc *g_disc;

static struct { char name[16]; FWorld w; u8 *data; } g_worlds[16];
static int g_nworlds;

FWorld *world_of(const char *name)
{
    char file[32];
    DWORD size;
    int i;
    for (i = 0; i < g_nworlds; i++) if (!strcmp(g_worlds[i].name, name)) return &g_worlds[i].w;
    if (g_nworlds == 16) die("too many worlds");
    snprintf(file, sizeof(file), "%s.WLD", name);
    g_worlds[g_nworlds].data = disc_read_file(g_disc, file, &size);
    if (!g_worlds[g_nworlds].data) die("%s: %s can't be read", g_disc->cue, file);
    snprintf(g_worlds[g_nworlds].name, sizeof(g_worlds[0].name), "%s", name);
    flink_world(&g_worlds[g_nworlds].w, g_worlds[g_nworlds].data, size);
    return &g_worlds[g_nworlds++].w;
}

/* each map's placements, made once, so that a placement found on one pass is the same one later */
static struct { const FWorld *w; int m; Places pl; } g_places[128];
static int g_nplaces;

Places *places_of(FWorld *w, int m)
{
    int i;
    for (i = 0; i < g_nplaces; i++) if (g_places[i].w == w && g_places[i].m == m) return &g_places[i].pl;
    if (g_nplaces == 128) die("too many maps");
    if (m < 1 || m > w->maps.n) die("no map %d", m);
    g_places[g_nplaces].w = w;
    g_places[g_nplaces].m = m;
    g_places[g_nplaces].pl = flink_placements(w, &w->maps.v[m - 1]);
    return &g_places[g_nplaces++].pl;
}

static char g_gameDir[MAX_PATH];

u8 *read_game_world(const char *name, size_t *n)
{
    return read_game_file(g_gameDir, name, n);
}

u8 *read_game_file(const char *dir, const char *name, size_t *n)
{
    char path[MAX_PATH];
    HANDLE f;
    DWORD size, got;
    u8 *d;
    snprintf(path, sizeof(path), "%s\\WORLDS\\%s", dir, name);
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        die("Lomax's %s was not found.\n\nFLINK.EXE goes into the game's MODS\\FLINK folder: it reads the game's "
            "own worlds from WORLDS two folders up.", path);
    size = GetFileSize(f, NULL);
    d = xalloc(size);
    if (!ReadFile(f, d, size, &got, NULL) || got != size) die("can't read %s", path);
    CloseHandle(f);
    *n = size;
    return d;
}

static Recs *main_recs(const FWorld *w, const char *name)
{
    int i;
    for (i = 0; i < w->nmy.n; i++) if (!strcmp(w->nmy.v[i], name)) return &w->recs.v[i];
    die("MAIN.WLD has no '%s' type", name);
    return NULL;
}

static unsigned le16(const u8 *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }

/* lomax_digits: Lomax's MAIN.WLD -> the Display type's KLEI record's entries and its ANIM's frames */
static void lomax_digits(const u8 *d, size_t n)
{
    LChunks ch = lomax_chunks(d, n);
    const LChunk *nc = lchunk(&ch, "NMY "), *hc = lchunk(&ch, "HEAD");
    const u8 *nmy;
    size_t q = 0;
    int i, bank, found = 0;
    if (!nc || !hc) die("Lomax's MAIN.WLD: no NMY or HEAD");
    nmy = nc->p.p;
    while (!found) {
        unsigned ln = le16(nmy + q), nanim, k;
        int display;
        if (!ln) die("Lomax's MAIN.WLD: no Display type");
        q += 2;
        display = ln >= 7 && !memcmp(nmy + q, "Display", 7) && (ln == 7 || !nmy[q + 7]);
        q += ln;
        nanim = le16(nmy + q + 2);
        q += 6;
        for (k = 0; k < nanim; k++) {
            unsigned ne = le16(nmy + q + 8), j;
            if (display && !memcmp(nmy + q, "IELK", 4)) {         /* 'KLEI' as an LE int */
                free(g_digits.ents);
                g_digits.ents = xalloc((ne ? ne : 1) * sizeof(Ent));
                g_digits.nents = (int)ne;
                for (j = 0; j < ne; j++) {
                    const u8 *e = nmy + q + 28 + j * 8;
                    g_digits.ents[j].bank = e[0]; g_digits.ents[j].frame = e[1];
                    g_digits.ents[j].dx = (short)le16(e + 2); g_digits.ents[j].dy = (short)le16(e + 4); g_digits.ents[j].fl = (int)le16(e + 6);
                }
                found = 1;
            }
            q += 28 + ne * 8;
        }
    }
    bank = g_digits.ents[0].bank;
    for (i = 0; i < g_digits.nents; i++) if (g_digits.ents[i].bank != bank) die("KLEI: more than one bank");
    for (i = 0; i < ch.n; i++) {
        const u8 *a = ch.v[i].p.p;
        int nf, k;
        size_t o = 12;
        if (strcmp(ch.v[i].tag, "ANIM") || (int)le16(a + 8) + 1 != bank) continue;
        nf = (int)le16(a + 4);
        g_digits.frames.n = nf;
        g_digits.frames.f = xalloc((size_t)(nf ? nf : 1) * sizeof(Frame));
        for (k = 0; k < nf; k++) {
            int w = (short)le16(a + o), h = (short)le16(a + o + 2);
            g_digits.frames.f[k].w = w; g_digits.frames.f[k].h = h;
            g_digits.frames.f[k].px = xalloc((size_t)(w * h != 0 ? w * h : 1));
            memcpy(g_digits.frames.f[k].px, a + o + 8, (size_t)(w * h));
            o += 8 + (size_t)(w * h) + ((4 - (size_t)(w * h) % 4) % 4);
        }
        break;
    }
    if (i == ch.n) die("KLEI: no ANIM for bank %d", bank);
    for (i = 0; i < 256; i++) g_digits.pal[i] = le16(hc->p.p + 8 + 2 * i);
}

void converter_init(Disc *d, const char *gameDir)
{
    FWorld *mw;
    size_t n;
    u8 *main;
    g_disc = d;
    snprintf(g_gameDir, sizeof(g_gameDir), "%s", gameDir);
    g_tpl.data = read_game_file(gameDir, "MEDIEVAL.WLD", &g_tpl.n);
    g_tpl.ch = lomax_chunks(g_tpl.data, g_tpl.n);
    main = read_game_file(gameDir, "MAIN.WLD", &n);
    lomax_digits(main, n);
    mw = world_of("MAIN");
    g_main.w = *mw;
    g_main.shot = main_recs(mw, "Shot");
    g_main.chest = main_recs(mw, "Chest");
    g_main.dust = main_recs(mw, "Dustcloud");
    g_main.stones = main_recs(mw, "Stones");
    g_main.spirit = main_recs(mw, "Spirit");
    g_main.glass = main_recs(mw, "Display");
    g_main.ingr = main_recs(mw, "Available Ingredients");
    g_main.lightning = main_recs(mw, "Lightning");
    g_main.tornado = main_recs(mw, "Tornado");
    g_main.platform = main_recs(mw, "Platform");
    g_main.demon = main_recs(mw, "Demon");
    g_main.logo = main_recs(mw, "Logo");
    g_main.holder = main_recs(mw, "Ingredient Holder");
    g_main.thief = main_recs(mw, "Thief");
}

/* ---- the levels ---- */
typedef IntVec Chain;

static int in_list(const int *v, int n, int x)
{
    int i;
    for (i = 0; i < n; i++) if (v[i] == x) return 1;
    return 0;
}

static const Place *first_named(const Places *pl, const char *name)
{
    int i;
    for (i = 0; i < pl->n; i++) if (!strcmp(pl->v[i].name, name)) return &pl->v[i];
    return NULL;
}

Places *map_places(int n, const char **world, int *m, int *bg)
{
    level_maps(n, world, m, bg);
    return places_of(world_of(*world), *m);
}

/* Flink's world map (MAIN.WLD map 3) has 32 'Map ' points: params point-no., level-entry (a
   LevelSequenceTable index), the UP/DOWN/LEFT/RIGHT neighbours; checkbox 1 = the start point,
   where the game begins, which has no level. The other 31 are its main levels. A main level is a
   chain of maps: an 'Exit point' (params level = a LevelSequenceTable index, entry = the revive point
   there with that entry number; checkboxes 'move left', 'look up', 'fade music') leads to the next
   map, a 'Warp' ends the level. Doors ('look up') off a map that has a Warp lead to side rooms that
   come back to the same map (FOREST maps 9 and 20, VILLAGE map 12, MOUNTAIN map 13); they are not on
   the main path but are added to the level after it (side_rooms).

   -> [[sequence numbers (1-62) of the maps on each main level's path]] in LevelSequenceTable
   order, and {sequence number: the Exit point leading on, or the Warp / nothing ending the level}.

   Flink's order is its world map's path, not the sequence table's: from the START point it walks
   to the one unvisited neighbour each point opens (UP/DOWN/LEFT/RIGHT, params 2-5; a negative one
   is a path opened later). That puts point 30 (FOREST map 22) before point 7 (FOREST maps 16-17, the
   boss), whose sequence entries are the other way round. The levels on that walk take the slots
   of the sorted list in walk order; those off it (the secret levels, the paths opened later)
   keep theirs, so the secret levels stay 7 and 17. */
/* flink_main_levels: the maps on each main level's path, in Flink's world map order, and each map's
   end: the Exit point leading on, or the Warp (or nothing) ending the level */
static int flink_main_levels(Chain *levels, const Place **ends)
{
    FWorld *main = world_of("MAIN");
    Places *pts = places_of(main, 3);
    VEC(const Place *) mp = { 0 };
    IntVec starts = { 0 }, walk = { 0 }, onWalk = { 0 };
    const Place *cur = NULL;
    int i, k, nlev = 0, slot = 0;
    for (i = 0; i < pts->n; i++) if (!strcmp(pts->v[i].name, "Map ")) vpush(mp, &pts->v[i]);
    for (i = 0; i < mp.n; i++) if (!(mp.v[i]->check & 1)) vpush(starts, prm(mp.v[i], 1, 0) + 1);
    for (i = 0; i < starts.n; i++)
        for (k = i + 1; k < starts.n; k++)
            if (starts.v[k] < starts.v[i]) { int x = starts.v[i]; starts.v[i] = starts.v[k]; starts.v[k] = x; }
#define POINT(num, out) do { int _j; (out) = NULL; for (_j = 0; _j < mp.n; _j++) if (prm(mp.v[_j], 0, 0) == (num)) (out) = mp.v[_j]; } while (0)
    for (i = 0; i < mp.n; i++) if (mp.v[i]->check & 1) { cur = mp.v[i]; break; }
    while (cur) {
        const Place *nx = NULL;
        vpush(walk, prm(cur, 0, 0));
        for (k = 2; k < 6 && k < cur->np && !nx; k++) {
            int v = s16v(cur->params[k]);
            const Place *pt;
            POINT(v, pt);
            if (v > 0 && pt && !in_list(walk.v, walk.n, v)) nx = pt;
        }
        cur = nx;
    }
    for (i = 0; i < walk.n; i++) {
        const Place *pt;
        POINT(walk.v[i], pt);
        if (!(pt->check & 1)) vpush(onWalk, prm(pt, 1, 0) + 1);
    }
    for (i = 0; i < starts.n; i++) if (in_list(onWalk.v, onWalk.n, starts.v[i])) starts.v[i] = onWalk.v[slot++];
#undef POINT
    for (i = 0; i < starts.n; i++) {
        Chain chain = { 0 };
        int n = starts.v[i];
        while (n && !in_list(chain.v, chain.n, n)) {
            const char *world;
            int m, bg;
            Places *pl;
            const Place *warp, *ex = NULL, *firstFwd = NULL;
            vpush(chain, n);
            pl = map_places(n, &world, &m, &bg);
            warp = first_named(pl, "Warp");
            for (k = 0; k < pl->n; k++) {
                const Place *p = &pl->v[k];
                int t = prm(p, 0, 0) + 1;
                if (strcmp(p->name, "Exit point") || in_list(chain.v, chain.n, t) || in_list(starts.v, starts.n, t) || t < 1 || t > 62) continue;
                if (!firstFwd) firstFwd = p;
                if (!(p->check & 2)) { ex = p; break; }
            }
            if (!ex && !warp) ex = firstFwd;
            ends[n] = ex ? ex : warp;
            n = ex ? prm(ex, 0, 0) + 1 : 0;
        }
        levels[nlev++] = chain;
    }
    free(mp.v); free(starts.v); free(walk.v); free(onWalk.v);
    return nlev;
}

/* -> {level index: [the maps reached only through its maps' exits, in the order found]}: Flink's
   side rooms (FOREST maps 9 and 20, VILLAGE map 12, MOUNTAIN map 13), entered through doors and
   left through doors back into the level (FOREST map 20 also ends level 3 at its own Warp). FLINK.DLL
   plays them as the level's last sub-levels. */
/* side_rooms: the maps reached only through a level's maps' exits, in the order found */
static void side_rooms(const Chain *levels, int nlev, Chain *rooms)
{
    IntVec main = { 0 };
    int k, i;
    for (k = 0; k < nlev; k++) for (i = 0; i < levels[k].n; i++) vpush(main, levels[k].v[i]);
    for (k = 0; k < nlev; k++) {
        IntVec todo = { 0 };
        int head = 0;
        memset(&rooms[k], 0, sizeof(rooms[k]));
        for (i = 0; i < levels[k].n; i++) vpush(todo, levels[k].v[i]);
        while (head < todo.n) {
            const char *world;
            int m, bg;
            Places *pl = map_places(todo.v[head++], &world, &m, &bg);
            for (i = 0; i < pl->n; i++) {
                const Place *p = &pl->v[i];
                int t = !strcmp(p->name, "Exit point") && p->np ? p->params[0] + 1 : 0;
                if (t >= 1 && t <= 62 && !in_list(main.v, main.n, t) && !in_list(rooms[k].v, rooms[k].n, t)) {
                    vpush(rooms[k], t);
                    vpush(todo, t);
                }
            }
        }
        free(todo.v);
    }
    free(main.v);
}


/* ---- LEVELS.INI ---- */

/* Flink's messages (HelpTextTable 0x18250: a pointer per language, English, French, German, Spanish,
   Dutch, to messages ended by 0xFF): lines of u8 column, u8 row, the text, 0. Lomax's hint box is 24
   characters wide and draws lines 8 px apart in a frame of 16 px rows, so the rows become lines (the
   gaps blank lines, the columns indents), at most 20, made even, of up to 26 characters (hint_lines).
   Lomax's languages: 0 English, 1 French, 2 German, 3 Italian (none in Flink: English), 4 Spanish;
   n.L in [Hints] is Flink's message n in Lomax language L (HINT_LANGUAGES: 0, 1, 2, 4 from Flink's 0,
   1, 2, 3), its lines separated by '|' (FLINK.DLL falls back to n.0). */
static const int HINT_LL[4] = { 0, 1, 2, 4 }, HINT_FL[4] = { 0, 1, 2, 3 };
static struct { int n; char *text[4]; } g_hints[128];
static int g_nhints;

static const u8 *prg_find(const u8 *from, u8 c)
{
    const u8 *end = g_flinkPrg + g_flinkPrgSize;
    while (from < end && *from != c) from++;
    if (from >= end) die("FLINK.PRG: a message has no end");
    return from;
}

static char *hint_lines(int n, int lang)
{
    const u8 *q = g_flinkPrg + be32(g_flinkPrg + 0x18250 - PRG_LOAD + lang * 4) - PRG_LOAD, *end;
    struct { int set, x; char t[256]; } rows[256];
    int k, i, len, ymin = 256, ymax = -1, x0 = 1 << 30, nout = 0;
    char out[21][64], *res;
    size_t total = 0;
    for (k = 0; k < n; k++) q = prg_find(q, 0xFF) + 1;
    end = prg_find(q, 0xFF);
    len = (int)(end - q);
    memset(rows, 0, sizeof(rows));
    i = 0;
    while (i + 2 < len) {
        int x = q[i], y = q[i + 1], e, j, t = 0;
        for (e = i + 2; e < len && q[e]; e++) ;
        for (j = i + 2; j < e && t < 255; j++) rows[y].t[t++] = q[j] >= 32 && q[j] < 127 && q[j] != '|' ? (char)q[j] : ' ';
        rows[y].t[t] = 0;
        rows[y].set = 1;
        rows[y].x = x;
        i = e + 1;
    }
    for (k = 0; k < 256; k++)
        if (rows[k].set) { ymin = imin(ymin, k); ymax = imax(ymax, k); x0 = imin(x0, rows[k].x); }
    if (ymax < 0) { res = xalloc(1); return res; }
    for (k = ymin; k <= ymax && nout < 20; k++) {
        char line[512];
        int l = 0, j;
        if (rows[k].set) {
            for (j = 0; j < rows[k].x - x0 && l < 400; j++) line[l++] = ' ';
            for (j = 0; rows[k].t[j] && l < 511; j++) line[l++] = rows[k].t[j];
        }
        while (l && line[l - 1] == ' ') l--;              /* rstrip */
        if (l > 26) l = 26;
        memcpy(out[nout], line, (size_t)l);
        out[nout][l] = 0;
        nout++;
    }
    if (nout & 1) out[nout++][0] = 0;
    for (k = 0; k < nout; k++) total += strlen(out[k]) + 1;
    res = xalloc(total + 1);
    for (k = 0; k < nout; k++) { if (k) strcat(res, "|"); strcat(res, out[k]); }
    return res;
}

void hints_add(int n)
{
    int i;
    for (i = 0; i < g_nhints; i++) if (g_hints[i].n == n) return;
    if (g_nhints == 128) die("too many hint texts");
    g_hints[g_nhints].n = n;
    for (i = 0; i < 4; i++) g_hints[g_nhints].text[i] = hint_lines(n, HINT_FL[i]);
    g_nhints++;
}

/* Secret levels. A Warp with checkbox 1 (0x2AEAA sets 0x827B) opens hidden paths of Flink's world map
   (0x2D420): its level's point turns each neighbour param (UP/DOWN/LEFT/RIGHT, params 2-5) that is
   negative and whose checkbox (bits 2-5) is set into a path to point -param. FOREST map 20 (3-5) opens
   point 29 (level 7, FOREST map 21) and VILLAGE map 20 (14-2) point 32 (level 17, VILLAGE map 21),
   each a room of chests. FLINK.DLL plays them only after their level is finished at that Warp and then
   goes on with the level after it; the main flow skips them (README "Secret levels").

   -> {secret level index: the sequence number of the map whose secret Warp opens it} */
/* secret_levels: a Warp with checkbox 1 opens hidden paths of Flink's world map: its level's point
   turns each neighbour param (UP/DOWN/LEFT/RIGHT, params 2-5) that is negative and whose checkbox
   (bits 2-5) is set into a path to point -param -> {secret level index: the map whose Warp opens it} */
static void secret_levels(const Chain *levels, int nlev, const Chain *rooms, int *secret)
{
    Places *pts = places_of(world_of("MAIN"), 3);
    VEC(const Place *) nodes = { 0 };
    int i, k, j, d;
    for (i = 0; i < 64; i++) secret[i] = 0;
    for (i = 0; i < pts->n; i++)
        if (!strcmp(pts->v[i].name, "Map ") && !(pts->v[i].check & 1)) vpush(nodes, &pts->v[i]);
    for (k = 0; k < nlev; k++) {
        const Place *node = NULL;
        IntVec all = { 0 };
        for (i = 0; i < nodes.n; i++) if (prm(nodes.v[i], 1, 0) + 1 == levels[k].v[0]) { node = nodes.v[i]; break; }
        for (i = 0; i < levels[k].n; i++) vpush(all, levels[k].v[i]);
        for (i = 0; i < rooms[k].n; i++) vpush(all, rooms[k].v[i]);
        for (j = 0; j < all.n; j++) {
            const char *world;
            int m, bg, warp = 0;
            Places *pl = map_places(all.v[j], &world, &m, &bg);
            for (i = 0; i < pl->n; i++) if (!strcmp(pl->v[i].name, "Warp") && pl->v[i].check & 1) warp = 1;
            if (!node || !warp) continue;
            for (d = 2; d < 6; d++) {
                int v = node->np > d ? s16v(node->params[d]) : 0, lev = -1, q;
                /* level_of: the point -> the level whose first map is its level-entry (the last such point) */
                for (q = 0; q < nodes.n; q++)
                    if (prm(nodes.v[q], 0, 0) == -v) {
                        int z;
                        lev = -1;
                        for (z = 0; z < nlev; z++) if (levels[z].v[0] == prm(nodes.v[q], 1, 0) + 1) { lev = z; break; }
                    }
                if (node->check >> d & 1 && v < 0 && lev >= 0) secret[lev] = all.v[j];
            }
        }
        free(all.v);
    }
    free(nodes.v);
}

void ini_add(Buf *ini, const char *fmt, ...)
{
    char line[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    buf_add(ini, line, strlen(line));
    buf_add(ini, "\r\n", 2);
}

/* Arrivals. Flink's Exit point (0x2AF7A) loads level 'level' (g_LevelSeqIndex 0x81D6) with entry
   'entry' (0x8318); there the Revive point (0x2B0EC) spawned at once (spawn flag 6) whose 'entry'
   param matches (0 counts as 1) puts Flink on itself (0x2B124: player x/y 0x81F6/0x81FA and the
   restart point 0x831A/0x831C), so he just appears there, standing. Lomax's next sub-level instead
   has him walk in from x = -100 at the height of the Start trapdoor's floor, without following the
   ground. LEVELS.INI's [Arrivals] gives, for each Exit, the point it arrives at, which FLINK.DLL puts
   Lomax on (trapdoor_spawn): 'FILE X Y=NEXTFILE AX AY', X Y the Exit's placement, AX AY the revive
   point's x and floor line. Two exits can lead to different points (2-1's upper and lower routes).
   Doors: an Exit point with 'look up' checked is entered with Up (0x2AFDC: Flink within 16 px of its
   x, his feet 0..80 px above its y; 10 frames later the map loads) and can lead anywhere in the
   level, to an earlier map, the same map (FOREST map 10 and VILLAGE map 8 have pairs of doors between
   two spots) or a side room (see side_rooms). Such a line ends with ' door'.

   A map's Flink Exit points that lead to a map of its level (maps: LevelSequenceTable numbers).

   GHOST's and SHRINK's areas: Flink's Kill area (0x22C18) and Shrink area (0x22CCE) let the spell work
   while the player stands at their y within 30 px of their x; LEVELS.INI's [Areas] lists them for
   FLINK.DLL (spells.c), which makes each a shortcut. They aren't placed as objects. */
void convert_levels(const char *outDir)
{
    Chain levels[64], rooms[64];
    const Place *ends[64];
    static Links links[64];
    int done[64], order[64], norder = 0, secret[64];
    int nlev, k, j, nmaps = 0, i;
    Buf ini = { 0 }, arrivals = { 0 }, areas = { 0 };
    char path[MAX_PATH];
    memset(ends, 0, sizeof(ends));
    memset(done, 0, sizeof(done));
    nlev = flink_main_levels(levels, ends);
    side_rooms(levels, nlev, rooms);
    make_dir(outDir);
    ini_add(&ini, "[Levels]");
    ini_add(&ini, "Count=%d", nlev);
    for (k = 0; k < nlev; k++) {
        IntVec maps = { 0 };
        char list[256] = "";
        for (i = 0; i < levels[k].n; i++) vpush(maps, levels[k].v[i]);
        for (i = 0; i < rooms[k].n; i++) vpush(maps, rooms[k].v[i]);
        if (maps.n > 5) die("level %d: %d maps, Lomax has 5 sub-levels", k + 1, maps.n);
        for (j = 0; j < maps.n; j++) {
            int n = maps.v[j], m, bg, finish, nex = 0;
            const char *world;
            Places *pl = map_places(n, &world, &m, &bg);
            FWorld *w = world_of(world);
            const Place *end_p, *warp = NULL, *exits[64];
            Buf out = { 0 };
            char info[4096];
            if (j < levels[k].n) {
                finish = j == levels[k].n - 1;
                end_p = ends[n];
                if (!finish) warp = first_named(pl, "Warp");    /* a Warp too: the level also ends here (14-1) */
            } else {                                            /* a side room: a Finish only at its own Warp */
                end_p = first_named(pl, "Warp");
                finish = end_p != NULL;
            }
            for (i = 0; i < pl->n; i++)                         /* level_exits */
                if (!strcmp(pl->v[i].name, "Exit point") && in_list(maps.v, maps.n, prm(&pl->v[i], 0, 0) + 1)) {
                    if (nex == 64) die("too many exits");
                    exits[nex++] = &pl->v[i];
                }
            convert(w, world, m, bg, finish, end_p, exits, nex, warp, &out, info, sizeof(info), &links[n]);
            if (!done[n]) { done[n] = 1; order[norder++] = n; }
            snprintf(path, sizeof(path), "%s\\L%02d.WLD", outDir, n);
            write_file(path, out.p, out.n);
            for (i = 0; i < pl->n; i++)                         /* GHOST's and SHRINK's areas (SPELL_AREAS) */
                if (!strcmp(pl->v[i].name, "Shrink area") || !strcmp(pl->v[i].name, "Kill area"))
                    ini_add(&areas, "L%02d.WLD=%s,%d,%d", n, pl->v[i].name, pl->v[i].x, pl->v[i].y);
            printf("  %d-%d L%02d.WLD%s%s: %s\n", k + 1, j + 1, n, j < levels[k].n ? "" : " (side room)",
                   warp ? " (also a Finish at its Warp)" : "", info);
            buf_free(&out);
            nmaps++;
            snprintf(list + strlen(list), sizeof(list) - strlen(list), "%sL%02d.WLD", j ? " " : "", n);
        }
        ini_add(&ini, "%d=%s", k + 1, list);
        /* arrival_points: each Exit (map, x, y) -> the map it leads to and where Lomax arrives there */
        for (j = 0; j < maps.n; j++) {
            const Links *l = &links[maps.v[j]];
            for (i = 0; i < l->exits.n; i++) {
                const ExitEntry *e = &l->exits.v[i];
                const Links *t = &links[e->t];
                const Arrive *a = NULL, *one = NULL;
                int q;
                for (q = 0; q < t->arrive.n; q++) {
                    if (t->arrive.v[q].entry == e->e) a = &t->arrive.v[q];
                    if (t->arrive.v[q].entry == 1) one = &t->arrive.v[q];
                }
                if (!a) a = one;
                if (!a) die("L%02d.WLD: no arrival point", e->t);
                ini_add(&arrivals, "L%02d.WLD %d %d=L%02d.WLD %d %d%s", maps.v[j], e->x, e->y, e->t, a->x, a->y, e->door ? " door" : "");
            }
        }
        free(maps.v);
    }
    /* the hint hats' texts, by message and language */
    ini_add(&ini, "");
    ini_add(&ini, "; Flink's messages shown by the hint hats that replace its information notes");
    ini_add(&ini, "[Hints]");
    {
        int ord[128];
        for (i = 0; i < g_nhints; i++) ord[i] = i;
        for (i = 0; i < g_nhints; i++)
            for (j = i + 1; j < g_nhints; j++)
                if (g_hints[ord[j]].n < g_hints[ord[i]].n) { int x = ord[i]; ord[i] = ord[j]; ord[j] = x; }
        for (i = 0; i < g_nhints; i++)
            for (j = 0; j < 4; j++) ini_add(&ini, "%d.%d=%s", g_hints[ord[i]].n, HINT_LL[j], g_hints[ord[i]].text[j]);
    }
    ini_add(&ini, "");
    ini_add(&ini, "; each Exit (map, x, y) -> the map it leads to and where Lomax arrives there (x, floor y),");
    ini_add(&ini, "; 'door': entered with Up (see arrival_points)");
    ini_add(&ini, "[Arrivals]");
    buf_add(&ini, arrivals.p, arrivals.n);
    /* the secret levels: s=FILE, level s comes only after its level is finished at that map's Warp */
    secret_levels(levels, nlev, rooms, secret);
    ini_add(&ini, "");
    ini_add(&ini, "; secret levels: s=FILE, level s comes only after its level is finished at that map's");
    ini_add(&ini, "; Warp, then the level after that one; the main flow skips it");
    ini_add(&ini, "[Secret]");
    for (k = 0; k < nlev; k++) if (secret[k]) ini_add(&ini, "%d=L%02d.WLD", k + 1, secret[k]);
    for (k = 0; k < nlev; k++) if (secret[k]) printf("  level %d: secret, opened by L%02d.WLD\n", k + 1, secret[k]);
    ini_add(&ini, "");
    ini_add(&ini, "; the areas where GHOST and SHRINK work: FILE=type,x,y (FLINK.DLL spells.c)");
    ini_add(&ini, "[Areas]");
    buf_add(&ini, areas.p, areas.n);
    ini_add(&ini, "");
    ini_add(&ini, "; each map's music (its Music trigger's param; 0 = none, the music goes on)");
    ini_add(&ini, "[Music]");
    for (i = 0; i < norder; i++) ini_add(&ini, "L%02d.WLD=%d", order[i], links[order[i]].music);
    /* Flink's world map, title screen, intro and ending */
    ini_add(&ini, "");
    worldmap(levels, rooms, nlev, outDir, &ini);
    ini_add(&ini, "");
    title(outDir, &ini);
    ini_add(&ini, "");
    scenes(outDir, &ini);
    snprintf(path, sizeof(path), "%s\\LEVELS.INI", outDir);
    write_file(path, ini.p, ini.n);
    printf("  %d levels, %d maps, %d hint texts\n", nlev, nmaps, g_nhints);
    buf_free(&ini); buf_free(&arrivals); buf_free(&areas);
}
