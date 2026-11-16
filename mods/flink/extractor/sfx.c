/* sfx.c - Flink's PCM sound effects as WAV files (SFX\Enn.WAV) for FLINK.DLL.

   Flink's sound effects are played by the sub CPU's RF5C164 PCM driver (0x3F660-0x3FEBE; all of it
   is in ..\..\Flink\re\pcm_sound.md). Its 18 samples are a chain of IFF 8SVX files at 0x44268, and
   each of its 47 effects is a script (PcmEffectTable 0x3ED5A) that picks samples, notes (Amiga
   periods), volumes and slides, run at 60 ticks a second. The game asks for a sound by ID
   (Sound_PlayEffect 0x358AA, IDs 0x17-0x53), and SoundIdTable (0x358FC) turns the ID into an effect
   (>= 0) or a main-CPU FM sound (< 0, not here). Addresses are sub-CPU addresses: FLINK.PRG is
   loaded at 0x62FA.

   Each effect n becomes SFX\Enn.WAV (E00 ... E46): the script run as Pcm_UpdateChannel (0x3FB06)
   runs it, the samples stepped as the chip steps them (FD = 0x356C1 / period, 5.11 fixed point at
   12.5 MHz / 384 = 32552 Hz, each sample looping over its whole length), then resampled to 22050 Hz
   16-bit mono with the plain 44-byte header, the only format Lomax's SndDrv_LoadWav (0x4382D0)
   accepts. FLINK.DLL (dll\sfx.c) maps each Flink sound ID to its effect and loads the files it uses.

   Two effects never end in Flink: E41 (sample 9 held at period 0x153, the Wagon's and the pushed
   Rock's rolling, stopped by Pcm_StopEffect) is written as one seamless turn of its sample, for
   Lomax's looping voices; E36 (the final area's, played until Pcm_StopAll) is cut at 4 s.

   The arithmetic is IEEE double throughout, each operation in a fixed order
   (main() sets the x87 unit to 53-bit precision), and round() is round-half-to-even (rint). */
#include "extractor.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOAD 0x62FA                    /* FLINK.PRG's load address on the sub CPU */
#define SAMPLE_CHAIN 0x44268           /* the first FORM/8SVX (Sub_Init 0x143EA: lea $44268,a0 / jsr Pcm_Init) */
#define EFFECT_TABLE 0x3ED5A           /* PcmEffectTable: u32[47] script pointers */
#define EFFECTS 47
#define SAMPLES 18
#define SOUND_IDS 0x358FC              /* SoundIdTable: s16[61], IDs 0x17-0x53 */
#define CHIP_RATE (12500000.0 / 384)   /* the RF5C164's output rate on the Mega-CD */
#define TICK_RATE 60                   /* Pcm_Tick (0x359C8): 60 updates a second on NTSC and PAL */
#define PERIOD_TO_FD 0x356C1           /* FD = 0x356C1 / period (divu.w at 0x3FC42) */
#define MIN_PERIOD 0x78
#define OUT_RATE 22050                 /* SndDrv_LoadWav's only rate (0x5622) */
#define LOOP_EFFECT 41                 /* endless: one turn of its sample */
#define LOOP_PERIOD 0x153
#define CUT_SECONDS 4.0                /* E36 */
#define KERNEL_HALF 12

static const u8 *g_prg;
static DWORD g_prgSize;

static unsigned u16at(DWORD a)
{
    if (a < LOAD || a - LOAD + 2 > g_prgSize) die("FLINK.PRG: read past its end at 0x%X", (unsigned)a);
    return be16(g_prg + a - LOAD);
}
static unsigned u32at(DWORD a)
{
    if (a < LOAD || a - LOAD + 4 > g_prgSize) die("FLINK.PRG: read past its end at 0x%X", (unsigned)a);
    return be32(g_prg + a - LOAD);
}
static int s16(unsigned v) { v &= 0xFFFF; return v & 0x8000 ? (int)v - 0x10000 : (int)v; }

/* a growable list of doubles */
typedef struct { double *v; size_t n, cap; } Pcm;
static void pcm_add(Pcm *p, double v)
{
    if (p->n == p->cap) { p->cap = p->cap * 2 + 4096; p->v = xrealloc(p->v, p->cap * sizeof(double)); }
    p->v[p->n++] = v;
}

typedef struct { int *v; int n; } Sample;
static Sample g_samples[SAMPLES];

/* Pcm_CopySample (0x3F76E): signed 8SVX -> sign-magnitude, +127 -> +126 (0xFF is the loop marker),
   -128 -> +0; kept as the signed values the chip plays */
static void chip_values(const u8 *raw, DWORD n, Sample *s)
{
    DWORD i;
    s->v = xalloc(n * sizeof(int));
    s->n = (int)n;
    for (i = 0; i < n; i++) {
        int v = raw[i] & 0x80 ? raw[i] - 256 : raw[i];
        if (v == 127) v = 126;
        if (v == -128) v = 0;
        s->v[i] = v;
    }
}

/* the FORM/8SVX chain as Pcm_Init (0x3F660) reads it -> how many samples */
static int load_samples(void)
{
    DWORD a = SAMPLE_CHAIN;
    int n = 0;
    while (a + 12 - LOAD <= g_prgSize && !memcmp(g_prg + a - LOAD, "FORM", 4)) {
        DWORD size = u32at(a + 4), p = a + 12, end = a + 8 + size;
        while (p < end) {
            DWORD clen = u32at(p + 4);
            if (!memcmp(g_prg + p - LOAD, "BODY", 4)) {
                if (n == SAMPLES) return n + 1;
                if (p + 8 + clen - LOAD > g_prgSize) return n;
                chip_values(g_prg + p + 8 - LOAD, clen, &g_samples[n++]);
            }
            p += 8 + clen + (clen & 1);
        }
        a = end + (size & 1);
    }
    return n;
}

/* one PcmChannel (0x3F9EE + 0x18 n) running an effect's script, and its chip voice */
typedef struct {
    int effect;
    DWORD ptr, loop_ptr;
    int loop_cnt, pslide, vslide, period, vol, vbase, wait, speed;
    const Sample *sample;
    int keyon, fd, env, alive;
    long long addr;
} Channel;

static void channel_init(Channel *c, int effect)
{
    memset(c, 0, sizeof(*c));
    c->effect = effect;
    c->ptr = u32at(EFFECT_TABLE + 4 * effect);
    c->alive = 1;
}

/* Pcm_UpdateChannel (0x3FB06): a row's opcodes when its wait is up, then the commit */
static void channel_tick(Channel *c)
{
    if (c->wait == 0) {
        DWORD a = c->ptr;
        c->wait = c->speed;
        for (;;) {
            unsigned op = u16at(a), v;
            a += 2;
            if (op == 3) { c->vbase = c->vol = (int)u16at(a); a += 2; }
            else if (op == 4) { c->vslide = s16(u16at(a)); a += 2; }
            else if (op == 7) {                                  /* 0x7FFF (random) is used by no script */
                v = u16at(a); a += 2;
                c->pslide = v == 0x7FFF ? 0 : s16(v);
            }
            else if (op == 8) { c->period = (c->period + (int)u16at(a)) & 0xFFFF; a += 2; }
            else if (op == 2) {                                  /* Pcm_OpSelectSample: 1-based */
                int n = (int)u16at(a) - 1;
                a += 2;
                if (c->effect >= 0x2F) n += 13;
                if (n < 0 || n >= SAMPLES) die("FLINK.PRG: effect %d picks sample %d", c->effect, n + 1);
                c->sample = &g_samples[n];
                c->keyon = 1;
            }
            else if (op == 9) { c->speed = c->wait = g_prg[a + 1 - LOAD]; a += 2; }
            else if (op == 0xA) {                                /* one loop level; n + 1 runs, 0xFFFF forever */
                unsigned n = u16at(a);
                a += 2;
                if (n == 0) { c->loop_ptr = a; c->loop_cnt = 0xFF; }
                else if (c->loop_cnt == 0xFF) { c->loop_cnt = n & 0xFF; a = c->loop_ptr; }
                else {
                    c->loop_cnt = (c->loop_cnt - 1) & 0xFF;
                    if (c->loop_cnt) a = c->loop_ptr;
                }
            }
            else if (op == 0xB) { c->alive = 0; return; }
            else if (op == 5) { c->period = (int)u16at(a); a += 2; break; }
            else if (op == 6) { c->period = (int)u16at(a); a += 2; c->vol = c->vbase; break; }
            else if (op == 1) { c->vol = c->vbase; break; }
            else break;                                          /* 0 and unknown opcodes end the row */
        }
        c->ptr = a;
    }
    c->wait = (c->wait - 1) & 0xFF;                              /* the commit (0x3FC1C, Pcm_CommitVoice 0x3FC7C) */
    c->period = s16((unsigned)(c->period + c->pslide));
    if (c->period < MIN_PERIOD) c->period = MIN_PERIOD;
    c->fd = (PERIOD_TO_FD / c->period) & 0xFFFF;
    c->vol = s16((unsigned)(c->vol + c->vslide));
    if (c->vol > 0x3F) c->vol = 0x3F;
    if (c->vol < 0) c->vol = 0;
    c->env = c->vol * 4 < 0xFF ? c->vol * 4 : 0xFF;
    if (c->keyon) { c->keyon = 0; c->addr = 0; }
}

/* n chip samples: the address steps FD / 2048 a sample, back to the start at the marker */
static void channel_render(Channel *c, int n, Pcm *out)
{
    double g;
    int k;
    if (!c->sample) { for (k = 0; k < n; k++) pcm_add(out, 0.0); return; }
    g = c->env / 255.0;
    for (k = 0; k < n; k++) {
        long long i = c->addr >> 11;
        if (i >= c->sample->n) { c->addr = 0; i = 0; }
        pcm_add(out, c->sample->v[i] * g);
        c->addr += c->fd;
    }
}

/* the effect at the chip's rate, until its script ends (or CUT_SECONDS) */
static void render_effect(int n, Pcm *pcm)
{
    Channel ch;
    double carry = 0.0;
    int t;
    channel_init(&ch, n);
    for (t = 0; t < (int)(CUT_SECONDS * TICK_RATE); t++) {
        int k;
        channel_tick(&ch);
        if (!ch.alive) break;
        carry += CHIP_RATE / TICK_RATE;
        k = (int)carry;
        carry -= k;
        channel_render(&ch, k, pcm);
    }
}

/* a Blackman-windowed sinc low pass at fc (cycles a source sample), half-width KERNEL_HALF */
static double kernel(double fc, double x)
{
    const double pi = 3.141592653589793, half = KERNEL_HALF;
    double s, w;
    if (fabs(x) >= half) return 0.0;
    s = x == 0 ? 2 * fc : sin(2 * pi * fc * x) / (pi * x);
    w = 0.42 + 0.5 * cos(pi * x / half) + 0.08 * cos(2 * pi * x / half);
    return s * w;
}

/* n_out samples, src read every `step` source samples through the low pass; cyclic: src repeats */
static void resample(const double *src, long ln, double step, long n_out, int cyclic, Pcm *out)
{
    double fc = 0.45 * (1.0 < 1.0 / step ? 1.0 : 1.0 / step);
    long j, k;
    for (j = 0; j < n_out; j++) {
        double t = j * step, acc = 0.0;
        long i0 = (long)floor(t);
        for (k = i0 - KERNEL_HALF + 1; k < i0 + KERNEL_HALF + 1; k++) {
            double v;
            if (cyclic) v = src[(k % ln + ln) % ln];
            else if (k >= 0 && k < ln) v = src[k];
            else continue;
            acc += v * kernel(fc, t - k);
        }
        pcm_add(out, acc);
    }
}

/* E41: its sample (op 2 n) held at LOOP_PERIOD at its volume: one turn, resampled so that it
   repeats without a seam */
static void loop_effect(Pcm *frames)
{
    DWORD a = u32at(EFFECT_TABLE + 4 * LOOP_EFFECT);
    const Sample *smp = NULL;
    int vol = 0, i;
    double hz, g, *src;
    long n;
    Pcm tmp = { 0 };
    while (!smp || !vol) {
        unsigned op = u16at(a);
        if (op == 2) {
            int s = (int)u16at(a + 2) - 1;
            if (s < 0 || s >= SAMPLES) die("FLINK.PRG: effect %d picks sample %d", LOOP_EFFECT, s + 1);
            smp = &g_samples[s];
        }
        if (op == 3) vol = (int)u16at(a + 2);
        a += op == 0 || op == 1 || op == 0xB ? 2 : 4;
    }
    hz = CHIP_RATE * (PERIOD_TO_FD / LOOP_PERIOD) / 2048;          /* the sample's playback rate */
    n = (long)rint(smp->n * (double)OUT_RATE / hz);
    g = ((vol < 0x3F ? vol : 0x3F) * 4 < 0xFF ? (vol < 0x3F ? vol : 0x3F) * 4 : 0xFF) / 255.0;
    src = xalloc(smp->n * sizeof(double));
    for (i = 0; i < smp->n; i++) src[i] = smp->v[i];
    resample(src, smp->n, (double)smp->n / n, n, 1, &tmp);
    for (i = 0; i < (int)tmp.n; i++) pcm_add(frames, tmp.v[i] * g);
    free(src);
    free(tmp.v);
}

static void write_wav(const char *path, const Pcm *frames)
{
    size_t n = frames->n * 2, i;
    u8 *d = xalloc(44 + n);
    memcpy(d, "RIFF", 4); put_le32(d + 4, (unsigned)(36 + n)); memcpy(d + 8, "WAVEfmt ", 8);
    put_le32(d + 16, 16); put_le16(d + 20, 1); put_le16(d + 22, 1);            /* PCM, mono */
    put_le32(d + 24, OUT_RATE); put_le32(d + 28, OUT_RATE * 2); put_le16(d + 32, 2); put_le16(d + 34, 16);
    memcpy(d + 36, "data", 4); put_le32(d + 40, (unsigned)n);                    /* SndDrv_LoadWav: the data at +0x2C, its size at +0x28 */
    for (i = 0; i < frames->n; i++) {
        double v = rint(frames->v[i] * 256);
        int s = v > 32767 ? 32767 : v < -32768 ? -32768 : (int)v;
        put_le16(d + 44 + i * 2, (unsigned)s);
    }
    write_file(path, d, 44 + n);
    free(d);
}

void sfx_extract(const u8 *prg, DWORD prgSize, const char *outDir)
{
    int n, i;
    g_prg = prg;
    g_prgSize = prgSize;
    if ((n = load_samples()) != SAMPLES)
        die("FLINK.PRG: %d samples at 0x%X, not %d", n, SAMPLE_CHAIN, SAMPLES);
    make_dir(outDir);
    for (n = 0; n < EFFECTS; n++) {
        char path[MAX_PATH], ids[256] = "";
        Pcm frames = { 0 };
        if (n == LOOP_EFFECT) {
            loop_effect(&frames);
        } else {
            Pcm pcm = { 0 };
            render_effect(n, &pcm);
            resample(pcm.v, (long)pcm.n, CHIP_RATE / OUT_RATE, (long)(pcm.n * (double)OUT_RATE / CHIP_RATE), 0, &frames);
            free(pcm.v);
        }
        snprintf(path, sizeof(path), "%s\\E%02d.WAV", outDir, n);
        write_wav(path, &frames);
        for (i = 0; i < 61; i++)
            if (s16(u16at(SOUND_IDS + 2 * i)) == n) snprintf(ids + strlen(ids), sizeof(ids) - strlen(ids), " %02X", 0x17 + i);
        printf("  SFX\\E%02d.WAV %5.2f s  IDs%s\n", n, frames.n / (double)OUT_RATE, *ids ? ids : " -");
        free(frames.v);
    }
    printf("  %d effects written\n", EFFECTS);
}
