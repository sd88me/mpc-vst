/* Dynamic-text probe: does MPC's skin refresh a parameter's value/name text that the
   plugin changes on its own? Three params show a seconds counter:
     0 "Static"      - no notification
     1 "UpdDisp"     - audioMasterUpdateDisplay once a second (its NAME also changes)
     2 "Automate"    - audioMasterAutomate with a changing value once a second
   Silent instrument; log in /tmp/textprobe.log. */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

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

static audioMasterCallback master;
static AEffect fx;
static long samples, secs;
static float val2;
static FILE *lg;
static int logged_disp;

static intptr_t dispatcher(AEffect *e, int32_t op, int32_t idx, intptr_t v, void *p, float o) {
    (void)e; (void)v; (void)o;
    switch (op) {
    case 35: return 2;
    case 45: case 48: strcpy(p, "MPC Text Probe"); return 1;
    case 47: strcpy(p, "sd88me"); return 1;
    case 58: return 2400;
    case 8:
        if (idx == 0) strcpy(p, "Static");
        else if (idx == 1) snprintf(p, 24, "UpdDisp %ld", secs);
        else strcpy(p, "Automate");
        return 1;
    case 7:
        snprintf(p, 24, "T+%ld", secs);
        if (lg && logged_disp < 40) { fprintf(lg, "display? idx %d at %lds\n", idx, secs); fflush(lg); logged_disp++; }
        return 1;
    case 6: strcpy(p, "s"); return 1;
    default: return 0;
    }
}
static float getParameter(AEffect *e, int32_t i) { (void)e; return i == 2 ? val2 : 0.5f; }
static void setParameter(AEffect *e, int32_t i, float v) { (void)e; if (i == 2) val2 = v; }

static void processReplacing(AEffect *e, float **in, float **out, int32_t n) {
    (void)in;
    memset(out[0], 0, n * sizeof(float));
    memset(out[1], 0, n * sizeof(float));
    samples += n;
    if (samples >= 44100) {
        samples -= 44100;
        secs++;
        intptr_t r1 = master(e, 42, 0, 0, 0, 0);              /* audioMasterUpdateDisplay */
        val2 = (secs % 100) / 100.0f;
        intptr_t r2 = master(e, 0, 2, 0, 0, val2);             /* audioMasterAutomate */
        if (lg && secs <= 5) { fprintf(lg, "tick %ld updateDisplay=%ld automate=%ld\n", secs, (long)r1, (long)r2); fflush(lg); }
    }
}

__attribute__((visibility("default"))) AEffect *VSTPluginMain(audioMasterCallback m) {
    master = m;
    if (!lg) lg = fopen("/tmp/textprobe.log", "a");
    memset(&fx, 0, sizeof fx);
    fx.magic = 0x56737450;
    fx.dispatcher = dispatcher;
    fx.getParameter = getParameter;
    fx.setParameter = setParameter;
    fx.processReplacing = processReplacing;
    fx.numParams = 3; fx.numInputs = 0; fx.numOutputs = 2;
    fx.flags = (1 << 4) | (1 << 8);
    fx.uniqueID = 0x54785062; /* 'TxPb' */
    fx.version = 1000;
    return &fx;
}
