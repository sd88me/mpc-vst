/* render_preview.c -- host-side preview tool for Maze Voice's shadow-mode
 * pages. Shares the same drawing primitives/widget logic that gets
 * ported into force_shadow.c's real renderer, but outputs a plain PPM
 * image instead of writing into a DRM dumb buffer -- lets UI layout be
 * iterated and checked visually on a dev machine, without a live device
 * round-trip for every tweak. Builds and runs natively (no cross-compile,
 * no QEMU): `gcc -O2 -o render_preview render_preview.c -lm && ./render_preview`.
 *
 * Deliberately skips the portrait buffer transform live load test #6
 * established (see DESIGN.md) -- this renders straight into a landscape
 * RGB canvas, since that transform is already proven separately and
 * isn't what's being checked here (page layout, widget legibility,
 * knob/label positioning). The real force_shadow.c build still goes
 * through put_px_land()'s transform as always.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "../src/font8x8.h"

#define LAND_W 1280
#define LAND_H 800

static uint8_t canvas[LAND_H][LAND_W][3];

typedef struct { uint8_t r, g, b; } rgb_t;
static rgb_t rgb(uint32_t hex) {
    rgb_t c = { (hex >> 16) & 0xff, (hex >> 8) & 0xff, hex & 0xff };
    return c;
}

/* ---- palette (Maze Voice web GUI default; overridable per-page by
 * theme_<name>=RRGGBB / style= top-level .conf keys -- see load_conf()'s
 * top-level key handling and force_shadow.c's own THEME_DEFAULT/theme_
 * parsing, which this mirrors field-for-field). Not `const` any more
 * since a page can override them at load time. ---- */
static uint32_t PLATE      = 0x131211;
static uint32_t PLATE_HI   = 0x1c1a17;
static uint32_t PLATE_LINE = 0x2a2823;
static uint32_t INK        = 0xefe9d8;
static uint32_t INK_DIM    = 0x8f8878;
static uint32_t INK_FAINT  = 0x5c584c;
static uint32_t ACCENT     = 0xc1552f;
static uint32_t ACCENT_HI  = 0xe2793f;
static uint32_t KNOB_FACE  = 0xefe9d8;
static uint32_t KNOB_RING  = 0x2a2823;
static uint32_t BAR_BG     = 0x0d0c0a;
static uint32_t TAB_ON_BG  = 0x1a120d;   /* theme_tab_on */
static uint32_t TAB_ON_TX  = 0xfdf3ea;   /* theme_tab_on_tx (default matches BTN_TEXT's own) */
static uint32_t SEG_ACTIVE    = 0xf2f1ee;   /* theme_seg_active */
static uint32_t SEG_INACTIVE  = 0x050403;   /* theme_seg_inactive */
static uint32_t SEG_ACTIVE_TX = 0x1c1a17;   /* theme_seg_active_tx */
static uint32_t LCD_BG        = 0x1a120d;   /* theme_lcd */
static uint32_t BTN_TEXT      = 0x050403;   /* theme_btn_text */
static uint32_t KNOB_DOT_COLOR = 0xc1552f;  /* theme_knob_dot -- defaults to ACCENT's own
                                                default, mirroring force_shadow.c's
                                                THEME_DEFAULT.knob_dot */

/* style=td3 ("Acid") -- light chassis, charcoal boxes, red buttons, pill
 * engine button and tab highlight. See force_shadow.c's own ui_theme_t
 * comment ("style=td3 (Acid): light chassis, charcoal boxes, red
 * buttons, pill engine button") -- this preview mirrors that branch, not
 * an approximation of it. */
static int      G_TD3      = 0;
static int      G_LCD      = 0;   /* style=lcd -- LCD nameplate title box (DX7's own style) */
static uint32_t TD3_BOX        = 0x1f1f1f;
static uint32_t TD3_BTN_BG     = 0xe8341c;
static uint32_t TD3_CHROME_INK = 0x0a0a0a;
static uint32_t TD3_GO_ON      = 0x35d07f;
static uint32_t TD3_GO_OFF     = 0xe8341c;
static uint32_t TD3_TABS_BG    = 0xd9ac00;

/* topbar_style=display -- whole top bar is a simulated dot-matrix LCD (JV-880/
 * DX7's own style, per their shadow_page.conf headers). Mirrors force_shadow.c's
 * ui_theme_t.dsp/dsp_bg/dsp_cell/dsp_ink/dsp_off/dsp_bezel field-for-field --
 * this preview used to have no idea these keys existed and fell through to
 * the plain default topbar instead, which is why it didn't match the real
 * device render for these two addons. Defaults match force_shadow.c's own
 * THEME_DEFAULT fallback values. */
static int      G_DSP       = 0;
static uint32_t DSP_BG      = 0x1c2612;
static uint32_t DSP_CELL    = 0x24301a;
static uint32_t DSP_INK     = 0xcdeb63;
static uint32_t DSP_OFF     = 0x2c3a1d;
static uint32_t DSP_BEZEL   = 0x0d1108;

/* ---- primitives ---- */
static void put_px(int x, int y, uint32_t color) {
    if (x < 0 || x >= LAND_W || y < 0 || y >= LAND_H) return;
    rgb_t c = rgb(color);
    canvas[y][x][0] = c.r; canvas[y][x][1] = c.g; canvas[y][x][2] = c.b;
}
static void fill_rect(int x0, int y0, int w, int h, uint32_t color) {
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++)
            put_px(x, y, color);
}
/* Mirrors force_shadow.c's own put_px_blend_land/circle_edge_coverage
 * (see that file's own comments for why: no sqrt, an integer
 * approximation of (r-dist) good enough for a ~1px AA band) so this
 * preview stays a faithful reference for the real anti-aliased edges. */
static void put_px_blend(int x, int y, uint32_t color, int alpha) {
    if (alpha <= 0) return;
    if (alpha >= 255) { put_px(x, y, color); return; }
    if (x < 0 || x >= LAND_W || y < 0 || y >= LAND_H) return;
    rgb_t bg_c = { canvas[y][x][0], canvas[y][x][1], canvas[y][x][2] };
    rgb_t fg_c = rgb(color);
    canvas[y][x][0] = (uint8_t)((fg_c.r * alpha + bg_c.r * (255 - alpha)) / 255);
    canvas[y][x][1] = (uint8_t)((fg_c.g * alpha + bg_c.g * (255 - alpha)) / 255);
    canvas[y][x][2] = (uint8_t)((fg_c.b * alpha + bg_c.b * (255 - alpha)) / 255);
}
static int circle_edge_coverage(int d2, int r) {
    if (r <= 0) return 0;
    int cov = 128 + ((r * r - d2) * 128) / (2 * r);
    if (cov < 0) cov = 0;
    if (cov > 255) cov = 255;
    return cov;
}
static void fill_circle(int cx, int cy, int r, uint32_t color) {
    for (int y = -r - 1; y <= r + 1; y++)
        for (int x = -r - 1; x <= r + 1; x++) {
            int cov = circle_edge_coverage(x*x + y*y, r);
            if (cov <= 0) continue;
            if (cov >= 255) put_px(cx + x, cy + y, color);
            else put_px_blend(cx + x, cy + y, color, cov);
        }
}
static void draw_ring(int cx, int cy, int r, int thick, uint32_t color) {
    int r_in = r - thick;
    for (int y = -r - 1; y <= r + 1; y++)
        for (int x = -r - 1; x <= r + 1; x++) {
            int d2 = x*x + y*y;
            int outer_cov = circle_edge_coverage(d2, r);
            int inner_cov = 255 - circle_edge_coverage(d2, r_in);
            int cov = outer_cov < inner_cov ? outer_cov : inner_cov;
            if (cov <= 0) continue;
            if (cov >= 255) put_px(cx + x, cy + y, color);
            else put_px_blend(cx + x, cy + y, color, cov);
        }
}
static void draw_hline(int x0, int y, int w, uint32_t color) { fill_rect(x0, y, w, 1, color); }
static void draw_vline(int x, int y0, int h, uint32_t color) { fill_rect(x, y0, 1, h, color); }

/* Proper rounded rect (corner insets from the circle equation), ported
 * verbatim from force_shadow.c's own fill_rr_land() -- used for style=td3
 * frames/buttons/tab pills/engine button. */
static void fill_rr(int x, int y, int w, int h, int r, uint32_t color) {
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    for (int j = 0; j < h; j++) {
        int dy = j < r ? r - j - 1 : (j >= h - r ? j - (h - r) : -1);
        int inset = 0;
        if (dy >= 0) {
            while (inset < r && (r - inset - 1) * (r - inset - 1) + dy * dy >= r * r) inset++;
        }
        fill_rect(x + inset, y + j, w - 2 * inset, 1, color);
    }
}
/* Small filled triangle arrow, ported from force_shadow.c's draw_arrow_land()
 * intent (stepper's prev/next glyphs) -- not byte-identical geometry, but
 * the same recognizable shape at the same scale. dir: -1 left, 1 right. */
static void draw_arrow(int cx, int cy, int size, int dir, uint32_t color) {
    int base_x = cx - dir * size;   /* flat side */
    for (int c = 0; c <= size; c++) {
        int half = size - c;        /* full height at the base, tapers to the tip */
        fill_rect(base_x + dir * c, cy - half, 1, half * 2 + 1, color);
    }
}

/* ---- Dot-matrix display (topbar_style=display), ported from
 * force_shadow.c's own DOTFONT/dot_glyph/dot_text_width/dot_cell/
 * dot_cell_fit (5x7 HD44780-style font, distinct from the 8x8 font below --
 * this is what JV-880/DX7's "yellow-green LCD-backlight" topbar actually
 * draws; without it, this preview fell back to the plain default topbar,
 * which is why it didn't match how these two addons really look. */
typedef struct { char c; uint8_t r[7]; } dotglyph_t;
static const dotglyph_t DOTFONT[] = {
 {'0',{14,17,19,21,25,17,14}},{'1',{4,12,4,4,4,4,14}},{'2',{14,17,1,2,4,8,31}},{'3',{31,2,4,2,1,17,14}},
 {'4',{2,6,10,18,31,2,2}},{'5',{31,16,30,1,1,17,14}},{'6',{6,8,16,30,17,17,14}},{'7',{31,1,2,4,8,8,8}},
 {'8',{14,17,17,14,17,17,14}},{'9',{14,17,17,15,1,2,12}},
 {'A',{14,17,17,31,17,17,17}},{'B',{30,17,17,30,17,17,30}},{'C',{14,17,16,16,16,17,14}},{'D',{28,18,17,17,17,18,28}},
 {'E',{31,16,16,30,16,16,31}},{'F',{31,16,16,30,16,16,16}},{'G',{14,17,16,23,17,17,15}},{'H',{17,17,17,31,17,17,17}},
 {'I',{14,4,4,4,4,4,14}},{'J',{7,2,2,2,2,18,12}},{'K',{17,18,20,24,20,18,17}},{'L',{16,16,16,16,16,16,31}},
 {'M',{17,27,21,21,17,17,17}},{'N',{17,17,25,21,19,17,17}},{'O',{14,17,17,17,17,17,14}},{'P',{30,17,17,30,16,16,16}},
 {'Q',{14,17,17,17,21,18,13}},{'R',{30,17,17,30,20,18,17}},{'S',{15,16,16,14,1,1,30}},{'T',{31,4,4,4,4,4,4}},
 {'U',{17,17,17,17,17,17,14}},{'V',{17,17,17,17,17,10,4}},{'W',{17,17,17,21,21,21,10}},{'X',{17,17,10,4,10,17,17}},
 {'Y',{17,17,10,4,4,4,4}},{'Z',{31,1,2,4,8,16,31}},
 {'-',{0,0,0,31,0,0,0}},{'.',{0,0,0,0,0,12,12}},{'/',{1,1,2,4,8,16,16}},{':',{0,12,12,0,12,12,0}},
 {'+',{0,4,4,31,4,4,0}},{'#',{10,10,31,10,31,10,10}},{'&',{12,18,20,8,21,18,13}},{'>',{16,8,4,2,4,8,16}},
};
static const uint8_t *dot_glyph(char ch) {
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    for (size_t i = 0; i < sizeof(DOTFONT) / sizeof(DOTFONT[0]); i++)
        if (DOTFONT[i].c == ch) return DOTFONT[i].r;
    return NULL;
}
static int dot_text_width(const char *s, int p) { return (int)strlen(s) * 6 * p; }
static void dot_cell(int x, int y, int w, int h, const char *s, int p,
                      uint32_t cell_bg, uint32_t unlit, uint32_t lit) {
    fill_rr(x, y, w, h, 5, cell_bg);
    int ncols = s ? (int)strlen(s) * 6 - 1 : 0;
    int gcols = (w - 8) / p, grows = (h - 6) / p;
    int gx = x + (w - gcols * p) / 2 + (p - (p > 3 ? 3 : 2)) / 2, gy = y + (h - grows * p) / 2 + 1;
    int du = p - 2;
    int c0 = (gcols - ncols) / 2, r0 = (grows - 7) / 2;
    for (int r = 0; r < grows; r++)
        for (int c = 0; c < gcols; c++)
            fill_rect(gx + c * p, gy + r * p, du, du, unlit);
    for (int i = 0; s && s[i]; i++) {
        const uint8_t *g = dot_glyph(s[i]);
        if (!g) continue;
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 5; c++)
                if (g[r] & (16 >> c))
                    fill_rect(gx + (c0 + i * 6 + c) * p, gy + (r0 + r) * p, du, du, lit);
    }
}
static void dot_cell_fit(int x, int y, int w, int h, const char *s,
                          uint32_t cell_bg, uint32_t unlit, uint32_t lit) {
    char tb[48]; snprintf(tb, sizeof(tb), "%s", s);
    int p = 4;
    if (dot_text_width(tb, p) > w - 20) p = 3;
    while (strlen(tb) > 1 && dot_text_width(tb, p) > w - 20) tb[strlen(tb) - 1] = 0;
    dot_cell(x, y, w, h, tb, p, cell_bg, unlit, lit);
}

/* ---- text (font8x8.h) ---- */
#define GLYPH_CELL 9
static int font_glyph_index(char ch) {
    for (size_t i = 0; i < strlen(font_chars); i++)
        if (font_chars[i] == ch) return (int)i;
    return 0; /* space */
}
/* Every glyph previously advanced by a fixed (GLYPH_CELL+1) cell regardless of its actual ink
 * width, so "i" and "M" took the same space -- a typewriter/monospace look, not the "normal
 * typed font spacing" callers actually want (mpc-vst-plugins docs/NOTES.md, jv880 port). Advance
 * by each glyph's real rightmost lit column instead, computed once and cached (81-cell scan is
 * cheap, but this runs per character per draw call). Rendering itself (draw_char) is unchanged --
 * always draws the full 9x9 box -- only the gap between characters gets tighter for narrow ones. */
static int font_glyph_width(int idx) {
    static int cache[128]; static char have[128];
    if (idx >= 0 && idx < 128 && have[idx]) return cache[idx];
    const uint8_t *g = font8x8[idx];
    int maxcol = -1;
    for (int row = 0; row < GLYPH_CELL; row++)
        for (int col = 0; col < GLYPH_CELL; col++)
            if (g[row * GLYPH_CELL + col] > 0 && col > maxcol) maxcol = col;
    int w = maxcol < 0 ? 4 : maxcol + 2;   /* blank glyph (space): fixed narrow advance */
    if (idx >= 0 && idx < 128) { cache[idx] = w; have[idx] = 1; }
    return w;
}
/* `scale` is a float here too, mirroring force_shadow.c's own fractional-
 * scale support (2026-09-19) -- nearest-neighbor destination-pixel
 * upscale of the coverage bitmap, so a specific widget (knob label/value)
 * can use e.g. 1.5x without every integer-scaled caller changing too. */
static void draw_char(int x, int y, char ch, float scale, uint32_t color) {
    const uint8_t *g = font8x8[font_glyph_index(ch)];
    int out_cell = (int)(GLYPH_CELL * scale + 0.5f);
    for (int dy = 0; dy < out_cell; dy++) {
        int row = (int)((float)dy / scale);
        if (row >= GLYPH_CELL) row = GLYPH_CELL - 1;
        for (int dx = 0; dx < out_cell; dx++) {
            int col = (int)((float)dx / scale);
            if (col >= GLYPH_CELL) col = GLYPH_CELL - 1;
            int cov = g[row * GLYPH_CELL + col];
            if (cov <= 0) continue;
            put_px_blend(x + dx, y + dy, color, cov);
        }
    }
}
static int text_width(const char *s, float scale) {
    float w = 0;
    for (const char *p = s; *p; p++) w += (float)font_glyph_width(font_glyph_index(*p)) * scale;
    return (int)w;
}
static void draw_text(int x, int y, const char *s, float scale, uint32_t color) {
    int cx = x;
    for (const char *p = s; *p; p++) {
        int idx = font_glyph_index(*p);
        draw_char(cx, y, *p, scale, color);
        cx += (int)((float)font_glyph_width(idx) * scale);
    }
}
static void draw_text_c(int cx, int y, const char *s, float scale, uint32_t color) {
    draw_text(cx - text_width(s, scale)/2, y, s, scale, color);
}

/* ---- knob pointer angle (libm ok here -- host tool only) ---- */
static void knob_dot(int cx, int cy, int r, int pct, int *dx, int *dy) {
    double angle = (-135.0 + 270.0 * pct / 100.0) * M_PI / 180.0;
    int dist = (int)(r * 0.72);
    *dx = cx + (int)(dist * sin(angle));
    *dy = cy - (int)(dist * cos(angle));
}

/* ---- widgets ---- */
static void widget_knob(int cx, int cy, int r, int pct, const char *label, const char *value) {
    draw_ring(cx, cy, r + 3, 3, KNOB_RING);
    fill_circle(cx, cy, r, KNOB_FACE);
    int dx, dy; knob_dot(cx, cy, r, pct, &dx, &dy);
    fill_circle(dx, dy, r/7 + 2, KNOB_DOT_COLOR);
    /* Mirrors force_shadow.c's render_widget() -- full 1.5x on both label
     * and value now (2026-09-19: "increase everything by 50%"). */
    draw_text_c(cx, cy + r + 12, label, 1.5f, INK);
    draw_text_c(cx, cy + r + 29, value, 1.5f, INK_FAINT);
}
static void widget_toggle(int cx, int cy, const char *label, int on) {
    int pw = 51, ph = 27;
    fill_rect(cx - pw/2, cy - ph/2, pw, ph, 0x050403);
    draw_ring(cx - pw/2 + ph/2, cy, ph/2 - 2, 1, PLATE_LINE);
    int lx = on ? (cx + pw/2 - ph/2) : (cx - pw/2 + ph/2);
    fill_circle(lx, cy, ph/2 - 4, on ? ACCENT_HI : 0x4c473d);
    draw_text_c(cx, cy + ph/2 + 10, label, 1.5f, INK);
}
/* color_override: 0 = use theme.btn_bg (TD3_BTN_BG/ACCENT) as before;
 * mirrors force_shadow.c's own ui_widget_t.btn_color/has_btn_color
 * (`color=` on a `button` line) -- one button (e.g. SEARCH) can stand
 * out from the page's usual button color without a second theme. */
static void widget_button(int cx, int cy, const char *label, uint32_t color_override) {
    int w = text_width(label, 1.15f) + 36, h = 39;   /* scale was 1.5f, see frame_box()'s comment */
    if (G_TD3) {
        w += 24; h = 48;
        uint32_t bg = color_override ? color_override : TD3_BTN_BG;
        fill_rr(cx - w/2 - 2, cy - h/2 - 2, w + 4, h + 4, 10, PLATE_LINE);
        fill_rr(cx - w/2, cy - h/2, w, h, 8, bg);
        draw_text_c(cx, cy - 7, label, 1.15f, BTN_TEXT);
        return;
    }
    fill_rect(cx - w/2, cy - h/2, w, h, color_override ? color_override : ACCENT);
    draw_text_c(cx, cy - 5, label, 1.15f, 0xfdf3ea);
}
static void widget_enum_h(int cx, int cy, const char *label, const char **opts, int n, int active, int sw_override) {
    int seg_w = sw_override > 0 ? sw_override : 117, seg_h = 33, gap = 2;
    draw_text_c(cx, cy - seg_h/2 - 22, label, 1.5f, INK);
    int total = n * seg_w + (n-1)*gap;
    int x0 = cx - total/2;
    for (int i = 0; i < n; i++) {
        int x = x0 + i*(seg_w+gap);
        fill_rect(x, cy - seg_h/2, seg_w, seg_h, i == active ? SEG_ACTIVE : SEG_INACTIVE);
        draw_text_c(x + seg_w/2, cy - seg_h/2 + seg_h/2 - 6, opts[i], 1.5f, i == active ? SEG_ACTIVE_TX : INK_DIM);
    }
}
static void widget_enum_v(int cx, int cy, const char *label, const char **opts, int n, int active) {
    int seg_w = 135, seg_h = 30, gap = 2;
    int hit_hh = (n*(seg_h+gap))/2;
    draw_text_c(cx, cy - hit_hh - 24, label, 1.5f, ACCENT_HI);
    int y0 = cy - hit_hh;
    for (int i = 0; i < n; i++) {
        int y = y0 + i*(seg_h+gap);
        fill_rect(cx - seg_w/2, y, seg_w, seg_h, i == active ? SEG_ACTIVE : SEG_INACTIVE);
        draw_text_c(cx, y + seg_h/2 - 6, opts[i], 1.5f, i == active ? SEG_ACTIVE_TX : INK_DIM);
    }
}
static void frame_box(int x, int y, int w, int h, const char *title) {
    /* Title scale was 1.5f: the baked font (font8x8.h) has lowercase, but at
     * 1.5x its fixed monospace cell (10px/char * scale) read as too wide once
     * callers moved off all-caps text -- see mpc-vst-plugins docs/NOTES.md
     * ("No draggable/graph widgets..." entry's neighbour) for where this was
     * first noticed (an mpc-vst-plugins skin build, which #includes this file
     * as its production asset renderer -- force_shadow.c's own on-device
     * renderer is a separate, hand-ported copy and is untouched by this). */
    if (G_TD3) {
        fill_rr(x, y, w, h, 10, PLATE_LINE);
        fill_rr(x + 2, y + 2, w - 4, h - 4, 9, TD3_BOX);
        draw_text(x + 20, y + 14, title, 1.15f, ACCENT);
        fill_rect(x + 18, y + 38, w - 36, 1, INK_FAINT);
        return;
    }
    draw_ring(0,0,0,0,0); /* no-op, keeps signature symmetry */
    fill_rect(x, y, w, 1, PLATE_LINE);
    fill_rect(x, y, 1, h, PLATE_LINE);
    fill_rect(x+w-1, y, 1, h, PLATE_LINE);
    fill_rect(x, y+h-1, w, 1, PLATE_LINE);
    draw_text(x + 18, y + 14, title, 1.15f, ACCENT_HI);
    fill_rect(x + 18, y + 36, w - 36, 1, PLATE_LINE);
}

/* ---- chrome: top bar + tab bar ---- */
#define TOPBAR_H 72
#define TABBAR_H 72
/* Was briefly pulled up to a 720px "TOUCHABLE_H" (2026-09-18/19) on the
 * mistaken belief the touch digitizer can't sense the bottom 80px of the
 * real 800px screen. Live testing (2026-09-19) found the real bug was
 * force_shadow.c's touch_to_landscape() py scale topping out at 720
 * instead of 800 -- fixed there, so the tab bar belongs flush against
 * LAND_H again. Kept in sync with force_shadow.c so this preview stays a
 * faithful reference for future pages. */
static const char *TABS[] = { "VOICE", "WAVEFOLDER / FILTER", "MOD / RANDOM / MIX" };

/* Top-bar engine on/off button (2026-09-19) -- replaces the old static
 * "LIVE" text. Geometry kept in sync with force_shadow.c's own
 * ENGINE_BTN_X/Y/W/H. */
#define ENGINE_BTN_W 220
#define ENGINE_BTN_H 40
#define ENGINE_BTN_X (LAND_W - ENGINE_BTN_W - 20)
#define ENGINE_BTN_Y 16


/* ---- generic .conf-driven rendering (force-webstream preview) ----
 * Not the real parse_shadow_page_conf() from force_shadow.c (that one is
 * entangled with live polling/socket/addon_table state not needed here) -
 * this is a small purpose-built parser for exactly the widget kinds our
 * page uses (frame/button/readout/knob/enum_h/toggle/list), driving the
 * SAME widget_* draw calls above plus two new ones (widget_readout,
 * widget_list) ported from force_shadow.c's render_widget() W_READOUT/
 * W_LIST cases (see that function, ~line 2001) so the visual output
 * matches the real renderer, not an approximation. */

static void widget_readout(int cx, int cy, int w, int h, const char *label, const char *text) {
    int x0 = cx - w/2, y0 = cy - h/2;
    if (label[0]) draw_text(x0, y0 - 22, label, 1.5f, INK_DIM);
    /* topbar_style=display: this readout sits in the dot-matrix LCD bar
     * (JV-880/DX7's bank_name display) -- drawn as a dot cell, not the
     * plain LCD-well box every other page uses. See draw_chrome_named()'s
     * own G_DSP branch for the surrounding bezel. */
    if (G_DSP && cy < TOPBAR_H) {
        dot_cell_fit(x0, y0, w, h, text, DSP_CELL, DSP_OFF, DSP_INK);
        return;
    }
    fill_rect(x0, y0, w, h, LCD_BG);
    fill_rect(x0, y0, w, 1, PLATE_LINE);
    fill_rect(x0, y0 + h - 1, w, 1, PLATE_LINE);
    float sc = 2.0f;
    char tb[64]; snprintf(tb, sizeof(tb), "%s", text);
    while (strlen(tb) > 1 && text_width(tb, sc) > w - 16) tb[strlen(tb) - 1] = 0;
    draw_text_c(cx, cy - (int)(4.5f * sc), tb, sc, ACCENT);
}

/* `<  text  >` index control, ported from force_shadow.c's render_widget()
 * W_STEPPER case (~line 2052): square rounded end buttons with an arrow
 * glyph flank a centre LCD box showing the current text. */
static void widget_stepper(int cx, int cy, int w, int h, const char *label, const char *text) {
    int x0 = cx - w/2, y0 = cy - h/2;
    if (label[0]) draw_text(x0, y0 - 22, label, 1.5f, INK_DIM);
    int topdsp = G_DSP && cy < TOPBAR_H;
    uint32_t abg = topdsp ? DSP_BEZEL : PLATE_LINE;
    uint32_t afg = topdsp ? DSP_BG    : ACCENT_HI;
    fill_rr(x0, y0, h, h, 5, abg);
    fill_rr(x0 + w - h, y0, h, h, 5, abg);
    draw_arrow(x0 + h/2, cy, h/4, -1, afg);
    draw_arrow(x0 + w - h/2, cy, h/4, 1, afg);
    int bx = x0 + h + 3, bw = w - 2*h - 6;
    if (topdsp) {
        dot_cell_fit(bx, y0, bw, h, text, DSP_CELL, DSP_OFF, DSP_INK);
        return;
    }
    fill_rect(bx, y0, bw, h, LCD_BG);
    fill_rect(bx, y0, bw, 1, PLATE_LINE);
    fill_rect(bx, y0 + h - 1, bw, 1, PLATE_LINE);
    char tb[48]; snprintf(tb, sizeof(tb), "%s", text);
    while (strlen(tb) > 1 && text_width(tb, 1.5f) > bw - 16) tb[strlen(tb) - 1] = 0;
    draw_text_c(bx + bw/2, cy - 7, tb, 1.5f, ACCENT);
}

/* x,y = TOP-LEFT (matches shadow_page.conf's `list` spec, unlike every
 * other widget here which takes a centre) */
static void widget_list(int x, int y, int w, int h, const char **items, int n,
                         int sel, int cols, int rows, int tile_h, int gap, int numbered) {
    int tw = (w - (cols - 1) * gap) / cols;
    int pp = cols * rows;
    for (int p = 0; p < pp && p < n; p++) {
        int col = p % cols, row = p / cols;
        int tx = x + col * (tw + gap);
        int ty = y + row * (tile_h + gap);
        int is_sel = (p == sel);
        fill_rect(tx, ty, tw, tile_h, is_sel ? ACCENT : LCD_BG);
        if (!is_sel) {
            fill_rect(tx, ty, tw, 1, PLATE_LINE);
            fill_rect(tx, ty + tile_h - 1, tw, 1, PLATE_LINE);
        }
        char tb[64];
        if (numbered) snprintf(tb, sizeof(tb), "%02d %s", p + 1, items[p]);
        else snprintf(tb, sizeof(tb), "%s", items[p]);
        while (strlen(tb) > 1 && text_width(tb, 1.5f) > tw - 20) tb[strlen(tb) - 1] = 0;
        draw_text(tx + 10, ty + tile_h/2 - 7, tb, 1.5f, is_sel ? BTN_TEXT : ACCENT);
    }
    int grid_h = rows * tile_h + (rows - 1) * gap;
    if (grid_h > h)
        fprintf(stderr, "WARNING: list grid_h=%d exceeds declared h=%d (cols=%d rows=%d th=%d gap=%d)\n",
                grid_h, h, cols, rows, tile_h, gap);
}

static void draw_chrome_named(const char *title, const char **tabs, int ntabs, int active_tab, int engine_on) {
    fill_rect(0, 0, LAND_W, LAND_H, PLATE);
    fill_rect(0, 0, LAND_W, TOPBAR_H, PLATE_HI);
    draw_hline(0, TOPBAR_H, LAND_W, PLATE_LINE);

    if (G_DSP) {
        /* Whole bar = backlit dot-matrix LCD: dark rounded bezel, green
         * (or whatever theme_display_* says) glass, dark dot cells for the
         * nameplate and engine button. Ported from force_shadow.c's own
         * G_DSP/th.dsp branch (~line 2489) -- this preview used to have no
         * idea this style existed and fell through to the plain default
         * title text instead, which is why JV-880/DX7 didn't match their
         * real on-device look. */
        fill_rr(8, 5, LAND_W - 16, TOPBAR_H - 10, 12, DSP_BEZEL);
        fill_rr(12, 9, LAND_W - 24, TOPBAR_H - 18, 9, DSP_BG);
        int tw_px = dot_text_width(title, 4) + 24;
        fill_rr(17, 11, tw_px + 6, 50, 8, DSP_BEZEL);
        dot_cell(20, 14, tw_px, 44, title, 4, DSP_BEZEL, 0x1c2612, 0xcdeb63);
    } else if (G_LCD) {
        /* LCD nameplate instead of the plain title (DX7's own style, ported
         * from force_shadow.c's th.lcd branch, ~line 2498) -- this preview
         * previously fell through to the plain title text for style=lcd
         * too, which is the other half of why DX7 didn't match its real
         * on-device look. */
        int tw_px = text_width(title, 2.5f) + 48;
        fill_rect(24, 12, tw_px, TOPBAR_H - 24, LCD_BG);
        fill_rect(24, 12, tw_px, 1, ACCENT);
        fill_rect(24, TOPBAR_H - 13, tw_px, 1, ACCENT);
        draw_text(48, 24, title, 2.5f, ACCENT_HI);
        fill_rect(0, TOPBAR_H - 2, LAND_W, 2, ACCENT);
    } else {
        draw_text(40, G_TD3 ? 20 : 28, title, G_TD3 ? 3 : 2, G_TD3 ? TD3_CHROME_INK : INK);
    }

    if (G_DSP) {
        /* Outlined cell always; ON = inverted (dark glass, lit dots). */
        fill_rr(ENGINE_BTN_X - 3, 11, ENGINE_BTN_W + 6, 50, 8, DSP_BEZEL);
        if (engine_on)
            dot_cell(ENGINE_BTN_X, 14, ENGINE_BTN_W, 44, "POWER ON", 3, DSP_BEZEL, 0x1c2612, 0xcdeb63);
        else
            dot_cell(ENGINE_BTN_X, 14, ENGINE_BTN_W, 44, "POWER OFF", 3, DSP_CELL, DSP_OFF, DSP_INK);
    } else if (G_TD3) {
        /* black pill: lit dot + START (red) when stopped, RUNNING (green) when up */
        uint32_t c = engine_on ? TD3_GO_ON : TD3_GO_OFF;
        fill_rr(ENGINE_BTN_X, ENGINE_BTN_Y, ENGINE_BTN_W, ENGINE_BTN_H, ENGINE_BTN_H/2, PLATE_LINE);
        fill_circle(ENGINE_BTN_X + 26, ENGINE_BTN_Y + ENGINE_BTN_H/2, 8, c);
        draw_text_c(ENGINE_BTN_X + ENGINE_BTN_W/2 + 14, ENGINE_BTN_Y + ENGINE_BTN_H/2 - 6,
                    engine_on ? "RUNNING" : "START", 2, c);
    } else {
        uint32_t bbg = engine_on ? ACCENT : PLATE_LINE;
        uint32_t bfg = engine_on ? INK : INK_FAINT;
        fill_rect(ENGINE_BTN_X, ENGINE_BTN_Y, ENGINE_BTN_W, ENGINE_BTN_H, bbg);
        draw_text_c(ENGINE_BTN_X + ENGINE_BTN_W/2, ENGINE_BTN_Y + ENGINE_BTN_H/2 - 6,
                    engine_on ? "POWER ON" : "POWER OFF", 2, bfg);
    }

    int tabbar_y = LAND_H - TABBAR_H;
    fill_rect(0, tabbar_y, LAND_W, TABBAR_H, G_TD3 ? TD3_TABS_BG : BAR_BG);
    draw_hline(0, tabbar_y, LAND_W, G_TD3 ? TD3_BOX : PLATE_LINE);
    int tw = LAND_W / ntabs;
    for (int i = 0; i < ntabs; i++) {
        if (G_TD3) {
            if (i == active_tab) fill_rr(i*tw + 10, tabbar_y + 10, tw - 20, TABBAR_H - 14, 8, TAB_ON_BG);
            /* TAB_ON_TX, decoupled from both ACCENT and BTN_TEXT (a real
             * bug force-cratedigger's own page hit first: BTN_TEXT and a
             * dark tab_on_bg happened to work for Acid's palette but not
             * generally) -- see force_shadow.c's own comment. */
            draw_text_c(i*tw + tw/2, tabbar_y + TABBAR_H/2 - 4, tabs[i], 2,
                        i == active_tab ? TAB_ON_TX : TD3_CHROME_INK);
            continue;
        }
        if (i == active_tab) {
            fill_rect(i*tw, tabbar_y, tw, 3, ACCENT);
            fill_rect(i*tw, tabbar_y, tw, TABBAR_H, TAB_ON_BG);
        }
        draw_text_c(i*tw + tw/2, tabbar_y + TABBAR_H/2 - 6,
                    tabs[i], 2, i == active_tab ? INK : INK_FAINT);
    }
}

/* ---- minimal shadow_page.conf parser: frame/button/readout/knob/enum_h/toggle/list only ---- */
#define MAX_TABS 8
#define MAX_LINES_PER_TAB 64

typedef struct { char tab[24]; char line[512]; } conf_line_t;

static conf_line_t g_lines[MAX_TABS][MAX_LINES_PER_TAB];
static int g_line_count[MAX_TABS] = {0};
static char g_tab_names[MAX_TABS][24];
static int g_ntabs = 0;
static char g_display_name[64] = "SHADOW PAGE";

/* pulls a quoted "..." value or a bare token after `key=` */
static int kv_str(const char *line, const char *key, char *out, size_t outlen) {
    char pat[32]; snprintf(pat, sizeof(pat), "%s=", key);
    const char *p = strstr(line, pat);
    if (!p) return 0;
    p += strlen(pat);
    if (*p == '"') {
        p++;
        const char *end = strchr(p, '"');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n >= outlen) n = outlen - 1;
        memcpy(out, p, n); out[n] = 0;
    } else {
        size_t n = 0;
        while (p[n] && p[n] != ' ' && p[n] != '\n') n++;
        if (n >= outlen) n = outlen - 1;
        memcpy(out, p, n); out[n] = 0;
    }
    return 1;
}
static int kv_int(const char *line, const char *key, int def) {
    char buf[32];
    if (!kv_str(line, key, buf, sizeof(buf))) return def;
    return atoi(buf);
}
static float kv_float(const char *line, const char *key, float def) {
    char buf[32];
    if (!kv_str(line, key, buf, sizeof(buf))) return def;
    return (float)atof(buf);
}

static void load_conf(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
    char line[1024];
    int cur_tab = -1;
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n'); if (nl) *nl = 0;
        char *s = line; while (*s == ' ' || *s == '\t') s++;
        if (!*s || *s == '#') continue;
        if (!strncmp(s, "display_name=", 13)) { kv_str(s, "display_name", g_display_name, sizeof(g_display_name)); continue; }
        if (!strncmp(s, "style=", 6)) { G_TD3 = !strcmp(s + 6, "td3"); G_LCD = !strcmp(s + 6, "lcd"); continue; }
        if (!strncmp(s, "topbar_style=", 13)) { G_DSP = !strcmp(s + 13, "display"); continue; }
        if (!strncmp(s, "theme_", 6)) {
            /* theme_<name>=RRGGBB (no '#') -- mirrors force_shadow.c's own
             * theme_ parsing (parse_shadow_page_conf()) field-for-field. */
            char key[32] = {0}, val[16] = {0};
            const char *eq = strchr(s, '=');
            if (eq) {
                size_t klen = (size_t)(eq - (s + 6));
                if (klen >= sizeof(key)) klen = sizeof(key) - 1;
                memcpy(key, s + 6, klen); key[klen] = 0;
                snprintf(val, sizeof(val), "%s", eq + 1);
                uint32_t c = (uint32_t)strtoul(val, NULL, 16);
                if      (!strcmp(key, "bg"))          PLATE = c;
                else if (!strcmp(key, "panel"))       PLATE_HI = c;
                else if (!strcmp(key, "line"))        PLATE_LINE = c;
                else if (!strcmp(key, "ink"))         INK = c;
                else if (!strcmp(key, "ink_dim"))     INK_DIM = c;
                else if (!strcmp(key, "ink_faint"))   INK_FAINT = c;
                else if (!strcmp(key, "accent"))      ACCENT = c;
                else if (!strcmp(key, "accent_hi"))   ACCENT_HI = c;
                else if (!strcmp(key, "knob_face"))   KNOB_FACE = c;
                else if (!strcmp(key, "knob_ring"))   KNOB_RING = c;
                else if (!strcmp(key, "bar"))         BAR_BG = c;
                else if (!strcmp(key, "btn_text"))    BTN_TEXT = c;
                else if (!strcmp(key, "tab_on"))      TAB_ON_BG = c;
                else if (!strcmp(key, "tab_on_tx"))   TAB_ON_TX = c;
                else if (!strcmp(key, "lcd"))         LCD_BG = c;
                else if (!strcmp(key, "seg_active"))  SEG_ACTIVE = c;
                else if (!strcmp(key, "seg_inactive")) SEG_INACTIVE = c;
                else if (!strcmp(key, "seg_active_tx")) SEG_ACTIVE_TX = c;
                else if (!strcmp(key, "box"))         TD3_BOX = c;
                else if (!strcmp(key, "btn_bg"))      TD3_BTN_BG = c;
                else if (!strcmp(key, "chrome_ink"))  TD3_CHROME_INK = c;
                else if (!strcmp(key, "go_on"))       TD3_GO_ON = c;
                else if (!strcmp(key, "go_off"))      TD3_GO_OFF = c;
                else if (!strcmp(key, "tabs"))        TD3_TABS_BG = c;
                else if (!strcmp(key, "knob_dot"))    KNOB_DOT_COLOR = c;
                else if (!strcmp(key, "display_bg"))     DSP_BG = c;
                else if (!strcmp(key, "display_cell"))   DSP_CELL = c;
                else if (!strcmp(key, "display_ink"))    DSP_INK = c;
                else if (!strcmp(key, "display_off"))    DSP_OFF = c;
                else if (!strcmp(key, "display_bezel"))  DSP_BEZEL = c;
                /* well/knob_off not used by any widget kind our own page
                 * needs yet -- add when a page that does comes along. */
            }
            continue;
        }
        if (s[0] == '[') {
            char name[24] = {0};
            sscanf(s, "[tab %23[^]]]", name);
            /* strip trailing space before ] if any (sscanf %[^]] keeps it) */
            size_t n = strlen(name); while (n > 0 && name[n-1] == ' ') name[--n] = 0;
            if (g_ntabs < MAX_TABS) {
                snprintf(g_tab_names[g_ntabs], sizeof(g_tab_names[0]), "%s", name);
                cur_tab = g_ntabs;
                g_ntabs++;
            }
            continue;
        }
        if (cur_tab < 0) continue; /* top-level keys we don't care about for preview */
        if (g_line_count[cur_tab] < MAX_LINES_PER_TAB) {
            snprintf(g_lines[cur_tab][g_line_count[cur_tab]].line,
                     sizeof(g_lines[0][0].line), "%s", s);
            g_line_count[cur_tab]++;
        }
    }
    fclose(f);
}

/* Mirrors force_shadow.c's own MAX_FRAMES - that real cap is enforced
 * silently there (`if (n_page_frames >= MAX_FRAMES) return;`, no error),
 * which is exactly how a 16-frame PADS page rendered fine in this tool
 * (which never tracked a frame count at all) while only the first 6
 * frames ever showed on the real device. Tracked and warned about here
 * now, the same way the list-widget overflow check already warns. */
#define PREVIEW_MAX_FRAMES 20

static void render_tab(int tab_idx) {
    const char *tab_ptrs[MAX_TABS];
    for (int i = 0; i < g_ntabs; i++) tab_ptrs[i] = g_tab_names[i];
    draw_chrome_named(g_display_name, tab_ptrs, g_ntabs, tab_idx, 1);
    int n_frames_drawn = 0;
    for (int i = 0; i < g_line_count[tab_idx]; i++) {
        const char *ln = g_lines[tab_idx][i].line;
        char kind[16] = {0};
        sscanf(ln, "%15s", kind);

        if (!strcmp(kind, "frame")) {
            if (n_frames_drawn >= PREVIEW_MAX_FRAMES) {
                fprintf(stderr, "WARNING: tab %d exceeds MAX_FRAMES=%d - this frame "
                        "would not render on the real device (silently dropped)\n",
                        tab_idx, PREVIEW_MAX_FRAMES);
                continue;
            }
            n_frames_drawn++;
            int x = kv_int(ln, "x", 0), y = kv_int(ln, "y", 0);
            int w = kv_int(ln, "w", 100), h = kv_int(ln, "h", 100);
            char title[64] = {0}; kv_str(ln, "title", title, sizeof(title));
            frame_box(x, y, w, h, title);

        } else if (!strcmp(kind, "button")) {
            int cx = kv_int(ln, "cx", 0), cy = kv_int(ln, "cy", 0);
            char label[64] = {0}; kv_str(ln, "label", label, sizeof(label));
            char colstr[16] = {0}; kv_str(ln, "color", colstr, sizeof(colstr));
            uint32_t color = colstr[0] ? (uint32_t)strtoul(colstr, NULL, 16) : 0;
            widget_button(cx, cy, label, color);

        } else if (!strcmp(kind, "toggle")) {
            int cx = kv_int(ln, "cx", 0), cy = kv_int(ln, "cy", 0);
            char label[64] = {0}; kv_str(ln, "label", label, sizeof(label));
            int on = kv_int(ln, "on", 0);
            widget_toggle(cx, cy, label, on);

        } else if (!strcmp(kind, "knob")) {
            int cx = kv_int(ln, "cx", 0), cy = kv_int(ln, "cy", 0), r = kv_int(ln, "r", 40);
            char label[64] = {0}; kv_str(ln, "label", label, sizeof(label));
            int pct = kv_int(ln, "pct", 50);
            float min = kv_float(ln, "min", 0), max = kv_float(ln, "max", 100);
            float val = min + (max - min) * pct / 100.0f;
            char valbuf[24]; snprintf(valbuf, sizeof(valbuf), "%.2f", val);
            widget_knob(cx, cy, r, pct, label, valbuf);

        } else if (!strcmp(kind, "enum_h")) {
            int cx = kv_int(ln, "cx", 0), cy = kv_int(ln, "cy", 0);
            char label[64] = {0}; kv_str(ln, "label", label, sizeof(label));
            char optstr[128] = {0}; kv_str(ln, "options", optstr, sizeof(optstr));
            int active = kv_int(ln, "active", 0);
            const char *opts[8]; int n = 0;
            char *tok = strtok(optstr, ",");
            while (tok && n < 8) { opts[n++] = tok; tok = strtok(NULL, ","); }
            int sw = kv_int(ln, "sw", 0);   /* 0 = widget_enum_h's own 117px default */
            widget_enum_h(cx, cy, label, opts, n, active, sw);

        } else if (!strcmp(kind, "readout")) {
            int cx = kv_int(ln, "cx", 0), cy = kv_int(ln, "cy", 0);
            int w = kv_int(ln, "w", 200), h = kv_int(ln, "h", 44);
            char label[64] = {0}; kv_str(ln, "label", label, sizeof(label));
            char get[64] = {0}; kv_str(ln, "get", get, sizeof(get));
            /* stand-in sample values, since no live engine is running.
             * force-webstream's *_shadow GET keys run real values through
             * shadow_font_safe() (upper-case, font_chars-only) before the
             * device ever sees them, so these samples are pre-sanitized
             * the same way for a faithful preview -- see that project's
             * addon/shadow_page.conf and src/webstream_host.cpp. */
            const char *sample = "STREAMING";
            if (!strcmp(get, "playback_time")) sample = "2:47";
            /* Real values (yt_stream_plugin.c's set_search_status() calls):
             * "searching"/"idle"/"done"/"no_results"/"error"/"queued" --
             * always one word, never a result count appended (that's the
             * separate search_count field, see search_results_*_json). */
            else if (!strcmp(get, "search_status_shadow") || !strcmp(get, "search_status")) sample = "DONE";
            else if (!strcmp(get, "stream_status_shadow") || !strcmp(get, "stream_status")) sample = "STREAMING";
            else if (!strcmp(get, "cratedig_decade_text")) sample = "1990S";
            widget_readout(cx, cy, w, h, label, sample);

        } else if (!strcmp(kind, "stepper")) {
            int cx = kv_int(ln, "cx", 0), cy = kv_int(ln, "cy", 0);
            int w = kv_int(ln, "w", 300), h = kv_int(ln, "h", 56);
            char label[64] = {0}; kv_str(ln, "label", label, sizeof(label));
            char get[64] = {0}; kv_str(ln, "get", get, sizeof(get));
            const char *sample = "ANY";
            if (!strcmp(get, "cratedig_genre_text")) sample = "ELECTRONIC";
            else if (!strcmp(get, "cratedig_style_text")) sample = "ACID HOUSE";
            else if (!strcmp(get, "cratedig_decade_text")) sample = "1990S";
            else if (!strcmp(get, "cratedig_region_text")) sample = "EUROPE";
            else if (!strcmp(get, "cratedig_country_text")) sample = "GERMANY";
            widget_stepper(cx, cy, w, h, label, sample);

        } else if (!strcmp(kind, "list")) {
            int x = kv_int(ln, "x", 0), y = kv_int(ln, "y", 0);
            int w = kv_int(ln, "w", 400), h = kv_int(ln, "h", 300);
            int cols = kv_int(ln, "cols", 1), rows = kv_int(ln, "rows", 4);
            int th = kv_int(ln, "th", 56), gap = kv_int(ln, "gap", 4);
            int numbered = kv_int(ln, "numbered", 0);
            /* Pre-sanitized the same way shadow_font_safe() would leave
             * them (upper-case, only font_chars' set survives) -- these
             * are what search_results_shadow_json actually sends, not
             * the natural-case titles search_results_json sends the web
             * GUI. No provider prefix (cratedigger_host.cpp's own build_
             * search_results_json_locked() dropped that once this addon
             * became crate-dig-only -- every result resolves via YouTube
             * internally regardless, so it was never informative); the
             * Discogs release year takes that slot instead. See that
             * function's own comment. */
            static const char *result_items[8] = {
                "APHEX TWIN - XTAL OFFICIAL VIDEO 1993",
                "BOARDS OF CANADA - ROYGBIV 1995",
                "BURIAL - ARCHANGEL 2007",
                "VARIOUS - WARP10+3 WARP RECORDS 1999",
                "AUTECHRE - GANTZ GRAF 2002",
                "LIVE AT THE BLUE NOTE 1987",
            };
            widget_list(x, y, w, h, result_items, 6, 2, cols, rows, th, gap, numbered);
        }
    }
}

/* ---- PPM output ---- */
static void write_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", LAND_W, LAND_H);
    fwrite(canvas, 1, sizeof(canvas), f);
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <shadow_page.conf> <out-dir>\n", argv[0]);
        return 2;
    }
    load_conf(argv[1]);
    const char *out_dir = argv[2];
    fprintf(stderr, "loaded %d tab(s): ", g_ntabs);
    for (int i = 0; i < g_ntabs; i++) fprintf(stderr, "[%s] ", g_tab_names[i]);
    fprintf(stderr, "\n");
    for (int i = 0; i < g_ntabs; i++) {
        render_tab(i);
        char path[512];
        snprintf(path, sizeof(path), "%s/tab_%d.ppm", out_dir, i);
        write_ppm(path);
        fprintf(stderr, "wrote %s (tab %d: %s)\n", path, i, g_tab_names[i]);
    }
    return 0;
}
