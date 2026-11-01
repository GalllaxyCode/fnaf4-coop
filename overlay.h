/* Name bubbles and door panels, drawn in a click-through window above the game. */
#ifndef OVERLAY_H
#define OVERLAY_H
#include <stdbool.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define OVERLAY_MAX_BUBBLES 8
#define OVERLAY_MAX_QUADS 3
#define OVERLAY_TEXT_LEN 60

/* Coordinates are in the game's 1024x768 view. */
typedef struct {
    int x, y;
    char text[OVERLAY_TEXT_LEN];
} OverlayBubble;

typedef struct {
    int x[4], y[4]; /* top left, top right, bottom right, bottom left */
    unsigned color; /* 0xRRGGBB */
    int alpha;
    int knob_u; /* knob position across the door, in percent */
    bool louver; /* slatted closet door instead of a panelled one */
} OverlayQuad;

#define OVERLAY_MAX_RECTS 40
#define OVERLAY_MAX_TEXTS 40
typedef enum { ALIGN_LEFT, ALIGN_CENTER } OverlayAlign;

typedef struct {
    int x0, y0, x1, y1;
    unsigned color;
    int alpha;
    int radius;
} OverlayRect;

typedef struct {
    int x, y;
    int size;
    OverlayAlign align;
    unsigned color;
    char text[OVERLAY_TEXT_LEN];
} OverlayText;

typedef struct {
    int rect_count;
    OverlayRect rects[OVERLAY_MAX_RECTS];
    int text_count;
    OverlayText texts[OVERLAY_MAX_TEXTS];
    int bubble_count;
    OverlayBubble bubbles[OVERLAY_MAX_BUBBLES];
    int quad_count;
    OverlayQuad quads[OVERLAY_MAX_QUADS];
} OverlayScene;

void overlay_show(const OverlayScene *scene);
void overlay_hide(void);
#ifdef _WIN32
HWND overlay_game_window(void);
bool overlay_view_point(POINT at, int *x, int *y); /* a screen point in 1024x768 view coordinates */
#endif
/* Test only: saves the game's own window, with the overlay on top, as a bitmap file. */
bool overlay_save(const char *path);

#endif
