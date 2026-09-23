/* Minimal VST2 stereo gain for the Force's built-in JUCE host (PoC). */
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

enum { effOpen=0, effClose=1, effGetParamLabel=6, effGetParamDisplay=7, effGetParamName=8,
       effCanBeAutomated=26, effGetPlugCategory=35, effGetEffectName=45,
       effGetVendorString=47, effGetProductString=48, effGetVendorVersion=49, effGetVstVersion=58 };

static float g_gain = 1.0f;

static intptr_t dispatcher(AEffect *e, int32_t op, int32_t idx, intptr_t v, void *p, float o) {
    (void)e; (void)idx; (void)v; (void)o;
    switch (op) {
    case effGetPlugCategory: return 1;
    case effGetEffectName: case effGetProductString: strcpy(p, "Force Gain PoC"); return 1;
    case effGetVendorString: strcpy(p, "sd88me"); return 1;
    case effGetVendorVersion: return 1000;
    case effGetVstVersion: return 2400;
    case effCanBeAutomated: return 1;
    case effGetParamName: strcpy(p, "Gain"); return 1;
    case effGetParamLabel: strcpy(p, "dB"); return 1;
    case effGetParamDisplay:
        if (g_gain <= 0.0001f) strcpy(p, "-inf");
        else snprintf(p, 8, "%.1f", 20.0 * log10(g_gain));
        return 1;
    case effOpen: case effClose: return 1;
    default: return 0;
    }
}
static void setParameter(AEffect *e, int32_t i, float v) { (void)e; if (!i) g_gain = v < 0 ? 0 : v > 1 ? 1 : v; }
static float getParameter(AEffect *e, int32_t i) { (void)e; return i ? 0 : g_gain; }
static void processReplacing(AEffect *e, float **in, float **out, int32_t n) {
    (void)e;
    for (int32_t i = 0; i < n; i++) { out[0][i] = in[0][i] * g_gain; out[1][i] = in[1][i] * g_gain; }
}

static AEffect fx;
__attribute__((visibility("default"))) AEffect *VSTPluginMain(audioMasterCallback m) {
    memset(&fx, 0, sizeof fx);
    fx.magic = 0x56737450; /* 'VstP' */
    fx.dispatcher = dispatcher;
    fx.setParameter = setParameter;
    fx.getParameter = getParameter;
    fx.processReplacing = processReplacing;
    fx.numParams = 1; fx.numInputs = 2; fx.numOutputs = 2;
    fx.flags = 1 << 4; /* effFlagsCanReplacing */
    fx.uniqueID = 0x46476e50; /* 'FGnP' */
    fx.version = 1000;
    fx.user = (void *)m;
    return &fx;
}
