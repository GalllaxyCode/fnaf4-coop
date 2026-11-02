#include "bank.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- inflate (RFC 1950/1951) */

typedef struct {
    const uint8_t *in;
    size_t in_size, in_pos;
    uint32_t bits;
    int bit_count;
    uint8_t *out;
    size_t out_size, out_pos;
    bool bad;
} Inflater;

typedef struct {
    uint16_t counts[16];
    uint16_t symbols[288];
} Huffman;

static int get_bit(Inflater *s) {
    if (s->bit_count == 0) {
        if (s->in_pos >= s->in_size) {
            s->bad = true;
            return 0;
        }
        s->bits = s->in[s->in_pos++];
        s->bit_count = 8;
    }
    int bit = s->bits & 1;
    s->bits >>= 1;
    s->bit_count--;
    return bit;
}

static int get_bits(Inflater *s, int n) {
    int v = 0;
    for (int i = 0; i < n; i++) v |= get_bit(s) << i;
    return v;
}

/* Canonical codes from a list of code lengths. Returns false if they over-subscribe. */
static bool build(Huffman *h, const uint8_t *lengths, int n) {
    uint16_t offsets[16];
    memset(h->counts, 0, sizeof h->counts);
    for (int i = 0; i < n; i++) h->counts[lengths[i]]++;
    h->counts[0] = 0;
    int left = 1;
    for (int len = 1; len < 16; len++) {
        left = left * 2 - h->counts[len];
        if (left < 0) return false;
    }
    offsets[1] = 0;
    for (int len = 1; len < 15; len++) offsets[len + 1] = (uint16_t)(offsets[len] + h->counts[len]);
    for (int i = 0; i < n; i++)
        if (lengths[i]) h->symbols[offsets[lengths[i]]++] = (uint16_t)i;
    return true;
}

static int decode_symbol(Inflater *s, const Huffman *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= get_bit(s);
        int count = h->counts[len];
        if (code - count < first) return h->symbols[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
        if (s->bad) return -1;
    }
    s->bad = true;
    return -1;
}

static const uint16_t len_base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t dist_base[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static bool inflate_codes(Inflater *s, const Huffman *lit, const Huffman *dist) {
    for (;;) {
        int sym = decode_symbol(s, lit);
        if (sym < 0 || s->bad) return false;
        if (sym < 256) {
            if (s->out_pos >= s->out_size) return false;
            s->out[s->out_pos++] = (uint8_t)sym;
        } else if (sym == 256) {
            return true;
        } else {
            sym -= 257;
            if (sym >= 29) return false;
            int length = len_base[sym] + get_bits(s, len_extra[sym]);
            int d = decode_symbol(s, dist);
            if (d < 0 || d >= 30) return false;
            size_t distance = (size_t)(dist_base[d] + get_bits(s, dist_extra[d]));
            if (s->bad || distance > s->out_pos || s->out_pos + (size_t)length > s->out_size) return false;
            for (int i = 0; i < length; i++, s->out_pos++) s->out[s->out_pos] = s->out[s->out_pos - distance];
        }
    }
}

static bool inflate_fixed(Inflater *s) {
    static Huffman lit, dist;
    static bool ready;
    if (!ready) {
        uint8_t lengths[288];
        int i = 0;
        for (; i < 144; i++) lengths[i] = 8;
        for (; i < 256; i++) lengths[i] = 9;
        for (; i < 280; i++) lengths[i] = 7;
        for (; i < 288; i++) lengths[i] = 8;
        build(&lit, lengths, 288);
        for (i = 0; i < 30; i++) lengths[i] = 5;
        build(&dist, lengths, 30);
        ready = true;
    }
    return inflate_codes(s, &lit, &dist);
}

static bool inflate_dynamic(Inflater *s) {
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    uint8_t lengths[320];
    Huffman lens, lit, dist;
    int nlen = get_bits(s, 5) + 257, ndist = get_bits(s, 5) + 1, ncode = get_bits(s, 4) + 4;
    if (nlen > 286 || ndist > 30) return false;
    memset(lengths, 0, sizeof lengths);
    for (int i = 0; i < ncode; i++) lengths[order[i]] = (uint8_t)get_bits(s, 3);
    if (!build(&lens, lengths, 19)) return false;
    for (int i = 0; i < nlen + ndist;) {
        int sym = decode_symbol(s, &lens);
        if (sym < 0 || s->bad) return false;
        if (sym < 16) {
            lengths[i++] = (uint8_t)sym;
            continue;
        }
        int repeat, value = 0;
        if (sym == 16) {
            if (i == 0) return false;
            value = lengths[i - 1];
            repeat = 3 + get_bits(s, 2);
        } else if (sym == 17) {
            repeat = 3 + get_bits(s, 3);
        } else {
            repeat = 11 + get_bits(s, 7);
        }
        if (i + repeat > nlen + ndist) return false;
        while (repeat--) lengths[i++] = (uint8_t)value;
    }
    if (!lengths[256]) return false;
    if (!build(&lit, lengths, nlen) || !build(&dist, lengths + nlen, ndist)) return false;
    return inflate_codes(s, &lit, &dist);
}

int bank_inflate(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size) {
    Inflater s;
    if (in_size < 2 || (in[0] & 0x0F) != 8 || ((in[0] << 8) | in[1]) % 31 != 0 || (in[1] & 0x20)) return -1;
    memset(&s, 0, sizeof s);
    s.in = in;
    s.in_size = in_size;
    s.in_pos = 2;
    s.out = out;
    s.out_size = out_size;
    int last;
    do {
        last = get_bit(&s);
        int type = get_bits(&s, 2);
        bool ok;
        if (type == 0) {
            s.bit_count = 0; /* stored blocks start on a byte */
            if (s.in_pos + 4 > in_size) return -1;
            unsigned len = in[s.in_pos] | in[s.in_pos + 1] << 8, nlen = in[s.in_pos + 2] | in[s.in_pos + 3] << 8;
            s.in_pos += 4;
            if ((len ^ 0xFFFF) != nlen || s.in_pos + len > in_size || s.out_pos + len > out_size) return -1;
            memcpy(out + s.out_pos, in + s.in_pos, len);
            s.in_pos += len;
            s.out_pos += len;
            ok = true;
        } else if (type == 1) {
            ok = inflate_fixed(&s);
        } else if (type == 2) {
            ok = inflate_dynamic(&s);
        } else {
            ok = false;
        }
        if (!ok || s.bad) return -1;
    } while (!last);
    return (int)s.out_pos;
}

/* ---------------------------------------------------------------- image records */

#define IMAGE_HEADER 32
#define FLAG_RLE_ANY 0x07
#define FLAG_LZX 0x08
#define FLAG_ALPHA 0x10
#define MODE_24BIT 4

static int16_t s16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }
static int32_t s32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }

bool bank_decode(const uint8_t *raw, size_t size, BankImage *out) {
    memset(out, 0, sizeof *out);
    if (size < IMAGE_HEADER) return false;
    int data_size = s32(raw + 8), w = s16(raw + 12), h = s16(raw + 14), mode = raw[16], flags = raw[17];
    /* Only the layout sprites here use: plain 24-bit rows padded to an even width, then alpha rows padded to 4. */
    if (mode != MODE_24BIT || (flags & (FLAG_RLE_ANY | FLAG_LZX)) || w <= 0 || h <= 0 || w > 4096 || h > 4096) return false;
    int pad = (w * 3) % 2, alpha_pad = (4 - w % 4) % 4;
    size_t colour = (size_t)(w + pad) * 3 * h, alpha = flags & FLAG_ALPHA ? (size_t)(w + alpha_pad) * h : 0;
    if (data_size < 0 || (size_t)data_size < colour + alpha || IMAGE_HEADER + colour + alpha > size) return false;
    const uint8_t *data = raw + IMAGE_HEADER, *key = raw + 28;
    uint32_t *argb = malloc((size_t)w * h * 4);
    if (!argb) return false;
    for (int y = 0; y < h; y++) {
        const uint8_t *row = data + (size_t)y * (w + pad) * 3;
        for (int x = 0; x < w; x++) {
            const uint8_t *p = row + x * 3; /* blue, green, red */
            bool clear = !(flags & FLAG_ALPHA) && p[0] == key[0] && p[1] == key[1] && p[2] == key[2];
            argb[y * w + x] = (clear ? 0u : 0xFF000000u) | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
        }
    }
    if (flags & FLAG_ALPHA)
        for (int y = 0; y < h; y++) {
            const uint8_t *row = data + colour + (size_t)y * (w + alpha_pad);
            for (int x = 0; x < w; x++) argb[y * w + x] = (argb[y * w + x] & 0x00FFFFFFu) | (uint32_t)row[x] << 24;
        }
    out->w = w;
    out->h = h;
    out->hot_x = s16(raw + 20);
    out->hot_y = s16(raw + 22);
    out->argb = argb;
    return true;
}

/* ---------------------------------------------------------------- the exe's image bank */

#define MAX_CACHED 32
#define CHUNK_IMAGES 0x6666
#define CHUNK_LAST 0x7F7F
#define PACK_SEARCH (8u << 20)

#ifdef _WIN32
#define seek64 _fseeki64
#else
#define seek64 fseeko
#endif

typedef struct {
    uint32_t handle;
    long long offset; /* of the zlib block */
    uint32_t packed, unpacked;
} Entry;

static FILE *file;
static Entry *entries;
static int entry_count;
static struct {
    unsigned handle;
    bool tried;
    BankImage image;
} cache[MAX_CACHED];
static int cache_count;
static volatile bool opened;

bool bank_ready(void) { return opened; }

static bool read_at(long long at, void *buffer, size_t n) { return seek64(file, at, SEEK_SET) == 0 && fread(buffer, 1, n, file) == n; }

/* The pack starts where the exe's sections end. */
static long long pack_start(void) {
    uint8_t dos[64], pe[24], section[40];
    long long end = 0;
    if (!read_at(0, dos, sizeof dos)) return -1;
    long long at = s32(dos + 60);
    if (!read_at(at, pe, sizeof pe) || memcmp(pe, "PE\0\0", 4) != 0) return -1;
    int sections = s16(pe + 6), optional = s16(pe + 20);
    for (int i = 0; i < sections; i++) {
        if (!read_at(at + 24 + optional + i * 40, section, sizeof section)) return -1;
        long long stop = (long long)(uint32_t)s32(section + 20) + (uint32_t)s32(section + 16);
        if (stop > end) end = stop;
    }
    return end;
}

bool bank_open(const char *exe_path) {
    if (file) return entries != NULL;
    file = fopen(exe_path, "rb");
    if (!file) return false;
    long long start = pack_start();
    if (start < 0) return false;
    uint8_t *window = malloc(PACK_SEARCH);
    if (!window) return false;
    seek64(file, start, SEEK_SET);
    size_t got = fread(window, 1, PACK_SEARCH, file);
    long long at = -1;
    for (size_t i = 0; i + 4 <= got; i++)
        if (memcmp(window + i, "PAMU", 4) == 0 || memcmp(window + i, "PAME", 4) == 0) {
            at = start + (long long)i + 16; /* header, runtime version and build */
            break;
        }
    free(window);
    if (at < 0) return false;
    for (;;) {
        uint8_t head[8];
        if (!read_at(at, head, sizeof head)) return false;
        unsigned id = (uint16_t)s16(head);
        int flags = s16(head + 2), size = s32(head + 4);
        if (id == CHUNK_LAST || size < 0) return false;
        if (id == CHUNK_IMAGES && flags == 0) {
            uint8_t count_bytes[4];
            if (!read_at(at + 8, count_bytes, 4)) return false;
            int count = s32(count_bytes);
            if (count <= 0 || count > 65536) return false;
            entries = calloc((size_t)count, sizeof *entries);
            if (!entries) return false;
            long long item = at + 12;
            for (int i = 0; i < count; i++) {
                uint8_t h[12];
                if (!read_at(item, h, sizeof h)) return false;
                entries[i].handle = (uint32_t)s32(h);
                entries[i].unpacked = (uint32_t)s32(h + 4);
                entries[i].packed = (uint32_t)s32(h + 8);
                entries[i].offset = item + 12;
                item += 12 + (long long)entries[i].packed;
            }
            entry_count = count;
            opened = true;
            return true;
        }
        at += 8 + (long long)size;
    }
}

const BankImage *bank_image(unsigned handle) {
    for (int i = 0; i < cache_count; i++)
        if (cache[i].handle == handle) return cache[i].image.argb ? &cache[i].image : NULL;
    if (!entries || cache_count >= MAX_CACHED) return NULL;
    int slot = cache_count++;
    cache[slot].handle = handle;
    cache[slot].tried = true;
    for (int i = 0; i < entry_count; i++) {
        const Entry *e = &entries[i];
        if (e->handle != handle) continue;
        uint8_t *packed = malloc(e->packed), *raw = malloc(e->unpacked);
        if (packed && raw && e->unpacked <= (64u << 20) && read_at(e->offset, packed, e->packed)) {
            int n = bank_inflate(packed, e->packed, raw, e->unpacked);
            if (n > 0) bank_decode(raw, (size_t)n, &cache[slot].image);
        }
        free(packed);
        free(raw);
        break;
    }
    return cache[slot].image.argb ? &cache[slot].image : NULL;
}
