#include "screamers.h"
#include <string.h>
#include <wchar.h>

static bool same(const wchar_t *a, const wchar_t *b) {
    while (*a && *b) {
        wchar_t x = *a++, y = *b++;
        if (x >= L'A' && x <= L'Z') x += L'a' - L'A';
        if (y >= L'A' && y <= L'Z') y += L'a' - L'A';
        if (x != y) return false;
    }
    return *a == *b;
}

bool screamers_path(const wchar_t *current, bool enable, wchar_t *out, size_t capacity, bool *current_enabled) {
    static const wchar_t *const original[] = {L"FiveNightsatFreddys4.exe", L"HalloweenEdition.exe"};
    static const wchar_t *const blackout[] = {L"FiveNightsatFreddys4.blackout.exe", L"HalloweenEdition.blackout.exe"};
    if (!current || !out || !capacity || !current_enabled) return false;
    const wchar_t *name = current;
    for (const wchar_t *p = current; *p; p++) if (*p == L'/' || *p == L'\\') name = p + 1;
    for (size_t i = 0; i < sizeof original / sizeof original[0]; i++) {
        if (!same(name, original[i]) && !same(name, blackout[i])) continue;
        *current_enabled = same(name, original[i]);
        size_t dir = (size_t)(name - current);
        const wchar_t *target = enable ? original[i] : blackout[i];
        if (dir + wcslen(target) + 1 > capacity) return false;
        wmemcpy(out, current, dir);
        wcscpy(out + dir, target);
        return true;
    }
    return false;
}
