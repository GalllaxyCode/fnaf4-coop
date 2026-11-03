#ifndef SCREAMERS_H
#define SCREAMERS_H
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

/* Select a sibling executable without changing directories or game saves.
 * Only the two supported game filenames and their blackout copies are valid. */
bool screamers_path(const wchar_t *current, bool enable, wchar_t *out, size_t capacity, bool *current_enabled);
#endif
