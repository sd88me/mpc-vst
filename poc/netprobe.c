/* Capability probe: what can a VST2 plugin inside MPC OS do? Pass-through effect;
   on first instantiation a background thread tests DNS + HTTP, a file write into the
   user's documents, and child-process spawning (posix_spawn, incl. from /sdcard).
   Results: /tmp/netprobe.log (also copied next to the written test file). */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netdb.h>
#include <sys/socket.h>
#include <time.h>

extern char **environ;
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

static FILE *lg;
#define LOG(...) do { if (lg) { fprintf(lg, __VA_ARGS__); fflush(lg); } } while (0)

static void http_test(const char *host, const char *path) {
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_socktype = SOCK_STREAM;
    int e = getaddrinfo(host, "80", &hints, &res);
    if (e) { LOG("DNS %s: FAIL %s\n", host, gai_strerror(e)); return; }
    int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    struct timeval tv = {5, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    if (s < 0 || connect(s, res->ai_addr, res->ai_addrlen)) { LOG("TCP %s: FAIL\n", host); freeaddrinfo(res); if (s >= 0) close(s); return; }
    char req[256], buf[512] = {0};
    snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: mpc-vst-probe\r\n\r\n", path, host);
    send(s, req, strlen(req), 0);
    int n = recv(s, buf, sizeof buf - 1, 0);
    char *nl = strchr(buf, '\r'); if (nl) *nl = 0;
    LOG("HTTP %s%s: %s (%d bytes first read)\n", host, path, n > 0 ? buf : "NO REPLY", n);
    close(s); freeaddrinfo(res);
}

static void spawn_test(const char *label, char *const argv[]) {
    pid_t pid; int st = -1;
    int e = posix_spawn(&pid, argv[0], NULL, NULL, argv, environ);
    if (e) { LOG("spawn %s: FAIL errno %d\n", label, e); return; }
    waitpid(pid, &st, 0);
    LOG("spawn %s: pid %d exit %d\n", label, pid, WIFEXITED(st) ? WEXITSTATUS(st) : -1);
}

static void *probe(void *arg) {
    (void)arg;
    LOG("== probe start, uid %d, cwd-free, pid %d\n", getuid(), getpid());
    http_test("example.com", "/");
    http_test("api.discogs.com", "/");
    const char *dirs[] = { "/sdcard/Force Documents", "/sdcard/MPC Documents", "/sdcard" };
    for (int i = 0; i < 3; i++) {
        struct stat sb;
        if (stat(dirs[i], &sb)) continue;
        char p[256]; snprintf(p, sizeof p, "%s/mpc-vst-probe.txt", dirs[i]);
        FILE *f = fopen(p, "w");
        LOG("write %s: %s\n", p, f ? "OK" : "FAIL");
        if (f) { fprintf(f, "written by a VST2 plugin inside MPC OS at %ld\n", (long)time(NULL)); fclose(f); }
        break;
    }
    char *a1[] = { "/bin/sh", "-c", "id > /tmp/netprobe_child.txt; echo child-ok >> /tmp/netprobe_child.txt", NULL };
    spawn_test("/bin/sh", a1);
    FILE *sc = fopen("/sdcard/vst/netprobe_exec.sh", "w");
    if (sc) { fprintf(sc, "#!/bin/sh\necho sdcard-exec-ok >> /tmp/netprobe_child.txt\n"); fclose(sc); chmod("/sdcard/vst/netprobe_exec.sh", 0755); }
    char *a2[] = { "/sdcard/vst/netprobe_exec.sh", NULL };
    spawn_test("/sdcard script (noexec check)", a2);
    LOG("== probe done\n");
    return NULL;
}

static intptr_t dispatcher(AEffect *e, int32_t op, int32_t idx, intptr_t v, void *p, float o) {
    (void)e; (void)idx; (void)v; (void)o;
    switch (op) {
    case 35: return 1;
    case 45: case 48: strcpy(p, "MPC Capability Probe"); return 1;
    case 47: strcpy(p, "sd88me"); return 1;
    case 58: return 2400;
    default: return 0;
    }
}
static void processReplacing(AEffect *e, float **in, float **out, int32_t n) {
    (void)e;
    memcpy(out[0], in[0], n * sizeof(float));
    memcpy(out[1], in[1], n * sizeof(float));
}

static AEffect fx;
__attribute__((visibility("default"))) AEffect *VSTPluginMain(audioMasterCallback m) {
    (void)m;
    static int started;
    if (!lg) lg = fopen("/tmp/netprobe.log", "a");
    if (!started) {
        pthread_t t;
        started = 1;
        if (pthread_create(&t, NULL, probe, NULL) == 0) pthread_detach(t);
    }
    memset(&fx, 0, sizeof fx);
    fx.magic = 0x56737450;
    fx.dispatcher = dispatcher;
    fx.processReplacing = processReplacing;
    fx.numInputs = 2; fx.numOutputs = 2;
    fx.flags = 1 << 4;
    fx.uniqueID = 0x4e505262; /* 'NPRb' */
    fx.version = 1000;
    return &fx;
}
