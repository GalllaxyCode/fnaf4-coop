#include "input.h"
#include <windows.h>
#include <string.h>

static SHORT(WINAPI *real_async)(int);
static SHORT(WINAPI *real_state)(int);
static BOOL(WINAPI *real_board)(PBYTE);
static BOOL(WINAPI *real_cursor)(LPPOINT);
static POINT frozen_at;
static bool frozen;
static bool forced[256], bot[256], blocked[256], hooked, muted;

static bool bot_or_real(int vk) { return (vk >= 0 && vk < 256 && bot[vk]) || (real_async && (real_async(vk) & 0x8000)); }

bool key_real(int vk) { return bot_or_real(vk); }
void key_block(int vk, bool on) { if (vk >= 0 && vk < 256) blocked[vk] = on; }
bool key_usable(int vk) { return key_real(vk) && !(vk >= 0 && vk < 256 && blocked[vk]); }
void key_force(int vk, bool down) { if (vk >= 0 && vk < 256) forced[vk] = down; }
void key_bot(int vk, bool down) { if (vk >= 0 && vk < 256) bot[vk] = down; }
bool input_hooked(void) { return hooked; }
void key_mute(bool on) { muted = on; }

static SHORT WINAPI hook_async(int vk) {
    SHORT v = real_async(vk);
    if (muted) return 0;
    if (vk >= 0 && vk < 256 && blocked[vk]) v = 0;
    if (vk >= 0 && vk < 256 && (forced[vk] || (bot[vk] && !blocked[vk]))) v |= (SHORT)0x8000;
    return v;
}

static SHORT WINAPI hook_state(int vk) {
    SHORT v = real_state(vk);
    if (muted) return 0;
    if (vk >= 0 && vk < 256 && blocked[vk]) v = 0;
    if (vk >= 0 && vk < 256 && (forced[vk] || (bot[vk] && !blocked[vk]))) v |= (SHORT)0x8000;
    return v;
}

static BOOL WINAPI hook_board(PBYTE keys) {
    BOOL ok = real_board(keys);
    if (ok && muted) memset(keys, 0, 256);
    else if (ok)
        for (int i = 0; i < 256; i++) {
            if (blocked[i]) keys[i] &= 0x7F;
            if (forced[i] || (bot[i] && !blocked[i])) keys[i] |= 0x80;
        }
    return ok;
}

static bool bot_cursor;

void cursor_set(int x, int y) {
    bot_cursor = true;
    frozen_at.x = x;
    frozen_at.y = y;
}

static BOOL WINAPI hook_cursor(LPPOINT at) {
    if (frozen || bot_cursor) {
        *at = frozen_at;
        return TRUE;
    }
    return real_cursor(at);
}

void cursor_freeze(bool on) {
    if (on && !frozen && !bot_cursor && real_cursor) real_cursor(&frozen_at);
    frozen = on;
}

static void patch(void **slot, void *fn) {
    DWORD old;
    if (VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old)) {
        *slot = fn;
        VirtualProtect(slot, sizeof *slot, old, &old);
    }
}

void input_install(void) {
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    HMODULE user32 = GetModuleHandleA("user32.dll");
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    if (!user32) return;
    real_async = (SHORT(WINAPI *)(int))GetProcAddress(user32, "GetAsyncKeyState");
    real_state = (SHORT(WINAPI *)(int))GetProcAddress(user32, "GetKeyState");
    real_board = (BOOL(WINAPI *)(PBYTE))GetProcAddress(user32, "GetKeyboardState");
    real_cursor = (BOOL(WINAPI *)(LPPOINT))GetProcAddress(user32, "GetCursorPos");
    for (; d->Name; d++) {
        if (_stricmp((char *)(base + d->Name), "user32.dll") != 0) continue;
        void **slot = (void **)(base + d->FirstThunk);
        for (; *slot; slot++) {
            if (*slot == (void *)real_async) { patch(slot, (void *)hook_async); hooked = true; }
            else if (*slot == (void *)real_state) { patch(slot, (void *)hook_state); hooked = true; }
            else if (*slot == (void *)real_board) { patch(slot, (void *)hook_board); hooked = true; }
            else if (*slot == (void *)real_cursor) patch(slot, (void *)hook_cursor);
        }
    }
}
