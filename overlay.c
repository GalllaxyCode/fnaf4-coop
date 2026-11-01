#include <windows.h>
#include <string.h>
#include "overlay.h"

#define VIEW_W 1024
#define VIEW_H 768
#define GLYPH_ADVANCE 6
#define PAD_X 12
#define PAD_Y 6
#define RADIUS 12
#define TAIL 9

static HWND win, game;
static HDC dc;
static HBITMAP bitmap;
static DWORD *pixels;
static int width, height;
static OverlayScene shown;
static bool visible;

static BOOL CALLBACK find_game(HWND h, LPARAM best_area) {
    RECT r;
    if (h != win && IsWindowVisible(h) && GetClientRect(h, &r)) {
        long area = (long)r.right * r.bottom;
        if (area > *(long *)best_area) {
            *(long *)best_area = area;
            game = h;
        }
    }
    return TRUE;
}

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(h, m, w, l);
}

static bool create_window(void) {
    long area = 0;
    EnumThreadWindows(GetCurrentThreadId(), find_game, (LPARAM)&area);
    if (!game) return false;
    if (win) return true;
    WNDCLASSW c;
    memset(&c, 0, sizeof c);
    c.lpfnWndProc = proc;
    c.hInstance = GetModuleHandleW(NULL);
    c.lpszClassName = L"fnaf4coop_overlay";
    RegisterClassW(&c);
    /* Owned by the game window, so it stays above the game without covering other apps. */
    win = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, c.lpszClassName, L"", WS_POPUP, 0, 0, 16, 16, game, NULL, c.hInstance, NULL);
    dc = CreateCompatibleDC(NULL);
    return win != NULL;
}

static void resize(int w, int h) {
    if (w == width && h == height && bitmap) return;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    if (bitmap) DeleteObject(bitmap);
    bitmap = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
    SelectObject(dc, bitmap);
    width = w;
    height = h;
}

/* Writes a premultiplied pixel over what is already there. */
static void blend(int x, int y, unsigned rgb, int alpha) {
    if (x < 0 || y < 0 || x >= width || y >= height || alpha <= 0) return;
    DWORD *p = &pixels[y * width + x];
    int ia = 255 - alpha;
    int a = alpha + ((*p >> 24) & 255) * ia / 255;
    int r = ((rgb >> 16) & 255) * alpha / 255 + ((*p >> 16) & 255) * ia / 255;
    int g = ((rgb >> 8) & 255) * alpha / 255 + ((*p >> 8) & 255) * ia / 255;
    int b = (rgb & 255) * alpha / 255 + (*p & 255) * ia / 255;
    *p = (DWORD)a << 24 | (DWORD)r << 16 | (DWORD)g << 8 | (DWORD)b;
}

static void rounded(int x0, int y0, int x1, int y1, int radius, unsigned rgb, int alpha) {
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            int dx = x < x0 + radius ? x0 + radius - x : x >= x1 - radius ? x - (x1 - radius - 1) : 0;
            int dy = y < y0 + radius ? y0 + radius - y : y >= y1 - radius ? y - (y1 - radius - 1) : 0;
            if (dx * dx + dy * dy <= radius * radius) blend(x, y, rgb, alpha);
        }
}

/* Wine here has no FreeType, so GDI text measures as empty on every machine. This is the printable
 * ASCII range as five columns of eight pixels. */
static const unsigned char glyphs[95][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x5F, 0x00, 0x00},
    {0x00, 0x07, 0x00, 0x07, 0x00},
    {0x14, 0x7F, 0x14, 0x7F, 0x14},
    {0x24, 0x2A, 0x7F, 0x2A, 0x12},
    {0x23, 0x13, 0x08, 0x64, 0x62},
    {0x36, 0x49, 0x56, 0x20, 0x50},
    {0x00, 0x08, 0x07, 0x03, 0x00},
    {0x00, 0x1C, 0x22, 0x41, 0x00},
    {0x00, 0x41, 0x22, 0x1C, 0x00},
    {0x2A, 0x1C, 0x7F, 0x1C, 0x2A},
    {0x08, 0x08, 0x3E, 0x08, 0x08},
    {0x00, 0x80, 0x70, 0x30, 0x00},
    {0x08, 0x08, 0x08, 0x08, 0x08},
    {0x00, 0x00, 0x60, 0x60, 0x00},
    {0x20, 0x10, 0x08, 0x04, 0x02},
    {0x3E, 0x51, 0x49, 0x45, 0x3E},
    {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x72, 0x49, 0x49, 0x49, 0x46},
    {0x21, 0x41, 0x49, 0x4D, 0x33},
    {0x18, 0x14, 0x12, 0x7F, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x31},
    {0x41, 0x21, 0x11, 0x09, 0x07},
    {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x46, 0x49, 0x49, 0x29, 0x1E},
    {0x00, 0x00, 0x14, 0x00, 0x00},
    {0x00, 0x40, 0x34, 0x00, 0x00},
    {0x00, 0x08, 0x14, 0x22, 0x41},
    {0x14, 0x14, 0x14, 0x14, 0x14},
    {0x00, 0x41, 0x22, 0x14, 0x08},
    {0x02, 0x01, 0x59, 0x09, 0x06},
    {0x3E, 0x41, 0x5D, 0x59, 0x4E},
    {0x7C, 0x12, 0x11, 0x12, 0x7C},
    {0x7F, 0x49, 0x49, 0x49, 0x36},
    {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x49, 0x49, 0x49, 0x41},
    {0x7F, 0x09, 0x09, 0x09, 0x01},
    {0x3E, 0x41, 0x41, 0x51, 0x73},
    {0x7F, 0x08, 0x08, 0x08, 0x7F},
    {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01},
    {0x7F, 0x08, 0x14, 0x22, 0x41},
    {0x7F, 0x40, 0x40, 0x40, 0x40},
    {0x7F, 0x02, 0x1C, 0x02, 0x7F},
    {0x7F, 0x04, 0x08, 0x10, 0x7F},
    {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06},
    {0x3E, 0x41, 0x51, 0x21, 0x5E},
    {0x7F, 0x09, 0x19, 0x29, 0x46},
    {0x26, 0x49, 0x49, 0x49, 0x32},
    {0x03, 0x01, 0x7F, 0x01, 0x03},
    {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F},
    {0x3F, 0x40, 0x38, 0x40, 0x3F},
    {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x03, 0x04, 0x78, 0x04, 0x03},
    {0x61, 0x59, 0x49, 0x4D, 0x43},
    {0x00, 0x7F, 0x41, 0x41, 0x41},
    {0x02, 0x04, 0x08, 0x10, 0x20},
    {0x00, 0x41, 0x41, 0x41, 0x7F},
    {0x04, 0x02, 0x01, 0x02, 0x04},
    {0x40, 0x40, 0x40, 0x40, 0x40},
    {0x00, 0x03, 0x07, 0x08, 0x00},
    {0x20, 0x54, 0x54, 0x78, 0x40},
    {0x7F, 0x28, 0x44, 0x44, 0x38},
    {0x38, 0x44, 0x44, 0x44, 0x28},
    {0x38, 0x44, 0x44, 0x28, 0x7F},
    {0x38, 0x54, 0x54, 0x54, 0x18},
    {0x00, 0x08, 0x7E, 0x09, 0x02},
    {0x18, 0xA4, 0xA4, 0x9C, 0x78},
    {0x7F, 0x08, 0x04, 0x04, 0x78},
    {0x00, 0x44, 0x7D, 0x40, 0x00},
    {0x20, 0x40, 0x40, 0x3D, 0x00},
    {0x7F, 0x10, 0x28, 0x44, 0x00},
    {0x00, 0x41, 0x7F, 0x40, 0x00},
    {0x7C, 0x04, 0x78, 0x04, 0x78},
    {0x7C, 0x08, 0x04, 0x04, 0x78},
    {0x38, 0x44, 0x44, 0x44, 0x38},
    {0xFC, 0x18, 0x24, 0x24, 0x18},
    {0x18, 0x24, 0x24, 0x18, 0xFC},
    {0x7C, 0x08, 0x04, 0x04, 0x08},
    {0x48, 0x54, 0x54, 0x54, 0x24},
    {0x04, 0x04, 0x3F, 0x44, 0x24},
    {0x3C, 0x40, 0x40, 0x20, 0x7C},
    {0x1C, 0x20, 0x40, 0x20, 0x1C},
    {0x3C, 0x40, 0x30, 0x40, 0x3C},
    {0x44, 0x28, 0x10, 0x28, 0x44},
    {0x4C, 0x90, 0x90, 0x90, 0x7C},
    {0x44, 0x64, 0x54, 0x4C, 0x44},
    {0x00, 0x08, 0x36, 0x41, 0x00},
    {0x00, 0x00, 0x77, 0x00, 0x00},
    {0x00, 0x41, 0x36, 0x08, 0x00},
    {0x02, 0x01, 0x02, 0x04, 0x02}};

static void draw_text(int x, int y, const char *text, int scale, unsigned color) {
    for (; *text; text++, x += GLYPH_ADVANCE * scale) {
        int c = (unsigned char)*text;
        if (c < 32 || c > 126) c = '?';
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 8; row++)
                if (glyphs[c - 32][col] & (1 << row))
                    for (int dy = 0; dy < scale; dy++)
                        for (int dx = 0; dx < scale; dx++) blend(x + col * scale + dx, y + row * scale + dy, color, 255);
    }
}

static void draw_bubble(const OverlayBubble *b, double sx, double sy) {
    int scale = (int)(sy * 2.4 + 0.5);
    if (scale < 1) scale = 1;
    int cx = (int)(b->x * sx), cy = (int)(b->y * sy);
    int w = (int)strlen(b->text) * GLYPH_ADVANCE * scale + PAD_X * 2, h = 8 * scale + PAD_Y * 2;
    int x0 = cx - w / 2, y0 = cy - h - TAIL;
    if (x0 < 4) x0 = 4;
    if (x0 + w > width - 4) x0 = width - 4 - w;
    if (y0 < 4) y0 = 4;
    rounded(x0, y0, x0 + w, y0 + h, RADIUS, 0x101014, 225);
    if (cx > x0 + RADIUS && cx < x0 + w - RADIUS)
        for (int i = 0; i < TAIL; i++)
            for (int x = cx - (TAIL - i); x <= cx + (TAIL - i); x++) blend(x, y0 + h + i, 0x101014, 225);
    draw_text(x0 + PAD_X, y0 + PAD_Y, b->text, scale, 0xFFFFFF);
}

static void draw_rect(const OverlayRect *r, double sx, double sy) {
    rounded((int)(r->x0 * sx), (int)(r->y0 * sy), (int)(r->x1 * sx), (int)(r->y1 * sy), r->radius, r->color, r->alpha);
}

static void draw_label(const OverlayText *t, double sx, double sy) {
    int scale = (int)(sy * 1.9 * t->size + 0.5);
    if (scale < 1) scale = 1;
    int x = (int)(t->x * sx), y = (int)(t->y * sy);
    if (t->align == ALIGN_CENTER) x -= (int)strlen(t->text) * GLYPH_ADVANCE * scale / 2;
    draw_text(x, y, t->text, scale, t->color);
}

/* Fills a door-shaped quad row by row, with inset panels and a knob placed in the door's own space. */
static void draw_quad(const OverlayQuad *q, double sx, double sy) {
    int top = q->y[0], bottom = q->y[0];
    for (int i = 1; i < 4; i++) {
        if (q->y[i] < top) top = q->y[i];
        if (q->y[i] > bottom) bottom = q->y[i];
    }
    for (int y = (int)(top * sy); y < (int)(bottom * sy); y++) {
        double vy = y / sy, lx = 1e9, rx = -1e9;
        for (int i = 0; i < 4; i++) {
            int j = (i + 1) % 4;
            if ((q->y[i] <= vy && q->y[j] > vy) || (q->y[j] <= vy && q->y[i] > vy)) {
                double cross = q->x[i] + (q->x[j] - q->x[i]) * (vy - q->y[i]) / (double)(q->y[j] - q->y[i]);
                if (cross < lx) lx = cross;
                if (cross > rx) rx = cross;
            }
        }
        if (rx <= lx) continue;
        for (int x = (int)(lx * sx); x < (int)(rx * sx); x++) {
            double u = (x / sx - q->x[0]) / (double)(q->x[1] - q->x[0]);
            double edge_top = q->y[0] + (q->y[1] - q->y[0]) * u, edge_bottom = q->y[3] + (q->y[2] - q->y[3]) * u;
            double v = (vy - edge_top) / (edge_bottom - edge_top);
            double light = 1.0 - 0.28 * v;
            if (q->louver) {
                light *= (int)(v * 44) % 2 ? 0.74 : 1.0;
                blend(x, y, (unsigned)(((q->color >> 16) & 255) * light) << 16 | (unsigned)(((q->color >> 8) & 255) * light) << 8 | (unsigned)((q->color & 255) * light), q->alpha);
                continue;
            }
            double panel_u = u < 0.5 ? (u - 0.1) / 0.32 : (u - 0.58) / 0.32;
            double panel_v = v < 0.42 ? (v - 0.06) / 0.32 : (v - 0.47) / 0.46;
            if (panel_u > 0 && panel_u < 1 && panel_v > 0 && panel_v < 1) {
                bool rim = panel_u < 0.07 || panel_u > 0.93 || panel_v < 0.04 || panel_v > 0.96;
                light *= rim ? 0.72 : 0.9;
            }
            int r = (int)(((q->color >> 16) & 255) * light), g = (int)(((q->color >> 8) & 255) * light), b = (int)((q->color & 255) * light);
            double ku = (u - (q->knob_u / 100.0)) * (q->x[1] - q->x[0]), kv = (v - 0.56) * (edge_bottom - edge_top);
            if (ku * ku + kv * kv < 64) {
                r = 176;
                g = 132;
                b = 44;
            }
            blend(x, y, (unsigned)r << 16 | (unsigned)g << 8 | (unsigned)b, q->alpha);
        }
    }
}

HWND overlay_game_window(void) {
    if (!game) {
        long area = 0;
        EnumThreadWindows(GetCurrentThreadId(), find_game, (LPARAM)&area);
    }
    return game;
}

void overlay_hide(void) {
    if (win && visible) ShowWindow(win, SW_HIDE);
    visible = false;
    memset(&shown, 0, sizeof shown);
}

void overlay_show(const OverlayScene *scene) {
    RECT r;
    POINT origin = {0, 0}, zero = {0, 0};
    if (!create_window() || !GetClientRect(game, &r) || !ClientToScreen(game, &origin)) return;
    int w = r.right, h = r.bottom;
    if (w <= 0 || h <= 0 || IsIconic(game)) return;
    if (scene->bubble_count == 0 && scene->quad_count == 0 && scene->rect_count == 0) {
        overlay_hide();
        return;
    }
    bool same = visible && w == width && h == height && memcmp(scene, &shown, sizeof shown) == 0;
    if (!same) {
        resize(w, h);
        memset(pixels, 0, (size_t)w * h * 4);
        double sx = (double)w / VIEW_W, sy = (double)h / VIEW_H;
        for (int i = 0; i < scene->rect_count; i++) draw_rect(&scene->rects[i], sx, sy);
        for (int i = 0; i < scene->text_count; i++) draw_label(&scene->texts[i], sx, sy);
        for (int i = 0; i < scene->quad_count; i++) draw_quad(&scene->quads[i], sx, sy);
        for (int i = 0; i < scene->bubble_count; i++) draw_bubble(&scene->bubbles[i], sx, sy);
        shown = *scene;
        GdiFlush();
    }
    SIZE size = {w, h};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(win, NULL, &origin, &size, dc, &zero, 0, &bf, ULW_ALPHA);
    if (!visible) {
        ShowWindow(win, SW_SHOWNOACTIVATE);
        visible = true;
    }
}

bool overlay_save(const char *path) {
    RECT r;
    long area = 0;
    if (!game) EnumThreadWindows(GetCurrentThreadId(), find_game, (LPARAM)&area);
    if (!game || !GetClientRect(game, &r) || r.right <= 0 || r.bottom <= 0) return false;
    int w = r.right, h = r.bottom;
    BITMAPINFO bi;
    DWORD *bits;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    HDC from = GetDC(game), to = CreateCompatibleDC(from);
    HBITMAP bm = CreateDIBSection(to, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    if (!bm) return false;
    HGDIOBJ old = SelectObject(to, bm);
    BitBlt(to, 0, 0, w, h, from, 0, 0, SRCCOPY);
    GdiFlush();
    if (visible && pixels && w == width && h == height)
        for (int i = 0; i < w * h; i++) {
            DWORD o = pixels[i], b = bits[i];
            int ia = 255 - (int)(o >> 24);
            int rr = (int)((o >> 16) & 255) + (int)((b >> 16) & 255) * ia / 255;
            int gg = (int)((o >> 8) & 255) + (int)((b >> 8) & 255) * ia / 255;
            int bb = (int)(o & 255) + (int)(b & 255) * ia / 255;
            bits[i] = (DWORD)rr << 16 | (DWORD)gg << 8 | (DWORD)bb;
        }
    bi.bmiHeader.biHeight = h;
    BITMAPFILEHEADER fh;
    memset(&fh, 0, sizeof fh);
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof bi.bmiHeader;
    fh.bfSize = fh.bfOffBits + (DWORD)w * h * 4;
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD done;
    if (f != INVALID_HANDLE_VALUE) {
        WriteFile(f, &fh, sizeof fh, &done, NULL);
        WriteFile(f, &bi.bmiHeader, sizeof bi.bmiHeader, &done, NULL);
        for (int y = h - 1; y >= 0; y--) WriteFile(f, bits + y * w, (DWORD)w * 4, &done, NULL);
        CloseHandle(f);
    }
    SelectObject(to, old);
    DeleteObject(bm);
    DeleteDC(to);
    ReleaseDC(game, from);
    return f != INVALID_HANDLE_VALUE;
}
