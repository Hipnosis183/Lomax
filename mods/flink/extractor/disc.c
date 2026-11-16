/* disc.c - the Flink disc image: its .cue sheet and the data track's ISO 9660 file system.

   The image is found as Lomax+'s LOADER.EXE finds the Lomax disc (LOADER\loader.c find_cue): a .cue
   in FLINK.EXE's folder, then in each folder one level down. Here every .cue found is tried, and
   the first whose data track holds FLINK.PRG is Flink's, so a Lomax disc image next to it is passed
   over. A .cue lists FILE lines (the .bin files, relative to the .cue unless they hold a drive) and
   under each its TRACK lines (TRACK nn AUDIO, TRACK nn MODE1/2352 ...) with their INDEX 01 (mm:ss:ff,
   75 sectors a second, from the start of that FILE).

   The data track holds an ISO 9660 file system: 2048 user bytes a sector, at offset 16 of a raw
   2352-byte Mode 1 sector, 24 of a Mode 2 Form 1 one (the mode byte, offset 15, is 2), 8 of a
   MODE2/2336 one and 0 of a MODE1/2048 image. The primary volume descriptor (sector 16) gives the
   root folder; a folder's records give each entry's first sector, length and name ("FLINK.PRG;1"). */
#include "extractor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_cue(const char *cuePath, CueTrack *t)
{
    char line[1024], dir[MAX_PATH], file[MAX_PATH] = "";
    int n = 0;
    FILE *f = fopen(cuePath, "r");
    if (!f) return 0;
    snprintf(dir, sizeof(dir), "%s", cuePath);
    *(strrchr(dir, '\\') + 1) = 0;
    while (fgets(line, sizeof(line), f)) {
        char *p = line, *q;
        int num, mm, ss, ff;
        while (*p == ' ' || *p == '\t') p++;
        if (!_strnicmp(p, "FILE", 4) && (p[4] == ' ' || p[4] == '\t')) {
            p += 5;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '"') { p++; q = strchr(p, '"'); } else q = strpbrk(p, " \t\r\n");
            if (q) *q = 0;
            snprintf(file, sizeof(file), "%s%s", strchr(p, ':') ? "" : dir, p);
        } else if (!_strnicmp(p, "TRACK", 5) && (p[5] == ' ' || p[5] == '\t') && n < CUE_MAX_TRACKS &&
                   sscanf(p + 6, "%d", &num) == 1) {
            t[n].num = num;
            t[n].audio = strstr(p, "AUDIO") != NULL;
            q = strchr(p, '/');                                /* MODE1/2048, MODE2/2352, ... */
            t[n].secSize = q ? atoi(q + 1) : 2352;
            snprintf(t[n].file, MAX_PATH, "%s", file);
            t[n].start = (DWORD)-1;
            n++;
        } else if (!_strnicmp(p, "INDEX", 5) && n && sscanf(p + 5, "%d %d:%d:%d", &num, &mm, &ss, &ff) == 4 && num == 1) {
            t[n - 1].start = (DWORD)((mm * 60 + ss) * 75 + ff);
        }
    }
    fclose(f);
    return n;
}

/* ---- ISO 9660 ---- */
#define ISO_CHUNK 32                                 /* sectors a read */

typedef struct {
    HANDLE f;
    LONGLONG base;                                   /* the track's first sector, in bytes */
    int secSize, dataOff;                            /* raw sector size, its user data's offset */
} Iso;

typedef struct { DWORD lba, size; int dir; char name[64]; } IsoEntry;

static int iso_read(Iso *im, DWORD lba, int n, u8 *out)
{
    static u8 raw[2352 * ISO_CHUNK];
    LARGE_INTEGER pos;
    DWORD got;
    int i;
    pos.QuadPart = im->base + (LONGLONG)lba * im->secSize;
    if (!SetFilePointerEx(im->f, pos, NULL, FILE_BEGIN) ||
        !ReadFile(im->f, raw, (DWORD)(n * im->secSize), &got, NULL) || got != (DWORD)(n * im->secSize))
        return 0;
    for (i = 0; i < n; i++) memmove(out + i * 2048, raw + i * im->secSize + im->dataOff, 2048);
    return 1;
}

/* opens the cue's first data track; 0 if it has none or it isn't an ISO 9660 track */
static int iso_open(Iso *im, const CueTrack *t, int n, IsoEntry *root)
{
    const CueTrack *data = NULL;
    u8 pvd[2048], hdr[16];
    DWORD got;
    int i;
    for (i = 0; i < n && !data; i++)
        if (!t[i].audio) data = &t[i];
    if (!data || data->start == (DWORD)-1 || (data->secSize != 2048 && data->secSize != 2336 && data->secSize != 2352))
        return 0;
    im->f = CreateFileA(data->file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (im->f == INVALID_HANDLE_VALUE) return 0;
    im->secSize = data->secSize;
    im->base = (LONGLONG)data->start * im->secSize;
    im->dataOff = im->secSize == 2048 ? 0 : im->secSize == 2336 ? 8 : 16;
    if (im->secSize == 2352) {                       /* raw: the header's mode byte decides */
        LARGE_INTEGER pos;
        pos.QuadPart = im->base + 16LL * 2352;
        if (SetFilePointerEx(im->f, pos, NULL, FILE_BEGIN) && ReadFile(im->f, hdr, 16, &got, NULL) && got == 16 &&
            hdr[15] == 2)
            im->dataOff = 24;
    }
    if (!iso_read(im, 16, 1, pvd) || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5)) {
        CloseHandle(im->f);
        return 0;
    }
    memset(root, 0, sizeof(*root));
    memcpy(&root->lba, pvd + 156 + 2, 4);
    memcpy(&root->size, pvd + 156 + 10, 4);
    root->dir = 1;
    return 1;
}

/* the entry called name in folder dir or a folder below it (depth: how many levels down still) */
static int iso_find(Iso *im, const IsoEntry *dir, const char *name, IsoEntry *found, int depth)
{
    static u8 sec[2048];
    IsoEntry sub[64];
    int nsub = 0, i;
    DWORD off;
    for (off = 0; off < dir->size; off += 2048) {
        int p = 0;
        if (!iso_read(im, dir->lba + off / 2048, 1, sec)) return 0;
        while (p + 33 < 2048 && sec[p]) {
            int len = sec[p], nl = sec[p + 32];
            IsoEntry e;
            char *q;
            if (len < 34 || p + len > 2048 || 33 + nl > len) break;
            if (!(nl == 1 && sec[p + 33] <= 1)) {           /* not "." or ".." */
                memcpy(&e.lba, sec + p + 2, 4);
                memcpy(&e.size, sec + p + 10, 4);
                e.dir = (sec[p + 25] & 2) != 0;
                if (nl > 63) nl = 63;
                memcpy(e.name, sec + p + 33, nl);
                e.name[nl] = 0;
                if ((q = strchr(e.name, ';'))) *q = 0;     /* "FLINK.PRG;1" */
                if (*e.name && e.name[strlen(e.name) - 1] == '.') e.name[strlen(e.name) - 1] = 0;
                if (!e.dir && !_stricmp(e.name, name)) { *found = e; return 1; }
                if (e.dir && nsub < 64) sub[nsub++] = e;
            }
            p += len;
        }
    }
    for (i = 0; i < nsub && depth > 0; i++)
        if (iso_find(im, &sub[i], name, found, depth - 1)) return 1;
    return 0;
}

static u8 *iso_file(const CueTrack *t, int n, const char *name, DWORD *size)
{
    Iso im;
    IsoEntry root, e;
    u8 *out = NULL;
    DWORD lba, done;
    if (!iso_open(&im, t, n, &root)) return NULL;
    if (iso_find(&im, &root, name, &e, 4)) {
        static u8 chunk[2048 * ISO_CHUNK];
        out = xalloc(e.size);
        for (lba = e.lba, done = 0; done < e.size; lba += ISO_CHUNK) {
            DWORD k = e.size - done < sizeof(chunk) ? e.size - done : sizeof(chunk);
            if (!iso_read(&im, lba, (int)((k + 2047) / 2048), chunk)) { free(out); out = NULL; break; }
            memcpy(out + done, chunk, k);
            done += k;
        }
        if (out) *size = e.size;
    }
    CloseHandle(im.f);
    return out;
}

/* ---- finding the image ---- */
static int try_cue(Disc *d, const char *cue)
{
    DWORD size;
    u8 *prg;
    d->n = parse_cue(cue, d->t);
    if (!d->n) return 0;
    prg = iso_file(d->t, d->n, "FLINK.PRG", &size);
    if (!prg) return 0;
    free(prg);
    snprintf(d->cue, sizeof(d->cue), "%s", cue);
    return 1;
}

static int try_folder(Disc *d, const char *dir)
{
    char pat[MAX_PATH], cue[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int ok = 0;
    snprintf(pat, sizeof(pat), "%s\\*.cue", dir);
    if ((h = FindFirstFileA(pat, &fd)) == INVALID_HANDLE_VALUE) return 0;
    do {
        snprintf(cue, sizeof(cue), "%s\\%s", dir, fd.cFileName);
        ok = try_cue(d, cue);
    } while (!ok && FindNextFileA(h, &fd));
    FindClose(h);
    return ok;
}

void disc_open(Disc *d)
{
    char pat[MAX_PATH], sub[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    memset(d, 0, sizeof(*d));
    if (try_folder(d, g_dir)) return;
    snprintf(pat, sizeof(pat), "%s\\*", g_dir);
    if ((h = FindFirstFileA(pat, &fd)) != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
            snprintf(sub, sizeof(sub), "%s\\%s", g_dir, fd.cFileName);
            if (try_folder(d, sub)) { FindClose(h); return; }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    die("no Flink disc image was found.\n\nPut the disc image of The Misadventures of Flink (Mega-CD; its .cue and "
        ".bin files) in\n%s\nor in a folder inside it, and run FLINK.EXE again.", g_dir);
}

u8 *disc_read_file(Disc *d, const char *name, DWORD *size)
{
    return iso_file(d->t, d->n, name, size);
}
