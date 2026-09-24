/* =============================================================================
 * dx7_vst.c -- port-specific VST2 wrapper for Force DX7 (force-dx7's dx7_host).
 *
 * This is NOT wrapper/vst2_wrap.c (that one links a Schwung plugin_api_v2 DSP
 * module in-process and calls render_block() directly). force-dx7 doesn't fit
 * that shape (see mpc-vst/docs/PORTING.md category 2, "engine with host-side
 * glue"): the real DSP lives inside a standalone process, dx7_host, which:
 *   - owns its own render clock (a timer thread) and writes audio into a
 *     POSIX shared-memory ring that ForceAudioJack.so (an LD_PRELOAD shim in
 *     the *Force's* MPC process) mixes into what MPC reads from capture --
 *     not a per-block DSP callback this VST's processReplacing can call.
 *   - is controlled entirely over a Unix control socket, "SET key val\n" /
 *     "GET key\n" -> "val\n" / "ERR\n", key parsed by dx7_host with atoi()
 *     (see force-dx7/src/dx7_host.cpp's handle_ctrl_line).
 *   - takes MIDI over its own RtMidi virtual ALSA port ("<client>:In (Mockba)",
 *     default client "DX7"), not via this VST's own MIDI-in events.
 *
 * So this wrapper: spawns/attaches to dx7_host over its control socket for
 * every parameter get/set (params.h, generated from params.json, which is
 * itself derived from addon/shadow_page.conf's full ~150-key control surface
 * -- module.json's own chain_params only lists a curated ~20), forwards VST
 * MIDI events to dx7_host's ALSA port, and serializes chunk state as its own
 * key=value blob (dx7_host has no "state" key the way Schwung DSPs do).
 *
 * AUDIO PASSTHROUGH IS NOT SOLVED HERE -- processReplacing outputs silence.
 * See the big comment above processReplacing() for exactly why and what the
 * smallest viable fix looks like. Do not treat this port as producing sound
 * through the VST audio path; it is a remote-control + MIDI-conduit plugin
 * for whichever single dx7_host is running (or gets spawned) on the device,
 * which itself still reaches the speakers only via ForceAudioJack, exactly as
 * it does today for the shadow-GUI addon. See mpc-vst/force-dx7/NOTES.md.
 * ========================================================================== */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <alsa/asoundlib.h>
#include "params.h"

extern char **environ;

/* ---- defaults, overridable by env vars so the same .so works whether a
 * dx7_host is already running (normal device use, started from /moduler per
 * NSMODULE.json) or needs spawning (host-test / a fresh instance) --------- */
#ifndef DX7_DEFAULT_CTRL_SOCK
#define DX7_DEFAULT_CTRL_SOCK "/tmp/dx7_ctrl.sock"
#endif
#ifndef DX7_DEFAULT_MODULE_DIR
#define DX7_DEFAULT_MODULE_DIR "/media/662522/AddOns/ForceDX7"
#endif
#ifndef DX7_DEFAULT_HOST_BIN
#define DX7_DEFAULT_HOST_BIN "/media/662522/AddOns/ForceDX7/dx7_host"
#endif
#define DX7_MIX_SLOT "2"          /* matches addon/NSMODULE.json's curated default */
#define DX7_CTRL_CHANNEL "1"
#define DX7_ALSA_CLIENT "DX7"
#define DX7_ALSA_PORT_SUBSTR "In (Mockba)"

/* ---- VST2 ABI (hand-written; no Steinberg SDK; same layout as wrapper/vst2_wrap.c) */
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
typedef struct { int32_t numEvents; intptr_t reserved; VstEvent *events[64]; } VstEvents;

enum {
    effOpen = 0, effClose = 1, effGetParamLabel = 6, effGetParamDisplay = 7, effGetParamName = 8,
    effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12, effGetChunk = 23,
    effSetChunk = 24, effProcessEvents = 25, effCanBeAutomated = 26, effGetPlugCategory = 35,
    effGetEffectName = 45, effGetVendorString = 47, effGetProductString = 48,
    effGetVendorVersion = 49, effCanDo = 51, effGetVstVersion = 58,
};
enum { effFlagsCanReplacing = 1 << 4, effFlagsProgramChunks = 1 << 5, effFlagsIsSynth = 1 << 8 };

#define PLUG_NAME "Force DX7"
#define PLUG_VENDOR "sd88me"
#define PLUG_UID 0x46447837 /* 'FDx7' */
#define PLUG_VERSION 1000

/* ---------------------------------------------------------------------------
 * Control-socket client: one shared connection per plugin instance. Reused
 * across every get/set (dx7_host handles one line per accept()ed connection
 * -- see handle_ctrl_line/ctrl_server_loop -- so we keep our own fd open and
 * reconnect lazily on failure rather than dialing fresh for every call).
 * ------------------------------------------------------------------------- */
typedef struct {
    AEffect fx;
    audioMasterCallback master;
    int ctrl_fd;
    char ctrl_sock[256];
    pid_t spawned_pid;      /* >0 if THIS instance spawned dx7_host itself */
    snd_seq_t *seq;
    int seq_port;
    int midi_dst_client, midi_dst_port;  /* -1 until resolved */
} wrap_t;

static int ctrl_connect(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) { close(fd); return -1; }
    return fd;
}

/* posix_spawn dx7_host with LD_PRELOAD stripped (MockbaMod units preload C++
 * libs into MPC's own environment that abort in a plain child -- see
 * mpc-vst/docs/NOTES.md "Beyond synths" -- and per CLAUDE.md/PORTING.md this
 * repo never uses fork() to avoid copying MPC's large RT process). */
static pid_t spawn_dx7_host(const char *bin, const char *module_dir, const char *ctrl_sock) {
    char *argv[] = {
        (char *)bin,
        "--module-dir", (char *)module_dir,
        "--ctrl-sock", (char *)ctrl_sock,
        "--control-channel", DX7_CTRL_CHANNEL,
        "--mix-slot", DX7_MIX_SLOT,
        NULL
    };
    /* clean environment: copy everything except LD_PRELOAD */
    int n = 0;
    while (environ[n]) n++;
    char **env = calloc((size_t)n + 1, sizeof(char *));
    int j = 0;
    for (int i = 0; i < n; i++)
        if (strncmp(environ[i], "LD_PRELOAD=", 11) != 0) env[j++] = environ[i];
    env[j] = NULL;

    pid_t pid;
    int rc = posix_spawn(&pid, bin, NULL, NULL, argv, env);
    free(env);
    if (rc != 0) { fprintf(stderr, "[dx7_vst] posix_spawn(%s) failed: %s\n", bin, strerror(rc)); return -1; }
    return pid;
}

/* Connect to an already-running dx7_host; if that fails, spawn our own
 * (DX7_VST_HOST_BIN / DX7_VST_MODULE_DIR env overrides for host-test, else
 * the on-device defaults matching addon/NSMODULE.json's engine_arguments).
 *
 * IMPORTANT: dx7_host's control-socket server is single-threaded and
 * strictly one-connection-at-a-time -- accept(), recv() ONE line, reply,
 * close(), loop back to accept() (see ctrl_server_loop/handle_ctrl_line).
 * A connection that's accepted but never sent to blocks that server in
 * recv() forever, starving every other client (including this same plugin's
 * OTHER instances) -- so we must never hold an idle connected fd open. This
 * probe connects only to test reachability, then closes immediately;
 * ctrl_txn() always dials a fresh connection per request/reply. (An earlier
 * version of this wrapper kept w->ctrl_fd open across calls "for reuse" --
 * that deadlocked the two-instance host test: instance b's idle post-open
 * connection got accepted and starved every later request from instance a.) */
static void ctrl_open(wrap_t *w) {
    const char *sock = getenv("DX7_VST_CTRL_SOCK");
    if (sock) strncpy(w->ctrl_sock, sock, sizeof(w->ctrl_sock) - 1);
    else strncpy(w->ctrl_sock, DX7_DEFAULT_CTRL_SOCK, sizeof(w->ctrl_sock) - 1);

    int probe = ctrl_connect(w->ctrl_sock);
    if (probe >= 0) { close(probe); return; }

    const char *bin = getenv("DX7_VST_HOST_BIN");
    if (!bin) bin = DX7_DEFAULT_HOST_BIN;
    const char *mdir = getenv("DX7_VST_MODULE_DIR");
    if (!mdir) mdir = DX7_DEFAULT_MODULE_DIR;
    if (access(bin, X_OK) != 0) {
        fprintf(stderr, "[dx7_vst] no dx7_host running on %s and no executable at %s -- "
                        "parameters/MIDI will not work until one is started\n", w->ctrl_sock, bin);
        return;
    }
    w->spawned_pid = spawn_dx7_host(bin, mdir, w->ctrl_sock);
    if (w->spawned_pid <= 0) return;
    for (int i = 0; i < 50; i++) {   /* up to ~2.5s for it to bind+listen */
        usleep(50000);
        probe = ctrl_connect(w->ctrl_sock);
        if (probe >= 0) { close(probe); return; }
    }
    fprintf(stderr, "[dx7_vst] spawned dx7_host (pid %d) but could not connect to %s\n",
            (int)w->spawned_pid, w->ctrl_sock);
}

static void ctrl_close(wrap_t *w) {
    /* We only kill it if we spawned it ourselves; an already-running
     * dx7_host (the normal on-device case, started from /moduler) is left
     * alone -- other tracks/instances may still be using it. */
    if (w->spawned_pid > 0) {
        kill(w->spawned_pid, SIGTERM);
        int status;
        waitpid(w->spawned_pid, &status, 0);
    }
}

/* One request/reply over the control socket: always a fresh dial-send-recv-
 * close, matching dx7_host's one-line-per-connection server (see ctrl_open's
 * comment on why no persistent connection is kept). */
static int ctrl_txn(wrap_t *w, const char *req, char *reply, int replylen) {
    int fd = ctrl_connect(w->ctrl_sock);
    if (fd < 0) return -1;
    size_t len = strlen(req);
    if (send(fd, req, len, 0) != (ssize_t)len) { close(fd); return -1; }
    ssize_t n = recv(fd, reply, replylen - 1, 0);
    close(fd);
    if (n <= 0) return -1;
    reply[n] = 0;
    char *nl = strchr(reply, '\n');
    if (nl) *nl = 0;
    return (int)strlen(reply);
}

static int ctrl_set(wrap_t *w, const char *key, const char *val) {
    char req[320], reply[64];
    snprintf(req, sizeof req, "SET %s %s\n", key, val);
    return ctrl_txn(w, req, reply, sizeof reply) >= 0 && !strcmp(reply, "OK") ? 0 : -1;
}
static int ctrl_get(wrap_t *w, const char *key, char *buf, int buflen) {
    char req[96];
    snprintf(req, sizeof req, "GET %s\n", key);
    int n = ctrl_txn(w, req, buf, buflen);
    if (n < 0 || !strcmp(buf, "ERR")) return -1;
    return n;
}

/* ---------------------------------------------------------------------------
 * ALSA seq: forward VST MIDI-in events to dx7_host's virtual port. Same
 * approach as poc/midiport.c (open our own client/port, snd_seq_event_output_
 * direct), except here WE are the sender connecting TO an existing input
 * port (dx7_host's "<client>:In (Mockba)") rather than exposing our own port
 * for MPC to route into -- so resolve the destination client:port by name
 * once (cached), reconnect lazily if dx7_host was restarted (new client id).
 * ------------------------------------------------------------------------- */
static int resolve_dx7_port(snd_seq_t *seq, int *out_client, int *out_port) {
    snd_seq_client_info_t *cinfo;
    snd_seq_port_info_t *pinfo;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_port_info_alloca(&pinfo);
    snd_seq_client_info_set_client(cinfo, -1);
    while (snd_seq_query_next_client(seq, cinfo) >= 0) {
        int client = snd_seq_client_info_get_client(cinfo);
        if (strcmp(snd_seq_client_info_get_name(cinfo), DX7_ALSA_CLIENT) != 0) continue;
        snd_seq_port_info_set_client(pinfo, client);
        snd_seq_port_info_set_port(pinfo, -1);
        while (snd_seq_query_next_port(seq, pinfo) >= 0) {
            if (strstr(snd_seq_port_info_get_name(pinfo), DX7_ALSA_PORT_SUBSTR)) {
                *out_client = client;
                *out_port = snd_seq_port_info_get_port(pinfo);
                return 0;
            }
        }
    }
    return -1;
}

static void midi_open(wrap_t *w) {
    w->seq = NULL;
    w->seq_port = -1;
    w->midi_dst_client = w->midi_dst_port = -1;
    if (snd_seq_open(&w->seq, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) { w->seq = NULL; return; }
    snd_seq_set_client_name(w->seq, "Force DX7 VST");
    w->seq_port = snd_seq_create_simple_port(w->seq, "MIDI Out",
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
}

static void midi_send(wrap_t *w, const unsigned char *m) {
    if (!w->seq || w->seq_port < 0) return;
    if (w->midi_dst_client < 0 &&
        resolve_dx7_port(w->seq, &w->midi_dst_client, &w->midi_dst_port) != 0)
        return;  /* dx7_host not up (or no MIDI port yet) -- silently drop, matches
                   * "parameters/MIDI don't work until dx7_host exists" behaviour */
    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, w->seq_port);
    snd_seq_ev_set_dest(&ev, w->midi_dst_client, w->midi_dst_port);
    snd_seq_ev_set_direct(&ev);
    uint8_t status = m[0] & 0xF0, chan = m[0] & 0x0F;
    if (status == 0x90 && m[2]) snd_seq_ev_set_noteon(&ev, chan, m[1], m[2]);
    else if (status == 0x80 || (status == 0x90 && !m[2])) snd_seq_ev_set_noteoff(&ev, chan, m[1], 0);
    else if (status == 0xB0) snd_seq_ev_set_controller(&ev, chan, m[1], m[2]);
    else if (status == 0xE0) snd_seq_ev_set_pitchbend(&ev, chan, ((m[2] << 7) | m[1]) - 8192);
    else if (status == 0xD0) { snd_seq_ev_set_chanpress(&ev, chan, m[1]); }
    else if (status == 0xA0) { snd_seq_ev_set_keypress(&ev, chan, m[1], m[2]); }
    else return;
    int r = snd_seq_event_output_direct(w->seq, &ev);
    if (r < 0) { w->midi_dst_client = -1; }   /* stale route (dx7_host restarted): re-resolve next time */
}

/* ---------------------------------------------------------------------------
 * Parameters
 * ------------------------------------------------------------------------- */
static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static void norm_to_str(const param_t *p, float n, char *buf, int len) {
    if (p->nopts) snprintf(buf, len, "%d", (int)lroundf(clamp01(n) * (p->nopts - 1)));
    else snprintf(buf, len, "%g", p->min + (p->max - p->min) * clamp01(n));
}
static float str_to_norm(const param_t *p, const char *s) {
    if (p->nopts) {
        int idx = isdigit((unsigned char)s[0]) ? atoi(s) : 0;
        if (idx < 0) idx = 0;
        if (idx >= p->nopts) idx = p->nopts - 1;
        return p->nopts > 1 ? (float)idx / (p->nopts - 1) : 0;
    }
    return p->max > p->min ? clamp01((float)((atof(s) - p->min) / (p->max - p->min))) : 0;
}

static float get_norm(wrap_t *w, int i) {
    char buf[64];
    if (i < 0 || i >= NPARAMS) return 0;
    if (ctrl_get(w, PARAMS[i].key, buf, sizeof buf) <= 0) return PARAMS[i].def;
    return str_to_norm(&PARAMS[i], buf);
}

static void setParameter(AEffect *e, int32_t i, float n) {
    wrap_t *w = e->object;
    char buf[64];
    if (i < 0 || i >= NPARAMS || PARAMS[i].string_display) return;  /* string readouts are read-only */
    norm_to_str(&PARAMS[i], n, buf, sizeof buf);
    ctrl_set(w, PARAMS[i].key, buf);
}
static float getParameter(AEffect *e, int32_t i) { return get_norm(e->object, i); }

static void processReplacing(AEffect *e, float **in, float **out, int32_t n) {
    /* ---------------------------------------------------------------------
     * AUDIO PASSTHROUGH: NOT IMPLEMENTED. dx7_host renders into a POSIX
     * shared-memory ring (force-dx7/src/forceAudioInject.h, "/forceAudioInject2"
     * by default) that is documented as strictly single-producer/single-
     * consumer -- ForceAudioJack.so is the one consumer, draining it on
     * MPC's own real-time capture thread and advancing `tail` itself. A
     * second reader (this VST) attaching the same shm segment could FOLLOW
     * `head` read-only without touching `tail` (never claim the consumer
     * role), but then either:
     *   (a) it duplicates ForceAudioJack's audio (fine, if the point is
     *       "the same dx7_host sound now also appears at this VST's own
     *       track fader/output", which is a real, different thing from
     *       "used instead of ForceAudioJack"), racing only on the read
     *       side of `head`/`ring[]` (safe: `head` is release-published,
     *       we just never move `tail`), or
     *   (b) if force-dx7 is running WITHOUT ForceAudioJack also enabled
     *       (matches nothing draining `tail`, ring fills, `ring_push`
     *       drops frames once full) -- the ring keeps rendering regardless,
     *       so this VST could genuinely be the ONLY consumer in that mode.
     * Neither is implemented here: doing it correctly needs its own
     * shadowed read cursor (this instance's own `tail`-equivalent, never
     * written into the shared struct) plus resample/rebuffer from the
     * ring's 44.1k/128-frame producer cadence into whatever block size the
     * VST host calls with -- real work, and untestable offline without a
     * running dx7_host + shm segt (see NOTES.md "Offline test" section).
     * Smallest viable fix instead of a hacky shadow-reader: teach dx7_host
     * an alternate output mode (`--vst-shm <name>` or reuse an unused mix
     * slot) that a VST wrapper can open as sole owner/consumer, exactly
     * like a normal SPSC ring -- a small, additive change to
     * force-dx7/src/dx7_host.cpp, no risk to the existing shadow-GUI path.
     * Until then: silence out, parameters/MIDI/chunk fully wired. Do not
     * present this as "plays audio" -- it currently does not. */
    (void)e; (void)in;
    for (int32_t i = 0; i < n; i++) { out[0][i] = 0.0f; out[1][i] = 0.0f; }
}

static void copy_str(void *dst, const char *src, size_t max) {
    strncpy(dst, src, max - 1);
    ((char *)dst)[max - 1] = 0;
}

/* ---------------------------------------------------------------------------
 * Chunk save/restore: dx7_host has no "state" key (unlike a Schwung DSP's
 * get_param("state")/set_param("state", ...) -- see wrapper/vst2_wrap.c) so
 * we build our own key=value;key=value;... blob from every non-string
 * param's current GET, same pattern as force-acid's acid_vst.cpp wrapper
 * (mpc-vst/docs/NOTES.md "MIDI-generator VST wrapping a standalone-process
 * engine"). Patch/bank identity travels via `preset` + whichever bank is
 * loaded (syx_bank_name is read-only/informational, not restorable this
 * way -- restoring a specific .syx bank selection is not wired to a
 * settable key in dx7_host's control protocol; only the settable numeric/
 * enum params round-trip through this chunk). */
#define CHUNK_MAX 8192

static intptr_t dispatcher(AEffect *e, int32_t op, int32_t idx, intptr_t v, void *p, float o) {
    wrap_t *w = e->object;
    (void)o;
    switch (op) {
    case effOpen: return 1;
    case effClose:
        ctrl_close(w);
        if (w->seq) snd_seq_close(w->seq);
        free(w);
        return 1;
    case effGetPlugCategory: return 2;
    case effGetEffectName:
    case effGetProductString: copy_str(p, PLUG_NAME, 32); return 1;
    case effGetVendorString: copy_str(p, PLUG_VENDOR, 32); return 1;
    case effGetVendorVersion: return PLUG_VERSION;
    case effGetVstVersion: return 2400;
    case effCanBeAutomated: return idx >= 0 && idx < NPARAMS && !PARAMS[idx].string_display;
    case effGetParamName:
        if (idx >= 0 && idx < NPARAMS) copy_str(p, PARAMS[idx].name, 32);
        return 1;
    case effGetParamLabel:
        if (idx >= 0 && idx < NPARAMS) copy_str(p, "", 8);
        return 1;
    case effGetParamDisplay: {
        char buf[64];
        if (idx < 0 || idx >= NPARAMS) return 0;
        const param_t *pp = &PARAMS[idx];
        if (pp->nopts) {
            int k = (int)lroundf(get_norm(w, idx) * (pp->nopts - 1));
            copy_str(p, pp->opts[k], 24);
        } else if (ctrl_get(w, pp->key, buf, sizeof buf) > 0) {
            if (pp->string_display) copy_str(p, buf, 24);
            else snprintf(p, 24, "%.*f", fabs(pp->max - pp->min) > 20 ? 0 : 1, atof(buf));
        } else {
            copy_str(p, "?", 24);
        }
        return 1;
    }
    case effSetSampleRate: case effSetBlockSize: case effMainsChanged: return 1;
    case effProcessEvents: {
        VstEvents *ev = p;
        for (int i = 0; i < ev->numEvents; i++)
            if (ev->events[i]->type == 1) {
                VstMidiEvent *m = (VstMidiEvent *)ev->events[i];
                midi_send(w, m->midiData);
            }
        return 1;
    }
    case effCanDo:
        return (!strcmp(p, "receiveVstEvents") || !strcmp(p, "receiveVstMidiEvent")) ? 1 : -1;
    case effGetChunk: {
        static char chunk[CHUNK_MAX];
        int off = 0;
        for (int i = 0; i < NPARAMS && off < CHUNK_MAX - 96; i++) {
            if (PARAMS[i].string_display) continue;
            char val[64];
            if (ctrl_get(w, PARAMS[i].key, val, sizeof val) <= 0) continue;
            off += snprintf(chunk + off, CHUNK_MAX - off, "%s=%s;", PARAMS[i].key, val);
        }
        *(void **)p = chunk;
        return off + 1;
    }
    case effSetChunk: {
        if (v <= 0 || (size_t)v > CHUNK_MAX) return 0;
        char *chunk = malloc((size_t)v + 1);
        memcpy(chunk, p, (size_t)v);
        chunk[v] = 0;
        char *save = NULL;
        for (char *tok = strtok_r(chunk, ";", &save); tok; tok = strtok_r(NULL, ";", &save)) {
            char *eq = strchr(tok, '=');
            if (!eq) continue;
            *eq = 0;
            ctrl_set(w, tok, eq + 1);
        }
        free(chunk);
        return 1;
    }
    default: return 0;
    }
}

__attribute__((visibility("default"))) AEffect *VSTPluginMain(audioMasterCallback master) {
    wrap_t *w = calloc(1, sizeof *w);
    if (!w) return NULL;
    w->ctrl_fd = -1;
    w->spawned_pid = -1;
    ctrl_open(w);
    midi_open(w);
    w->master = master;
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
