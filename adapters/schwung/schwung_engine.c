/* Schwung plugin_api_v2 DSP -> mpc_engine_t (wrapper/engine.h). Linked in by build_port.sh
 * when a port's vst.json names a Schwung "module". Schwung's own contract (44.1 kHz, 128-frame
 * int16 blocks) is the same as the engine interface's, so this is a straight mapping. */
#include <stddef.h>
#include "../../wrapper/engine.h"

typedef struct {
    uint32_t api_version;
    void *(*create_instance)(const char *module_dir, const char *json_defaults);
    void (*destroy_instance)(void *instance);
    void (*on_midi)(void *instance, const uint8_t *msg, int len, int source);
    void (*set_param)(void *instance, const char *key, const char *val);
    int (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    int (*get_error)(void *instance, char *buf, int buf_len);
    void (*render_block)(void *instance, int16_t *out_lr, int frames);
} plugin_api_v2_t;
extern plugin_api_v2_t *move_plugin_init_v2(const void *host);

#define MIDI_SOURCE_EXTERNAL 2

static plugin_api_v2_t *api;

static void *create(const char *dir) { return api->create_instance(dir, NULL); }
static void destroy(void *i) { api->destroy_instance(i); }
static void midi(void *i, const uint8_t *m, int n) { api->on_midi(i, m, n, MIDI_SOURCE_EXTERNAL); }
static void set_param(void *i, const char *k, const char *v) { api->set_param(i, k, v); }
static int get_param(void *i, const char *k, char *b, int n) { return api->get_param(i, k, b, n); }
static void render(void *i, int16_t *out, int frames) { api->render_block(i, out, frames); }

static const mpc_engine_t engine = { create, destroy, midi, set_param, get_param, render };

const mpc_engine_t *mpc_engine(void) {
    if (!api) api = move_plugin_init_v2(NULL);
    return api ? &engine : NULL;
}
