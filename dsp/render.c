/*
 * render.c — integration test: render a known sequence to WAV under
 * both fil-c and gcc, diff the outputs byte-for-byte.
 *
 * Avoids voice.c / sequencer.c entirely (both have known bugs that would
 * either crash or produce wrong output). Uses only the primitives we
 * verified work in earlier sections.
 *
 * Output: writes a 1-second WAV containing 4 evenly-spaced 440 Hz
 * square-wave notes with envelope. Each note:
 *   - 0.1s attack (linear ramp 0 → 1)
 *   - 0.15s decay (linear ramp 1 → 0)
 * Followed by silence for the remainder of the second.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "wav_writer.h"
#include "oscillator.h"

/* SAMPLE_RATE comes from oscillator.h */
#define NUM_SAMPLES SAMPLE_RATE  /* 1 second */
#define NOTE_HZ 440.0f
#define ATTACK_SAMPLES (SAMPLE_RATE / 10)     /* 100 ms */
#define DECAY_SAMPLES  (SAMPLE_RATE * 15 / 100) /* 150 ms */
#define NOTE_DURATION_SAMPLES (ATTACK_SAMPLES + DECAY_SAMPLES)
#define NUM_NOTES 4
#define STEP_SAMPLES (SAMPLE_RATE / NUM_NOTES)

static void render_to_buffer(short *buf, int n) {
    memset(buf, 0, n * sizeof(short));

    for (int note = 0; note < NUM_NOTES; note++) {
        const int start = note * STEP_SAMPLES;
        const float phase_inc = NOTE_HZ / (float)SAMPLE_RATE;

        for (int i = 0; i < NOTE_DURATION_SAMPLES; i++) {
            const int idx = start + i;
            if (idx >= n) break;

            /* Envelope: linear ramp up over attack, down over decay */
            float env;
            if (i < ATTACK_SAMPLES) {
                env = (float)i / (float)ATTACK_SAMPLES;
            } else {
                const int di = i - ATTACK_SAMPLES;
                env = 1.0f - (float)di / (float)DECAY_SAMPLES;
            }

            /* Square wave via oscillator.h */
            const float phase = (float)i * phase_inc;
            const float sig = square_wave(phase);
            const float scaled = sig * env * 16000.0f;

            /* Mix (additive — notes don't overlap in this test) */
            int sample = (int)buf[idx] + (int)scaled;
            if (sample > 32767) sample = 32767;
            if (sample < -32768) sample = -32768;
            buf[idx] = (short)sample;
        }
    }
}

int main(int argc, char **argv) {
    const char *out_path = "render.wav";
    if (argc > 1) {
        out_path = argv[1];
    }

    static short samples[NUM_SAMPLES];
    render_to_buffer(samples, NUM_SAMPLES);

    const int rc = wav_write(out_path, samples, NUM_SAMPLES);
    if (rc != 0) {
        fprintf(stderr, "wav_write failed (rc=%d) for path=%s\n", rc, out_path);
        return 1;
    }
    printf("render: wrote %d samples to %s\n", NUM_SAMPLES, out_path);
    return 0;
}