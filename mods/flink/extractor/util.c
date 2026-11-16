/* util.c - errors, memory, big- and little-endian words, files. */
#include "extractor.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char g_dir[MAX_PATH];

void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("Error: ", stdout);
    vprintf(fmt, ap);
    va_end(ap);
    fputs("\n", stdout);
    fflush(stdout);
    exit(1);
}

void *xalloc(size_t n)
{
    void *p = calloc(1, n ? n : 1);
    if (!p) die("out of memory (%u bytes)", (unsigned)n);
    return p;
}

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p) die("out of memory (%u bytes)", (unsigned)n);
    return p;
}

unsigned be16(const u8 *p) { return (unsigned)p[0] << 8 | p[1]; }
unsigned be32(const u8 *p) { return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3]; }
void put_le16(u8 *p, unsigned v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
void put_le32(u8 *p, unsigned v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

unsigned crc32(const u8 *p, size_t n)
{
    unsigned c = 0xFFFFFFFFu;
    size_t i;
    int k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320u & -(c & 1));
    }
    return ~c;
}

void make_dir(const char *path)
{
    if (!CreateDirectoryA(path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        die("can't make the folder %s", path);
}

int file_exists(const char *path)
{
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

void write_file(const char *path, const void *data, size_t n)
{
    char tmp[MAX_PATH];
    DWORD put = 0;
    HANDLE f;
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    f = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) die("can't write %s", tmp);
    if (!WriteFile(f, data, (DWORD)n, &put, NULL) || put != n) { CloseHandle(f); DeleteFileA(tmp); die("can't write %s", tmp); }
    CloseHandle(f);
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) { DeleteFileA(tmp); die("can't write %s", path); }
}

void buf_add(Buf *b, const void *data, size_t n)
{
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2 + 256;
        b->p = xrealloc(b->p, b->cap);
    }
    if (data) memcpy(b->p + b->n, data, n);
    else memset(b->p + b->n, 0, n);
    b->n += n;
}

void buf_free(Buf *b)
{
    free(b->p);
    b->p = NULL;
    b->n = b->cap = 0;
}
