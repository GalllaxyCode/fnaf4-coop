/* Tests for bank.c: the inflater against the system zlib, the image decoder on a made-up record, and,
 * when the game is beside this repo, the real image bank against a Python reference. */
#include "bank.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static int failures, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); puts(""); } } while (0)

static void round_trip(const uint8_t *data, size_t n, int level, const char *what) {
    uLongf packed_size = compressBound(n);
    uint8_t *packed = malloc(packed_size), *out = malloc(n + 16);
    compress2(packed, &packed_size, data, n, level);
    int got = bank_inflate(packed, packed_size, out, n + 16);
    CHECK(got == (int)n && memcmp(out, data, n) == 0, "%s at level %d: got %d of %zu", what, level, got, n);
    CHECK(bank_inflate(packed, packed_size, out, n ? n - 1 : 0) == -1 || n == 0, "%s at level %d overran a short buffer", what, level);
    if (packed_size > 8) {
        packed[packed_size / 2] ^= 0x5A;
        int bad = bank_inflate(packed, packed_size, out, n + 16);
        CHECK(bad == -1 || bad >= 0, "corrupt input must not crash"); /* only that it returns */
    }
    free(packed);
    free(out);
}

static void test_inflate(void) {
    size_t n = 200000;
    uint8_t *data = malloc(n);
    srand(7);
    for (size_t i = 0; i < n; i++) data[i] = (uint8_t)rand();
    for (int level = 0; level <= 9; level += 3) round_trip(data, n, level, "random");
    for (size_t i = 0; i < n; i++) data[i] = (uint8_t)((i / 97) % 7 * 31 + (i % 3));
    for (int level = 0; level <= 9; level++) round_trip(data, n, level, "patterned");
    round_trip(data, 1, 6, "one byte");
    round_trip(data, 0, 6, "empty");
    uint8_t junk[4] = {1, 2, 3, 4}, out[4];
    CHECK(bank_inflate(junk, 4, out, 4) == -1, "not a zlib stream");
    free(data);
}

static void put16(uint8_t *p, int v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, int v) { put16(p, v); put16(p + 2, v >> 16); }

static void test_decode(void) {
    /* 3 x 2, odd width: colour rows are padded by a pixel, alpha rows to 4. */
    int w = 3, h = 2, colour = (w + 1) * 3 * h, alpha = (w + 1) * h;
    uint8_t raw[32 + 64];
    memset(raw, 0, sizeof raw);
    put32(raw + 8, colour + alpha);
    put16(raw + 12, w);
    put16(raw + 14, h);
    raw[16] = 4;
    raw[17] = 0x10;
    put16(raw + 20, 1);
    put16(raw + 22, 2);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t *p = raw + 32 + y * (w + 1) * 3 + x * 3;
            p[0] = (uint8_t)(10 + x); /* blue */
            p[1] = (uint8_t)(20 + y); /* green */
            p[2] = 30;                /* red */
            raw[32 + colour + y * (w + 1) + x] = (uint8_t)(x * 100);
        }
    BankImage img;
    CHECK(bank_decode(raw, 32 + colour + alpha, &img), "decode failed");
    CHECK(img.w == 3 && img.h == 2 && img.hot_x == 1 && img.hot_y == 2, "size %dx%d hotspot %d,%d", img.w, img.h, img.hot_x, img.hot_y);
    CHECK(img.argb && img.argb[1 * 3 + 2] == (200u << 24 | 30u << 16 | 21u << 8 | 12u), "pixel 2,1 is %08x", img.argb ? img.argb[5] : 0);
    free(img.argb);
    CHECK(!bank_decode(raw, 40, &img), "a record cut short must fail");
    raw[17] = 0x11;
    CHECK(!bank_decode(raw, sizeof raw, &img), "run-length images are not decoded");
}

static uint32_t sum(const BankImage *img) {
    uint32_t s = 0;
    for (int i = 0; i < img->w * img->h; i++) s += img->argb[i];
    return s;
}

static void test_real_bank(void) {
    const char *exe = "../game/FiveNightsatFreddys4.exe";
    FILE *f = fopen(exe, "rb");
    if (!f) {
        puts("(no game beside the repo, real bank skipped)");
        return;
    }
    fclose(f);
    CHECK(bank_open(exe), "the image bank wasn't found");
    const BankImage *a = bank_image(984), *b = bank_image(991);
    CHECK(a && a->w == 84 && a->h == 155 && a->hot_x == 42 && a->hot_y == 154, "boy standing: %dx%d", a ? a->w : 0, a ? a->h : 0);
    CHECK(a && sum(a) == 0xd74aa19cu, "boy standing pixels %08x", a ? sum(a) : 0);
    CHECK(b && b->w == 83 && b->h == 152 && sum(b) == 0x33b83afbu, "boy walking down: %dx%d %08x", b ? b->w : 0, b ? b->h : 0, b ? sum(b) : 0);
    CHECK(bank_image(984) == a, "an image was decoded twice");
    CHECK(bank_image(99999) == NULL, "a missing handle");
}

int main(void) {
    test_inflate();
    test_decode();
    test_real_bank();
    printf("%d bank checks, %d failures\n", checks, failures);
    return failures != 0;
}
