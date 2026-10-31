#include "game.h"
#include <string.h>

/* Offsets from Clickteam's Cncf.h, confirmed against this exe's code and by running it.
 * objInfoList is 146 bytes here, not the header's 142: this runtime has the HWABETA oilColList field
 * at the end. Counters are stored inverted: the runtime applies `not` on every read. */
#define RVA_APP 0xB39CCu
#define RVA_RUNHEADER 0xB39D4u
#define APP_FRAME 0x1F0
#define RH_FRAME 28
#define RH_QUIT 48
#define RH_QUIT_PARAM 0x38
#define RH_NUMBER_OI 84
#define RH_OI_LIST 88
#define RH_OBJECT_LIST 0x1278
#define FRAME_EVENTS 128
#define OIL_SIZE 146
#define OIL_OI 0
#define OIL_OBJECT 6
#define OIL_NAME 90
#define OBJ_LIST_STRIDE 8
#define HO_X 76
#define HO_Y 84
#define HO_OFFSET_VALUE 128
#define RC_CHANGED (184 + 68)
#define CNT_TYPE 0x202
#define CNT_VALUE 0x20A
#define CV_DOUBLE 2
#define CV_SIZE 16
#define EVG_HEADER 16
#define EVG_INACTIVE 0x4000
#define COND_HEADER 16
#define ACT_HEADER 14
#define EVENT_OI 6
#define EVENT_OI_LIST 8
#define EVENT_PARAMS 12
#define LOOPEXIT_NEXTLEVEL 1
#define LOOPEXIT_GOTOLEVEL 3
#define MAX_GROUPS 1024

static BYTE *base, *rh;
static int frame_index = -1;
static BYTE *groups[MAX_GROUPS];
static int group_count;
static BYTE *groups_for;

static BYTE *read_ptr(BYTE *at) {
    if (IsBadReadPtr(at, sizeof(BYTE *))) return NULL;
    return *(BYTE **)at;
}

void game_init(BYTE *exe_base) { base = exe_base; }

static void index_groups(void) {
    BYTE *frame = read_ptr(rh + RH_FRAME);
    BYTE *first = frame ? read_ptr(frame + FRAME_EVENTS) : NULL;
    if (first == groups_for) return;
    groups_for = first;
    group_count = 0;
    if (!first) return;
    for (BYTE *p = first; group_count < MAX_GROUPS && !IsBadReadPtr(p, EVG_HEADER) && *(short *)p < 0; p -= *(short *)p)
        groups[group_count++] = p;
}

bool game_begin(void) {
    BYTE *app = read_ptr(base + RVA_APP);
    rh = read_ptr(base + RVA_RUNHEADER);
    frame_index = -1;
    if (!app || !rh || IsBadReadPtr(rh, RH_OBJECT_LIST + 4) || IsBadReadPtr(app + APP_FRAME, 4)) return false;
    frame_index = *(int *)(app + APP_FRAME);
    index_groups();
    return true;
}

int game_frame(void) { return frame_index; }
BYTE *game_oi_list(void) { return rh ? read_ptr(rh + RH_OI_LIST) : NULL; }

void game_goto(int frame) {
    *(DWORD *)(rh + RH_QUIT_PARAM) = (DWORD)frame | 0x8000;
    *(short *)(rh + RH_QUIT) = LOOPEXIT_GOTOLEVEL;
}

void game_next_frame(void) { *(short *)(rh + RH_QUIT) = LOOPEXIT_NEXTLEVEL; }

static BYTE *find_oil(const wchar_t *name) {
    int count = *(int *)(rh + RH_NUMBER_OI);
    BYTE *list = read_ptr(rh + RH_OI_LIST);
    if (!list || count <= 0 || count > 4096 || IsBadReadPtr(list, (UINT_PTR)count * OIL_SIZE)) return NULL;
    for (int i = 0; i < count; i++)
        if (wcsncmp((const wchar_t *)(list + i * OIL_SIZE + OIL_NAME), name, 24) == 0) return list + i * OIL_SIZE;
    return NULL;
}

bool frame_has(const wchar_t *name) { return rh && find_oil(name) != NULL; }

BYTE *obj(const wchar_t *name) {
    if (!rh) return NULL;
    BYTE *oil = find_oil(name), *objects = read_ptr(rh + RH_OBJECT_LIST);
    if (!oil || !objects) return NULL;
    short first = *(short *)(oil + OIL_OBJECT);
    return first < 0 ? NULL : read_ptr(objects + first * OBJ_LIST_STRIDE);
}

bool cnt_read(BYTE *o, int *out) {
    if (!o || IsBadReadPtr(o + CNT_TYPE, CV_SIZE)) return false;
    UINT type = *(UINT *)(o + CNT_TYPE);
    if (type == 0) *out = ~*(int *)(o + CNT_VALUE);
    else if (type == CV_DOUBLE) *out = ~(int)*(double *)(o + CNT_VALUE);
    else return false;
    return true;
}

void cnt_write(BYTE *o, int value) {
    if (!o) return;
    if (*(UINT *)(o + CNT_TYPE) == CV_DOUBLE) *(double *)(o + CNT_VALUE) = ~value;
    else *(int *)(o + CNT_VALUE) = ~value;
}

int cnt_get(const wchar_t *name, int fallback) {
    int v;
    return cnt_read(obj(name), &v) ? v : fallback;
}

void cnt_set(const wchar_t *name, int value) { cnt_write(obj(name), value); }

static BYTE *av_slot(BYTE *o, int index) {
    if (!o || IsBadReadPtr(o, HO_OFFSET_VALUE + 4)) return NULL;
    int offset = *(int *)(o + HO_OFFSET_VALUE);
    if (offset <= 0) return NULL;
    BYTE *values = read_ptr(o + offset);
    if (!values || IsBadReadPtr(values + index * CV_SIZE, CV_SIZE)) return NULL;
    return values + index * CV_SIZE;
}

bool av_read(BYTE *o, int index, int *out) {
    BYTE *slot = av_slot(o, index);
    if (!slot) return false;
    if (*(UINT *)slot == CV_DOUBLE) *out = (int)*(double *)(slot + 8);
    else *out = *(int *)(slot + 8);
    return true;
}

void av_write(BYTE *o, int index, int value) {
    BYTE *slot = av_slot(o, index);
    if (!slot) return;
    if (*(UINT *)slot == CV_DOUBLE) *(double *)(slot + 8) = value;
    else *(int *)(slot + 8) = value;
}

bool pos_read(BYTE *o, int *x, int *y) {
    if (!o || IsBadReadPtr(o, HO_Y + 4)) return false;
    *x = *(int *)(o + HO_X);
    *y = *(int *)(o + HO_Y);
    return true;
}

void pos_write(BYTE *o, int x, int y) {
    if (!o || IsBadWritePtr(o, RC_CHANGED + 4)) return;
    *(int *)(o + HO_X) = x;
    *(int *)(o + HO_Y) = y;
    *(int *)(o + RC_CHANGED) = 1;
}

/* Threats sit on a zone with an offset of at most one pixel. */
bool at_zone(BYTE *o, const wchar_t *zone) {
    int x, y, zx, zy;
    if (!pos_read(o, &x, &y) || !pos_read(obj(zone), &zx, &zy)) return false;
    return abs(x - zx) <= 2 && abs(y - zy) <= 2;
}

int evg_count(void) { return group_count; }
BYTE *evg(int index) { return index >= 0 && index < group_count ? groups[index] : NULL; }
BYTE *evg_base(void) { return groups_for; }

void evg_disable(int index) {
    BYTE *g = evg(index);
    if (g) *(WORD *)(g + 4) |= EVG_INACTIVE;
}

bool evg_is_disabled(int index) {
    BYTE *g = evg(index);
    return g && (*(WORD *)(g + 4) & EVG_INACTIVE);
}

static int event_size(BYTE *e) {
    short s = *(short *)e;
    return s < 0 ? -s : s;
}

BYTE *cond_at(BYTE *group, int index) {
    if (!group || index >= group[2]) return NULL;
    BYTE *p = group + EVG_HEADER;
    for (int i = 0; i < index; i++) p += event_size(p);
    return p;
}

BYTE *act_at(BYTE *group, int index) {
    if (!group || index >= group[3]) return NULL;
    BYTE *p = group + EVG_HEADER;
    for (int i = 0; i < group[2] + index; i++) p += event_size(p);
    return p;
}

static BYTE *param_at(BYTE *event, int header, int index) {
    if (!event || index >= event[EVENT_PARAMS]) return NULL;
    BYTE *p = event + header;
    for (int i = 0; i < index; i++) p += *(short *)p;
    return p;
}

BYTE *cond_param(BYTE *cond, int index) { return param_at(cond, COND_HEADER, index); }
BYTE *act_param(BYTE *act, int index) { return param_at(act, ACT_HEADER, index); }

/* An integer literal in an expression is stored as ffff 0000 0a00 <value>. */
int *literal_in(BYTE *event, int size, int value) {
    static const BYTE mark[6] = {0xFF, 0xFF, 0x00, 0x00, 0x0A, 0x00};
    for (int i = 0; i + 10 <= size; i++)
        if (memcmp(event + i, mark, sizeof mark) == 0 && *(int *)(event + i + 6) == value) return (int *)(event + i + 6);
    return NULL;
}

bool cond_retarget(BYTE *cond, const wchar_t *counter) {
    BYTE *list = game_oi_list(), *oil = find_oil(counter);
    if (!cond || !list || !oil) return false;
    *(short *)(cond + EVENT_OI) = *(short *)(oil + OIL_OI);
    *(short *)(cond + EVENT_OI_LIST) = (short)((oil - list) / OIL_SIZE);
    return true;
}
