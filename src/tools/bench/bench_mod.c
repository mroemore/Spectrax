/*
 * bench_mod.c — what modulation costs.
 *
 * The audio bench answers "how many voices fit in real time". This answers
 * the question a patch designer actually asks: as a patch gains envelopes,
 * stages, LFOs and routes, how much of the voice budget does the modulation
 * graph eat? A route that pushes a patch over the line is a real limit, and
 * the cost is per-voice because processModulations runs per active voice.
 *
 * The harness is deliberately built from the same primitives the instrument
 * uses (createEnvelope/addEnvelopeStage/addModulation), and every measurement
 * is a direct timing of the synth loop at a known voice count, so the numbers
 * can be compared across builds and OS settings.
 */
#include "bench_mod.h"

#include "io.h"
#include "modsystem.h"
#include "oscillator.h"
#include "sample.h"
#include "sequencer.h"
#include "settings.h"
#include "voice.h"
#include "wavetable.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUF 256
/* Fallback only: when the device profile reports the rate the stream actually
 * opened at, that wins, because the deadline a patch must fit inside depends
 * on it (44100 -> 5.81 ms, 48000 -> 5.33 ms for a 256-frame buffer). */
#define SR_DEFAULT 48000

static double seconds_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* A modulation graph of a given shape, owned by the bench. */
typedef struct {
    ModList *modList;
    ParamList *paramList;
    int routes;
    int envelopes;
    int stages;
    int lfos;
} ModGraph;

/* Builds a modulation graph on a dedicated param/mod list that this bench
 * owns and walks directly. The graph is the measurement's subject, so it is
 * kept separate from the voice's instrument lists -- that keeps the reset
 * between cases exact (clearModList + clearParamList own their members) and
 * makes the per-graph cost reproducible.
 *
 * processModulations is called on this list, exactly as the audio thread
 * calls it on a voice's, so the work measured is the real routing work:
 * every mod's generate() plus every connection's apply. */
static void build_mod_graph(ModGraph *g, int envelopes, int stages,
                            int lfos, int routes, unsigned seed) {
    if (g->paramList) clearParamList(g->paramList);
    if (g->modList) clearModList(g->modList);
    g->envelopes = envelopes;
    g->stages = stages;
    g->lfos = lfos;
    g->routes = routes;

    ParamList *paramList = g->paramList;
    ModList *modList = g->modList;

    /* Destinations are ordinary params owned by this graph's paramList. */
    Parameter *dests[64];
    int nDests = 0;
    for (int i = 0; i < 64; i++) {
        char name[32];
        snprintf(name, sizeof name, "bench_dest_%d", i);
        Parameter *p = createParameter(paramList, name, 0.5f, 0.0f, 1.0f);
        if (!p) break;
        dests[nDests++] = p;
    }
    if (nDests == 0) return;

    Mod *sources[64];
    int nSrc = 0;
    for (int i = 0; i < envelopes && nSrc < 64; i++) {
        char name[32];
        snprintf(name, sizeof name, "bench_env_%d", i);
        Mod *e = createEnvelope(paramList, modList, name);
        if (!e) break;
        for (int s = 0; s < stages; s++) {
            char sn[32];
            snprintf(sn, sizeof sn, "bench_stage_%d", s);
            addEnvelopeStage(paramList, e, (s % 2) == 0,
                             0.05f + 0.01f * (float)s,
                             1.0f, 0.0f, sn);
        }
        sources[nSrc++] = e;
    }
    for (int i = 0; i < lfos && nSrc < 64; i++) {
        char name[32];
        snprintf(name, sizeof name, "bench_lfo_%d", i);
        Mod *l = createLFO(paramList, modList, i,
                           0.5f + 0.25f * (float)i, 0, name);
        if (!l) break;
        sources[nSrc++] = l;
    }

    /* Routes: each source fans out to destinations, wrapping when there are
     * fewer sources than routes so the count is honoured exactly. */
    for (int i = 0; i < routes && nDests > 0 && nSrc > 0; i++) {
        Mod *src = sources[i % nSrc];
        Parameter *dst = dests[(i * 7 + seed) % nDests];
        addModulation(paramList, modList, src, dst, 0.5f, MO_ADD);
    }
}

/* Renders n_buffers of the modulation graph and reports the per-buffer cost.
 * This is the audio thread's work shape: for each sample, process the whole
 * mod list and every connection. */
static int g_rate = SR_DEFAULT;

static double time_graph_run(const ModGraph *g, int n_buffers, double *max_ms) {
    double sum = 0.0;
    double mx = 0.0;
    int done = 0;

    for (int b = 0; b < n_buffers; b++) {
        if (bench_stop_requested) break;
        double t0 = seconds_now();
        for (int s = 0; s < BUF; s++) {
            processModulations(g->paramList, g->modList, 1.0f / (float)g_rate);
        }
        double ms = (seconds_now() - t0) * 1000.0;
        sum += ms;
        if (ms > mx) mx = ms;
        done++;
    }
    if (max_ms) *max_ms = mx;
    return done > 0 ? sum / (double)done : 0.0;
}

int run_bench_mod(BenchReport *r, const BenchOptions *o) {
    int channels = o->channels > 0 ? o->channels : 1;
    int voices = o->voices > 0 ? o->voices : 8;
    g_rate = o->audio_rate > 0 ? o->audio_rate : SR_DEFAULT;
    int n_buffers = o->duration > 0 ? o->duration * (int)(g_rate / BUF)
                                    : (int)(g_rate / BUF);

    if (voices < 1) voices = 1;
    if (channels < 1) channels = 1;
    if (channels > MAX_SEQUENCER_CHANNELS) channels = MAX_SEQUENCER_CHANNELS;

    bench_report_log(r, "bench_mod: voices=%d channels=%d buffers=%d\n",
                     voices, channels, n_buffers);

    /* Mirrors bench_audio's bootstrap exactly: createSettings, real pools, a
     * real PresetBank, then initVoicePool per channel against the channel's
     * Instrument. Passing NULL for the bank crashes inside
     * createVoiceManager. */
    Settings settings;
    createSettings(&settings);
    settings.defaultVoiceCount = voices;
    settings.enabledChannels = channels;

    SamplePool *sp = createSamplePool();
    WavetablePool *wtp = createWavetablePool();
    PresetBank pb;
    initPresetBank(&pb);
    if (!sp || !wtp) {
        bench_report_log(r, "bench_mod: pool creation failed\n");
        return 1;
    }
    if (access("resources/samples/", R_OK) == 0) {
        loadSamplesfromDirectory("resources/samples/", sp);
    }
    VoiceManager *vm = createVoiceManager(&settings, sp, wtp, &pb);
    if (!vm) {
        bench_report_log(r, "bench_mod: createVoiceManager failed\n");
        return 1;
    }

    int total = 0;
    for (int c = 0; c < channels; c++) {
        Instrument *inst = vm->instruments[c];
        initVoicePool(vm, c, voices, inst);
        total += vm->voiceCount[c];
    }
    if (total <= 0) {
        bench_report_log(r, "bench_mod: no voices allocated\n");
        return 1;
    }

    /* Sweep the graph shape. Each row adds one dimension so the marginal
     * cost of a route, a stage and an LFO is separable. */
    struct {
        const char *label;
        int envs, stages, lfos, routes;
    } cases[] = {
        { "baseline_none",      0,  0, 0,  0 },
        { "env1_stage4",        1,  4, 0,  0 },
        { "env4_stage4",        4,  4, 0,  0 },
        { "env4_stage8",        4,  8, 0,  0 },
        { "env4_s8_lfo4",       4,  8, 4,  0 },
        { "env4_s8_lfo4_r16",   4,  8, 4, 16 },
        { "env4_s8_lfo4_r64",   4,  8, 4, 64 },
        { "env8_s8_lfo8_r64",   8,  8, 8, 64 },
        { "env8_s8_lfo8_r128",  8,  8, 8, 128 },
    };
    int nCases = (int)(sizeof cases / sizeof cases[0]);

    ModGraph g;
    memset(&g, 0, sizeof g);
    g.paramList = createParamList();
    g.modList = createModList();
    if (!g.paramList || !g.modList) {
        bench_report_log(r, "bench_mod: graph list creation failed\n");
        return 1;
    }

    for (int i = 0; i < nCases && !bench_stop_requested; i++) {
        build_mod_graph(&g, cases[i].envs, cases[i].stages,
                        cases[i].lfos, cases[i].routes, (unsigned)i);

        double max_ms = 0.0;
        double avg = time_graph_run(&g, n_buffers, &max_ms);
        /* What a 256-frame buffer costs in real time at the stream rate. */
        double budget_ms = 1000.0 * (double)BUF / (double)g_rate;

        char key[64];
        snprintf(key, sizeof key, "case_%s", cases[i].label);
        bench_report_metric(r, "mod", key, avg);

        snprintf(key, sizeof key, "case_%s_budget_pct", cases[i].label);
        bench_report_metric(r, "mod", key, 100.0 * avg / budget_ms);

        snprintf(key, sizeof key, "case_%s_max_ms", cases[i].label);
        bench_report_metric(r, "mod", key, max_ms);

        printf("  %-20s avg=%7.3f ms  max=%7.3f ms  budget=%6.1f%%  (env%d s%d lfo%d r%d)\n",
               cases[i].label, avg, max_ms, 100.0 * avg / budget_ms,
               cases[i].envs, cases[i].stages, cases[i].lfos, cases[i].routes);

    }

    if (g.modList) freeModList(g.modList);
    if (g.paramList) freeParamList(g.paramList);

    bench_report_metric(r, "mod", "buffer_frames", (double)BUF);
    bench_report_metric(r, "mod", "budget_ms", 1000.0 * (double)BUF / (double)g_rate);
    bench_report_metric(r, "mod", "sample_rate", (double)g_rate);
    bench_report_metric(r, "mod", "voices_per_channel", (double)voices);
    bench_report_metric(r, "mod", "channels", (double)channels);

    freeVoiceManager(vm);
    freeSamplePool(sp);
    freeWavetablePool(wtp);
    return 0;
}
