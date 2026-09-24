/* Native-picker probe: which mechanism makes MPC's menu overlay list a VST2 parameter's choices?
     0 "Props"  - effGetParameterProperties (int steps 0..3) only
     1 "VSTXML" - a ValueType in menuprobe.vstxml (next to the .so) only
     2 "Both"   - both
     3 "States" - VSTXML numberOfStates="4" only, no ValueType (JUCE builds the list from getText)
   Every param has 4 choices: RED, GREEN, BLUE, YELLOW. JUCE's VSTXMLInfo only parses children of
   <VSTParametersStructure>, so the ValueType must sit inside it.
     4 "Font Sample" - fixed display text for the skin's font-name test rows (menuprobe_skin.py)
   Param 0 doubles as the PICKER tab's "open" flag; setting param 2 while it is on clears it.
   Log: /tmp/menuprobe.log */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

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
typedef struct {
    float stepFloat, smallStepFloat, largeStepFloat;
    char label[64];
    int32_t flags, minInteger, maxInteger, stepInteger, largeStepInteger;
    char shortLabel[8];
    int16_t displayIndex, category, numParametersInCategory, reserved;
    char categoryLabel[24];
    char future[16];
} VstParameterProperties;
enum { kIsSwitch = 1, kUsesIntegerMinMax = 2, kUsesFloatStep = 4, kUsesIntStep = 8 };

static const char *NAMES[5] = { "Props", "VSTXML", "Both", "States", "Font Sample" };
#define NP 5
static const char *OPTS[4] = { "RED", "GREEN", "BLUE", "YELLOW" };
static float val[NP];
static FILE *lg;
static audioMasterCallback master;
static volatile int close_picker;
#define LOG(...) do { if (lg) { fprintf(lg, __VA_ARGS__); fflush(lg); } } while (0)

static int idx_of(float v) { int k = (int)lroundf(v * 3); return k < 0 ? 0 : k > 3 ? 3 : k; }

static intptr_t dispatcher(AEffect *e, int32_t op, int32_t i, intptr_t v, void *p, float o) {
    (void)e; (void)v; (void)o;
    if (op != 7 && op != 25 && op != 53) LOG("op %d idx %d\n", op, i);
    switch (op) {
    case 35: return 2;
    case 45: case 48: strcpy(p, "MPC Menu Probe"); return 1;
    case 47: strcpy(p, "sd88me"); return 1;
    case 58: return 2400;
    case 8: if (i >= 0 && i < NP) strcpy(p, NAMES[i]); return 1;
    case 7: if (i == 4) strcpy(p, "Hamburgefonstiv Il1 0123"); else if (i >= 0 && i < NP) strcpy(p, OPTS[idx_of(val[i])]); return 1;
    case 26: return 1;
    case 56: {   /* effGetParameterProperties */
        if (i != 0 && i != 2) return 0;
        VstParameterProperties *pp = p;
        memset(pp, 0, sizeof *pp);
        pp->flags = kUsesIntegerMinMax | kUsesIntStep;
        pp->minInteger = 0; pp->maxInteger = 3; pp->stepInteger = 1; pp->largeStepInteger = 1;
        strcpy(pp->label, NAMES[i]);
        strcpy(pp->shortLabel, "Col");
        LOG("properties for %d\n", i);
        return 1;
    }
    case 51: LOG("canDo %s\n", (char *)p); return -1;
    default: return 0;
    }
}
static float getParameter(AEffect *e, int32_t i) { (void)e; return i >= 0 && i < NP ? val[i] : 0; }
/* PICKER tab: param 0 is the pop-up's "open" flag; picking an option (param 2) closes it. The host is
 * told from processReplacing, not re-entered from its own setParameter call (as in vst2_wrap.c). */
static void setParameter(AEffect *e, int32_t i, float v) {
    (void)e;
    if (i < 0 || i >= NP) return;
    val[i] = v;
    LOG("set %d = %.3f\n", i, v);
    if (i == 2 && val[0] > 0.5f) { val[0] = 0; close_picker = 1; }
}
static void processReplacing(AEffect *e, float **in, float **out, int32_t n) {
    (void)in; memset(out[0], 0, n * 4); memset(out[1], 0, n * 4);
    if (close_picker && master) { close_picker = 0; master(e, 0 /* audioMasterAutomate */, 0, 0, 0, 0.0f); }
}
static AEffect fx;
__attribute__((visibility("default"))) AEffect *VSTPluginMain(audioMasterCallback m) {
    master = m;
    if (!lg) lg = fopen("/tmp/menuprobe.log", "a");
    LOG("menuprobe v2 (picker auto-close)\n");
    memset(&fx, 0, sizeof fx);
    fx.magic = 0x56737450;
    fx.dispatcher = dispatcher; fx.getParameter = getParameter; fx.setParameter = setParameter;
    fx.processReplacing = processReplacing;
    fx.numParams = NP; fx.numInputs = 0; fx.numOutputs = 2;
    fx.flags = (1 << 4) | (1 << 8);
    fx.uniqueID = 0x4d6e5062; /* 'MnPb' */
    fx.version = 1000;
    return &fx;
}
