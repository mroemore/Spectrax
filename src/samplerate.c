#include "oscillator.h"

/* Rate the audio stream actually runs at; every phase increment divides by
 * it. Set from the opened stream (see openAudioStream in main.c) before the
 * callback runs.
 *
 * It lives in its own translation unit so that exactly one definition is
 * shared by everything that needs it. It used to live in oscillator.c, which
 * left src/tools/sample_analyser (a standalone target that links sample.c
 * but not the oscillator) with an undefined reference. */
float g_sampleRate = SAMPLE_RATE_DEFAULT;
