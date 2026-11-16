/* main.c - FLINK.EXE: makes the Flink mod's data from the Flink disc image.

   It takes no parameters. It sits in the game's MODS\FLINK, next to FLINK.DLL, with the disc image
   of The Misadventures of Flink (Mega-CD, Europe: its .cue and .bin files, in that folder or in a
   folder inside it). It writes into its own folder, which is where the mod reads them:
     SFX\E00.WAV ... E46.WAV          Flink's PCM sound effects (sfx.c)
     MUSIC\TRACK102.WAV ... 118.WAV   Flink's CD audio tracks (music.c; kept when already there)
   It copies nothing anywhere else. The game folder is two levels up (..\..): the converter's stage
   reads Lomax's own worlds there (WORLDS\MEDIEVAL.WLD, MAIN.WLD, MAP.WLD, TITLE.WLD), never writing
   to them.

   Flink's files come only from the image's data track (disc.c). The converter's addresses are those
   of the European disc's FLINK.PRG, so any other FLINK.PRG is refused (PRG_SIZE, PRG_CRC). */
#include "convert.h"
#include <conio.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PRG_SIZE 334180               /* the European disc's FLINK.PRG */
#define PRG_CRC 0x7B35BDA7u

/* started from Explorer (the console is its own): keeps the window open until a key is pressed */
static void wait_key(void)
{
    DWORD ids[2];
    if (GetConsoleProcessList(ids, 2) == 1) {
        printf("\nPress a key to close this window.\n");
        _getch();
    }
}

static void finish(void)
{
    fflush(stdout);
    wait_key();
}

int main(void)
{
    char out[MAX_PATH], *p;
    Disc disc;
    u8 *prg;
    DWORD prgSize = 0;

    /* every double operation rounded to 53 bits, as IEEE doubles: the sound effects' arithmetic
       (sfx.c) and the converter's are the same on every computer (the 32-bit build uses the x87 unit, not SSE2,
       for the processors Windows XP runs on) */
    _controlfp(_PC_53, _MCW_PC);
    atexit(finish);
    setvbuf(stdout, NULL, _IONBF, 0);

    GetModuleFileNameA(NULL, g_dir, sizeof(g_dir));
    if ((p = strrchr(g_dir, '\\'))) *p = 0;
    printf("Flink extractor: makes the Flink mod's files from the Flink disc image.\n\n");

    disc_open(&disc);
    printf("Disc image: %s\n", disc.cue);
    prg = disc_read_file(&disc, "FLINK.PRG", &prgSize);
    if (!prg) die("%s: FLINK.PRG can't be read", disc.cue);
    if (prgSize != PRG_SIZE || crc32(prg, prgSize) != PRG_CRC)
        die("%s: its FLINK.PRG (%u bytes, CRC32 %08X) is not the European disc's (%u bytes, CRC32 %08X).",
            disc.cue, (unsigned)prgSize, crc32(prg, prgSize), PRG_SIZE, PRG_CRC);

    printf("\nSound effects:\n");
    snprintf(out, sizeof(out), "%s\\SFX", g_dir);
    sfx_extract(prg, prgSize, out);

    printf("\nLevels:\n");
    g_flinkPrg = prg;
    g_flinkPrgSize = prgSize;
    {
        char game[MAX_PATH], full[MAX_PATH];
        snprintf(game, sizeof(game), "%s\\..\\..", g_dir);
        if (!GetFullPathNameA(game, sizeof(full), full, NULL)) snprintf(full, sizeof(full), "%s", game);
        converter_init(&disc, full);
    }
    snprintf(out, sizeof(out), "%s\\WORLDS", g_dir);
    convert_levels(out);

    printf("\nMusic:\n");
    snprintf(out, sizeof(out), "%s\\MUSIC", g_dir);
    music_extract(&disc, out);

    printf("\nDone. The files are in %s.\n", g_dir);
    free(prg);
    return 0;
}
