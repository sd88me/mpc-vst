/* MIDI-output PoC: while the transport plays, emits C3 on every 1/8 note (host ppqPos),
   and echoes incoming notes + an octave up. A short blip marks each emitted note.
   Logs host behaviour to /tmp/midipoc.log. */
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
typedef struct { int32_t numEvents; intptr_t reserved; VstEvent *events[64]; } VstEvents;
typedef struct {
    double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos;
    int32_t timeSigNumerator, timeSigDenominator, smpteOffset, smpteFrameRate, samplesToNextClock, flags;
} VstTimeInfo;

enum { kVstTransportPlaying = 1 << 1, kVstPpqPosValid = 1 << 9, kVstTempoValid = 1 << 10 };
enum { audioMasterGetTime = 7, audioMasterProcessEvents = 8 };

static audioMasterCallback master;
static AEffect fx;
static FILE *lg;
static float sr = 44100;
static VstMidiEvent pool[64];
static VstEvents out;
static VstMidiEvent in_q[32];
static int in_n, cur_note = -1, logged_blocks;
static double blip;
static long sent_ok, sent_fail;

#define LOG(...) do { if (lg) { fprintf(lg, __VA_ARGS__); fflush(lg); } } while (0)

static void add(int delta, int st, int d1, int d2) {
    if (out.numEvents >= 64) return;
    VstMidiEvent *m = &pool[out.numEvents];
    memset(m, 0, sizeof *m);
    m->type = 1; m->byteSize = sizeof *m; m->deltaFrames = delta;
    m->midiData[0] = st; m->midiData[1] = d1; m->midiData[2] = d2;
    out.events[out.numEvents++] = (VstEvent *)m;
}

static intptr_t dispatcher(AEffect *e, int32_t op, int32_t idx, intptr_t v, void *p, float o) {
    (void)e; (void)idx; (void)v;
    if (op != 25 && op != 53) LOG("dispatch op=%d idx=%d\n", op, idx);
    switch (op) {
    case 35: return 2;
    case 45: case 48: strcpy(p, "MPC MIDI Out PoC"); return 1;
    case 47: strcpy(p, "sd88me"); return 1;
    case 49: return 1000;
    case 58: return 2400;
    case 10: sr = o; return 1;
    case 25: {
        VstEvents *ev = p;
        for (int i = 0; i < ev->numEvents && in_n < 32; i++)
            if (ev->events[i]->type == 1) in_q[in_n++] = *(VstMidiEvent *)ev->events[i];
        return 1;
    }
    case 51:
        LOG("canDo? %s\n", (char *)p);
        return (!strcmp(p, "sendVstEvents") || !strcmp(p, "sendVstMidiEvent") ||
                !strcmp(p, "receiveVstEvents") || !strcmp(p, "receiveVstMidiEvent") ||
                !strcmp(p, "receiveVstTimeInfo")) ? 1 : -1;
    default: return 0;
    }
}

static void processReplacing(AEffect *e, float **in, float **o, int32_t n) {
    (void)in;
    out.numEvents = 0;
    for (int i = 0; i < in_n; i++) {           /* echo + octave up */
        VstMidiEvent *m = &in_q[i];
        int st = m->midiData[0] & 0xf0;
        add(m->deltaFrames, m->midiData[0], m->midiData[1], m->midiData[2]);
        if ((st == 0x90 || st == 0x80) && m->midiData[1] < 116)
            add(m->deltaFrames, m->midiData[0], m->midiData[1] + 12, m->midiData[2]);
        if (st == 0x90 && m->midiData[2]) blip = 1;
    }
    in_n = 0;

    VstTimeInfo *ti = (VstTimeInfo *)master(e, audioMasterGetTime, 0, kVstPpqPosValid | kVstTempoValid, 0, 0);
    if (logged_blocks < 3 && ti) { LOG("time flags=%x ppq=%.3f tempo=%.2f\n", ti->flags, ti->ppqPos, ti->tempo); logged_blocks++; }
    if (ti && (ti->flags & kVstTransportPlaying) && (ti->flags & kVstPpqPosValid) && ti->tempo > 0) {
        double ppqPerSample = ti->tempo / 60.0 / sr, step = 0.5;
        double start = ti->ppqPos, end = start + n * ppqPerSample;
        double next = ceil(start / step) * step;
        for (; next < end; next += step) {
            int d = (int)((next - start) / ppqPerSample);
            if (cur_note >= 0) add(d, 0x80, cur_note, 0);
            cur_note = 48;
            add(d, 0x90, cur_note, 100);
            blip = 1;
        }
    } else if (cur_note >= 0) {
        add(0, 0x80, cur_note, 0);
        cur_note = -1;
    }
    if (out.numEvents) {
        intptr_t r = master(e, audioMasterProcessEvents, 0, 0, &out, 0);
        if (r) sent_ok++; else sent_fail++;
        if ((sent_ok + sent_fail) <= 5 || (sent_ok + sent_fail) % 50 == 0)
            LOG("sent %d events, host returned %ld (ok=%ld fail=%ld)\n", out.numEvents, (long)r, sent_ok, sent_fail);
    }
    for (int i = 0; i < n; i++) {               /* blip */
        float s = blip > 0.001 ? (float)(0.2 * blip * sin(i * 0.3)) : 0;
        blip *= 0.9995;
        o[0][i] = s; o[1][i] = s;
    }
}

__attribute__((visibility("default"))) AEffect *VSTPluginMain(audioMasterCallback m) {
    master = m;
    if (!lg) lg = fopen("/tmp/midipoc.log", "a");
    LOG("VSTPluginMain\n");
    memset(&fx, 0, sizeof fx);
    fx.magic = 0x56737450;
    fx.dispatcher = dispatcher;
    fx.processReplacing = processReplacing;
    fx.numInputs = 0; fx.numOutputs = 2;
    fx.flags = (1 << 4) | (1 << 8);
    fx.uniqueID = 0x4d4f7550; /* 'MOuP' */
    fx.version = 1000;
    return &fx;
}
