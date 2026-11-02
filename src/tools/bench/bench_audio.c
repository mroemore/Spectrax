/*
 * bench_audio.c — sustained-voice DSP loop measuring callback CPU + per-buffer latency.
 *
 * Drives the synth loop directly (not via PortAudio). The goal is a
 * deterministic, headless CPU-bound workload that exercises the same DSP path
 * the audio callback would. The --no-neon flag selects between the vectorised
 * mix kernel and a portable scalar fallback so we can quantify the SIMD win:
 * an A53 has 128-bit NEON, an A35 only 64-bit, so the two differ.
 */
#include "bench_audio.h"
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
#include <time.h>

#if defined(__aarch64__) || defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define BENCH_HAVE_NEON 1
#endif

/* The engine's rate (SPECTRAX_ENGINE_RATE). The bench drives the engine's
 * own render path, so it must synthesise at the rate the engine uses. */
#define SR SPECTRAX_ENGINE_RATE
#define BUF 256

static double seconds_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* The mix kernel: sum the per-voice sample buffers into the output. This is
 * the vectorisable part of the callback, so it is where the scalar/vector
 * comparison belongs. */
static void mix_scalar(const float *in, float *out, int n) {
    for (int i = 0; i < n; i++) out[i] += in[i];
}

#if defined(BENCH_HAVE_NEON)
static void mix_neon(const float *in, float *out, int n) {
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        float32x4_t v = vld1q_f32(in + i);
        float32x4_t o = vld1q_f32(out + i);
        vst1q_f32(out + i, vaddq_f32(o, v));
    }
    for (; i < n; i++) out[i] += in[i];
}
#endif

/* The scalar variant must not be auto-vectorised, or the comparison would
 * measure nothing. */
__attribute__((optimize("no-tree-vectorize")))
static void mix_scalar_novec(const float *in, float *out, int n) {
    for (int i = 0; i < n; i++) out[i] += in[i];
}

static void mix(const float *in, float *out, int n, int use_neon) {
    if (!use_neon) {
        mix_scalar_novec(in, out, n);
        return;
    }
#if defined(BENCH_HAVE_NEON)
    mix_neon(in, out, n);
#else
    mix_scalar(in, out, n);
#endif
}

int bench_audio_run(const BenchOptions *opts, BenchReport *r) {
    int duration = opts->duration;
    int voices = opts->voices;
    int channels = opts->channels;
    int no_neon = opts->no_neon;
    unsigned seed = opts->seed;

    if (voices < 1) voices = 1;
    if (channels < 1) channels = 1;
    if (channels > MAX_SEQUENCER_CHANNELS) channels = MAX_SEQUENCER_CHANNELS;

    bench_report_log(r, "bench_audio: voices=%d channels=%d duration=%ds neon=%s\n",
                     voices, channels, duration, no_neon ? "off" : "on");

    Settings settings;
    createSettings(&settings);
    SamplePool *sp = createSamplePool();
    WavetablePool *wtp = createWavetablePool();
    PresetBank pb;
    initPresetBank(&pb);
    if (!sp || !wtp) {
        bench_report_log(r, "bench_audio: pool creation failed\n");
        return 1;
    }
    /* The sample-interpolation part of the voice only does work when the
     * pool holds samples. They are cwd-relative (the port ships them under
     * resources/), so load them when they are there. */
    if (access("resources/samples/", R_OK) == 0) {
        loadSamplesfromDirectory("resources/samples/", sp);
    }
    VoiceManager *vm = createVoiceManager(&settings, sp, wtp, &pb);
    if (!vm) {
        bench_report_log(r, "bench_audio: createVoiceManager failed\n");
        return 1;
    }

    int total = 0;
    for (int ch_i = 0; ch_i < channels; ch_i++) {
        Instrument *inst = vm->instruments[ch_i];
        if (!inst) continue;
        initVoicePool(vm, ch_i, voices, inst);
        total += vm->voiceCount[ch_i];
    }
    if (total <= 0) {
        bench_report_log(r, "bench_audio: no voices allocated\n");
        return 1;
    }

    /* Activate the voices. generateVoice runs regardless, but the app's real
     * callback only syncs and renders active voices, so an inactive pool
     * measures a path that production never takes. */
    for (int c = 0; c < channels; c++) {
        for (int v_i = 0; v_i < vm->voiceCount[c]; v_i++) {
            Voice *v = vm->voicePools[c][v_i];
            if (v) {
                triggerVoice(v, (const int[]){ 57, 0 });
                v->active = 1;
            }
        }
    }

    float *scratch = calloc((size_t)total * BUF, sizeof(float));
    float *out = calloc(BUF, sizeof(float));
    if (!scratch || !out) {
        bench_report_log(r, "bench_audio: out of memory\n");
        free(scratch);
        free(out);
        return 1;
    }

    const char *cols[] = {"buffer_idx", "elapsed_ms", "callback_ms"};
    bench_report_series_begin(r, "audio_callbacks", cols, 3);
    bench_report_str(r, "audio", "sample_rate", "48000");
    bench_report_str(r, "audio", "buffer_size", "256");
    bench_report_str(r, "audio", "neon", no_neon ? "off" : "on");
    bench_report_metric(r, "audio", "voices_requested", voices * channels);
    {
        /* What syncVoiceGraph actually walks: the per-voice copy/atten/dest
         * array sizes. If these are small the in-loop sync is a handful of
         * branches and hoisting it saves nothing. */
        Voice *v0 = vm->voicePools[0][0];
        if (v0) {
            bench_report_log(r, "sync counts: clones=%d attens=%d dests=%d\n",
                             v0->cloneCount, v0->attenCount, v0->destCount);
            printf("  sync counts: clones=%d attens=%d dests=%d\n",
                   v0->cloneCount, v0->attenCount, v0->destCount);
        }
    }
    bench_report_metric(r, "audio", "voices_actual", total);

    int n_buffers = (duration * SR) / BUF;
    if (n_buffers < 1) n_buffers = 1;

    double t0 = seconds_now();
    double min_ms = 1e9, max_ms = 0, sum_ms = 0;
    int xrun = 0, sustained = 0, buffers_done = 0;
    for (int b = 0; b < n_buffers; b++) {
        if (bench_stop_requested) break;
        double bt0 = seconds_now();
        memset(out, 0, BUF * sizeof(float));
        int vi = 0;
        for (int ch_i = 0; ch_i < channels; ch_i++) {
            for (int v_i = 0; v_i < vm->voiceCount[ch_i]; v_i++) {
                Voice *v = vm->voicePools[ch_i][v_i];
                if (!v) continue;
                float *dst = scratch + (size_t)vi * BUF;
                for (int s = 0; s < BUF; s++) {
                    OutVal o = generateVoice(vm, v, 1.0f, 440.0f);
                    dst[s] = o.L + o.R;
                    processModulations(v->paramList, v->modList, 1.0f / SR);
                    if (dst[s] != dst[s]) xrun++;
                    else sustained++;
                }
                vi++;
            }
        }
        for (int i = 0; i < vi; i++) {
            mix(scratch + (size_t)i * BUF, out, BUF, !no_neon);
        }
        double bt_ms = (seconds_now() - bt0) * 1000.0;
        if (bt_ms < min_ms) min_ms = bt_ms;
        if (bt_ms > max_ms) max_ms = bt_ms;
        sum_ms += bt_ms;
        buffers_done++;
        double row[3] = { (double)b, (seconds_now() - t0) * 1000.0, bt_ms };
        bench_report_series_row(r, "audio_callbacks", row);
    }
    bench_report_series_end(r);

    if (buffers_done > 0) {
        bench_report_metric(r, "audio", "buffers", buffers_done);
        bench_report_metric(r, "audio", "callback_ms_avg", sum_ms / buffers_done);
        bench_report_metric(r, "audio", "callback_ms_min", min_ms);
        bench_report_metric(r, "audio", "callback_ms_max", max_ms);
        bench_report_metric(r, "audio", "samples_total",
                            (double)total * (double)buffers_done * BUF);
        bench_report_metric(r, "audio", "samples_sustained", (double)sustained);
        bench_report_metric(r, "audio", "xrun_count", xrun);
        bench_report_metric(r, "audio", "wallclock_s", seconds_now() - t0);
        bench_report_metric(r, "audio", "real_time_ratio",
                            (duration > 0) ? (seconds_now() - t0) / duration : 0.0);
    }
    bench_report_metric(r, "audio", "seed", (double)seed);

    free(scratch);
    free(out);
    freeVoiceManager(vm);
    freeSamplePool(sp);
    return 0;
}
