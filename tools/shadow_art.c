/* shadow_art.c — export MPC plugin-skin artwork drawn by force-shadow's own
 * offline renderer (tools/render_conf_preview.c), so a skin generated from a
 * shadow_page.conf is pixel-identical to the Force Shadow page it came from.
 *
 * Build (x86 host is fine; path to a force-shadow checkout):
 *   gcc -O2 -I<force-shadow>/tools -o shadow_art shadow_art.c -lm
 *
 * Reads commands on stdin, one per line, fields separated by '|':
 *   clear|RRGGBB                      fill the whole 1280x800 canvas
 *   frame|x|y|w|h|TITLE               titled frame box
 *   text|cx|y|scale|RRGGBB|TEXT       centred text (baked 9x9 font; uppercase only)
 *   knob|cx|cy|r|pct                  knob body: ring, face, pointer dot (no label/value)
 *   pill|cx|cy|on                     toggle pill (no label)
 *   button|cx|cy|RRGGBB|LABEL         push button
 *   seg|x|y|w|h|RRGGBB|RRGGBB|LABEL   one enum segment: fill colour, text colour
 *   crop|out.ppm|x|y|w|h              write a region of the canvas
 *   strip|out.ppm|r|frames|RRGGBB     vertical knob filmstrip (frames x (2r+10)^2) on a bg colour
 */
#define main render_conf_preview_main
#include "render_conf_preview.c"
#undef main

static void write_region(FILE *f, int x, int y, int w, int h) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int px = x + i, py = y + j;
            unsigned char c[3] = {0, 0, 0};
            if (px >= 0 && px < LAND_W && py >= 0 && py < LAND_H) memcpy(c, canvas[py][px], 3);
            fwrite(c, 1, 3, f);
        }
}

static void crop(const char *path, int x, int y, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    write_region(f, x, y, w, h);
    fclose(f);
}

static void knob_body(int cx, int cy, int r, int pct) {
    /* widget_knob() minus its label/value text (those come from the skin) */
    draw_ring(cx, cy, r + 3, 3, KNOB_RING);
    fill_circle(cx, cy, r, KNOB_FACE);
    int dx, dy;
    knob_dot(cx, cy, r, pct, &dx, &dy);
    fill_circle(dx, dy, r / 7 + 2, KNOB_DOT_COLOR);
}

static void pill(int cx, int cy, int on) {
    /* widget_toggle() minus its label */
    int pw = 51, ph = 27;
    fill_rect(cx - pw / 2, cy - ph / 2, pw, ph, 0x050403);
    draw_ring(cx - pw / 2 + ph / 2, cy, ph / 2 - 2, 1, PLATE_LINE);
    int lx = on ? (cx + pw / 2 - ph / 2) : (cx - pw / 2 + ph / 2);
    fill_circle(lx, cy, ph / 2 - 4, on ? ACCENT_HI : 0x4c473d);
}

static void clear(uint32_t c) {
    fill_rect(0, 0, LAND_W, LAND_H, c);
}

static void strip(const char *path, int r, int frames, uint32_t bg) {
    int s = 2 * r + 10, c = s / 2;
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", s, s * frames);
    for (int k = 0; k < frames; k++) {
        fill_rect(0, 0, s, s, bg);
        knob_body(c, c, r, (int)lround(100.0 * k / (frames - 1)));
        write_region(f, 0, 0, s, s);
    }
    fclose(f);
}

#define HEX(s) ((uint32_t)strtoul((s), NULL, 16))

int main(void) {
    char line[512], *a[10];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        int n = 0;
        for (char *t = strtok(line, "|"); t && n < 10; t = strtok(NULL, "|")) a[n++] = t;
        if (!n) continue;
        const char *op = a[0];
        if (!strcmp(op, "clear") && n == 2) clear(HEX(a[1]));
        else if (!strcmp(op, "frame") && n == 6) frame_box(atoi(a[1]), atoi(a[2]), atoi(a[3]), atoi(a[4]), a[5]);
        else if (!strcmp(op, "text") && n == 6) draw_text_c(atoi(a[1]), atoi(a[2]), a[5], (float)atof(a[3]), HEX(a[4]));
        else if (!strcmp(op, "knob") && n == 5) knob_body(atoi(a[1]), atoi(a[2]), atoi(a[3]), atoi(a[4]));
        else if (!strcmp(op, "pill") && n == 4) pill(atoi(a[1]), atoi(a[2]), atoi(a[3]));
        else if (!strcmp(op, "button") && n == 5) widget_button(atoi(a[1]), atoi(a[2]), a[4], HEX(a[3]));
        else if (!strcmp(op, "seg") && n == 8) {
            int x = atoi(a[1]), y = atoi(a[2]), w = atoi(a[3]), h = atoi(a[4]);
            fill_rect(x, y, w, h, HEX(a[5]));
            draw_text_c(x + w / 2, y + h / 2 - 6, a[7], 1.5f, HEX(a[6]));
        }
        else if (!strcmp(op, "crop") && n == 6) crop(a[1], atoi(a[2]), atoi(a[3]), atoi(a[4]), atoi(a[5]));
        else if (!strcmp(op, "strip") && n == 5) strip(a[1], atoi(a[2]), atoi(a[3]), HEX(a[4]));
        else { fprintf(stderr, "shadow_art: bad command: %s (%d fields)\n", op, n); return 1; }
    }
    return 0;
}
