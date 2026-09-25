/* Engine for the native Meter probe (docs/ROADMAP.md "A native Meter component"): a single "level"
 * param free-runs a ~2s sawtooth 0..1 on its own, so the skin's meter has something to redraw with no
 * user interaction. Silent (no audio). */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../wrapper/engine.h"

typedef struct { long samples; float level; } state_t;

static void *create(const char *data_dir) {
    (void)data_dir;
    state_t *s = calloc(1, sizeof(state_t));
    return s;
}
static void destroy(void *inst) { free(inst); }
static void midi(void *inst, const uint8_t *msg, int len) { (void)inst; (void)msg; (void)len; }

static void set_param(void *inst, const char *key, const char *val) {
    (void)inst; (void)key; (void)val;   /* level is engine-driven, not settable */
}
static int get_param(void *inst, const char *key, char *buf, int buf_len) {
    state_t *s = inst;
    if (strcmp(key, "level") == 0) return snprintf(buf, buf_len, "%.4f", (double)s->level);
    return 0;
}
static void render(void *inst, int16_t *out_lr, int frames) {
    state_t *s = inst;
    memset(out_lr, 0, frames * 2 * sizeof(int16_t));
    s->samples += frames;
    if (s->samples >= 88200) s->samples -= 88200;   /* ~2s sawtooth */
    s->level = (float)s->samples / 88200.0f;
}

static const mpc_engine_t ENGINE = { create, destroy, midi, set_param, get_param, render };
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
