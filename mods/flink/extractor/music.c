/* music.c - Flink's CD audio tracks as WAV files (MUSIC\TRACKnnn.WAV).

   Each audio track t of the disc (2-18 on Flink's) becomes TRACK<MUSIC_BASE + t>.WAV (TRACK102 ...
   TRACK118), 44.1 kHz 16-bit stereo, copied sample for sample from the raw 2352-byte sectors. Lomax's
   own tracks are 2-26, so the two sets don't collide. A track runs from its INDEX 01 (after the 2 s
   pregap) to the next track's INDEX 01 in the same file, or to the end of its file: what the game's
   "play cdaudio from n to n+1" plays.

   Lomax plays music by disc track number (Music trigger param n: CD_PlayTrack(n) plays track n + 1),
   and Lomax+ answers the game's CD audio commands from MUSIC\TRACKnn.* files, then from the running
   mod's MODS\<name>\MUSIC. The converter gives each converted map a Music trigger with param
   MUSIC_BASE + t - 1 for the Flink track t of the map's Music actor, so Lomax+ plays TRACK1tt.
   Files already there are kept (delete them to extract again); a track is written through
   TRACKnnn.WAV.tmp, so an interrupted one is extracted again. */
#include "extractor.h"
#include <stdio.h>
#include <string.h>

#define SECTOR 2352
#define MUSIC_BASE 100        /* Flink disc track t -> Lomax track MUSIC_BASE + t (the converter's MUSIC_BASE) */

static void wav_header(u8 *h, DWORD n)
{
    memcpy(h, "RIFF", 4); put_le32(h + 4, 36 + n); memcpy(h + 8, "WAVEfmt ", 8);
    put_le32(h + 16, 16); put_le16(h + 20, 1); put_le16(h + 22, 2);            /* PCM, stereo */
    put_le32(h + 24, 44100); put_le32(h + 28, 44100 * 4); put_le16(h + 32, 4); put_le16(h + 34, 16);
    memcpy(h + 36, "data", 4); put_le32(h + 40, n);
}

static void copy_track(const char *src, LONGLONG from, DWORD n, const char *out)
{
    static u8 buf[SECTOR * 75 * 4];
    char tmp[MAX_PATH];
    u8 hdr[44];
    DWORD left = n, got, put;
    LARGE_INTEGER pos;
    HANDLE in = CreateFileA(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL), o;
    if (in == INVALID_HANDLE_VALUE) die("can't read %s", src);
    snprintf(tmp, sizeof(tmp), "%s.tmp", out);
    o = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (o == INVALID_HANDLE_VALUE) die("can't write %s", tmp);
    wav_header(hdr, n);
    pos.QuadPart = from;
    if (!WriteFile(o, hdr, sizeof(hdr), &put, NULL) || put != sizeof(hdr) || !SetFilePointerEx(in, pos, NULL, FILE_BEGIN))
        die("can't write %s", tmp);
    while (left) {
        DWORD want = left < sizeof(buf) ? left : sizeof(buf);
        if (!ReadFile(in, buf, want, &got, NULL) || !got) die("%s ends early", src);
        if (!WriteFile(o, buf, got, &put, NULL) || put != got) die("can't write %s", tmp);
        left -= got;
    }
    CloseHandle(in);
    CloseHandle(o);
    if (!MoveFileExA(tmp, out, MOVEFILE_REPLACE_EXISTING)) die("can't write %s", out);
}

void music_extract(const Disc *d, const char *outDir)
{
    int i, done = 0, kept = 0;
    make_dir(outDir);
    for (i = 0; i < d->n; i++) {
        const CueTrack *t = &d->t[i], *nxt = i + 1 < d->n ? &d->t[i + 1] : NULL;
        char out[MAX_PATH];
        LONGLONG end, from;
        DWORD n;
        if (!t->audio) continue;
        snprintf(out, sizeof(out), "%s\\TRACK%03d.WAV", outDir, MUSIC_BASE + t->num);
        if (file_exists(out)) { kept++; continue; }
        if (t->start == (DWORD)-1) die("%s: track %d has no INDEX 01", d->cue, t->num);
        from = (LONGLONG)t->start * SECTOR;
        if (nxt && !_stricmp(nxt->file, t->file) && nxt->start != (DWORD)-1) {
            end = (LONGLONG)nxt->start * SECTOR;
        } else {
            WIN32_FILE_ATTRIBUTE_DATA fa;
            if (!GetFileAttributesExA(t->file, GetFileExInfoStandard, &fa))
                die("the disc image's track %d is missing:\n%s", t->num, t->file);
            end = (LONGLONG)fa.nFileSizeHigh << 32 | fa.nFileSizeLow;
        }
        if (end <= from) die("%s: track %d is empty", d->cue, t->num);
        n = (DWORD)(end - from);
        printf("  MUSIC\\TRACK%03d.WAV  track %d, %d:%02d\n", MUSIC_BASE + t->num, t->num,
               (int)(n / 176400 / 60), (int)(n / 176400 % 60));
        fflush(stdout);
        copy_track(t->file, from, n, out);
        done++;
    }
    printf("  %d tracks written", done);
    if (kept) printf(", %d already there (delete them to extract again)", kept);
    printf("\n");
}
