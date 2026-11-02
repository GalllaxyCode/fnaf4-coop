/* Images from the game's own exe, read at run time, so the mod can draw game art it doesn't ship.
 * Fusion 2.5 keeps every image in chunk 0x6666 of the data pack appended to the exe: a count, then per
 * image a handle, its unpacked size, its packed size and a zlib block. Only the 24-bit layout used
 * by sprites is decoded here. */
#ifndef BANK_H
#define BANK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int w, h, hot_x, hot_y;
    uint32_t *argb; /* w * h pixels, 0xAARRGGBB, not premultiplied */
} BankImage;

/* Indexes the image bank of this exe. Cheap: it reads headers only. */
bool bank_open(const char *exe_path);
bool bank_ready(void); /* bank_open finished; it may run on another thread */
/* Decodes an image by its stored handle, once, and keeps it. NULL if it can't. */
const BankImage *bank_image(unsigned handle);

/* Pure helpers, tested on their own. */
int bank_inflate(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size); /* zlib stream; bytes out or -1 */
bool bank_decode(const uint8_t *raw, size_t size, BankImage *out); /* an unpacked image record */

#endif
