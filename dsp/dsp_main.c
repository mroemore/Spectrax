/*
 * dsp_main.c — minimal entry point for the fil-c DSP-only build.
 *
 * Renders silence to a WAV file. Later sections will replace this with a
 * real synth pipeline.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wav_writer.h"

#define DURATION_SECONDS 1
#define NUM_SAMPLES (WAV_SAMPLE_RATE * DURATION_SECONDS)

int main(int argc, char **argv) {
    const char *out_path = "out.wav";
    if (argc > 1) {
        out_path = argv[1];
    }

    static short silence[NUM_SAMPLES];
    memset(silence, 0, sizeof(silence));

    const int rc = wav_write(out_path, silence, NUM_SAMPLES);
    if (rc != 0) {
        fprintf(stderr, "wav_write failed (rc=%d) for path=%s\n", rc, out_path);
        return 1;
    }

    printf("wrote %d samples of silence to %s\n", NUM_SAMPLES, out_path);
    return 0;
}