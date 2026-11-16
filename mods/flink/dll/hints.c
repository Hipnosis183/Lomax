/*
 * hints.c - Flink's information notes in Lomax's hint box (README "Chests").
 *
 * A chest holding one is a hint hat (object 69) with param HINT_BASE + Flink's message number (see
 * the converter). HintHat_Init keeps it at +0x114; when Lomax lands next to the hint guy,
 * HintGuy_UpdateAppear (0x433B40) allocates the text box, an actor drawn by HintText_Draw (0x433DF0,
 * stored at 0x433B9E) with the index at +0x84, which prints that many lines of the game's text block
 * (table 0x4770B8) at its x/y, 8 px apart (shifted 4 px down for an even count), in a 12 x count/2
 * window of 16 px cells (Ui_DrawWindowFrame); +0x80 set = remove it, +0x88 ticks before it shows.
 * Flink's box is a character wider: a hint with longer lines gets a 13 cells wide window.
 * hint_text_draw does the same for indexes from HINT_BASE with Flink's lines, read from LEVELS.INI's
 * [Hints] (n.L = message n in Lomax language L, g_Language 0x464570: 0 English, 1 French, 2 German,
 * 3 Italian, 4 Spanish; lines separated by '|'), falling back to English. Lomax's own hints are
 * unchanged. hint_box opens the same box with no hint guy (the final boss's message, bos1.c).
 */
#include "flink.h"

#define SET_HINTTEXT_DRAW  0x00433B9Eu   /* HintGuy_UpdateAppear: mov dword [eax+0x1C], HintText_Draw */
static const BYTE hintDrawOrig[7] = { 0xC7, 0x40, 0x1C, 0xF0, 0x3D, 0x43, 0x00 };
#define HINT_BASE          1000
#define MAX_HINTS          64
#define HINT_LINES         20
#define HINT_CHARS         26            /* (Lomax's box holds 24; some of Flink's German lines have 25) */
static struct { int msg, lang, n, w; char line[HINT_LINES][HINT_CHARS + 1]; } g_hints[MAX_HINTS];
static int g_nhints;

void hints_load(const char *ini)
{
    static char buf[32768];
    char *p, *v, *s;
    DWORD n = GetPrivateProfileSectionA("Hints", buf, sizeof(buf), ini);
    for (p = buf; n && *p && g_nhints < MAX_HINTS; p += strlen(p) + 1) {
        int msg, lang, k = 0;
        if (!(v = strchr(p, '=')) || sscanf(p, "%d.%d", &msg, &lang) != 2) continue;
        g_hints[g_nhints].msg = msg;
        g_hints[g_nhints].lang = lang;
        g_hints[g_nhints].w = 12;                /* the box's width in 16 px cells (2 characters) */
        for (s = v + 1; k < HINT_LINES; k++) {
            size_t len = strcspn(s, "|");
            if (len > HINT_CHARS) len = HINT_CHARS;
            snprintf(g_hints[g_nhints].line[k], HINT_CHARS + 1, "%.*s", (int)len, s);
            if ((int)(len + 1) / 2 > g_hints[g_nhints].w) g_hints[g_nhints].w = (int)(len + 1) / 2;
            if (!s[len]) { k++; break; }
            s += len + 1;
        }
        g_hints[g_nhints++].n = k + (k & 1);     /* (an even count, as the box's frame needs) */
    }
    flog("%d hint texts", g_nhints);
}

static void __cdecl hint_text_draw(BYTE *a)
{
    int idx = *(int *)(a + 0x84), t, k, h = -1;
    if (idx < HINT_BASE) { HintText_Draw(a); return; }
    if (*(int *)(a + 0x80)) { Actor_Despawn(a); return; }
    if ((t = *(int *)(a + 0x88)) > 0) *(int *)(a + 0x88) = --t;
    if (t) return;
    for (k = 0; k < g_nhints; k++)
        if (g_hints[k].msg == idx - HINT_BASE && (g_hints[k].lang == *G_LANGUAGE || (h < 0 && !g_hints[k].lang))) h = k;
    if (h < 0) return;
    for (k = 0; k < g_hints[h].n; k++)
        Text_DrawString(a, A_X(a), A_Y(a) + 4 + 8 * k, g_hints[h].line[k]);
    Ui_DrawWindowFrame(a, g_hints[h].w, g_hints[h].n / 2);
}

static int hint_of(int msg)
{
    int k, h = -1;
    for (k = 0; k < g_nhints; k++)
        if (g_hints[k].msg == msg && (g_hints[k].lang == *G_LANGUAGE || (h < 0 && !g_hints[k].lang))) h = k;
    return h;
}

/* HintGuy_UpdateAppear's text box (0x433B7E: Actor_Alloc(NULL, "Cset"), its fields as it sets them)
   with message msg, at x view_w / 2 - its half width (Lomax's 0x40 at 320 for 12 cells), y 0x3C */
BYTE *hint_box(int msg, int view_w)
{
    int h = hint_of(msg);
    BYTE *d = h >= 0 ? Actor_Alloc(NULL, "Cset") : NULL;
    if (!d) return NULL;
    A_DRAW(d) = (void *)hint_text_draw;
    A_FLAGS(d) |= 2;
    A_LAYER(d) = -1;
    A_CULL(d) = CULL_NEVER;
    A_X(d) = view_w / 2 - g_hints[h].w * 8;
    A_Y(d) = 0x3C;
    *(int *)(d + 0x80) = 0;
    *(int *)(d + 0x84) = HINT_BASE + msg;
    *(int *)(d + 0x88) = 5;
    flog("hint box: message %d", msg);
    return d;
}

int hints_retail(void) { return is_bytes(SET_HINTTEXT_DRAW, hintDrawOrig, sizeof(hintDrawOrig)); }

void hints_install(void) { patch_dword(SET_HINTTEXT_DRAW + 3, (DWORD)hint_text_draw); }
