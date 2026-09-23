/* Minimal VST2 mono synth PoC: saw -> resonant lowpass -> amp env. Tests MIDI into a plugin. */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

typedef struct AEffect AEffect;
typedef intptr_t (*audioMasterCallback)(AEffect*, int32_t, int32_t, intptr_t, void*, float);
struct AEffect {
    int32_t magic;
    intptr_t (*dispatcher)(AEffect*, int32_t, int32_t, intptr_t, void*, float);
    void (*process)(AEffect*, float**, float**, int32_t);
    void (*setParameter)(AEffect*, int32_t, float);
    float (*getParameter)(AEffect*, int32_t);
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t resvd1, resvd2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void *object, *user;
    int32_t uniqueID, version;
    void (*processReplacing)(AEffect*, float**, float**, int32_t);
    void (*processDoubleReplacing)(AEffect*, double**, double**, int32_t);
    char future[56];
};
typedef struct { int32_t type, byteSize, deltaFrames, flags; char data[16]; } VstEvent;
typedef struct {
    int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset;
    unsigned char midiData[4];
    char detune, noteOffVelocity, reserved1, reserved2;
} VstMidiEvent;
typedef struct { int32_t numEvents; intptr_t reserved; VstEvent *events[2]; } VstEvents;

enum { effOpen=0, effClose=1, effGetParamLabel=6, effGetParamDisplay=7, effGetParamName=8,
       effSetSampleRate=10, effProcessEvents=25, effCanBeAutomated=26, effGetPlugCategory=35,
       effGetEffectName=45, effGetVendorString=47, effGetProductString=48, effGetVendorVersion=49,
       effCanDo=51, effGetVstVersion=58 };

enum { P_CUTOFF, P_RESO, P_DECAY, P_VOLUME, NPARAMS };
static const char *pname[NPARAMS] = { "Cutoff", "Resonance", "Decay", "Volume" };
static float param[NPARAMS] = { 0.5f, 0.3f, 0.4f, 0.7f };

static float sr = 44100.0f;
static int note = -1;           /* currently held note, -1 = none */
static float phase, freq, env, vel;
static float lp[4];             /* 4-pole ladder-ish state */

static void midi(const unsigned char *m) {
    int st = m[0] & 0xf0;
    if (st == 0x90 && m[2]) {
        note = m[1];
        freq = 440.0f * powf(2.0f, (note - 69) / 12.0f);
        vel = m[2] / 127.0f;
        env = 1.0f;
    } else if ((st == 0x80 || st == 0x90) && m[1] == note) {
        note = -1;
    } else if (st == 0xb0 && (m[1] == 120 || m[1] == 123)) {
        note = -1; env = 0;
    }
}

static intptr_t dispatcher(AEffect *e, int32_t op, int32_t idx, intptr_t v, void *p, float o) {
    (void)e; (void)v;
    switch (op) {
    case effGetPlugCategory: return 2; /* synth */
    case effGetEffectName: case effGetProductString: strcpy(p, "MPC Synth PoC"); return 1;
    case effGetVendorString: strcpy(p, "sd88me"); return 1;
    case effGetVendorVersion: return 1000;
    case effGetVstVersion: return 2400;
    case effCanBeAutomated: return 1;
    case effGetParamName: if (idx < NPARAMS) strcpy(p, pname[idx]); return 1;
    case effGetParamLabel: strcpy(p, idx == P_CUTOFF ? "Hz" : idx == P_DECAY ? "ms" : "%"); return 1;
    case effGetParamDisplay:
        if (idx == P_CUTOFF) snprintf(p, 8, "%.0f", 40.0f * powf(400.0f, param[idx]));
        else if (idx == P_DECAY) snprintf(p, 8, "%.0f", 10.0f + 2990.0f * param[idx] * param[idx]);
        else snprintf(p, 8, "%.0f", param[idx] * 100.0f);
        return 1;
    case effSetSampleRate: sr = o; return 1;
    case effProcessEvents: {
        VstEvents *ev = p;
        for (int i = 0; i < ev->numEvents; i++)
            if (ev->events[i]->type == 1) midi(((VstMidiEvent *)ev->events[i])->midiData);
        return 1;
    }
    case effCanDo:
        return (!strcmp(p, "receiveVstEvents") || !strcmp(p, "receiveVstMidiEvent")) ? 1 : -1;
    case effOpen: case effClose: return 1;
    default: return 0;
    }
}
static void setParameter(AEffect *e, int32_t i, float v) { (void)e; if (i < NPARAMS) param[i] = v; }
static float getParameter(AEffect *e, int32_t i) { (void)e; return i < NPARAMS ? param[i] : 0; }

static void processReplacing(AEffect *e, float **in, float **out, int32_t n) {
    (void)e; (void)in;
    float fc = 40.0f * powf(400.0f, param[P_CUTOFF]);
    float g = 1.0f - expf(-2.0f * 3.14159265f * fminf(fc, sr * 0.45f) / sr);
    float k = param[P_RESO] * 3.8f;
    float decay_s = 0.01f + 2.99f * param[P_DECAY] * param[P_DECAY];
    float dmul = expf(-1.0f / (decay_s * sr));
    float rmul = expf(-1.0f / (0.005f * sr));
    for (int32_t i = 0; i < n; i++) {
        float s = 0;
        if (env > 0.0001f) {
            phase += freq / sr;
            if (phase >= 1.0f) phase -= 1.0f;
            float x = 2.0f * phase - 1.0f - k * lp[3];
            lp[0] += g * (tanhf(x) - lp[0]);
            lp[1] += g * (lp[0] - lp[1]);
            lp[2] += g * (lp[1] - lp[2]);
            lp[3] += g * (lp[2] - lp[3]);
            s = lp[3] * env * vel * param[P_VOLUME];
            env *= note >= 0 ? dmul : rmul;
        } else {
            env = 0;
        }
        out[0][i] = s; out[1][i] = s;
    }
}

static AEffect fx;
__attribute__((visibility("default"))) AEffect *VSTPluginMain(audioMasterCallback m) {
    memset(&fx, 0, sizeof fx);
    fx.magic = 0x56737450; /* 'VstP' */
    fx.dispatcher = dispatcher;
    fx.setParameter = setParameter;
    fx.getParameter = getParameter;
    fx.processReplacing = processReplacing;
    fx.numParams = NPARAMS; fx.numInputs = 0; fx.numOutputs = 2;
    fx.flags = (1 << 4) | (1 << 8); /* canReplacing | isSynth */
    fx.uniqueID = 0x46537950; /* 'FSyP' */
    fx.version = 1000;
    fx.user = (void *)m;
    return &fx;
}
