/* convert.h - the level converter's shared types.

   The output depends on floored division (fdiv / fmodp), halves rounded to even (rint) and the order
   in which cells, tiles and sprite banks are first met (insertion-ordered maps, hmap.c). */

/* The output world has three planes, in this order:
       0  Flink's high-priority tiles (drawn over sprites on the Mega Drive), scrolled 1:1 by a ScrollMap
       1  the play map: low-priority tiles, collision and the objects
       2  the background map, scrolled by a ScrollMap at half speed horizontally
   Lomax draws the first plane of a level in front and puts actors just above their own plane, so
   plane 0 covers Lomax as the priority tiles covered Flink.

   Objects: the play plane gets Lomax's Start trapdoor over Flink's start (TRAPDOOR_HEIGHT above the
   floor, as in Lomax's levels), Revive at the other revive points that are checkpoints (not the entry
   points) and Music; then either Exits at Flink's exits to the next map of the level (a sub-level that
   leads on), or a Finish trapdoor over the Warp that ends the level. LEVELS.INI's
   [Arrivals] gives the point on the next map that each Exit leads to (see arrival_points). Flink's platforms become Lomax platform objects with Flink's sprites
   (see PLATFORMS), and so do its enemies, as far as converted (see ENEMIES: so far the Forest Dummy, the Firemonster, the Ingredient Holder, the Thief, the Visje, the Statue, the Raketen wurm, the Puge, the Intree dummy, the Worm, the Meta dummy, the Storm Cloud, the Blitz dude, the Circling suckers, the Forest Boss, the Village Boss, the Donkey monkey, the Meta Boss and the final boss Bos1 with its Bos3 dudes, whose arenas' Finish appears only once they are defeated),
   its chests ('Pots Chest' with a Lomax pot's content, or a hint hat for an information note: see
   chest_content; LEVELS.INI's [Hints] gets the notes' texts) and its Tree actor, the vine
   columns in front of the screen ('Spik Tree' on plane 0 or the background plane), its lianas
   (Lomax ropes, 'Rope Liana'), its monkeys, whose tail is one ('Rope Monkey'), and its Knols
   ('Spik Knol', Lomax's harmful decoration), its Snake ('Spik Snake': see snake_records), its Stahlkugeln ('Spik Stahl', Stahlkugel), its Spikes ('Spik Spike'), its drop balls ('Spik Drop'), its Falling apples ('Spik Apple'), its rolling apples ('Spik Appl', 'Spik App2': see bigapple_records), its Butterflies ('Spik Butterfly'), its Rope slides ('Spik Slide'), its Rolling stones ('Spik Rolling'), its lava kinds (Lava columns 'Spik Lava', Protuberances 'Spik Prot', Great balls of fire 'Spik Great', Bursts of lava 'Spik Burst', Fountains 'Spik Fountain') and its stones ('Spik Stone', which Lomax picks up and
   throws: see stone_type), and its mud's edges ('Spik Ground', the Ground type actors). Flink's other
   actors (other items, spells...) are not converted; its Jumppads, seesaws and the rocks Lomax pushes are (see plat_jumppad, plat_seesaw, plat_rock). Flink's Generators, which spawn hidden
   actors when Flink touches them, become 'Gene Flink' triggers linked to those objects as Lomax's own
   triggers are (see generator_links).

   Format differences handled here (Flink's decompilation.md 3, Lomax's World_LoadWLD plate comment):
     - Flink chunks are XPK-packed, padded to even sizes and big-endian; Lomax's data is little-endian
       apart from the chunk headers, the HEAD size words, the TTAB and the MAP tile words. Every Lomax
       chunk is a multiple of 4 bytes long.
     - CELL: 4 bpp (2 pixels a byte, high nibble first) -> 8 bpp. Lomax builds a 16x16 tile with a
       single palette, but Flink can mix palette lines inside one tile, so the line goes into the
       pixel: value = line*16 + colour, 0 = transparent. All 64 colours go into Lomax palette 0.
     - HEAD palette: 4 lines x 16 Mega Drive colours (0000BBB0GGG0RRR0) -> 256 x 15-bit (bits 0-4
       blue, 5-9 green, 10-14 red; World_LoadWLD swaps them for the DIB). Palettes 1-9 (sprites)
       come from the template's first plane.
     - TTAB tile numbers are 0x180 + n, n counting the cells set in the map's HEAD bitmap; Lomax's
       TTAB (big-endian in the file: its MAP handler byte-swaps the TTAB and the tile words)
       indexes the world's CELL chunk directly. Flips move from bits 11/12 to 0x2000/0x4000.
     - MAP: one word per 16x16 cell (bits 0-10 tile, 11-15 collision) -> tile words, then a byte per
       cell. Lomax's collision byte has the same bit layout as Flink's top five bits: byte =
       (word >> 8) & 0xF8 (floor 0xC0 + shape bits 3-5; wall 0x80 + 0x08 ceiling / 0x10 left / 0x20 right). */
#ifndef CONVERT_H
#define CONVERT_H

#include "extractor.h"

/* floored division and remainder */
static inline int fdiv(int a, int b) { int q = a / b; return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q; }
static inline int fmodp(int a, int b) { int r = a % b; return (r != 0 && (r < 0) != (b < 0)) ? r + b : r; }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int s16v(int v) { v &= 0xFFFF; return v & 0x8000 ? v - 0x10000 : v; }

/* a growable array: VEC(T) v = {0}; vpush(v, item) */
#define VEC(T) struct { T *v; int n, cap; }
#define vpush(vec, item) do { if ((vec).n == (vec).cap) { (vec).cap = (vec).cap * 2 + 8; \
    (vec).v = xrealloc((vec).v, (size_t)(vec).cap * sizeof(*(vec).v)); } (vec).v[(vec).n++] = (item); } while (0)

typedef VEC(Buf) BufVec;
typedef VEC(int) IntVec;

/* a byte range (owned or a view) */
typedef struct { const u8 *p; size_t n; } Bytes;

/* ---- hash map: byte-string keys -> int, in insertion order (hmap.c) ---- */
typedef struct {
    Buf keys;                      /* the keys back to back */
    VEC(size_t) off;
    VEC(int) len, val;
    int *slot, nslot;              /* open addressing: entry index + 1, 0 = empty */
} HMap;
int hmap_get(const HMap *m, const void *k, int klen, int *val);   /* 1 if there */
void hmap_put(HMap *m, const void *k, int klen, int val);          /* adds or replaces */
void hmap_free(HMap *m);

/* ---- Flink side (flink_wld.c) ---- */
typedef struct { int a, f, dx, dy, fl; } Piece;           /* u8 anim, u8 frame, s8 dx, s8 dy, u8 flags */
typedef struct { char tag[5]; int speed, hb[4], box2[4]; Piece *pc; int npc; } Rec;
typedef struct { Rec *r; int n; } Recs;                    /* a type's records, in file order */
const Rec *rec_get(const Recs *rs, const char *tag);      /* the last with that tag, or NULL */

typedef struct { Bytes head, ttab, map, gnmy; } FMap;
typedef struct {
    Bytes cells;
    VEC(Bytes) anims;
    VEC(char *) nmy;                                       /* type names by NMY slot ("" for an empty one) */
    VEC(Recs) recs;
    VEC(FMap) maps;
} FWorld;

typedef struct {
    const char *name;                                      /* the converter's spelling (nmy_name) */
    int slot, x, y, check, *params, np, off, sf, link;     /* link -1: none */
} Place;
typedef VEC(Place) Places;

typedef struct { int w, h; u8 *px; } Frame;
typedef struct { Frame *f; int n; } Frames;

extern const u8 *g_flinkPrg;                               /* FLINK.PRG (loaded at 0x62FA) */
extern DWORD g_flinkPrgSize;
#define PRG_LOAD 0x62FA

void flink_world(FWorld *w, const u8 *data, size_t n);    /* the file's first world */
Places flink_placements(const FWorld *w, const FMap *m);
int prm(const Place *p, int i, int def);                   /* param i, def past the placement's params */
/* param i, past the placement's params defs[i - np]: the defaults follow the params */
static inline int prml(const Place *p, int i, const int *defs) { return i < p->np ? p->params[i] : defs[i - p->np]; }
const char *nmy_name(const char *name);
Frames flink_anim_frames(Bytes a);                         /* cached by the ANIM's address */
int head_used_cells(Bytes head, int *out);                 /* -> count; out: 8192 ints */
unsigned vdp_to_15(unsigned w);
void anim_piece_offset(Bytes a, int f, int *x, int *y);
void level_maps(int n, const char **world, int *m, int *bg);
int flink_music_track(int index);                          /* the disc track of a music index; -1 none */
int recs_has(const Recs *rs, const char *tag);
Recs *world_recs(FWorld *w, int slot);                     /* NULL past the list */

/* ---- Lomax side (lomax_wld.c) ---- */
typedef struct { u8 b[32]; } BankSet;                      /* anim banks 0-255 */
static inline void bank_add(BankSet *s, int b) { if (b < 0 || b > 255) die("anim bank %d out of range", b); s->b[b >> 3] |= (u8)(0x80 >> (b & 7)); }
static inline void bank_union(BankSet *s, const BankSet *t) { int i; for (i = 0; i < 32; i++) s->b[i] |= t->b[i]; }

typedef struct { int bank, frame, dx, dy, fl; } Ent;
typedef VEC(Ent) Ents;
typedef struct { Buf d; BankSet banks; } LRec;             /* a Lomax record and its banks */

typedef struct { char *name; int nparams; BankSet banks; } LType;
typedef VEC(LType) LTypes;
typedef struct { char tag[5]; Bytes p; } LChunk;
typedef VEC(LChunk) LChunks;
LChunks lomax_chunks(const u8 *d, size_t n);
const LChunk *lchunk(const LChunks *c, const char *tag);   /* the first */
int lomax_types(const u8 *nmy, size_t n, LTypes *out); /* -> offset of the closing nameLen 0 */
void chunk(Buf *out, const char *tag, const void *data, size_t n);
LRec pack_record(const char *fourcc, int speed, const int hb[4], const Ent *e, int ne);
void lomax_type(Buf *out, const char *name, int nparams, int nchecks, const LRec *recs, int nrecs);
void build_head(Buf *out, int w8, int h8, const unsigned *pal0, Bytes thead, const u8 *animbits);

/* ---- objects placed on a plane ---- */
typedef struct { int t, x, y, flags, prm[24], np; const Place *p; } Obj;
typedef VEC(Obj) Objs;
Obj obj(int t, int x, int y, int flags, const int *prm, int np, const Place *p);
void gnmy(Buf *out, const Objs *o);

/* ---- sprites and cells (sprites.c) ---- */
enum { SK_ANIM = 1, SK_BLANK, SK_FILL, SK_GLASS, SK_SCALED, SK_ROLLED, SK_LINE, SK_X2, SK_MAIN, SK_ADDED };
typedef struct { int kind, a, b, c; } SKey;
static inline SKey sk(int kind, int a, int b, int c) { SKey k; k.kind = kind; k.a = a; k.b = b; k.c = c; return k; }
#define SK_A(n) sk(SK_ANIM, (n), 0, 0)

typedef struct {
    const FWorld *w;                                       /* the map's world: its ANIMs */
    VEC(SKey) keys;
    VEC(Frames) frames;
} Sprites;
#define FLINK_ANIM_BASE 100
int sprites_bank(Sprites *s, SKey k);
int sprites_add(Sprites *s, SKey k, Frames fr);
const Frames *sprites_frames(Sprites *s, SKey k);
int sprites_width(Sprites *s, SKey k, int f);
void sprites_centre(Sprites *s, SKey k, int f, int fl, int *cx, int *cy);
void sprites_chunks(Sprites *s, Buf *out);                 /* every ANIM chunk */

typedef struct { const u8 *src; size_t srcLen; HMap ids; Buf data; int n; const u8 *remap; } Cells;   /* remap: SceneCells' (256 values, or NULL) */
void cells_init(Cells *c, Bytes src);
int cells_get(Cells *c, int n, int line, int opaque);      /* n -1: no cell (blank, or the backdrop when opaque) */
int cells_raw(Cells *c, const u8 *px);
void convert_layer(const FMap *fm, const int *used, Cells *c, int (*keep)(unsigned, int), int keeparg,
                   int opaque, Buf *ttab, IntVec *meta);

/* ---- records (records.c) ---- */
#define PLATFORM_EDGE 6
#define JUMPPAD_EDGE 1
#define WAGON_EDGE 1
typedef struct { const Rec *rec; int only, hasOnly; } RecSel;   /* rec NULL: a blank frame */
LRec lomax_record(const char *fourcc, const RecSel *recs, int nrecs, Sprites *s, int mirror, int dbl,
                  int repeat, int edge, int slots, int hide, int world);
LRec lomax_record1(const char *fourcc, const Rec *rec, Sprites *s, int edge, int world);
int flink_frames(const Rec *r, int *starts);               /* -> frame count; starts: npc+1 ints */
void lomax_pieces(const Piece *pc, int n, Sprites *s, int world, Ents *out);
void end_frame(Ents *e, int from);
LRec ring_record(const FWorld *fw, const Rec *rec, Sprites *s);
void attach_point(const Rec *rec, int hook, int *dx, int *dy);
void shot_record(const char *tag, Sprites *s, LRec *out);
int bigapple_records(const char *name, const Recs *recs, Sprites *s, LRec *out);
int snake_records(const Recs *recs, Sprites *s, LRec *out);
void free_lrec(LRec *r);
void spirit_type(Sprites *s, BufVec *types, BankSet *banks);
void glass_type(Sprites *s, BufVec *types, BankSet *banks);
void spells_type(Sprites *s, unsigned *pal, BufVec *types, BankSet *banks);
void cloud_type(Sprites *s, BufVec *types, BankSet *banks);
void stone_type(Sprites *s, BufVec *types, BankSet *banks);
void dustcloud_type(Sprites *s, BufVec *types, BankSet *banks);
void chest_type(Sprites *s, BufVec *types, BankSet *banks);

/* MAIN.WLD's records the converter uses (set by converter_init) */
typedef struct {
    FWorld w;
    Recs *shot, *chest, *dust, *stones, *spirit, *glass, *ingr, *lightning, *tornado, *platform, *demon, *logo;
    Recs *holder, *thief;                                  /* MAIN_ENEMIES: the Ingredient Holder's and the Thief's */
} MainWorld;
extern MainWorld g_main;

/* Lomax's template worlds, read once */
typedef struct {
    u8 *data; size_t n;
    LChunks ch;
} LFile;
extern LFile g_tpl;                                        /* WORLDS\MEDIEVAL.WLD */
typedef struct { Frames frames; Ent *ents; int nents; unsigned pal[256]; } LomaxDigits;
extern LomaxDigits g_digits;                               /* Lomax's MAIN.WLD KLEI */

/* ---- the converter (convert.c, platforms.c, levels.c) ---- */
#define WATER_TINT 254
#define WATER_TINT_H 255
#define FREE_COLOURS 64
#define BACKDROP 16
#define MUSIC_BASE 100

typedef struct {
    unsigned *words; int w, h;                             /* the play map's words, cells across / down */
    int hasWater, water, mean, wave_dy, tint;
} Ctx;
int ground_at(const Ctx *c, int x, int y, int *g);         /* 1 and *g, or 0 (no floor) */

typedef struct {
    BufVec types;                                          /* new NMY types */
    BankSet banks;
    Objs objs;
    VEC(const char *) skippedName; VEC(int) skippedCount;
} PlatOut;
void convert_platforms(FWorld *fw, Places *pl, Sprites *s, int first_type, Ctx *ctx, PlatOut *out);

typedef struct {
    BufVec types;                                          /* new NMY types */
    BankSet banks;
    Objs objs;
    VEC(const char *) placedName; VEC(int) placedCount;
} EnemyOut;
void convert_enemies(FWorld *fw, Places *pl, Sprites *s, int first_type, EnemyOut *out);

/* Flink's messages for the hint hats and Bos1 (levels.c): HINTS */
void hints_add(int n);

typedef struct { int x, y, e, t, door; } ExitEntry;
typedef struct { int entry, x, y; } Arrive;
typedef struct {
    VEC(ExitEntry) exits;
    VEC(Arrive) arrive;
    int music;
} Links;
/* one map -> its world bytes, the summary line, the links (arrival_points) */
void convert(FWorld *fw, const char *world, int m, int bgmap, int finish, const Place *end_p,
             const Place *const *exits, int nexits, const Place *warp, Buf *out, char *info, size_t infoSize, Links *links);

void converter_init(Disc *d, const char *gameDir);
FWorld *world_of(const char *name);
void convert_levels(const char *outDir);
Places *places_of(FWorld *w, int m);                      /* a map's placements, made once */
Places *map_places(int n, const char **world, int *m, int *bg);   /* Flink level n's play map's */
u8 *read_game_file(const char *dir, const char *name, size_t *n);
u8 *read_game_world(const char *name, size_t *n);          /* the game's WORLDS\name */
void ini_add(Buf *ini, const char *fmt, ...);              /* a LEVELS.INI line (CRLF) */

/* the world map, the title screen, the intro and ending (worldmap.c, title.c, scenes.c): each writes
   its worlds into outDir and adds its LEVELS.INI section */
void worldmap(const IntVec *levels, const IntVec *rooms, int nlev, const char *outDir, Buf *ini);
void title(const char *outDir, Buf *ini);
void scenes(const char *outDir, Buf *ini);
/* shared by them (convert.c) */
void template_sprite_colours(BufVec *anims, Bytes thead, unsigned *pal);
int keep_fg(unsigned w, int prio);
int keep_pl(unsigned w, int prio);
int keep_all(unsigned w, int prio);

#endif
