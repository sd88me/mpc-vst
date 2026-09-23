/* =============================================================================
 * vst2_wrap.c — expose a Schwung plugin_api_v2 DSP as a Linux VST2 plugin so
 * the built-in plugin host (JUCE) of MPC OS standalone devices can load it as a native track
 * instrument. Generic: the DSP is linked in, and the generated params.h
 * (gen_vst.py, from module.json) supplies the parameter table and identity.
 *
 * Host contract (MPC OS standalone): 44100 Hz, 128-frame blocks — the same as the Move,
 * so the DSP runs unmodified. Audio is rendered in 128-frame chunks through a
 * small FIFO, so any host block size works; with 128-frame host blocks each
 * process() call renders exactly one DSP block and MIDI lands at its start.
 * ========================================================================== */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include "params.h"

/* ---- Schwung plugin_api_v2 (see src/include/plugin_api_v1.h) ------------ */
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

#define DSP_BLOCK 128
#define MIDI_SOURCE_EXTERNAL 2

/* ---- VST2 ABI (hand-written; no Steinberg SDK) -------------------------- */
typedef struct AEffect AEffect;
typedef intptr_t (*audioMasterCallback)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
struct AEffect {
    int32_t magic;
    intptr_t (*dispatcher)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
    void (*process)(AEffect *, float **, float **, int32_t);
    void (*setParameter)(AEffect *, int32_t, float);
    float (*getParameter)(AEffect *, int32_t);
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t resvd1, resvd2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void *object, *user;
    int32_t uniqueID, version;
    void (*processReplacing)(AEffect *, float **, float **, int32_t);
    void (*processDoubleReplacing)(AEffect *, double **, double **, int32_t);
    char future[56];
};
typedef struct { int32_t type, byteSize, deltaFrames, flags; char data[16]; } VstEvent;
typedef struct {
    int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset;
    unsigned char midiData[4];
    char detune, noteOffVelocity, reserved1, reserved2;
} VstMidiEvent;
typedef struct { int32_t numEvents; intptr_t reserved; VstEvent *events[2]; } VstEvents;
typedef struct {
    double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos;
    int32_t timeSigNumerator, timeSigDenominator, smpteOffset, smpteFrameRate, samplesToNextClock, flags;
} VstTimeInfo;

enum {
    effOpen = 0, effClose = 1, effGetParamLabel = 6, effGetParamDisplay = 7, effGetParamName = 8,
    effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12, effGetChunk = 23,
    effSetChunk = 24, effProcessEvents = 25, effCanBeAutomated = 26, effGetPlugCategory = 35,
    effGetEffectName = 45, effGetVendorString = 47, effGetProductString = 48,
    effGetVendorVersion = 49, effCanDo = 51, effGetVstVersion = 58,
};
enum { audioMasterAutomate = 0, audioMasterGetTime = 7, kVstTempoValid = 1 << 10 };
enum { effFlagsCanReplacing = 1 << 4, effFlagsProgramChunks = 1 << 5, effFlagsIsSynth = 1 << 8 };

/* ---- per-instance state ------------------------------------------------- */
typedef struct {
    AEffect fx;
    audioMasterCallback master;
    void *dsp;
    int16_t block[DSP_BLOCK * 2];
    int pos;                 /* read position in block; DSP_BLOCK = empty */
    double bpm;
    volatile char release[NPARAMS];  /* momentary params to report back to 0 */
    char chunk[8192];
} wrap_t;

static plugin_api_v2_t *g_api;

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

/* normalized 0..1 -> DSP display value string */
static void norm_to_str(const param_t *p, float n, char *buf, int len) {
    if (p->nopts) snprintf(buf, len, "%d", (int)lroundf(clamp01(n) * (p->nopts - 1)));
    else snprintf(buf, len, "%g", p->min + (p->max - p->min) * clamp01(n));
}

/* DSP display value string (number or enum label) -> normalized 0..1 */
static float str_to_norm(const param_t *p, const char *s) {
    if (p->nopts) {
        int idx = -1;
        if (isdigit((unsigned char)s[0])) idx = atoi(s);
        else
            for (int i = 0; i < p->nopts; i++)
                if (!strcasecmp(s, p->opts[i])) idx = i;
        if (idx < 0) idx = 0;
        return p->nopts > 1 ? (float)idx / (p->nopts - 1) : 0;
    }
    return p->max > p->min ? clamp01((float)((atof(s) - p->min) / (p->max - p->min))) : 0;
}

static float get_norm(wrap_t *w, int i) {
    char buf[64];
    if (i < 0 || i >= NPARAMS) return 0;
    if (g_api->get_param(w->dsp, PARAMS[i].key, buf, sizeof buf) <= 0) return PARAMS[i].def;
    return str_to_norm(&PARAMS[i], buf);
}

static void setParameter(AEffect *e, int32_t i, float n) {
    wrap_t *w = e->object;
    char buf[64];
    if (i < 0 || i >= NPARAMS) return;
    const param_t *p = &PARAMS[i];
    if (p->nopts > 1) {
        /* A value on an option (button press, preset, automation) selects it. A value
         * between options is a Q-Link/encoder nudge from the current one: step one
         * option that way, else small nudges round back and never change state. */
        float pos = clamp01(n) * (p->nopts - 1);
        if (fabsf(pos - roundf(pos)) > 0.001f) {
            float cur = get_norm(w, i) * (p->nopts - 1);
            int idx = (int)lroundf(cur) + (pos > cur ? 1 : -1);
            if (idx < 0) idx = 0;
            if (idx > p->nopts - 1) idx = p->nopts - 1;
            n = (float)idx / (p->nopts - 1);
        }
    }
    norm_to_str(p, n, buf, sizeof buf);
    g_api->set_param(w->dsp, PARAMS[i].key, buf);
    if (PARAMS[i].momentary && n > 0.5f) w->release[i] = 1;
}

static float getParameter(AEffect *e, int32_t i) { return get_norm(e->object, i); }

static void update_tempo(wrap_t *w) {
    VstTimeInfo *ti = (VstTimeInfo *)w->master(&w->fx, audioMasterGetTime, 0, kVstTempoValid, 0, 0);
    if (!ti || !(ti->flags & kVstTempoValid) || ti->tempo <= 0) return;
    if (fabs(ti->tempo - w->bpm) > 0.01) {
        char buf[32];
        w->bpm = ti->tempo;
        snprintf(buf, sizeof buf, "%.2f", w->bpm);
        g_api->set_param(w->dsp, "lfo_bpm", buf);
    }
}

static void processReplacing(AEffect *e, float **in, float **out, int32_t n) {
    wrap_t *w = e->object;
    (void)in;
    if (HAS_LFO_BPM) update_tempo(w);
    /* A trigger param (e.g. Generate) fired: tell the host it is back to 0 so
     * buttons bound to it drop their highlight. Done here, not inside
     * setParameter, so the host is not re-entered from its own call. */
    for (int i = 0; i < NPARAMS; i++)
        if (w->release[i]) { w->release[i] = 0; w->master(&w->fx, audioMasterAutomate, i, 0, 0, 0.0f); }
    for (int32_t i = 0; i < n; i++) {
        if (w->pos >= DSP_BLOCK) {
            g_api->render_block(w->dsp, w->block, DSP_BLOCK);
            w->pos = 0;
        }
        out[0][i] = w->block[w->pos * 2] * (1.0f / 32768.0f);
        out[1][i] = w->block[w->pos * 2 + 1] * (1.0f / 32768.0f);
        w->pos++;
    }
}

static void copy_str(void *dst, const char *src, size_t max) {
    strncpy(dst, src, max - 1);
    ((char *)dst)[max - 1] = 0;
}

static intptr_t dispatcher(AEffect *e, int32_t op, int32_t idx, intptr_t v, void *p, float o) {
    wrap_t *w = e->object;
    (void)o;
    switch (op) {
    case effOpen: return 1;
    case effClose:
        g_api->destroy_instance(w->dsp);
        free(w);
        return 1;
    case effGetPlugCategory: return 2; /* kPlugCategSynth */
    case effGetEffectName:
    case effGetProductString: copy_str(p, PLUG_NAME, 32); return 1;
    case effGetVendorString: copy_str(p, PLUG_VENDOR, 32); return 1;
    case effGetVendorVersion: return PLUG_VERSION;
    case effGetVstVersion: return 2400;
    case effCanBeAutomated: return idx >= 0 && idx < NPARAMS;
    case effGetParamName:
        if (idx >= 0 && idx < NPARAMS) copy_str(p, PARAMS[idx].name, 32);
        return 1;
    case effGetParamLabel:
        if (idx >= 0 && idx < NPARAMS) copy_str(p, PARAMS[idx].unit, 8);
        return 1;
    case effGetParamDisplay: {
        char buf[64];
        if (idx < 0 || idx >= NPARAMS) return 0;
        const param_t *pp = &PARAMS[idx];
        if (pp->nopts) {
            int k = (int)lroundf(get_norm(w, idx) * (pp->nopts - 1));
            copy_str(p, pp->opts[k], 24);
        } else if (g_api->get_param(w->dsp, pp->key, buf, sizeof buf) > 0) {
            snprintf(p, 24, "%.*f", fabs(pp->max - pp->min) > 20 ? 0 : 1, atof(buf));
        }
        return 1;
    }
    case effSetSampleRate: case effSetBlockSize: case effMainsChanged: return 1;
    case effProcessEvents: {
        VstEvents *ev = p;
        for (int i = 0; i < ev->numEvents; i++)
            if (ev->events[i]->type == 1) {
                VstMidiEvent *m = (VstMidiEvent *)ev->events[i];
                g_api->on_midi(w->dsp, m->midiData, 3, MIDI_SOURCE_EXTERNAL);
            }
        return 1;
    }
    case effCanDo:
        return (!strcmp(p, "receiveVstEvents") || !strcmp(p, "receiveVstMidiEvent") ||
                !strcmp(p, "receiveVstTimeInfo")) ? 1 : -1;
    case effGetChunk: {
        int len = g_api->get_param(w->dsp, "state", w->chunk, sizeof w->chunk);
        if (len <= 0) return 0;
        *(void **)p = w->chunk;
        return (intptr_t)strlen(w->chunk) + 1;
    }
    case effSetChunk: {
        if (v <= 0 || (size_t)v > sizeof w->chunk) return 0;
        memcpy(w->chunk, p, v);
        w->chunk[v - 1] = 0;
        g_api->set_param(w->dsp, "state", w->chunk);
        return 1;
    }
    default: return 0;
    }
}

__attribute__((visibility("default"))) AEffect *VSTPluginMain(audioMasterCallback master) {
    if (!g_api) g_api = move_plugin_init_v2(NULL);
    if (!g_api) return NULL;
    wrap_t *w = calloc(1, sizeof *w);
    if (!w) return NULL;
    w->dsp = g_api->create_instance(NULL, NULL);
    if (!w->dsp) { free(w); return NULL; }
    w->master = master;
    w->pos = DSP_BLOCK;
    AEffect *e = &w->fx;
    e->magic = 0x56737450; /* 'VstP' */
    e->dispatcher = dispatcher;
    e->setParameter = setParameter;
    e->getParameter = getParameter;
    e->processReplacing = processReplacing;
    e->numParams = NPARAMS;
    e->numInputs = 0;
    e->numOutputs = 2;
    e->flags = effFlagsCanReplacing | effFlagsIsSynth | effFlagsProgramChunks;
    e->uniqueID = PLUG_UID;
    e->version = PLUG_VERSION;
    e->object = w;
    return e;
}
