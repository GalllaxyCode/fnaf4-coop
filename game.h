/* Access to the Fusion runtime inside FNAF 4 (Steam, build 288). */
#ifndef GAME_H
#define GAME_H
#include <windows.h>
#include <stdbool.h>

#define FRAME_WHAT_NIGHT 2
#define FRAME_LEVEL 3
#define FRAME_PLUSHTRAP 7
#define FRAME_BB 18

void game_init(BYTE *exe_base);
bool game_begin(void);
int game_frame(void);
BYTE *game_oi_list(void);
void game_goto(int frame);
void game_next_frame(void);

BYTE *obj(const wchar_t *name);
bool frame_has(const wchar_t *name);
bool cnt_read(BYTE *o, int *out);
void cnt_write(BYTE *o, int value);
int cnt_get(const wchar_t *name, int fallback);
void cnt_set(const wchar_t *name, int value);
bool av_read(BYTE *o, int index, int *out);
void av_write(BYTE *o, int index, int value);
bool pos_read(BYTE *o, int *x, int *y);
void pos_write(BYTE *o, int x, int y);
bool at_zone(BYTE *o, const wchar_t *zone);

/* Event rules, addressed by their number in the frame's sheet. */
int evg_count(void);
BYTE *evg(int index);
BYTE *evg_base(void);
void evg_disable(int index);
void evg_enable(int index);
bool evg_is_disabled(int index);
BYTE *cond_at(BYTE *group, int index);
BYTE *act_at(BYTE *group, int index);
BYTE *cond_param(BYTE *cond, int index);
BYTE *act_param(BYTE *act, int index);
int *literal_in(BYTE *event, int size, int value);
bool cond_retarget(BYTE *cond, const wchar_t *counter);

#endif
