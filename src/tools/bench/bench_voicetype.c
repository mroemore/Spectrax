/*
 * bench_voicetype.c — per-voice-type synthesis cost.
 *
 * A voice's price is not one number: SAMPLE, FM, BLEP, GRAIN and SPECTRAL run
 * completely different generators, and the cheapest way to raise the voice
 * ceiling may be to prefer one type over another rather than to optimise the
 * shared path. This drives each type through the same mixer loop the audio
 * bench uses, at the same buffer size, and reports the per-voice cost against
 * the callback deadline.
 *
 * It measures with the instrument's real preset shape (makeDefault*Preset),
 * not a synthetic parameter set, so the numbers reflect what a user gets when
 * they pick that engine.
 */
#include "bench_voicetype.h"

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

#define BUF_MAX 512
#define SR_DEFAULT 44100

static double seconds_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int g_rate = SR_DEFAULT;
static int g_buf = 256;

typedef struct {
    const char *tag;
    VoiceType type;
} TypeCase;

/* Times `voices` voices of one type for n_buffers and reports the per-voice
 * per-buffer cost. The voice pool is rebuilt per case so no state carries
 * over and no save/restore of the instrument is needed. */
static double time_type(const BenchOptions *o, VoiceType type, int voices,
                        int channels, int n_buffers, double *out_per_voice,
                        double *out_max) {
    Settings settings;
    createSettings(&settings);
    settings.defaultVoiceCount = voices;
    settings.enabledChannels = channels;

    SamplePool *sp = createSamplePool();
    WavetablePool *wtp = createWavetablePool();
    PresetBank pb;
    initPresetBank(&pb);
    if (!sp || !wtp) return -1.0;
    if (access("resources/samples/", R_OK) == 0) {
        loadSamplesfromDirectory("resources/samples/", sp);
    }
    if (access("data/samples/", R_OK) == 0) {
        loadSamplesfromDirectory("data/samples/", sp);
    }
    VoiceManager *vm = createVoiceManager(&settings, sp, wtp, &pb);
    if (!vm) return -1.0;

    /* Every channel uses the requested engine. FM gets the shipped default
     * FM patch; the others get their own default shape. */
    for (int c = 0; c < channels; c++) {
        Instrument *inst = vm->instruments[c];
        if (!inst) continue;
        Preset p;
        if (type == VOICE_TYPE_FM) {
            p = makeDefaultFmPreset();
        } else {
            memset(&p, 0, sizeof p);
            p.voiceType = (int)type;
            snprintf(p.name, sizeof p.name, "bench_%d", (int)type);
        }
        applyInstrumentPreset(inst, p);
        initVoicePool(vm, c, voices, inst);
    }

    int total = 0;
    for (int c = 0; c < channels; c++) total += vm->voiceCount[c];
    /* Report the graph the voice actually carries, so a bench number can be
     * attributed to a topology rather than to an unnamed "voice". */
    if (channels > 0 && vm->voicePools[0][0]) {
        Voice *v0 = vm->voicePools[0][0];
        printf("  [%s] graph: clones=%d attens=%d dests=%d\n",
               type == VOICE_TYPE_FM ? "fm" : "other",
               v0->cloneCount, v0->attenCount, v0->destCount);
    }
    if (total <= 0) {
        freeVoiceManager(vm);
        freeSamplePool(sp);
        freeWavetablePool(wtp);
        return -1.0;
    }

    float *scratch = calloc((size_t)total * g_buf, sizeof(float));
    float *out = calloc(g_buf, sizeof(float));
    if (!scratch || !out) {
        free(scratch); free(out);
        freeVoiceManager(vm);
        freeSamplePool(sp);
        freeWavetablePool(wtp);
        return -1.0;
    }

    /* Pre-roll so envelopes and oscillator state are past initialisation. */
    for (int b = 0; b < 8; b++) {
        for (int c = 0; c < channels; c++) {
            for (int v_i = 0; v_i < vm->voiceCount[c]; v_i++) {
                Voice *v = vm->voicePools[c][v_i];
                if (v) for (int s = 0; s < g_buf; s++) generateVoice(vm, v, 1.0f, 440.0f);
            }
        }
    }

    double sum = 0.0, mx = 0.0;
    int done = 0;
    for (int b = 0; b < n_buffers; b++) {
        if (bench_stop_requested) break;
        double t0 = seconds_now();
        memset(out, 0, g_buf * sizeof(float));
        int vi = 0;
        for (int c = 0; c < channels; c++) {
            for (int v_i = 0; v_i < vm->voiceCount[c]; v_i++) {
                Voice *v = vm->voicePools[c][v_i];
                if (!v) continue;
                for (int s = 0; s < g_buf; s++) {
                    OutVal o2 = generateVoice(vm, v, 1.0f, 440.0f);
                    scratch[(size_t)vi * g_buf + s] = o2.L + o2.R;
                }
                vi++;
            }
        }
        double ms = (seconds_now() - t0) * 1000.0;
        sum += ms;
        if (ms > mx) mx = ms;
        done++;
    }

    double avg = done > 0 ? sum / (double)done : 0.0;
    if (out_max) *out_max = mx;
    if (out_per_voice) *out_per_voice = total > 0 ? avg / (double)total : 0.0;

    free(scratch);
    free(out);
    freeVoiceManager(vm);
    freeSamplePool(sp);
    freeWavetablePool(wtp);
    return avg;
}

int run_bench_voicetype(BenchReport *r, const BenchOptions *o) {
    int voices = o->voices > 0 ? o->voices : 4;
    int channels = o->channels > 0 ? o->channels : 1;
    g_rate = o->audio_rate > 0 ? o->audio_rate : SR_DEFAULT;
    /* The engine reads the rate from g_sampleRate; the bench must set it so
     * phase increments, filter coefficients and envelope rates all follow.
     * This is what makes the rate sweep meaningful rather than cosmetic. */
    g_sampleRate = (float)g_rate;
    /* Holding the callback PERIOD constant is the only fair way to compare
     * rates: 48000/256 and 24000/128 are both 5.33 ms. Sampling at a lower
     * rate at the SAME buffer size changes nothing except the deadline, which
     * is why the naive comparison shows a phantom win. */
    g_buf = o->buffer_frames > 0 ? o->buffer_frames : 256;
    if (g_buf < 16) g_buf = 16;
    if (g_buf > BUF_MAX) g_buf = BUF_MAX;
    int n_buffers = o->duration > 0 ? o->duration * (int)(g_rate / g_buf)
                                    : (int)(g_rate / g_buf);
    double budget_ms = 1000.0 * (double)g_buf / (double)g_rate;

    if (channels < 1) channels = 1;
    if (channels > MAX_SEQUENCER_CHANNELS) channels = MAX_SEQUENCER_CHANNELS;
    if (n_buffers < 1) n_buffers = 1;

    bench_report_log(r, "bench_voicetype: voices=%d channels=%d buffers=%d\n",
                     voices, channels, n_buffers);

    TypeCase cases[] = {
        { "sample",   VOICE_TYPE_SAMPLE   },
        { "fm",       VOICE_TYPE_FM       },
        { "blep",     VOICE_TYPE_BLEP     },
        { "grain",    VOICE_TYPE_GRAIN    },
        { "spectral", VOICE_TYPE_SPECTRAL },
    };
    int nCases = (int)(sizeof cases / sizeof cases[0]);

    for (int i = 0; i < nCases && !bench_stop_requested; i++) {
        if (cases[i].type == VOICE_TYPE_SPECTRAL ||
            cases[i].type == VOICE_TYPE_GRAIN) {
            /* Not measurable: GRAIN's generate is a stub that returns
             * silence, and SPECTRAL dereferences voice->vd.spectral.
             * spectralData, which initialize_voice never assigns. */
            char key[64];
            snprintf(key, sizeof key, "%s_total_ms", cases[i].tag);
            bench_report_str(r, "voicetype", key, "unimplemented");
            printf("  %-10s UNIMPLEMENTED (generate is a stub / uninitialised state)\n",
                   cases[i].tag);
            continue;
        }
        double per_voice = 0.0, max_ms = 0.0;
        double avg = time_type(o, cases[i].type, voices, channels, n_buffers,
                               &per_voice, &max_ms);
        if (avg < 0.0) {
            printf("  %-10s FAILED\n", cases[i].tag);
            continue;
        }
        char key[64];
        snprintf(key, sizeof key, "%s_total_ms", cases[i].tag);
        bench_report_metric(r, "voicetype", key, avg);
        snprintf(key, sizeof key, "%s_per_voice_ms", cases[i].tag);
        bench_report_metric(r, "voicetype", key, per_voice);
        snprintf(key, sizeof key, "%s_budget_pct", cases[i].tag);
        bench_report_metric(r, "voicetype", key, 100.0 * avg / budget_ms);
        snprintf(key, sizeof key, "%s_max_ms", cases[i].tag);
        bench_report_metric(r, "voicetype", key, max_ms);
        /* CPU milliseconds spent to render ONE SECOND of audio. This is the
         * only figure comparable across sample rates: the frames in a buffer
         * and the callback deadline both move with the rate, so per-buffer and
         * "% of budget" numbers change meaning while this one does not.
         * Below 1000 ms means the engine renders faster than real time. */
        snprintf(key, sizeof key, "%s_cpu_ms_per_audio_sec", cases[i].tag);
        bench_report_metric(r, "voicetype", key, avg * (double)n_buffers);
        /* Work per second of rendered audio: the only figure comparable
         * across sample rates, because both the frames and the deadline
         * change with the rate. */
        snprintf(key, sizeof key, "%s_ms_per_audio_sec", cases[i].tag);
        {
            /* avg is per buffer; n_buffers is one second of audio, so the
             * CPU milliseconds spent rendering one second of audio is
             * avg * n_buffers. Below 1000 the engine fits in real time. */
            bench_report_metric(r, "voicetype", key, avg * (double)n_buffers);
        }
        /* How many voices of this type fit inside one full callback. */
        snprintf(key, sizeof key, "%s_voices_in_budget", cases[i].tag);
        bench_report_metric(r, "voicetype", key,
                            per_voice > 0.0 ? budget_ms / per_voice : 0.0);

        printf("  %-10s total=%7.3f ms  per_voice=%6.3f ms  budget=%6.1f%%  fits=%4.1f voices\n",
               cases[i].tag, avg, per_voice, 100.0 * avg / budget_ms,
               per_voice > 0.0 ? budget_ms / per_voice : 0.0);
    }

    bench_report_metric(r, "voicetype", "buffer_frames", (double)g_buf);
    bench_report_metric(r, "voicetype", "budget_ms", budget_ms);
    bench_report_metric(r, "voicetype", "sample_rate", (double)g_rate);
    return 0;
}
