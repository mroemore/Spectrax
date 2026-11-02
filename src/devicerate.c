#include "settings.h"

/* The rate the opened audio device actually runs at. Boundary only: the
 * stream open records it (main.c) and a future boundary resampler will need
 * it. No synthesis path may read it -- that is what keeps a voice's output
 * identical on every device.
 *
 * It lives in its own translation unit so that exactly one definition is
 * shared by everything that needs it. It used to live in oscillator.c, which
 * left src/tools/sample_analyser (a standalone target that links sample.c
 * but not the oscillator) with an undefined reference. */
float g_deviceRate = 0.0f;
