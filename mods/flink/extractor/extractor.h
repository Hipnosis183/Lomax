/* extractor.h - what FLINK.EXE's files share.

   FLINK.EXE takes no parameters. It finds the Flink disc image (.cue and its .bin files) in its
   own folder or one folder below (disc.c, as Lomax+'s LOADER.EXE finds the Lomax disc), reads
   Flink's files out of the image's data track and writes everything the mod needs into its own
   folder: SFX\Enn.WAV (sfx.c) and MUSIC\TRACKnnn.WAV (music.c). It sits in the game's MODS\FLINK,
   next to FLINK.DLL, so its folder is the mod's: it copies nothing anywhere else. */
#ifndef EXTRACTOR_H
#define EXTRACTOR_H

#include <windows.h>
#include <stddef.h>

/* ---- util.c ---- */
typedef unsigned char u8;

extern char g_dir[MAX_PATH];                 /* the folder FLINK.EXE is in, no trailing '\' */

void die(const char *fmt, ...) __attribute__((noreturn));   /* prints the message and exits with 1 */
void *xalloc(size_t n);                      /* zeroed; dies when out of memory */
void *xrealloc(void *p, size_t n);
unsigned be16(const u8 *p);
unsigned be32(const u8 *p);
void put_le16(u8 *p, unsigned v);
void put_le32(u8 *p, unsigned v);
unsigned crc32(const u8 *p, size_t n);
void make_dir(const char *path);             /* dies when it can't be made */
int file_exists(const char *path);
/* writes the file through PATH.tmp, renamed when complete; dies on failure */
void write_file(const char *path, const void *data, size_t n);

/* a growable byte buffer */
typedef struct { u8 *p; size_t n, cap; } Buf;
void buf_add(Buf *b, const void *data, size_t n);
void buf_free(Buf *b);

/* ---- disc.c ---- */
#define CUE_MAX_TRACKS 99
typedef struct {
    int num, audio, secSize;                 /* TRACK nn, AUDIO or not, MODE1/2352 -> 2352 */
    char file[MAX_PATH];                     /* the FILE the track is in */
    DWORD start;                             /* its INDEX 01, in sectors from the file's start; -1: none */
} CueTrack;

typedef struct {
    char cue[MAX_PATH];
    CueTrack t[CUE_MAX_TRACKS];
    int n;
} Disc;

/* finds the Flink disc image next to FLINK.EXE (or one folder below) and reads FLINK.PRG from
   it; dies when there is none */
void disc_open(Disc *d);
/* a file of the data track's ISO 9660 file system, found by name in any folder; NULL if it isn't
   there. *size gets its length. */
u8 *disc_read_file(Disc *d, const char *name, DWORD *size);

/* ---- xpk.c ---- */
/* an XPKF/NUKE stream (a chunk payload starting with "XPKF", n bytes) -> its unpacked bytes
   (malloc'ed, *outLen of them); dies on a broken stream */
u8 *xpk_unpack(const u8 *blob, size_t n, size_t *outLen);

/* ---- sfx.c ---- */
void sfx_extract(const u8 *prg, DWORD prgSize, const char *outDir);

/* ---- music.c ---- */
void music_extract(const Disc *d, const char *outDir);

#endif
