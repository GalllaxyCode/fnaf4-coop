#include "screamers.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    wchar_t out[256];
    bool current;
    int failed = 0;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL at line %d\n", __LINE__); failed++; } } while (0)
    CHECK(screamers_path(L"C:\\A B\\FiveNightsatFreddys4.exe", false, out, sizeof out / sizeof out[0], &current));
    CHECK(current && wcscmp(out, L"C:\\A B\\FiveNightsatFreddys4.blackout.exe") == 0);
    CHECK(screamers_path(L"C:\\A B\\fivenightsatfreddys4.BLACKOUT.EXE", true, out, sizeof out / sizeof out[0], &current));
    CHECK(!current && wcscmp(out, L"C:\\A B\\FiveNightsatFreddys4.exe") == 0);
    CHECK(screamers_path(L"HalloweenEdition.exe", false, out, sizeof out / sizeof out[0], &current));
    CHECK(current && wcscmp(out, L"HalloweenEdition.blackout.exe") == 0);
    CHECK(screamers_path(L"/folder/HalloweenEdition.blackout.exe", true, out, sizeof out / sizeof out[0], &current));
    CHECK(!current && wcscmp(out, L"/folder/HalloweenEdition.exe") == 0);
    CHECK(screamers_path(L"/\u0418\u0433\u0440\u044b/FiveNightsatFreddys4.exe", false, out, sizeof out / sizeof out[0], &current));
    CHECK(current && wcscmp(out, L"/\u0418\u0433\u0440\u044b/FiveNightsatFreddys4.blackout.exe") == 0);
    CHECK(!screamers_path(L"/folder/unknown.exe", false, out, sizeof out / sizeof out[0], &current));
    CHECK(!screamers_path(L"HalloweenEdition.exe", false, out, 4, &current));
    if (!failed) puts("12 screamers path checks, 0 failures");
    return failed != 0;
}
