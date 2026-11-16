/*
 * password.c - the shortcuts taken (levels.c, g_shortcuts) in Lomax's password (README "Spells",
 * Shortcuts).
 *
 * Lomax's password is 16 bits, 8 gems of 2 bits (Map_DrawPasswordGems 0x4243C0 shows it,
 * Password_DecodeAndApply 0x417950 reads it): v = code ^ 0xB7A4 holds a checksum (bits 0-3, (v >> 4
 * ^ v >> 8 ^ v >> 12) & 0xF), the section (4-8), the lives (9-13) and the continues (14-15). No bit
 * is free, so the two shortcut bits go into the checksum's two low bits: the code is XORed with
 * 0xB7A4 ^ g_shortcuts instead of 0xB7A4. A password without shortcuts is Lomax's own, and the
 * checksum keeps its two high bits.
 * - Map_DrawPasswordGems: its key (the immediate of xor cx, 0xB7A4 at 0x424464) follows
 *   g_shortcuts (password_sync, from map_draw in worldmap.c and wherever the flags change).
 * - Password_DecodeAndApply, called by Menu_OnEnterPassword at 0x40E703 after the dialog: the
 *   checksum's difference from the one the bits give is the shortcuts (0..3; more: a bad password).
 *   Its key (0x417968) is set to 0xB7A4 ^ that for the call, so the game takes it as a valid code,
 *   and its call of Game_ResetNewGame (0x417A43), made only when the player confirms it, then sets
 *   g_shortcuts.
 */
#include "flink.h"

#define DRAW_XOR           0x00424464u   /* Map_DrawPasswordGems: xor cx, 0xB7A4 */
#define DECODE_XOR         0x00417968u   /* Password_DecodeAndApply: xor cx, 0xB7A4 */
#define CALL_DECODE        0x0040E703u   /* Menu_OnEnterPassword: call Password_DecodeAndApply */
#define CALL_RESET         0x00417A43u   /* Password_DecodeAndApply: call Game_ResetNewGame (confirmed) */
#define PASSWORD_KEY       0xB7A4
#define G_PASSWORDCODE     ((volatile WORD *)0x00464418u)
#define Password_DecodeAndApply ((void (__cdecl *)(void))0x00417950u)
#define Game_ResetNewGame  ((void (__cdecl *)(void))0x0040EDC0u)

static const BYTE XOR_RETAIL[5] = { 0x66, 0x81, 0xF1, 0xA4, 0xB7 };
static int g_drawKey = PASSWORD_KEY;     /* the key in Map_DrawPasswordGems now */
static int g_entered;                    /* the shortcuts in the password being entered */

static void set_key(DWORD at, int key)
{
    WORD k = (WORD)key;
    patch(at + 3, &k, 2);
}

/* Map_DrawPasswordGems's key: the shortcuts taken */
void password_sync(void)
{
    int key = PASSWORD_KEY ^ (g_shortcuts & 3);
    if (key != g_drawKey) { set_key(DRAW_XOR, key); g_drawKey = key; }
}

/* Game_ResetNewGame in Password_DecodeAndApply: the password was confirmed */
static void __cdecl password_reset(void)
{
    Game_ResetNewGame();
    g_shortcuts = g_entered;
    password_sync();
    level_map_from_clear();
    flog("password: level %d, shortcuts %d", (int)(((*G_PASSWORDCODE ^ (PASSWORD_KEY ^ g_entered)) >> 4) & 0x1F) + 1, g_entered);
}

static void __cdecl password_decode(void)
{
    unsigned v = *G_PASSWORDCODE ^ PASSWORD_KEY, x = ((v >> 4 ^ v >> 8 ^ v >> 12) ^ v) & 0xF;
    g_entered = x <= 3 ? (int)x : 0;
    if (g_entered) set_key(DECODE_XOR, PASSWORD_KEY ^ g_entered);
    Password_DecodeAndApply();
    if (g_entered) set_key(DECODE_XOR, PASSWORD_KEY);
}

int password_retail(void)
{
    return is_bytes(DRAW_XOR, XOR_RETAIL, 5) && is_bytes(DECODE_XOR, XOR_RETAIL, 5) &&
           is_call(CALL_DECODE, (DWORD)Password_DecodeAndApply) && is_call(CALL_RESET, (DWORD)Game_ResetNewGame);
}

void password_install(void)
{
    patch_call(CALL_DECODE, (const void *)password_decode);
    patch_call(CALL_RESET, (const void *)password_reset);
}
