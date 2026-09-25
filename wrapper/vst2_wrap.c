/* =============================================================================
 * vst2_wrap.c — expose an engine (wrapper/engine.h) as a Linux VST2 plugin so
 * the built-in plugin host (JUCE) of MPC OS standalone devices can load it as a native track
 * instrument. Generic: the engine is linked in, and the generated params.h
 * (gen_vst.py, from the port's parameters) supplies the parameter table and identity.
 *
 * Host contract (MPC OS standalone): 44100 Hz, 128-frame blocks, the engine's own
 * block size. Audio is rendered in 128-frame chunks through a
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
#ifndef HAS_LFO_BPM
#define HAS_LFO_BPM 0 /* 1: pass the host tempo to the DSP as "lfo_bpm" */
#endif
#ifndef MODULE_DIR
#define MODULE_DIR NULL /* set via vst.json "defines" for a DSP that reads its own files
                          * (ROMs, etc.) from "<module_dir>/..." (see jv880's create_instance) */
#endif

#include "engine.h"
#include "popup.h"

#define DSP_BLOCK 128

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
enum { audioMasterAutomate = 0, audioMasterGetTime = 7, audioMasterUpdateDisplay = 42, kVstTempoValid = 1 << 10 };
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
    volatile char need_update_display;  /* deferred audioMasterUpdateDisplay -- see setParameter() */
    float open[NPARAMS];     /* popup "open" flags (popup.h): kept here, never sent to the DSP or saved */
    char chunk[8192];
} wrap_t;

static const mpc_engine_t *g_api;

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
    if (popup_is(i)) return w->open[i];
    if (g_api->get_param(w->dsp, PARAMS[i].key, buf, sizeof buf) <= 0) return PARAMS[i].def;
    return str_to_norm(&PARAMS[i], buf);
}

static void setParameter(AEffect *e, int32_t i, float n) {
    wrap_t *w = e->object;
    char buf[64];
    if (i < 0 || i >= NPARAMS) return;
    const param_t *p = &PARAMS[i];
    int nudge = 0;
    if (popup_set(w->open, i, n)) return;
    if (p->step_target >= 0) {
        /* A momentary nudge of ANOTHER param (see gen_vst.py's step_of/step_delta comment). Reads
         * the target's CURRENT value straight from the DSP, not our own cached norm, so it's
         * correct even if the DSP changed it independently (e.g. loading a bank shifts the patch).
         * This trigger's own key is never sent to the DSP at all. */
        if (n > 0.5f) {
            const param_t *tp = &PARAMS[p->step_target];
            if (g_api->get_param(w->dsp, tp->key, buf, sizeof buf) > 0) {
                float cur = (float)atof(buf) + p->step_delta;
                if (cur < tp->min) cur = tp->min;
                if (cur > tp->max) cur = tp->max;
                snprintf(buf, sizeof buf, "%g", cur);
                g_api->set_param(w->dsp, tp->key, buf);
            }
            w->release[i] = 1;
        }
        /* A string-display readout (e.g. patch_name/bank_name) bound elsewhere via get= has a
         * degenerate min==max range (its OWN reported normalized value never changes), so MPC has
         * no value-change signal telling it to re-poll THAT param's displayed text just because
         * THIS one changed it indirectly. audioMasterUpdateDisplay is the documented escape hatch
         * (docs/NOTES.md: MPC re-polls a Label "Name" on it; readouts stayed stuck on their
         * initial paint here without it -- confirmed on a real device, both via a stepper arrow
         * tap and a direct Q-Link turn on the underlying param). Deferred to processReplacing(),
         * same as w->release[] -- the host must not be re-entered from inside its own call to us. */
        w->need_update_display = 1;
        return;
    }
    if (p->nopts > 1) {
        /* A value on an option (button press, preset, automation) selects it. A value
         * between options is a Q-Link/encoder nudge from the current one: step one
         * option that way, else small nudges round back and never change state. */
        float pos = clamp01(n) * (p->nopts - 1);
        if (fabsf(pos - roundf(pos)) > 0.001f) {
            nudge = 1;
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
    if (!nudge) popup_picked(w->open, w->release, i);   /* a list pick closes it; a Q-Link nudge doesn't */
    w->need_update_display = 1;   /* deferred to processReplacing(), see the step_target branch above */
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

/* accumulate=1 is VST2's legacy process(), which must ADD to the output buffers; hosts here call
 * processReplacing, but a NULL e->process would crash any host that tried the old call. */
static void render_frames(wrap_t *w, float **out, int32_t n, int accumulate) {
    for (int32_t i = 0; i < n; i++) {
        if (w->pos >= DSP_BLOCK) {
            g_api->render(w->dsp, w->block, DSP_BLOCK);
            w->pos = 0;
        }
        float l = w->block[w->pos * 2] * (1.0f / 32768.0f), r = w->block[w->pos * 2 + 1] * (1.0f / 32768.0f);
        if (accumulate) { out[0][i] += l; out[1][i] += r; }
        else { out[0][i] = l; out[1][i] = r; }
        w->pos++;
    }
}

static void run_block(AEffect *e, float **out, int32_t n, int accumulate) {
    wrap_t *w = e->object;
    if (HAS_LFO_BPM) update_tempo(w);
    /* A trigger param (e.g. Generate) fired: tell the host it is back to 0 so
     * buttons bound to it drop their highlight. Done here, not inside
     * setParameter, so the host is not re-entered from its own call. */
    for (int i = 0; i < NPARAMS; i++)
        if (w->release[i]) { w->release[i] = 0; w->master(&w->fx, audioMasterAutomate, i, 0, 0, 0.0f); }
    if (w->need_update_display) {
        w->need_update_display = 0;
        w->master(&w->fx, audioMasterUpdateDisplay, 0, 0, 0, 0.0f);
    }
    render_frames(w, out, n, accumulate);
}

static void processReplacing(AEffect *e, float **in, float **out, int32_t n) { (void)in; run_block(e, out, n, 0); }
static void process(AEffect *e, float **in, float **out, int32_t n) { (void)in; run_block(e, out, n, 1); }

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
        g_api->destroy(w->dsp);
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
            if (pp->string_display) copy_str(p, buf, 24);   /* real text (a name, a status), not a number */
            else snprintf(p, 24, "%.*f", (pp->int_display || fabs(pp->max - pp->min) > 20) ? 0 : 1, atof(buf));
        }
        return 1;
    }
    case effSetSampleRate: case effSetBlockSize: case effMainsChanged: return 1;
    case effProcessEvents: {
        VstEvents *ev = p;
        for (int i = 0; i < ev->numEvents; i++)
            if (ev->events[i]->type == 1) {
                VstMidiEvent *m = (VstMidiEvent *)ev->events[i];
                g_api->midi(w->dsp, m->midiData, 3);
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
    if (!g_api) g_api = mpc_engine();
    if (!g_api) return NULL;
    wrap_t *w = calloc(1, sizeof *w);
    if (!w) return NULL;
    w->dsp = g_api->create(MODULE_DIR);
    if (!w->dsp) { free(w); return NULL; }
    w->master = master;
    w->pos = DSP_BLOCK;
    AEffect *e = &w->fx;
    e->magic = 0x56737450; /* 'VstP' */
    e->dispatcher = dispatcher;
    e->process = process;
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
