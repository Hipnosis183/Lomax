/* xpk.c - XPK/NUKE unpacking, as Flink's own decoder does it.

   Flink's world files pack most chunk payloads (ANIM, HEAD, TTAB, MAP, CELL) with the Amiga XPK
   library's NUKE packer (Flink's decompilation.md 3.2). This is the game's decoder, step for step:
   Xpk_Unpack (0x359F8) / Xpk_UnpackCb (0x359FC) walk the chunks, Nuke_DecodeChunk (0x35A36) sets up
   and Nuke_DecodeCore (0x35AC4, with the code at 0x35A62-0x35B44) decodes one chunk. Sub-CPU
   addresses; FLINK.PRG is loaded at 0x62FA.

   The stream:
     "XPKF" u32 packed length, "NUKE" u32 unpacked length (+0xC), 16 bytes (the first unpacked
     ones), 4 bytes (flags, header checksum, versions); then chunks of
     u8 type, u8 header checksum, u16 checksum, u16 packed length, u16 unpacked length, data.
   Xpk_UnpackCb starts at +0x28 (the first chunk's two lengths) and, after each chunk, skips the
   next chunk's first 4 bytes, until the unpacked lengths reach the header's.

   A chunk's data is read from both ends: from the front (a5) 16-bit words for two bit buffers
   (d2: the next step, d3: short counts, both MSB first, each refilled on its own when empty),
   32-bit longs giving 8 nibbles each (d4, low nibble first: a match's kind and long counts) and
   16-bit words for the match distances' bits (d6, d7 bits held); from the back (a4) the literal
   bytes, one by one, going down. All of them come from the front stream in the order the decoder
   asks for them, so the reads below follow the decoder's order exactly.

   The decoder:
     loop: bit 1 -> a match;
           bit 0, bit 1 -> one literal, then (if not at the end) a match;
           bit 0, bit 0 -> literals: 2 bits n (d3): n = 0: 3 literals and 2 bits more; 1: 4, 2: 3,
                           3: 2 literals; then (if not at the end) a match.
     a match: a nibble k; distance = NUKE_BITS[k] bits (d6) + NUKE_BASE[k]; length 2 (k 0-3),
              3 (k 4-9), else 3 + (2 bits n: 1: 3, 2: 2, 3: 1, 0: 3 + nibbles m: m = 0: 15 and
              another nibble, else 16 - m); copied byte by byte from distance bytes back; then the
              loop again (the end is tested only after literals).
   The roxr.l #4 that takes d4's nibbles rotates through the X flag, but the bits X brings in are
   never read before the next long is loaded: the 8 nibbles are the long's bits 0-3, 4-7, ... 28-31. */
#include "extractor.h"
#include <stdlib.h>
#include <string.h>

/* the table at 0x35BAE (a6), by nibble: bits of the distance (+0), its base (+0x20) and the jump
   into the copy code (+0x40: 0x20 a 2-byte match, 0x1E a 3-byte one, 0 a longer one) */
static const unsigned char NUKE_BITS[16] = { 4, 6, 8, 9, 4, 7, 9, 11, 13, 14, 5, 7, 9, 11, 13, 14 };
static const unsigned short NUKE_BASE[16] = {
    0x0000, 0x0010, 0x0050, 0x0150, 0x0000, 0x0010, 0x0090, 0x0290,
    0x0A90, 0x2A90, 0x0000, 0x0020, 0x00A0, 0x02A0, 0x0AA0, 0x2AA0 };

typedef struct {
    const u8 *front, *back, *lo, *hi, *srcEnd; /* a5 going up, a4 going down, the chunk's bounds, the stream's end */
    u8 *out, *dst, *end, *outEnd;            /* a0, a2 */
    unsigned d2, d3, d4, d5, d6, d7;
} Nuke;

/* the front stream's next n bytes, big-endian; past the stream's end the game reads whatever
   follows it in memory, here zeros */
static unsigned front(Nuke *z, int n)
{
    unsigned v = 0;
    while (n--) {
        v = v << 8 | (z->front < z->srcEnd ? *z->front : 0);
        z->front++;
    }
    return v;
}
#define front16(z) front(z, 2)

/* add.w dN,dN; bne.b +; move.w (a5)+,dN; addx.w dN,dN: the next bit of a 16-bit buffer (a set bit
   below the data marks its end) */
static int next_bit(Nuke *z, unsigned *buf)
{
    int bit = (*buf >> 15) & 1;
    *buf = (*buf << 1) & 0xFFFF;
    if (*buf) return bit;
    *buf = front16(z);                       /* (X is the marker shifted out: 1) */
    bit = (*buf >> 15) & 1;
    *buf = ((*buf << 1) | 1) & 0xFFFF;
    return bit;
}

/* two bits of d3 (0x35AE6 / 0x35AA6): the first refills, the second doesn't (d3 always holds an
   even number of bits) */
static int two_bits(Nuke *z)
{
    int n = next_bit(z, &z->d3);
    n = n << 1 | ((z->d3 >> 15) & 1);
    z->d3 = (z->d3 << 1) & 0xFFFF;
    return n;
}

/* dbra d5; moveq #7,d5; move.l (a5)+,d4 ...: the next nibble (0x35A62 / 0x35B32) */
static int nibble(Nuke *z)
{
    int k;
    if (z->d5 == 0) {
        z->d4 = front(z, 4);
        z->d5 = 8;
    }
    k = z->d4 & 15;
    z->d4 >>= 4;
    z->d5--;
    return k;
}

static void literal(Nuke *z)
{
    if (z->back <= z->lo) die("XPK/NUKE: a chunk's literals run past its start");
    if (z->dst >= z->outEnd) die("XPK/NUKE: unpacks past its length");
    *z->dst++ = *--z->back;
}

static void copy(Nuke *z, const u8 **from, int n)
{
    while (n--) {
        if (*from < z->out || *from >= z->outEnd || z->dst >= z->outEnd) die("XPK/NUKE: a match out of range");
        *z->dst++ = *(*from)++;
    }
}

/* 0x35A62 ...: a match */
static void match(Nuke *z)
{
    int k = nibble(z), n = NUKE_BITS[k], len;
    unsigned dist;
    const u8 *from;
    /* d6 holds d7 bits at the top of its low word; asl.l moves n of them into its high word */
    if ((int)z->d7 >= n) {
        z->d6 <<= n;
        z->d7 -= n;
    } else {
        int short_by = n - (int)z->d7;
        z->d6 <<= n;
        z->d6 |= front16(z) << short_by;
        z->d7 = 16 - short_by;
    }
    dist = ((z->d6 >> 16) + NUKE_BASE[k]) & 0xFFFF;   /* swap d6; add.w; suba.w (signed) */
    z->d6 &= 0xFFFF;                                  /* clr.w d6; swap d6 */
    from = z->dst - (short)dist;
    if (k < 4) len = 2;
    else if (k < 10) len = 3;
    else {
        int m = two_bits(z);
        len = 3 + (m == 1 ? 3 : m == 2 ? 2 : m == 3 ? 1 : 3);
        if (m == 0) {
            for (;;) {
                int q = nibble(z);
                if (q) { len += 16 - q; break; }
                len += 15;
            }
        }
    }
    copy(z, &from, len);
}

static void nuke_chunk(Nuke *z)
{
    z->d2 = z->d3 = 0x8000;
    z->d4 = z->d5 = z->d6 = z->d7 = 0;
    for (;;) {                                         /* 0x35AC4 */
        if (next_bit(z, &z->d2)) { match(z); continue; }
        if (next_bit(z, &z->d2)) literal(z);
        else {
            int n;
            while ((n = two_bits(z)) == 0) { literal(z); literal(z); literal(z); }
            n = n == 1 ? 4 : n == 2 ? 3 : 2;
            while (n--) literal(z);
        }
        if (z->dst >= z->end) return;
        match(z);
    }
}

u8 *xpk_unpack(const u8 *blob, size_t n, size_t *outLen)
{
    Nuke z;
    const u8 *a1;
    long left;
    size_t ulen;
    if (n < 0x2C || memcmp(blob, "XPKF", 4) || memcmp(blob + 8, "NUKE", 4)) die("XPK: not an XPKF/NUKE stream");
    ulen = be32(blob + 12);
    memset(&z, 0, sizeof(z));
    z.out = z.dst = xalloc(ulen + 0x10000);           /* (a match may run past a chunk's end) */
    z.outEnd = z.out + ulen + 0x10000;
    z.srcEnd = blob + n;
    a1 = blob + 0x28;
    left = (long)ulen;
    do {
        unsigned plen, uplen;
        if (a1 + 4 > blob + n) die("XPK: the chunks run past the stream's end");
        plen = be16(a1);
        uplen = be16(a1 + 2);
        a1 += 4;
        z.lo = a1;
        z.hi = z.back = a1 + (short)plen;
        if (z.hi < a1 || z.hi > blob + n) die("XPK: a chunk runs past the stream's end");
        z.front = a1;
        z.end = z.dst + (short)uplen;
        left -= uplen;
        nuke_chunk(&z);
        z.dst = z.end;                                 /* movem.l (a7)+,a0-a1: a0 = the chunk's end */
        a1 = z.hi + 4;
    } while (left > 0);
    *outLen = ulen;
    return z.out;
}
