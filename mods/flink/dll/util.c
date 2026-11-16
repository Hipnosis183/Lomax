/*
 * util.c - FLINK.LOG and patching the game's code and data in memory.
 */
#include <stdarg.h>
#include "flink.h"

char g_dir[MAX_PATH];
int g_log;

/* FLINK.LOG is written while Lomax+'s log switch is on: the DWORD HKCU\Software\Psygnosis\Lomax\
   Extra\Log, which its Properties > Extra page sets (only read here; no value, no log). RegGetValue
   isn't on Windows XP, so RegOpenKeyEx and RegQueryValueEx. */
void log_init(void)
{
    HKEY k;
    DWORD d = 0, n = sizeof(d), type = 0;
    g_log = 0;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Psygnosis\\Lomax\\Extra", 0, KEY_QUERY_VALUE, &k)) return;
    if (!RegQueryValueExA(k, "Log", NULL, &type, (BYTE *)&d, &n) && type == REG_DWORD) g_log = d != 0;
    RegCloseKey(k);
}

/* a line in FLINK.LOG next to the DLL, with the log on (log_init) */
void flog(const char *fmt, ...)
{
    char path[MAX_PATH];
    va_list ap;
    FILE *f;
    if (!g_log) return;
    snprintf(path, sizeof(path), "%s\\FLINK.LOG", g_dir);
    if (!(f = fopen(path, "a"))) return;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

void patch(DWORD addr, const void *bytes, size_t n)
{
    DWORD old;
    VirtualProtect((void *)addr, n, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)addr, bytes, n);
    VirtualProtect((void *)addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)addr, n);
}

void patch_dword(DWORD addr, DWORD value) { patch(addr, &value, 4); }

/* a call rel32 at `at` to target */
void patch_call(DWORD at, const void *target)
{
    BYTE call[5] = { 0xE8 };
    DWORD rel = (DWORD)target - (at + 5);
    memcpy(call + 1, &rel, 4);
    patch(at, call, 5);
}

/* there is a call rel32 to target at `at` */
int is_call(DWORD at, DWORD target)
{
    return *(BYTE *)at == 0xE8 && *(DWORD *)(at + 1) == target - (at + 5);
}

int is_bytes(DWORD at, const void *bytes, size_t n) { return !memcmp((const void *)at, bytes, n); }
